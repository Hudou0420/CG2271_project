/*
 * vault_protocol.h - MCXC444 <-> ESP32 UART protocol (shared by both boards).
 *
 * Plain C, no platform dependencies: both the ESP32 (PlatformIO) and the
 * MCXC444 (MCUXpresso) compile this same file so the two sides cannot drift.
 *
 * Frame:  | START 0xAA | TYPE | LEN | PAYLOAD (LEN bytes) | CHECKSUM |
 *         CHECKSUM = TYPE ^ LEN ^ payload[0] ^ ... ^ payload[LEN-1]
 *
 * Reliability: every good frame (except ACK/NAK) is answered with ACK,
 * a bad checksum with NAK. ACK/NAK carry the echoed TYPE as a 1-byte payload.
 * The sender retransmits on NAK or after VP_ACK_TIMEOUT_MS, up to
 * VP_MAX_RETRIES times.
 *
 * Multi-byte payload fields are little-endian.
 */
#ifndef VAULT_PROTOCOL_H
#define VAULT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Link parameters ---------------------------------------------------- */
#define VP_BAUD                 115200
#define VP_START                0xAA
#define VP_MAX_PAYLOAD          16
#define VP_MAX_FRAME            (VP_MAX_PAYLOAD + 4) /* START TYPE LEN .. CHK */
#define VP_ACK_TIMEOUT_MS       100  /* wait this long for ACK before resend */
#define VP_MAX_RETRIES          3    /* resends after the first attempt */
#define VP_INTERBYTE_TIMEOUT_MS 50   /* gap inside a frame -> drop it */

/* ---- Message types ------------------------------------------------------ */
typedef enum {
    /* ESP32 -> MCU */
    VP_ARM     = 0x01, /* d0 d1 d2 pattern_id                         (4 B) */
    VP_DISARM  = 0x02, /* -                                           (0 B) */
    VP_TAMPER  = 0x03, /* magnitude_mg (uint16 LE)                    (2 B) */

    /* MCU -> ESP32 */
    VP_ATTEMPT = 0x10, /* d0 d1 d2 attempt_no closeness feedback[3]   (8 B) */
    VP_SOLVED  = 0x11, /* attempts, seconds (uint16 LE)               (3 B) */
    VP_ALARM   = 0x12, /* cause (vp_alarm_cause_t)                    (1 B) */
    VP_STATUS  = 0x13, /* state + live sensor snapshot, see VP_ST_*  (12 B) */

    /* Both directions */
    VP_ACK     = 0x20, /* echoed TYPE                                 (1 B) */
    VP_NAK     = 0x21  /* echoed TYPE (may be garbage if corrupted)   (1 B) */
} vp_type_t;

/* Payload lengths, so both sides validate the same sizes. */
#define VP_LEN_ARM     4
#define VP_LEN_DISARM  0
#define VP_LEN_TAMPER  2
#define VP_LEN_ATTEMPT_LEGACY 5 /* ESP32 accepts old MCU frames without tile feedback. */
#define VP_LEN_ATTEMPT 8
#define VP_ATTEMPT_FEEDBACK 5 /* Three positional bytes: absent=0, present=1, correct=2. */
#define VP_DIGIT_ABSENT  0
#define VP_DIGIT_PRESENT 1
#define VP_DIGIT_CORRECT 2
#define VP_LEN_SOLVED  3
#define VP_LEN_ALARM   1
#define VP_LEN_STATUS  12
#define VP_LEN_ACK     1
#define VP_LEN_NAK     1

/* Game FSM state carried in STATUS. */
typedef enum {
    VP_STATE_IDLE     = 0,
    VP_STATE_LOCKED   = 1,
    VP_STATE_DIALING  = 2,
    VP_STATE_UNLOCKED = 3,
    VP_STATE_ALARM    = 4
} vp_state_t;

/* STATUS payload offsets. Sent every second, and within ~100 ms of a change. */
#define VP_ST_STATE         0  /* vp_state_t                                  */
#define VP_ST_DIGIT_INDEX   1  /* digits confirmed so far (0..2)              */
#define VP_ST_CURRENT_DIGIT 2  /* digit currently on the dial (0..9)          */
#define VP_ST_KNOCKS        3  /* knocks counted towards the rhythm           */
#define VP_ST_ATTEMPTS      4  /* attempts since arming                       */
#define VP_ST_CLOSENESS     5  /* last attempt's closeness, 0..100            */
#define VP_ST_LIGHT         6  /* light sensor raw ADC, 0..4095 (uint16 LE)   */
#define VP_ST_ENCODER       8  /* raw encoder steps (int16 LE, wraps)         */
#define VP_ST_EVENTS        10 /* sensor events sent to the game (wraps)      */
#define VP_ST_FLAGS         11 /* VP_STF_* bits                               */

#define VP_STF_LIGHT_VALID  0x01 /* VP_ST_LIGHT holds a real reading      */
#define VP_STF_BEAM_ENABLED 0x02 /* beam-break detection is switched on   */
#define VP_STF_INPUTS_READY 0x04 /* tap/encoder inputs are running        */

/* Cause carried in ALARM. */
typedef enum {
    VP_CAUSE_BEAM   = 0,
    VP_CAUSE_TAMPER = 1
} vp_alarm_cause_t;

/* ---- Frames ------------------------------------------------------------- */
typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t payload[VP_MAX_PAYLOAD];
} vp_frame_t;

/*
 * Encode a frame into buf. Returns the number of bytes written, or 0 if
 * len > VP_MAX_PAYLOAD or buf_size is too small.
 */
size_t vp_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                 uint8_t *buf, size_t buf_size);

/* ---- Receiver ----------------------------------------------------------- */
typedef enum {
    VP_RX_NONE = 0, /* byte consumed, no complete frame yet */
    VP_RX_OK,       /* *out holds a valid frame */
    VP_RX_BAD       /* checksum or length error; out->type is the TYPE seen */
} vp_rx_result_t;

typedef struct {
    uint8_t    stage;
    uint8_t    idx;
    uint8_t    chk;
    vp_frame_t frame;
} vp_parser_t;

void vp_parser_reset(vp_parser_t *p);

/* Feed one received byte. On VP_RX_OK / VP_RX_BAD, *out is filled in. */
vp_rx_result_t vp_parser_feed(vp_parser_t *p, uint8_t byte, vp_frame_t *out);

/* Non-zero while part-way through a frame (used for the inter-byte timeout). */
int vp_parser_busy(const vp_parser_t *p);

/* Little-endian helpers for multi-byte payload fields. */
static inline void vp_put_u16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)(v >> 8);
}

static inline uint16_t vp_get_u16(const uint8_t *src)
{
    return (uint16_t)(src[0] | ((uint16_t)src[1] << 8));
}

#ifdef __cplusplus
}
#endif

#endif /* VAULT_PROTOCOL_H */
