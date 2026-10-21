// imu.cpp - reads the MPU-6050 and sends TAMPER to the MCU when the box is
// shaken or lifted while the vault is armed.
//
// Detection: a sample is "over" if |a| moves more than TAMPER_ACC_DEV_G away
// from the resting value learnt at boot, or the rotation rate exceeds
// TAMPER_GYRO_DPS. TAMPER_SAMPLES consecutive "over" samples are needed, so a
// single bump of the table doesn't set off the alarm.
//
// The sensor is driven by register reads/writes over I2C rather than a
// library: many GY-521 boards carry an MPU-6500 (WHO_AM_I 0x70) instead of a
// genuine MPU-6050 (0x68), and the registers we use are the same on both.
#include "imu.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "config.h"
#include "link.h"
#include "vault_state.h"

// Registers (same addresses on MPU-6050 and MPU-6500).
#define REG_CONFIG        0x1A
#define REG_GYRO_CONFIG   0x1B
#define REG_ACCEL_CONFIG  0x1C
#define REG_ACCEL_CONFIG2 0x1D // MPU-6500 only: accelerometer low-pass filter
#define REG_ACCEL_XOUT_H  0x3B // 14 bytes: accel XYZ, temp, gyro XYZ
#define REG_PWR_MGMT_1    0x6B
#define REG_WHO_AM_I      0x75

#define WHO_MPU6050       0x68

// Full-scale ranges and the matching sensitivities from the datasheets.
#define ACCEL_FS_4G       0x08  // AFS_SEL = 1 -> +-4 g
#define GYRO_FS_500DPS    0x08  // FS_SEL = 1  -> +-500 deg/s
#define DLPF_20HZ         0x04  // ~20 Hz low-pass, plenty for 20 Hz sampling
static const float ACCEL_LSB_PER_G  = 8192.0f;
static const float GYRO_LSB_PER_DPS = 65.5f;

static volatile bool s_ok;
static volatile bool s_stream;
static float         s_baselineG = 1.0f;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ImuReading   s_latest;

// ---- I2C register access -----------------------------------------------------

static bool write_reg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(MPU_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool read_regs(uint8_t reg, uint8_t *buf, uint8_t n)
{
    Wire.beginTransmission(MPU_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)MPU_I2C_ADDR, n) != n) {
        return false;
    }
    for (uint8_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)Wire.read();
    }
    return true;
}

static const char *chip_name(uint8_t who)
{
    switch (who) {
    case 0x68: return "MPU-6050";
    case 0x70: return "MPU-6500";
    case 0x71: return "MPU-9250";
    default:   return "unknown MPU-6050 compatible";
    }
}

// Reset the chip, wake it and set the ranges. Returns false if it doesn't
// answer on I2C.
static bool mpu_begin()
{
    uint8_t who;
    if (!read_regs(REG_WHO_AM_I, &who, 1)) {
        return false;
    }
    if (!write_reg(REG_PWR_MGMT_1, 0x80)) { // device reset
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    bool ok = write_reg(REG_PWR_MGMT_1, 0x01) && // wake, gyro PLL clock
              write_reg(REG_CONFIG, DLPF_20HZ) &&
              write_reg(REG_GYRO_CONFIG, GYRO_FS_500DPS) &&
              write_reg(REG_ACCEL_CONFIG, ACCEL_FS_4G);
    if (ok && who != WHO_MPU6050) {
        ok = write_reg(REG_ACCEL_CONFIG2, DLPF_20HZ);
    }
    if (ok) {
        Serial.printf("[imu] found %s (WHO_AM_I 0x%02X) at 0x%02X\n",
                      chip_name(who), who, MPU_I2C_ADDR);
    }
    return ok;
}

static bool read_sample(float &accG, float &gyroDps)
{
    uint8_t b[14];
    if (!read_regs(REG_ACCEL_XOUT_H, b, sizeof(b))) {
        return false;
    }
    // Big-endian int16s: ax ay az temp gx gy gz.
    float v[7];
    for (int i = 0; i < 7; i++) {
        v[i] = (float)(int16_t)((b[2 * i] << 8) | b[2 * i + 1]);
    }
    accG    = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) / ACCEL_LSB_PER_G;
    gyroDps = sqrtf(v[4] * v[4] + v[5] * v[5] + v[6] * v[6]) / GYRO_LSB_PER_DPS;
    return true;
}

