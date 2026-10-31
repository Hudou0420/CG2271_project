#include "vault_fsm.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static VaultActions event(GameState *g, EventType type, int32_t value, uint32_t at) {
    VaultEvent e = {type, value, at};
    return vault_handle_event(g, &e);
}
static void enter(GameState *g, int code, uint32_t at) {
    vault_init(g);
    event(g, EV_ARM, code, at);
    assert(g->state == ST_LOCKED && vault_is_armed(g));
    event(g, EV_KNOCK, 0, at + 100);
    event(g, EV_KNOCK, 0, at + 400);
    event(g, EV_KNOCK, 0, at + 1300);
    assert(g->state == ST_DIALING && g->knock_count == 0);
}
static VaultActions guess(GameState *g, int code, uint32_t at) {
    event(g, EV_DIAL, code / 100, at);
    assert(!event(g, EV_PRESS, 0, at).send_solved);
    event(g, EV_DIAL, (code / 10) % 10, at + 1);
    assert(!event(g, EV_PRESS, 0, at + 1).send_solved);
    event(g, EV_DIAL, code % 10, at + 2);
    return event(g, EV_PRESS, 0, at + 2);
}
static void full_round(void) {
    GameState g;
    enter(&g, 7, 1000);
    assert(g.code[0] == 0 && g.code[1] == 0 && g.code[2] == 7);
    VaultActions a = guess(&g, 8, 3000);
    assert(a.send_attempt && !a.send_solved && g.state == ST_LOCKED && g.attempts == 1);
    assert(g.closeness == 97);
    event(&g, EV_KNOCK, 0, 4000);
    event(&g, EV_KNOCK, 0, 4300);
    event(&g, EV_KNOCK, 0, 5200);
    a = guess(&g, 7, 7000);
    assert(a.send_solved && !a.send_attempt && g.state == ST_UNLOCKED);
    assert(g.attempts == 2 && g.closeness == 100 && g.solve_time_s == 6);
    TxMsg packet;
    vault_make_packet(&g, PKT_SOLVED, &packet);
    assert(packet.len == 3 && packet.payload[0] == 2 && packet.payload[1] == 0 && packet.payload[2] == 6);
    event(&g, EV_ARM, 42, 8000);
    assert(g.state == ST_LOCKED && g.attempts == 0 && g.digit_index == 0 && g.knock_count == 0);
    event(&g, EV_DISARM, 0, 8100);
    assert(g.state == ST_IDLE && !vault_is_armed(&g));
    puts("PASS full round, leading zeroes, wrong guess, retry, solve, packets and rearm");
}
static void knock_boundaries(void) {
    const uint32_t gaps[][2] = {{210,630},{390,1170},{209,900},{391,900},{300,629},{300,1171}};
    for (unsigned i=0;i<sizeof(gaps)/sizeof(gaps[0]);++i) {
        uint32_t t[] = {100, 100+gaps[i][0], 100+gaps[i][0]+gaps[i][1]};
        assert(vault_knock_matches(t) == (i < 2));
    }
    GameState g; vault_init(&g); event(&g,EV_ARM,123,1);
    event(&g,EV_KNOCK,0,100); event(&g,EV_KNOCK,0,2101);
    assert(g.knock_count == 1 && g.knock_times[0] == 2101);
    event(&g,EV_KNOCK,0,2401); event(&g,EV_KNOCK,0,3301);
    assert(g.state == ST_DIALING);
    enter(&g,123,UINT32_MAX-700); // accepted capture times across timer rollover
    assert(g.state == ST_DIALING);
    puts("PASS knock tolerance edges, timeout reset and timestamp rollover");
}
static void state_restrictions(void) {
    for (int state=ST_IDLE;state<=ST_ALARM;++state) {
        for (int kind=EV_NONE;kind<=EV_LOCKOUT_DONE;++kind) {
            GameState g;vault_init(&g);g.state=(VaultState)state;
            GameState before=g;
            VaultActions a=event(&g,(EventType)kind,1,100);
            if (kind==EV_DIAL && state!=ST_DIALING) assert(!memcmp(&before,&g,sizeof(g)));
            if (kind==EV_PRESS && state!=ST_DIALING) assert(!memcmp(&before,&g,sizeof(g)));
            if (kind==EV_DISARM && state==ST_ALARM) assert(g.state==ST_ALARM);
            if (kind==EV_ARM && (state==ST_DIALING || state==ST_ALARM)) assert(!memcmp(&before,&g,sizeof(g)));
            if ((kind==EV_TAMPER || kind==EV_BEAM_BROKEN) && (state==ST_LOCKED || state==ST_DIALING)) {
                assert(g.state==ST_ALARM && a.send_alarm && a.start_lockout);
                assert(g.alarm_cause==(kind==EV_TAMPER?ALARM_TAMPER:ALARM_BEAM));
                assert(!event(&g,EV_TAMPER,0,101).start_lockout);
                event(&g,EV_LOCKOUT_DONE,0,30100);
                assert(g.state==ST_LOCKED && g.alarm_cause==ALARM_NONE);
            }
        }
    }
    GameState g;enter(&g,0,1);
    event(&g,EV_DIAL,-1,2000);assert(g.current_digit==9);
    event(&g,EV_DIAL,1,2001);assert(g.current_digit==0);
    assert(guess(&g,0,70000000).send_solved && g.solve_time_s==UINT16_MAX);
    assert(!strcmp(vault_state_name((VaultState)100),"?"));
    puts("PASS state/event restrictions, alarms, digit wrap and solve-time saturation");
}
static void digit_feedback(void) {
    static const struct { uint8_t code[3], guess[3], expected[3]; } cases[] = {
        {{1,2,3},{2,1,5},{1,1,0}},
        {{1,2,3},{1,2,5},{2,2,0}},
        {{1,2,3},{1,2,3},{2,2,2}},
        {{1,2,3},{4,5,6},{0,0,0}},
        {{1,2,3},{2,2,1},{0,2,1}},
        {{1,1,2},{2,2,1},{1,0,1}},
        {{1,1,2},{1,1,1},{2,2,0}},
        {{0,0,7},{7,0,0},{1,2,1}},
        {{0,0,0},{0,0,0},{2,2,2}},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        uint8_t feedback[3];
        vault_digit_feedback(cases[i].code,cases[i].guess,feedback);
        assert(!memcmp(feedback,cases[i].expected,3));
        GameState g;vault_init(&g);
        memcpy(g.code,cases[i].code,3);memcpy(g.guess,cases[i].guess,3);
        TxMsg packet;vault_make_packet(&g,PKT_ATTEMPT,&packet);
        assert(packet.len==8 && !memcmp(packet.payload,g.guess,3));
        assert(!memcmp(packet.payload+5,feedback,3));
    }
    puts("PASS per-digit feedback: requested examples, exact-first duplicate accounting, absent digits and leading zeroes");
}
int main(void) {full_round();knock_boundaries();state_restrictions();digit_feedback();}
