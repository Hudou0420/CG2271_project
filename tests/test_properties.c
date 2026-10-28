#include "vault_fsm.h"
#include "vault_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t rng;
static uint32_t random_word(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char **argv) {
    assert(argc==3);rng=(uint32_t)strtoul(argv[1],NULL,0);if(!rng)rng=1;
    unsigned count=(unsigned)strtoul(argv[2],NULL,0);
    printf("Replay: VAULT_TEST_SEED=%s VAULT_TEST_ITERATIONS=%s python3 ci/run_tests.py\n",argv[1],argv[2]);fflush(stdout);
    for(unsigned test=0;test<count;++test) {
        vp_parser_t parser;vp_parser_reset(&parser);vp_frame_t out;
        uint8_t data[VP_MAX_PAYLOAD],wire[VP_MAX_FRAME];
        uint8_t len=(uint8_t)(random_word()%(VP_MAX_PAYLOAD+1));
        uint8_t type=(uint8_t)random_word();
        for(unsigned j=0;j<len;++j)data[j]=(uint8_t)random_word();
        size_t n=vp_encode(type,data,len,wire,sizeof(wire));assert(n==(size_t)len+4);
        for(size_t j=0;j<n;++j)assert(vp_parser_feed(&parser,wire[j],&out)==(j==n-1?VP_RX_OK:VP_RX_NONE));
        assert(out.type==type && out.len==len && !memcmp(out.payload,data,len));
        wire[n-1]^=(uint8_t)(1u<<(random_word()%8));
        for(size_t j=0;j<n-1;++j)(void)vp_parser_feed(&parser,wire[j],&out);
        assert(vp_parser_feed(&parser,wire[n-1],&out)==VP_RX_BAD);
        for(unsigned j=0;j<256;++j) {
            vp_rx_result_t r=vp_parser_feed(&parser,(uint8_t)random_word(),&out);
            assert(r==VP_RX_NONE || r==VP_RX_BAD || r==VP_RX_OK);
            if(r==VP_RX_OK)assert(out.len<=VP_MAX_PAYLOAD);
        }
        vp_parser_reset(&parser);
        n=vp_encode(VP_DISARM,NULL,0,wire,sizeof(wire));
        for(size_t j=0;j<n;++j)(void)vp_parser_feed(&parser,wire[j],&out);
        assert(out.type==VP_DISARM && !vp_parser_busy(&parser));
        GameState g;vault_init(&g);uint32_t clock=random_word();
        for(unsigned j=0;j<64;++j) {
            clock+=random_word()%3000;
            EventType kind=(EventType)(random_word()%(EV_LOCKOUT_DONE+1));
            int32_t value=kind==EV_ARM?(int32_t)(random_word()%1000):(int32_t)(random_word()%3)-1;
            VaultEvent e={kind,value,clock};VaultActions a=vault_handle_event(&g,&e);
            assert(g.state>=ST_IDLE && g.state<=ST_ALARM && g.current_digit<10 && g.digit_index<CODE_LEN && g.knock_count<KNOCK_COUNT);
            assert(g.closeness<=100);
            assert(!a.send_solved || (g.state==ST_UNLOCKED && !memcmp(g.code,g.guess,CODE_LEN)));
            assert(!a.send_attempt || g.state==ST_LOCKED);
            assert(!a.send_alarm || (g.state==ST_ALARM && a.start_lockout));
        }
    }
    printf("PASS %u seeded parser/game scenarios\n",count);
}
