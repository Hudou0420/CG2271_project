#include "ai.h"
#include "https_guard.h"
// main.cpp - The Vault, ESP32 side.
//
// Tasks:
//   tLink  (prio 3)  UART link to the MCXC444: parse, ACK/NAK, retransmit
//   tImu   (prio 2)  MPU-6050 at 20 Hz -> TAMPER while armed
//   tCloud (prio 1)  Wi-Fi + Firebase: status, events, dashboard commands
//   loop() (prio 1)  debug console + Gemini, sharing one existing task stack
#include <Arduino.h>

#include "cloud.h"
#include "config.h"
#include "imu.h"
#include "link.h"
#include "vault_state.h"

// TLS needs the same stack budget as tCloud, without a dedicated AI task.
SET_LOOP_TASK_STACK_SIZE(12288);

// ---- Messages from the MCU (runs in tLink, keep it short) ------------------

static volatile bool s_sensorPrint = true; // 'sensors' command toggles it

static McuSensors decode_sensors(const uint8_t *p)
{
    McuSensors s;
    s.digitIndex   = p[VP_ST_DIGIT_INDEX];
    s.currentDigit = p[VP_ST_CURRENT_DIGIT];
    s.knocks       = p[VP_ST_KNOCKS];
    s.attempts     = p[VP_ST_ATTEMPTS];
    s.closeness    = p[VP_ST_CLOSENESS];
    s.light        = vp_get_u16(&p[VP_ST_LIGHT]);
    s.encoder      = (int16_t)vp_get_u16(&p[VP_ST_ENCODER]);
    s.events       = p[VP_ST_EVENTS];
    s.flags        = p[VP_ST_FLAGS];
    return s;
}

static void print_sensors(const char *prefix, const McuSensors &s)
{
    Serial.printf("%sdial %u (digits entered %u), knocks %u, attempts %u, "
                  "closeness %u, encoder %d, events %u, light ",
                  prefix, s.currentDigit, s.digitIndex, s.knocks, s.attempts,
                  s.closeness, s.encoder, s.events);
    if (s.flags & VP_STF_LIGHT_VALID) {
        Serial.printf("%u", s.light);
    } else {
        Serial.print("-");
    }
    Serial.println(s.flags & VP_STF_BEAM_ENABLED ? "" : " (beam detection off)");
}

// Everything except light-sensor drift; the MCU resends STATUS every second.
static bool sensors_changed(const McuSensors &a, const McuSensors &b)
{
    return a.digitIndex != b.digitIndex || a.currentDigit != b.currentDigit ||
           a.knocks != b.knocks || a.attempts != b.attempts ||
           a.closeness != b.closeness || a.encoder != b.encoder ||
           a.events != b.events || a.flags != b.flags ||
           abs((int)a.light - (int)b.light) >= 100;
}

static void on_status(const uint8_t *p)
{
    static McuSensors lastPrinted;
    static bool       printedOnce = false;

    VaultState prev = state_get();
    vp_state_t now  = (vp_state_t)p[VP_ST_STATE];
    McuSensors s    = decode_sensors(p);

    state_set_status(now, s);
    if (now != prev.mcuState || !prev.stateKnown) {
        Serial.printf("[mcu] state -> %s\n", state_name(now));
    }
    if (s_sensorPrint && (!printedOnce || sensors_changed(s, lastPrinted))) {
        print_sensors("[mcu] ", s);
        lastPrinted = s;
        printedOnce = true;
    }
}

static void on_message(const vp_frame_t &f)
{
    const uint8_t *p = f.payload;

    switch (f.type) {
    case VP_STATUS:
        on_status(p);
        break;
    case VP_ATTEMPT:
        Serial.printf("[mcu] ATTEMPT #%u: guess %u%u%u, closeness %u\n", p[3],
                      p[0], p[1], p[2], p[4]);
        break;
    case VP_SOLVED:
        Serial.printf("[mcu] SOLVED in %u attempts, %u s\n", p[0],
                      vp_get_u16(&p[1]));
        break;
    case VP_ALARM:
        Serial.printf("[mcu] ALARM: %s\n",
                      p[0] == VP_CAUSE_BEAM ? "beam broken" : "tamper");
        break;
    default:
        // ESP32->MCU types only show up here in loopback testing.
        Serial.printf("[mcu] received %s (%u bytes)\n",
                      link_type_name(f.type), f.len);
        break;
    }
    cloud_on_message(f);
}