// Explain a failed mpu_begin(): list whatever answers on the bus.
static void diagnose_i2c()
{
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[imu]   I2C device at 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) {
        Serial.printf("[imu]   no I2C devices answer: check SDA=GPIO%d, "
                      "SCL=GPIO%d and GND\n", PIN_I2C_SDA, PIN_I2C_SCL);
    } else {
        Serial.printf("[imu]   nothing at 0x%02X: if AD0 is high the sensor is "
                      "at 0x69 (MPU_I2C_ADDR in config.h)\n", MPU_I2C_ADDR);
    }
}

// Find the sensor and learn the resting |a|. Keep the box still during this.
static void connect_and_calibrate()
{
    while (!mpu_begin()) {
        Serial.println("[imu] MPU-6050 not found; retrying in 2 s");
        diagnose_i2c();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    Serial.println("[imu] calibrating, keep the box still...");
    float sum = 0;
    int   n   = 0;
    while (n < IMU_CALIB_SAMPLES) {
        float accG, gyroDps;
        if (read_sample(accG, gyroDps)) {
            sum += accG;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
    s_baselineG = sum / n;
    s_ok        = true;
    Serial.printf("[imu] ready, resting |a| = %.3f g\n", s_baselineG);
}

// ---- Task ------------------------------------------------------------------------

static void imu_task(void *)
{
    connect_and_calibrate();

    int        overCount    = 0;
    float      peakG        = 0;
    uint32_t   lastTamperMs = 0;
    TickType_t wake         = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IMU_PERIOD_MS));

        float accG, gyroDps;
        if (!read_sample(accG, gyroDps)) {
            Serial.println("[imu] read failed");
            continue;
        }
        ImuReading r = {accG, fabsf(accG - s_baselineG), gyroDps};
        taskENTER_CRITICAL(&s_mux);
        s_latest = r;
        taskEXIT_CRITICAL(&s_mux);

        bool over = r.devG > TAMPER_ACC_DEV_G || r.gyroDps > TAMPER_GYRO_DPS;
        if (over) {
            overCount++;
            peakG = fmaxf(peakG, accG);
        } else {
            overCount = 0;
            peakG     = 0;
        }

        if (s_stream) {
            Serial.printf("[imu] |a|=%.3f g  dev=%.3f g  gyro=%6.1f dps%s\n",
                          r.accG, r.devG, r.gyroDps, over ? "  OVER" : "");
        }

        if (overCount < TAMPER_SAMPLES) {
            continue;
        }
        overCount = 0;

        if (!state_is_armed()) {
            if (s_stream) {
                Serial.println("[imu] tamper detected, but vault not armed");
            }
            continue;
        }
        if (lastTamperMs != 0 && millis() - lastTamperMs < TAMPER_COOLDOWN_MS) {
            continue;
        }
        lastTamperMs = millis();

        uint8_t payload[VP_LEN_TAMPER];
        vp_put_u16(payload, (uint16_t)fminf(peakG * 1000.0f, 65535.0f));
        Serial.printf("[imu] TAMPER! peak |a| = %.2f g\n", peakG);
        link_send(VP_TAMPER, payload, sizeof(payload));
    }
}

// ---- Public API ------------------------------------------------------------------

void imu_init()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    xTaskCreate(imu_task, "tImu", TASK_STACK_WORDS, nullptr, IMU_TASK_PRIO,
                nullptr);
}

bool imu_ok()
{
    return s_ok;
}

ImuReading imu_latest()
{
    taskENTER_CRITICAL(&s_mux);
    ImuReading r = s_latest;
    taskEXIT_CRITICAL(&s_mux);
    return r;
}

void imu_set_stream(bool on)
{
    s_stream = on;
}
