// link.cpp - UART framing, ACK/NAK and retransmission (see link.h).
#include "link.h"

#include <Arduino.h>
#include "config.h"

struct OutMsg {
    uint8_t type;
    uint8_t len;
    uint8_t payload[VP_MAX_PAYLOAD];
};

struct Pending {
    bool     active;
    OutMsg   msg;
    uint8_t  retries;
    uint32_t sentAtMs;
};

static QueueHandle_t s_txQueue;
static link_rx_cb_t  s_onRx;
static link_tx_cb_t  s_onTx;
static LinkStats     s_stats;
static volatile bool s_rawLog;

static vp_parser_t s_parser;
static uint32_t    s_lastByteMs;
static Pending     s_pending;

const char *link_type_name(uint8_t type)
{
    switch (type) {
    case VP_ARM:     return "ARM";
    case VP_DISARM:  return "DISARM";
    case VP_TAMPER:  return "TAMPER";
    case VP_ATTEMPT: return "ATTEMPT";
    case VP_SOLVED:  return "SOLVED";
    case VP_ALARM:   return "ALARM";
    case VP_STATUS:  return "STATUS";
    case VP_ACK:     return "ACK";
    case VP_NAK:     return "NAK";
    default:         return "UNKNOWN";
    }
}

// Expected payload length for a type, or -1 if the type is unknown.
static int expected_len(uint8_t type)
{
    switch (type) {
    case VP_ARM:     return VP_LEN_ARM;
    case VP_DISARM:  return VP_LEN_DISARM;
    case VP_TAMPER:  return VP_LEN_TAMPER;
    case VP_ATTEMPT: return VP_LEN_ATTEMPT;
    case VP_SOLVED:  return VP_LEN_SOLVED;
    case VP_ALARM:   return VP_LEN_ALARM;
    case VP_STATUS:  return VP_LEN_STATUS;
    case VP_ACK:     return VP_LEN_ACK;
    case VP_NAK:     return VP_LEN_NAK;
    default:         return -1;
    }
}

// ---- Transmit --------------------------------------------------------------

static void write_frame(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t buf[VP_MAX_FRAME];
    size_t  n = vp_encode(type, payload, len, buf, sizeof(buf));
    if (n == 0) {
        return;
    }
    Serial1.write(buf, n);
    if (s_rawLog) {
        Serial.print("[raw] TX");
        for (size_t i = 0; i < n; i++) {
            Serial.printf(" %02X", buf[i]);
        }
        Serial.println();
    }
}

static void send_ack(uint8_t echoedType, bool ack)
{
    write_frame(ack ? VP_ACK : VP_NAK, &echoedType, 1);
    if (!ack) {
        s_stats.naksSent++;
    }
}

static void transmit_pending()
{
    write_frame(s_pending.msg.type, s_pending.msg.payload, s_pending.msg.len);
    s_pending.sentAtMs = millis();
}

static void finish_pending(bool ok)
{
    s_pending.active = false;
    if (!ok) {
        s_stats.txFailed++;
        Serial.printf("[link] %s not acknowledged after %d retries, dropped\n",
                      link_type_name(s_pending.msg.type), VP_MAX_RETRIES);
    }
    if (s_onTx) {
        s_onTx(s_pending.msg.type, ok);
    }
}

static void retry_pending(const char *why)
{
    if (s_pending.retries >= VP_MAX_RETRIES) {
        finish_pending(false);
        return;
    }
    s_pending.retries++;
    s_stats.txRetries++;
    Serial.printf("[link] %s: resending %s (retry %u/%d)\n", why,
                  link_type_name(s_pending.msg.type), s_pending.retries,
                  VP_MAX_RETRIES);
    transmit_pending();
}

static void service_tx()
{
    if (s_pending.active) {
        if (millis() - s_pending.sentAtMs >= VP_ACK_TIMEOUT_MS) {
            retry_pending("ACK timeout");
        }
        return;
    }
    OutMsg msg;
    if (xQueueReceive(s_txQueue, &msg, 0) == pdTRUE) {
        s_pending.active  = true;
        s_pending.msg     = msg;
        s_pending.retries = 0;
        s_stats.txFrames++;
        transmit_pending();
    }
}