// Delivery report for our own messages (runs in tLink).
static void on_delivered(uint8_t type, bool ok)
{
    cloud_on_delivered(type, ok);
    if (!ok) {
        return;
    }
    Serial.printf("[link] %s delivered\n", link_type_name(type));
    // Assume the MCU acted on it until its next STATUS says otherwise.
    if (type == VP_ARM) {
        state_set_mcu(VP_STATE_LOCKED, false);
    } else if (type == VP_DISARM) {
        state_set_mcu(VP_STATE_IDLE, false);
    }
}

// ---- Debug console ----------------------------------------------------------

static void print_help()
{
    Serial.println(
        "Commands:\n"
        "  arm <ddd> [pattern]  send ARM, e.g. 'arm 427' or 'arm 427 1'\n"
        "  disarm               send DISARM\n"
        "  tamper               send TAMPER now (ignores the IMU)\n"
        "  send <type> [bytes]  send any frame, hex, e.g. 'send 10 04 02 07 01 03'\n"
        "  imu                  toggle printing every IMU sample\n"
        "  raw                  toggle hex dump of UART bytes\n"
        "  sensors              toggle printing MCU sensor changes\n"
        "  stats                link counters and vault state\n"
        "  cloud                Wi-Fi / Firebase status\n"
        "  help                 this list");
}

static void print_stats()
{
    LinkStats  s  = link_stats();
    VaultState vs = state_get();
    ImuReading r  = imu_latest();

    Serial.printf("link: tx %lu, retries %lu, failed %lu | rx %lu, bad %lu, "
                  "timeouts %lu, NAKs sent %lu\n",
                  s.txFrames, s.txRetries, s.txFailed, s.rxFrames, s.rxBad,
                  s.rxTimeouts, s.naksSent);
    Serial.printf("vault: %s%s, MCU %s\n",
                  vs.stateKnown ? state_name(vs.mcuState) : "unknown",
                  state_is_armed() ? " (armed)" : "",
                  state_mcu_online() ? "online" : "offline (no STATUS)");
    if (vs.lastStatusMs != 0) {
        print_sensors("mcu: ", vs.sensors);
        Serial.printf("mcu: tap/encoder inputs %s\n",
                      vs.sensors.flags & VP_STF_INPUTS_READY ? "ready" : "NOT ready");
    }
    if (imu_ok()) {
        Serial.printf("imu: |a|=%.3f g, dev=%.3f g, gyro=%.1f dps\n", r.accG,
                      r.devG, r.gyroDps);
    } else {
        Serial.println("imu: not ready");
    }
}

static void print_cloud()
{
    CloudStats c = cloud_stats();

    if (!c.configured) {
        Serial.println("cloud: off (copy include/secrets.example.h to include/secrets.h)");
        return;
    }
    Serial.printf("cloud: Wi-Fi %s, Firebase %s | requests ok %lu, failed %lu, "
                  "last HTTP %d | commands %lu, events dropped %lu | free heap %lu\n",
                  c.wifiUp ? "up" : "DOWN", c.signedIn ? "signed in" : "not signed in",
                  c.requestsOk, c.requestsFailed, c.lastHttpCode, c.commandsHandled,
                  c.eventsDropped, (unsigned long)ESP.getFreeHeap());
    if (c.lastError[0] != '\0') {
        Serial.printf("cloud: last error: %s\n", c.lastError);
    }
}

