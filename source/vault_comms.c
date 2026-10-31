/*
 * vault_comms.c  --  Person 3
 *
 * LPUART1 driver, framing, ACK/NAK and retransmission (see vault_comms.h).
 * Tested against the ESP32 firmware in esp32/ on 21 October 2026.
 */
#include "vault_comms.h"

#include <string.h>

#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "fsl_lpuart.h"
#include "fsl_port.h"

#include "vault_protocol.h"
#include "vault_rtos.h"
#include "vault_sensors.h"

/* LPUART1 on PTE0 (J2-18, TX) / PTE1 (J2-20, RX), pin mux alternative 3.
 * Wire PTE0 -> ESP32 GPIO5, PTE1 <- ESP32 GPIO4, and GND <-> GND. */
#define COMMS_UART            LPUART1
#define COMMS_UART_IRQn       LPUART1_IRQn
#define COMMS_UART_IRQHandler LPUART1_IRQHandler
#define COMMS_UART_CLKSRC     1U /* SIM_SOPT2.LPUART1SRC: 1 = IRC48M */
#define COMMS_PORT            PORTE
#define COMMS_PORT_CLOCK      kCLOCK_PortE
#define COMMS_TX_PIN          0U
#define COMMS_RX_PIN          1U
#define COMMS_PIN_MUX         kPORT_MuxAlt3

#define COMMS_POLL_MS         10 /* max wait for RX before servicing TX/timeouts */
#define COMMS_STATE_WAIT_MS   10 /* max wait for xStateMutex for STATUS */
#define COMMS_STATUS_GAP_MS   100 /* min gap between change-triggered STATUS */
#define COMMS_LIGHT_DELTA     100 /* light change that counts as a change */

#define UART_ERROR_FLAGS \
    (kLPUART_RxOverrunFlag | kLPUART_NoiseErrorFlag | kLPUART_FramingErrorFlag)

/* tGame's packet numbers and states go on the wire unchanged. */
_Static_assert(PKT_ATTEMPT == VP_ATTEMPT && PKT_SOLVED == VP_SOLVED && PKT_ALARM == VP_ALARM,
               "vault_types.h PKT_* must match vault_protocol.h");
_Static_assert((int)ST_IDLE == (int)VP_STATE_IDLE && (int)ST_LOCKED == (int)VP_STATE_LOCKED &&
                   (int)ST_DIALING == (int)VP_STATE_DIALING &&
                   (int)ST_UNLOCKED == (int)VP_STATE_UNLOCKED && (int)ST_ALARM == (int)VP_STATE_ALARM,
               "VaultState must match vp_state_t");
_Static_assert(TX_MAX_PAYLOAD <= VP_MAX_PAYLOAD, "TxMsg payload too large for a frame");

/* A message in wire format (payload as defined in vault_protocol.h). */
typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t payload[VP_MAX_PAYLOAD];
} wire_msg_t;

typedef struct {
    bool       active;
    wire_msg_t msg;
    uint8_t    retries;
    TickType_t sentAt;
} pending_t;

static vp_parser_t s_parser;
static TickType_t  s_lastByteAt;
static pending_t   s_pending;
static bool        s_linkUp;

static TickType_t  s_lastStatusAt;
static uint8_t     s_lastStatus[VP_LEN_STATUS];
static bool        s_statusSent;

static volatile uint32_t s_uartErrors; /* overrun / noise / framing, from the ISR */
static volatile uint32_t s_rxDropped;  /* RX queue full, from the ISR */

static const char *type_name(uint8_t type)
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

/* Expected payload length for a type, or -1 if the type is unknown. */
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

_Static_assert(DIGIT_ABSENT == VP_DIGIT_ABSENT && DIGIT_PRESENT == VP_DIGIT_PRESENT &&
               DIGIT_CORRECT == VP_DIGIT_CORRECT && TX_MAX_PAYLOAD >= VP_LEN_ATTEMPT,
               "Digit feedback must match the shared UART protocol");

/* ---- ISR ------------------------------------------------------------------ */

