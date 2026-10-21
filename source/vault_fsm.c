/*
 * vault_fsm.c  --  Person 1
 *
 * The brain of the vault. PURE C: no FreeRTOS, no PRINTF, no hardware.
 * That is what lets you test it on your laptop with test_fsm.c.
 */
#include "vault_fsm.h"
#include <string.h>

/* The secret rhythm, as GAPS between knocks in ms.
 * knock .. (300 ms) .. knock .. (900 ms pause) .. knock
 * Change these two numbers to change the secret knock. */
static const uint32_t KNOCK_PATTERN_GAPS[KNOCK_GAPS] = { 300, 900 };

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
void vault_init(GameState *g)
{
    memset(g, 0, sizeof(*g));
    g->state = ST_IDLE;
}

bool vault_is_armed(const GameState *g)
{
    return (g->state == ST_LOCKED) || (g->state == ST_DIALING);
}

const char *vault_state_name(VaultState s)
{
    static const char *names[] = { "IDLE", "LOCKED", "DIALING", "UNLOCKED", "ALARM" };
    return ((unsigned)s < 5u) ? names[s] : "?";
}

/* Is |actual - expected| within KNOCK_TOLERANCE_PCT of expected?
 * (integer maths only, no floats) */
static bool gap_ok(uint32_t actual, uint32_t expected)
{
    uint32_t diff = (actual > expected) ? (actual - expected) : (expected - actual);
    return (diff * 100u) <= (expected * KNOCK_TOLERANCE_PCT);
}

bool vault_knock_matches(const uint32_t times[KNOCK_COUNT])
{
    for (int i = 0; i < KNOCK_GAPS; i++) {
        uint32_t gap = times[i + 1] - times[i];   /* huge if time went backwards -> fails */
        if (!gap_ok(gap, KNOCK_PATTERN_GAPS[i])) {
            return false;
        }
    }
    return true;
}

uint8_t vault_closeness(const uint8_t code[CODE_LEN], const uint8_t guess[CODE_LEN])
{
    int dist = 0;
    for (int i = 0; i < CODE_LEN; i++) {
        int d = (int)code[i] - (int)guess[i];
        dist += (d < 0) ? -d : d;
    }
    int max_dist = 9 * CODE_LEN;
    return (uint8_t)(100 - (dist * 100) / max_dist);
}

void vault_make_packet(const GameState *g, uint8_t pkt_type, TxMsg *out)
{
    memset(out, 0, sizeof(*out));
    out->type = pkt_type;

    switch (pkt_type) {
    case PKT_ATTEMPT:
        out->len = 5;
        out->payload[0] = g->guess[0];
        out->payload[1] = g->guess[1];
        out->payload[2] = g->guess[2];
        out->payload[3] = g->attempts;
        out->payload[4] = g->closeness;
        break;

    case PKT_SOLVED:
        out->len = 3;
        out->payload[0] = g->attempts;
        out->payload[1] = (uint8_t)(g->solve_time_s >> 8);
        out->payload[2] = (uint8_t)(g->solve_time_s & 0xFF);
        break;

    case PKT_ALARM:
        out->len = 1;
        out->payload[0] = (uint8_t)g->alarm_cause;
        break;

    default:
        out->len = 0;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The FSM                                                             */
/* ------------------------------------------------------------------ */
VaultActions vault_handle_event(GameState *g, const VaultEvent *e)
{
    VaultActions act = { false, false, false, false };

    switch (e->type) {

    /* ---------- ARM: set a new code and go to LOCKED ---------- */
    case EV_ARM:
        if (g->state == ST_IDLE || g->state == ST_LOCKED || g->state == ST_UNLOCKED) {
            uint32_t v = (uint32_t)e->value;          /* e.g. 123 -> 1,2,3 */
            for (int i = CODE_LEN - 1; i >= 0; i--) {
                g->code[i] = (uint8_t)(v % 10u);
                v /= 10u;
            }
            memset(g->guess, 0, sizeof(g->guess));
            g->digit_index   = 0;
            g->current_digit = 0;
            g->attempts      = 0;
            g->closeness     = 0;
            g->knock_count   = 0;
            g->alarm_cause   = ALARM_NONE;
            g->start_ms      = e->tick_ms;
            g->state         = ST_LOCKED;
        }
        break;

    /* ---------- DISARM: back to IDLE (not allowed during ALARM) ---------- */
    case EV_DISARM:
        if (g->state != ST_ALARM) {
            g->knock_count = 0;
            g->state = ST_IDLE;
        }
        break;

    /* ---------- KNOCK: only matters while LOCKED ---------- */
    case EV_KNOCK:
        if (g->state == ST_LOCKED) {
            /* Too long since the previous knock? Start the rhythm again. */
            if (g->knock_count > 0) {
                uint32_t last = g->knock_times[g->knock_count - 1];
                if ((e->tick_ms - last) > KNOCK_TIMEOUT_MS) {
                    g->knock_count = 0;
                }
            }
            g->knock_times[g->knock_count++] = e->tick_ms;

            if (g->knock_count == KNOCK_COUNT) {
                bool ok = vault_knock_matches(g->knock_times);
                g->knock_count = 0;                   /* always start fresh */
                if (ok) {
                    memset(g->guess, 0, sizeof(g->guess));
                    g->digit_index   = 0;
                    g->current_digit = 0;
                    g->state = ST_DIALING;
                }
            }
        }
        break;

    /* ---------- DIAL: turn the knob, digit wraps 0-9 ---------- */
    case EV_DIAL:
        if (g->state == ST_DIALING) {
            int d = (int)g->current_digit + (int)e->value;
            g->current_digit = (uint8_t)(((d % 10) + 10) % 10);
        }
        break;

    /* ---------- PRESS: confirm this digit ---------- */
    case EV_PRESS:
        if (g->state == ST_DIALING) {
            g->guess[g->digit_index++] = g->current_digit;
            g->current_digit = 0;

            if (g->digit_index >= CODE_LEN) {          /* all digits entered */
                g->digit_index = 0;
                g->attempts++;

                if (memcmp(g->code, g->guess, CODE_LEN) == 0) {
                    uint32_t secs = (e->tick_ms - g->start_ms) / 1000u;
                    g->solve_time_s = (secs > 65535u) ? 65535u : (uint16_t)secs;
                    g->closeness    = 100;
                    g->state        = ST_UNLOCKED;
                    act.send_solved = true;
                } else {
                    g->closeness    = vault_closeness(g->code, g->guess);
                    g->state        = ST_LOCKED;       /* must knock again */
                    act.send_attempt = true;
                }
            }
        }
        break;

    /* ---------- BEAM BROKEN / TAMPER: alarm if armed ---------- */
    case EV_BEAM_BROKEN:
    case EV_TAMPER:
        if (vault_is_armed(g)) {
            g->alarm_cause = (e->type == EV_BEAM_BROKEN) ? ALARM_BEAM : ALARM_TAMPER;
            g->knock_count = 0;
            g->state       = ST_ALARM;
            act.send_alarm    = true;
            act.start_lockout = true;
        }
        break;

    /* ---------- LOCKOUT_DONE: alarm over, back to LOCKED ---------- */
    case EV_LOCKOUT_DONE:
        if (g->state == ST_ALARM) {
            g->alarm_cause = ALARM_NONE;
            g->state       = ST_LOCKED;
        }
        break;

    default:
        break;
    }

    return act;
}
