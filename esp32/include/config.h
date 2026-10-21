// config.h - pins and tuning constants for the ESP32 side.
#pragma once

// ---- UART link to the MCXC444 (Serial1) -----------------------------------
// Cross-connect: ESP32 TX -> MCU RX, ESP32 RX <- MCU TX, and a common GND.
// (Avoid GPIO18: it drives the DevKitM-1's on-board RGB LED.)
#define PIN_MCU_TX 4
#define PIN_MCU_RX 5

// ---- MPU-6050 (GY-521) on I2C ---------------------------------------------
// ESP32-S2 Arduino default I2C pins. GY-521 VCC -> 3V3, AD0 left low (0x68).
// Works with a genuine MPU-6050 or the MPU-6500 found on many GY-521 boards.
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define MPU_I2C_ADDR 0x68

// ---- Tamper detection -----------------------------------------------------
#define IMU_PERIOD_MS         50     // 20 Hz sampling
#define IMU_CALIB_SAMPLES     40     // ~2 s at rest to learn the baseline
#define TAMPER_ACC_DEV_G      0.35f  // |a| differs from rest by more than this
#define TAMPER_GYRO_DPS       90.0f  // or the box rotates faster than this
#define TAMPER_SAMPLES        3      // consecutive samples over threshold
#define TAMPER_COOLDOWN_MS    3000   // minimum gap between TAMPER packets

// ---- Status heartbeat -----------------------------------------------------
#define MCU_OFFLINE_MS        3000   // no STATUS for this long -> MCU offline

// ---- Cloud link (Firebase Realtime Database, see web/README.md) ------------
// Wi-Fi and Firebase credentials live in secrets.h (see secrets.example.h).
#define CLOUD_ROOT                "/vault" // every path below sits under this
#define CLOUD_COMMAND_POLL_MS     1000     // check /vault/commands this often
#define CLOUD_STATUS_MIN_MS       1000     // publish /vault/status at most this often
#define CLOUD_STATUS_HEARTBEAT_MS 10000    // and at least this often
#define CLOUD_LIGHT_DELTA         100      // light change that counts as a change
#define CLOUD_HTTP_TIMEOUT_MS     5000
#define CLOUD_RETRY_MS            2000     // wait after a failed request
#define CLOUD_EVENT_QUEUE_LEN     16
#define CLOUD_PLAYER_MAX          24       // max player-name length from the dashboard

// ---- Task setup -----------------------------------------------------------
// Arduino's loop() (the debug console) runs at priority 1.
#define LINK_TASK_PRIO        3
#define IMU_TASK_PRIO         2
#define CLOUD_TASK_PRIO       1      // network waits must never delay tLink/tImu
#define TASK_STACK_WORDS      4096   // ESP-IDF stack sizes are in bytes
#define CLOUD_TASK_STACK      12288  // HTTPS + JSON need more
#define LINK_TX_QUEUE_LEN     8