/* Only moves the byte into the queue; all parsing happens in tComms. */
void COMMS_UART_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t   flags = LPUART_GetStatusFlags(COMMS_UART);

    if (flags & UART_ERROR_FLAGS) {
        LPUART_ClearStatusFlags(COMMS_UART, flags & UART_ERROR_FLAGS);
        s_uartErrors++;
    }
    if (flags & kLPUART_RxDataRegFullFlag) {
        uint8_t byte = LPUART_ReadByte(COMMS_UART);
        if (xQueueSendFromISR(xUartRxQueue, &byte, &woken) != pdTRUE) {
            s_rxDropped++;
        }
    }
    portYIELD_FROM_ISR(woken);
}

/* ---- Transmit ------------------------------------------------------------- */

/* vault_make_packet() payloads -> wire payloads. ATTEMPT is identical;
 * SOLVED time is big-endian in TxMsg but little-endian on the wire;
 * AlarmCause (1 beam, 2 tamper) -> vp_cause_t (0 beam, 1 tamper). */
static void to_wire(const TxMsg *in, wire_msg_t *out)
{
    out->type = in->type;
    out->len  = in->len;
    memcpy(out->payload, in->payload, in->len);

    switch (in->type) {
    case PKT_SOLVED:
        vp_put_u16(&out->payload[1], (uint16_t)((in->payload[1] << 8) | in->payload[2]));
        break;
    case PKT_ALARM:
        out->payload[0] = (in->payload[0] == ALARM_TAMPER) ? VP_CAUSE_TAMPER : VP_CAUSE_BEAM;
        break;
    default:
        break;
    }
}

static void write_frame(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t buf[VP_MAX_FRAME];
    size_t  n = vp_encode(type, payload, len, buf, sizeof(buf));

    if (n > 0) {
        /* At most 20 bytes (~1.7 ms at 115200), only ever called from tComms. */
        LPUART_WriteBlocking(COMMS_UART, buf, n);
    }
}

static void send_ack(uint8_t echoedType, bool ack)
{
    write_frame(ack ? VP_ACK : VP_NAK, &echoedType, 1);
}

static void transmit_pending(void)
{
    write_frame(s_pending.msg.type, s_pending.msg.payload, s_pending.msg.len);
    s_pending.sentAt = xTaskGetTickCount();
}

static void finish_pending(bool ok)
{
    uint8_t type = s_pending.msg.type;

    s_pending.active = false;

    /* STATUS goes out every second, so report link up/down instead of each one. */
    if (type == VP_STATUS) {
        if (ok != s_linkUp) {
            s_linkUp = ok;
            PRINTF("[comms] ESP32 link %s\r\n", ok ? "UP" : "DOWN (no ACK)");
        }
        return;
    }
    if (ok) {
        PRINTF("[comms] %s delivered\r\n", type_name(type));
    } else {
        PRINTF("[comms] %s not acknowledged after %d retries, dropped\r\n",
               type_name(type), VP_MAX_RETRIES);
    }
}

static void retry_pending(void)
{
    if (s_pending.retries >= VP_MAX_RETRIES) {
        finish_pending(false);
        return;
    }
    s_pending.retries++;
    if (s_pending.msg.type != VP_STATUS) {
        PRINTF("[comms] resending %s (retry %u/%d)\r\n", type_name(s_pending.msg.type),
               (unsigned)s_pending.retries, VP_MAX_RETRIES);
    }
    transmit_pending();
}

/* STATUS payload: game progress from gGame plus a sensor snapshot. */
static bool build_status(uint8_t *p)
{
    VaultSensorStats st;
    uint8_t          flags = 0;

    if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(COMMS_STATE_WAIT_MS)) != pdTRUE) {
        return false; /* try again on the next pass */
    }
    p[VP_ST_STATE]         = (uint8_t)gGame.state;
    p[VP_ST_DIGIT_INDEX]   = gGame.digit_index;
    p[VP_ST_CURRENT_DIGIT] = gGame.current_digit;
    p[VP_ST_KNOCKS]        = gGame.knock_count;
    p[VP_ST_ATTEMPTS]      = gGame.attempts;
    p[VP_ST_CLOSENESS]     = gGame.closeness;
    xSemaphoreGive(xStateMutex);

    vault_sensors_get_stats(&st);
    if (st.ldr_valid)              flags |= VP_STF_LIGHT_VALID;
    if (st.beam_detection_enabled) flags |= VP_STF_BEAM_ENABLED;
    if (st.inputs_ready)           flags |= VP_STF_INPUTS_READY;
    vp_put_u16(&p[VP_ST_LIGHT], st.ldr_raw);
    vp_put_u16(&p[VP_ST_ENCODER], (uint16_t)st.encoder_steps);
    p[VP_ST_EVENTS] = (uint8_t)st.game_events_sent;
    p[VP_ST_FLAGS]  = flags;
    return true;
}

