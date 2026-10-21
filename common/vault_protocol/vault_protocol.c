/*
 * vault_protocol.c - frame encoder and byte-at-a-time parser.
 * See vault_protocol.h for the frame format.
 */
#include "vault_protocol.h"

enum {
    STAGE_START = 0,
    STAGE_TYPE,
    STAGE_LEN,
    STAGE_PAYLOAD,
    STAGE_CHECKSUM
};

size_t vp_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                 uint8_t *buf, size_t buf_size)
{
    size_t  n = 0;
    uint8_t chk;
    uint8_t i;

    if (len > VP_MAX_PAYLOAD || buf_size < (size_t)len + 4) {
        return 0;
    }

    buf[n++] = VP_START;
    buf[n++] = type;
    buf[n++] = len;
    chk = type ^ len;
    for (i = 0; i < len; i++) {
        buf[n++] = payload[i];
        chk ^= payload[i];
    }
    buf[n++] = chk;
    return n;
}

void vp_parser_reset(vp_parser_t *p)
{
    p->stage = STAGE_START;
    p->idx   = 0;
    p->chk   = 0;
}

int vp_parser_busy(const vp_parser_t *p)
{
    return p->stage != STAGE_START;
}

vp_rx_result_t vp_parser_feed(vp_parser_t *p, uint8_t byte, vp_frame_t *out)
{
    switch (p->stage) {
    case STAGE_START:
        if (byte == VP_START) {
            p->stage = STAGE_TYPE;
        }
        /* Anything else is line noise between frames: ignore it. */
        return VP_RX_NONE;

    case STAGE_TYPE:
        p->frame.type = byte;
        p->chk        = byte;
        p->stage      = STAGE_LEN;
        return VP_RX_NONE;

    case STAGE_LEN:
        if (byte > VP_MAX_PAYLOAD) {
            /* Corrupted length: don't wait for up to 255 bytes of payload. */
            out->type = p->frame.type;
            out->len  = 0;
            vp_parser_reset(p);
            return VP_RX_BAD;
        }
        p->frame.len = byte;
        p->chk      ^= byte;
        p->idx       = 0;
        p->stage     = (byte == 0) ? STAGE_CHECKSUM : STAGE_PAYLOAD;
        return VP_RX_NONE;

    case STAGE_PAYLOAD:
        p->frame.payload[p->idx++] = byte;
        p->chk ^= byte;
        if (p->idx >= p->frame.len) {
            p->stage = STAGE_CHECKSUM;
        }
        return VP_RX_NONE;

    case STAGE_CHECKSUM:
    default: {
        vp_rx_result_t res = (byte == p->chk) ? VP_RX_OK : VP_RX_BAD;
        *out = p->frame;
        vp_parser_reset(p);
        return res;
    }
    }
}