static void handle_command(char *line)
{
    static bool imuStream = false;
    static bool rawLog    = false;

    char *save = nullptr;
    char *cmd  = strtok_r(line, " ", &save);
    if (cmd == nullptr) {
        return;
    }

    if (strcmp(cmd, "arm") == 0) {
        char *code    = strtok_r(nullptr, " ", &save);
        char *pattern = strtok_r(nullptr, " ", &save);
        if (code == nullptr || strlen(code) != 3 || !isdigit(code[0]) ||
            !isdigit(code[1]) || !isdigit(code[2])) {
            Serial.print("usage: arm <3 digits> [pattern]  (got:");
            for (const char *c = code; c != nullptr && *c != '\0'; c++) {
                Serial.printf(" %02X", (uint8_t)*c);
            }
            Serial.println(")");
            return;
        }
        uint8_t payload[VP_LEN_ARM] = {
            (uint8_t)(code[0] - '0'), (uint8_t)(code[1] - '0'),
            (uint8_t)(code[2] - '0'),
            (uint8_t)(pattern ? atoi(pattern) : 0)};
        ai_cancel_context();
        link_send(VP_ARM, payload, sizeof(payload));
    } else if (strcmp(cmd, "disarm") == 0) {
        ai_cancel_context();
        link_send(VP_DISARM, nullptr, 0);
    } else if (strcmp(cmd, "tamper") == 0) {
        uint8_t payload[VP_LEN_TAMPER];
        vp_put_u16(payload, (uint16_t)(imu_latest().accG * 1000.0f));
        link_send(VP_TAMPER, payload, sizeof(payload));
    } else if (strcmp(cmd, "send") == 0) {
        char *t = strtok_r(nullptr, " ", &save);
        if (t == nullptr) {
            Serial.println("usage: send <type hex> [payload bytes hex]");
            return;
        }
        uint8_t type = (uint8_t)strtoul(t, nullptr, 16);
        uint8_t payload[VP_MAX_PAYLOAD];
        uint8_t len = 0;
        char   *b;
        while ((b = strtok_r(nullptr, " ", &save)) != nullptr && len < VP_MAX_PAYLOAD) {
            payload[len++] = (uint8_t)strtoul(b, nullptr, 16);
        }
        ai_cancel_context();
        link_send(type, payload, len);
    } else if (strcmp(cmd, "imu") == 0) {
        imuStream = !imuStream;
        imu_set_stream(imuStream);
    } else if (strcmp(cmd, "raw") == 0) {
        rawLog = !rawLog;
        link_set_raw_log(rawLog);
        Serial.printf("raw UART log %s\n", rawLog ? "on" : "off");
    } else if (strcmp(cmd, "sensors") == 0) {
        s_sensorPrint = !s_sensorPrint;
        Serial.printf("MCU sensor printing %s\n", s_sensorPrint ? "on" : "off");
    } else if (strcmp(cmd, "stats") == 0) {
        print_stats();
    } else if (strcmp(cmd, "cloud") == 0) {
        print_cloud();
    } else if (strcmp(cmd, "help") == 0) {
        print_help();
    } else {
        Serial.printf("unknown command '%s', try 'help'\n", cmd);
    }
}

static void poll_console()
{
    static char   line[64];
    static size_t len = 0;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                line[len] = '\0';
                Serial.printf("> %s\n", line);
                handle_command(line);
                len = 0;
            }
        } else if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
            }
        } else if (isprint((unsigned char)c) && len < sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}

// ---- Arduino entry points ---------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== The Vault - ESP32 ===");

    https_guard_init();
    ai_init();
    state_init();
    link_init(on_message, on_delivered);
    imu_init();
    cloud_init();

    Serial.printf("MCU link: Serial1 %d baud, TX=GPIO%d RX=GPIO%d\n", VP_BAUD,
                  PIN_MCU_TX, PIN_MCU_RX);
    Serial.printf("MPU-6050: SDA=GPIO%d SCL=GPIO%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    print_help();
}

void loop()
{
    poll_console();
    ai_poll();
    vTaskDelay(pdMS_TO_TICKS(10));
}