/* Worth sending early: anything except small light-sensor drift. */
static bool status_changed(const uint8_t *p)
{
    int light;

    if (!s_statusSent) {
        return true;
    }
    if (memcmp(p, s_lastStatus, VP_ST_LIGHT) != 0 ||
        memcmp(&p[VP_ST_ENCODER], &s_lastStatus[VP_ST_ENCODER],
               VP_LEN_STATUS - VP_ST_ENCODER) != 0) {
        return true;
    }
    light = (int)vp_get_u16(&p[VP_ST_LIGHT]) - (int)vp_get_u16(&s_lastStatus[VP_ST_LIGHT]);
    return light >= COMMS_LIGHT_DELTA || light <= -COMMS_LIGHT_DELTA;
}

/* Heartbeat every VAULT_COMMS_STATUS_PERIOD_MS, or sooner when something
 * changed (at most every COMMS_STATUS_GAP_MS). Fills s_pending.msg. */
static bool take_status(void)
{
    TickType_t now   = xTaskGetTickCount();
    TickType_t since = now - s_lastStatusAt;
    uint8_t    p[VP_LEN_STATUS];

    if (s_statusSent && since < pdMS_TO_TICKS(COMMS_STATUS_GAP_MS)) {
        return false;
    }
    if (!build_status(p)) {
        return false;
    }
    if (s_statusSent && since < pdMS_TO_TICKS(VAULT_COMMS_STATUS_PERIOD_MS) &&
        !status_changed(p)) {
        return false;
    }
    memcpy(s_lastStatus, p, VP_LEN_STATUS);
    s_statusSent   = true;
    s_lastStatusAt = now;

    s_pending.msg.type = VP_STATUS;
    s_pending.msg.len  = VP_LEN_STATUS;
    memcpy(s_pending.msg.payload, p, VP_LEN_STATUS);
    return true;
}

/* tGame's packets go first; STATUS only when nothing else is waiting. */
static void service_tx(void)
{
    TxMsg m;

    if (s_pending.active) {
        if (xTaskGetTickCount() - s_pending.sentAt >= pdMS_TO_TICKS(VP_ACK_TIMEOUT_MS)) {
            retry_pending();
        }
        return;
    }
    if (xQueueReceive(xUartTxQueue, &m, 0) == pdTRUE) {
        to_wire(&m, &s_pending.msg);
    } else if (!take_status()) {
        return;
    }
    s_pending.active  = true;
    s_pending.retries = 0;
    transmit_pending();
}

/* ---- Receive -------------------------------------------------------------- */

/* Turn an ESP32 command into a tGame event. False = could not deliver it
 * (bad payload or xInputQueue full), so the ESP32 gets a NAK and resends. */
static bool post_event(const vp_frame_t *f)
{
    const uint8_t *p  = f->payload;
    VaultEvent     ev = { EV_NONE, 0, 0 };

    switch (f->type) {
    case VP_ARM:
        if (p[0] > 9 || p[1] > 9 || p[2] > 9) {
            PRINTF("[comms] ARM with invalid digits %u %u %u\r\n", p[0], p[1], p[2]);
            return false;
        }
        PRINTF("[esp32] ARM: code %u%u%u\r\n", p[0], p[1], p[2]);
        ev.type  = EV_ARM;
        ev.value = p[0] * 100 + p[1] * 10 + p[2];
        break;

    case VP_DISARM:
        PRINTF("[esp32] DISARM\r\n");
        ev.type = EV_DISARM;
        break;

    case VP_TAMPER:
        PRINTF("[esp32] TAMPER: peak %u mg\r\n", (unsigned)vp_get_u16(p));
        ev.type  = EV_TAMPER;
        ev.value = vp_get_u16(p);
        break;

    default:
        /* MCU -> ESP32 types (ATTEMPT, STATUS, ...) are not commands for us. */
        PRINTF("[comms] ignoring %s from the ESP32\r\n", type_name(f->type));
        return true;
    }

    if (xQueueSend(xInputQueue, &ev, 0) != pdPASS) {
        PRINTF("[comms] input queue full, %s refused\r\n", type_name(f->type));
        return false;
    }
    return true;
}

