// Unit tests for the shared protocol code. Run on the laptop:
//   pio test -e native
#include <string.h>
#include <unity.h>

#include "vault_protocol.h"

static vp_parser_t parser;

void setUp(void)
{
    vp_parser_reset(&parser);
}

void tearDown(void) {}

// Feed bytes; return the last non-NONE result (and the frame it produced).
static vp_rx_result_t feed_all(const uint8_t *buf, size_t n, vp_frame_t *out,
                               int *frames)
{
    vp_rx_result_t last = VP_RX_NONE;
    *frames             = 0;
    for (size_t i = 0; i < n; i++) {
        vp_rx_result_t r = vp_parser_feed(&parser, buf[i], out);
        if (r != VP_RX_NONE) {
            last = r;
            (*frames)++;
        }
    }
    return last;
}

static void test_encode_layout(void)
{
    uint8_t payload[] = {4, 2, 7, 1};
    uint8_t buf[VP_MAX_FRAME];
    size_t  n = vp_encode(VP_ARM, payload, 4, buf, sizeof(buf));

    uint8_t expect[] = {0xAA, 0x01, 0x04, 4, 2, 7, 1, 0x01 ^ 0x04 ^ 4 ^ 2 ^ 7 ^ 1};
    TEST_ASSERT_EQUAL(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

static void test_round_trip(void)
{
    uint8_t    payload[] = {1, 2, 3, 4, 5};
    uint8_t    buf[VP_MAX_FRAME];
    vp_frame_t f;
    int        frames;
    size_t     n = vp_encode(VP_ATTEMPT, payload, 5, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(VP_RX_OK, feed_all(buf, n, &f, &frames));
    TEST_ASSERT_EQUAL(1, frames);
    TEST_ASSERT_EQUAL_HEX8(VP_ATTEMPT, f.type);
    TEST_ASSERT_EQUAL(5, f.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, 5);
    TEST_ASSERT_FALSE(vp_parser_busy(&parser));
}

static void test_empty_payload(void)
{
    uint8_t    buf[VP_MAX_FRAME];
    vp_frame_t f;
    int        frames;
    size_t     n = vp_encode(VP_DISARM, NULL, 0, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL(VP_RX_OK, feed_all(buf, n, &f, &frames));
    TEST_ASSERT_EQUAL_HEX8(VP_DISARM, f.type);
    TEST_ASSERT_EQUAL(0, f.len);
}

static void test_bad_checksum(void)
{
    uint8_t    payload[] = {VP_STATE_LOCKED};
    uint8_t    buf[VP_MAX_FRAME];
    vp_frame_t f;
    int        frames;
    size_t     n = vp_encode(VP_STATUS, payload, 1, buf, sizeof(buf));

    buf[3] ^= 0x01; // flip a payload bit
    TEST_ASSERT_EQUAL(VP_RX_BAD, feed_all(buf, n, &f, &frames));
    TEST_ASSERT_EQUAL_HEX8(VP_STATUS, f.type); // still known, for the NAK
}

static void test_noise_before_start(void)
{
    uint8_t    raw[] = {0x00, 0x13, 0xFF, 0x55};
    uint8_t    buf[16 + VP_MAX_FRAME];
    uint8_t    payload[] = {VP_CAUSE_BEAM};
    vp_frame_t f;
    int        frames;

    memcpy(buf, raw, sizeof(raw));
    size_t n = sizeof(raw) +
               vp_encode(VP_ALARM, payload, 1, buf + sizeof(raw), VP_MAX_FRAME);

    TEST_ASSERT_EQUAL(VP_RX_OK, feed_all(buf, n, &f, &frames));
    TEST_ASSERT_EQUAL(1, frames);
    TEST_ASSERT_EQUAL_HEX8(VP_ALARM, f.type);
}

static void test_start_byte_inside_payload(void)
{
    uint8_t    payload[] = {0xAA, 0xAA}; // TAMPER magnitude 0xAAAA
    uint8_t    buf[VP_MAX_FRAME];
    vp_frame_t f;
    int        frames;
    size_t     n = vp_encode(VP_TAMPER, payload, 2, buf, sizeof(buf));

    TEST_ASSERT_EQUAL(VP_RX_OK, feed_all(buf, n, &f, &frames));
    TEST_ASSERT_EQUAL(0xAAAA, vp_get_u16(f.payload));
}

static void test_oversize_len_then_recover(void)
{
    uint8_t    junk[] = {0xAA, VP_ATTEMPT, 200}; // impossible LEN
    uint8_t    payload[] = {VP_STATE_IDLE};
    uint8_t    buf[VP_MAX_FRAME];
    vp_frame_t f;
    int        frames;

    TEST_ASSERT_EQUAL(VP_RX_BAD, feed_all(junk, sizeof(junk), &f, &frames));
    TEST_ASSERT_FALSE(vp_parser_busy(&parser));

    size_t n = vp_encode(VP_STATUS, payload, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(VP_RX_OK, feed_all(buf, n, &f, &frames));
    TEST_ASSERT_EQUAL_HEX8(VP_STATUS, f.type);
}

static void test_back_to_back_frames(void)
{
    uint8_t    buf[2 * VP_MAX_FRAME];
    uint8_t    solved[3];
    uint8_t    ack[] = {VP_ARM};
    vp_frame_t f;
    size_t     n = 0;

    solved[0] = 3;
    vp_put_u16(&solved[1], 754);
    n += vp_encode(VP_SOLVED, solved, 3, buf, sizeof(buf));
    n += vp_encode(VP_ACK, ack, 1, buf + n, sizeof(buf) - n);

    int ok = 0;
    for (size_t i = 0; i < n; i++) {
        if (vp_parser_feed(&parser, buf[i], &f) == VP_RX_OK) {
            ok++;
            if (ok == 1) {
                TEST_ASSERT_EQUAL_HEX8(VP_SOLVED, f.type);
                TEST_ASSERT_EQUAL(754, vp_get_u16(&f.payload[1]));
            } else {
                TEST_ASSERT_EQUAL_HEX8(VP_ACK, f.type);
                TEST_ASSERT_EQUAL_HEX8(VP_ARM, f.payload[0]);
            }
        }
    }
    TEST_ASSERT_EQUAL(2, ok);
}

static void test_encode_rejects_bad_sizes(void)
{
    uint8_t payload[VP_MAX_PAYLOAD + 1] = {0};
    uint8_t buf[VP_MAX_FRAME + 1];

    TEST_ASSERT_EQUAL(0, vp_encode(VP_ARM, payload, VP_MAX_PAYLOAD + 1, buf,
                                   sizeof(buf)));
    TEST_ASSERT_EQUAL(0, vp_encode(VP_ARM, payload, 4, buf, 7)); // needs 8
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_layout);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_empty_payload);
    RUN_TEST(test_bad_checksum);
    RUN_TEST(test_noise_before_start);
    RUN_TEST(test_start_byte_inside_payload);
    RUN_TEST(test_oversize_len_then_recover);
    RUN_TEST(test_back_to_back_frames);
    RUN_TEST(test_encode_rejects_bad_sizes);
    return UNITY_END();
}