// ---- Receive ---------------------------------------------------------------

static void handle_frame(const vp_frame_t &f)
{
    s_stats.rxFrames++;

    int want = expected_len(f.type);
    if (want < 0 || f.len != want) {
        // Checksum passed but the content doesn't match our protocol
        // (usually the two sides disagree on the spec).
        s_stats.rxBad++;
        Serial.printf("[link] rejected type 0x%02X len %u (expected %d)\n",
                      f.type, f.len, want);
        if (f.type != VP_ACK && f.type != VP_NAK) {
            send_ack(f.type, false);
        }
        return;
    }

    if (f.type == VP_ACK) {
        if (s_pending.active && f.payload[0] == s_pending.msg.type) {
            finish_pending(true);
        } else {
            Serial.printf("[link] stray ACK for %s\n",
                          link_type_name(f.payload[0]));
        }
        return;
    }
    if (f.type == VP_NAK) {
        // The echoed type may itself be corrupted, so any NAK while we have
        // a message in flight means "resend it".
        if (s_pending.active) {
            retry_pending("NAK received");
        }
        return;
    }

    send_ack(f.type, true);
    if (s_onRx) {
        s_onRx(f);
    }
}

static void service_rx()
{
    while (Serial1.available() > 0) {
        uint8_t  b   = (uint8_t)Serial1.read();
        uint32_t now = millis();

        if (s_rawLog) {
            Serial.printf("[raw] RX %02X\n", b);
        }
        if (vp_parser_busy(&s_parser) &&
            now - s_lastByteMs > VP_INTERBYTE_TIMEOUT_MS) {
            s_stats.rxTimeouts++;
            vp_parser_reset(&s_parser);
        }
        s_lastByteMs = now;

        vp_frame_t     frame;
        vp_rx_result_t res = vp_parser_feed(&s_parser, b, &frame);
        if (res == VP_RX_OK) {
            handle_frame(frame);
        } else if (res == VP_RX_BAD) {
            s_stats.rxBad++;
            Serial.printf("[link] bad frame (type 0x%02X), sending NAK\n",
                          frame.type);
            if (frame.type != VP_ACK && frame.type != VP_NAK) {
                send_ack(frame.type, false);
            }
        }
    }

    // A frame that stopped half-way with no more bytes coming.
    if (vp_parser_busy(&s_parser) &&
        millis() - s_lastByteMs > VP_INTERBYTE_TIMEOUT_MS) {
        s_stats.rxTimeouts++;
        vp_parser_reset(&s_parser);
    }
}

static void link_task(void *)
{
    for (;;) {
        service_rx();
        service_tx();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---- Public API ------------------------------------------------------------

void link_init(link_rx_cb_t onRx, link_tx_cb_t onTx)
{
    s_onRx    = onRx;
    s_onTx    = onTx;
    s_txQueue = xQueueCreate(LINK_TX_QUEUE_LEN, sizeof(OutMsg));
    vp_parser_reset(&s_parser);

    Serial1.setRxBufferSize(256);
    Serial1.begin(VP_BAUD, SERIAL_8N1, PIN_MCU_RX, PIN_MCU_TX);

    xTaskCreate(link_task, "tLink", TASK_STACK_WORDS, nullptr, LINK_TASK_PRIO,
                nullptr);
}

bool link_send(uint8_t type, const uint8_t *payload, uint8_t len)
{
    if (len > VP_MAX_PAYLOAD) {
        return false;
    }
    OutMsg msg;
    msg.type = type;
    msg.len  = len;
    if (len > 0) {
        memcpy(msg.payload, payload, len);
    }
    if (xQueueSend(s_txQueue, &msg, 0) != pdTRUE) {
        Serial.printf("[link] TX queue full, dropped %s\n", link_type_name(type));
        return false;
    }
    return true;
}

LinkStats link_stats()
{
    return s_stats;
}

void link_set_raw_log(bool on)
{
    s_rawLog = on;
}