static void handle_frame(const vp_frame_t *f)
{
    int want = expected_len(f->type);

    if (want < 0 || f->len != want) {
        PRINTF("[comms] rejected type 0x%x len %u (expected %d)\r\n", f->type,
               (unsigned)f->len, want);
        if (f->type != VP_ACK && f->type != VP_NAK) {
            send_ack(f->type, false);
        }
        return;
    }

    if (f->type == VP_ACK) {
        if (s_pending.active && f->payload[0] == s_pending.msg.type) {
            finish_pending(true);
        }
        return;
    }
    if (f->type == VP_NAK) {
        /* The echoed type may itself be corrupted: any NAK means "resend". */
        if (s_pending.active) {
            retry_pending();
        }
        return;
    }

    send_ack(f->type, post_event(f));
}

static void handle_byte(uint8_t byte)
{
    TickType_t     now = xTaskGetTickCount();
    vp_frame_t     frame;
    vp_rx_result_t res;

    if (vp_parser_busy(&s_parser) &&
        now - s_lastByteAt > pdMS_TO_TICKS(VP_INTERBYTE_TIMEOUT_MS)) {
        vp_parser_reset(&s_parser);
    }
    s_lastByteAt = now;

    res = vp_parser_feed(&s_parser, byte, &frame);
    if (res == VP_RX_OK) {
        handle_frame(&frame);
    } else if (res == VP_RX_BAD) {
        PRINTF("[comms] bad frame (type 0x%x), sending NAK\r\n", frame.type);
        if (frame.type != VP_ACK && frame.type != VP_NAK) {
            send_ack(frame.type, false);
        }
    }
}

/* ---- Task ----------------------------------------------------------------- */

void vault_comms_task(void *argument)
{
    uint8_t byte;

    (void)argument;
    for (;;) {
        /* Block until a byte arrives (or COMMS_POLL_MS passes), then drain. */
        if (xQueueReceive(xUartRxQueue, &byte, pdMS_TO_TICKS(COMMS_POLL_MS)) == pdTRUE) {
            do {
                handle_byte(byte);
            } while (xQueueReceive(xUartRxQueue, &byte, 0) == pdTRUE);
        }

        /* A frame that stopped half-way with no more bytes coming. */
        if (vp_parser_busy(&s_parser) &&
            xTaskGetTickCount() - s_lastByteAt > pdMS_TO_TICKS(VP_INTERBYTE_TIMEOUT_MS)) {
            vp_parser_reset(&s_parser);
        }

        service_tx();
    }
}

/* ---- Public API ----------------------------------------------------------- */

bool vault_comms_init(void)
{
    lpuart_config_t config;

    if (xUartRxQueue == NULL || xUartTxQueue == NULL || xInputQueue == NULL ||
        xStateMutex == NULL) {
        return false;
    }
    vp_parser_reset(&s_parser);

    CLOCK_EnableClock(COMMS_PORT_CLOCK);
    PORT_SetPinMux(COMMS_PORT, COMMS_TX_PIN, COMMS_PIN_MUX);
    PORT_SetPinMux(COMMS_PORT, COMMS_RX_PIN, COMMS_PIN_MUX);

    CLOCK_SetLpuart1Clock(COMMS_UART_CLKSRC);
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = VP_BAUD;
    config.enableTx     = true;
    config.enableRx     = true;
    if (LPUART_Init(COMMS_UART, &config, CLOCK_GetFreq(kCLOCK_McgIrc48MClk)) != kStatus_Success) {
        return false;
    }

    LPUART_EnableInterrupts(COMMS_UART, kLPUART_RxDataRegFullInterruptEnable |
                                            kLPUART_RxOverrunInterruptEnable);
    EnableIRQ(COMMS_UART_IRQn);
    return true;
}

bool vault_comms_link_up(void)
{
    return s_linkUp;
}
