#include "../esp32/src/link.cpp"
#include "fixtures/wire_vectors.h"
#include <cassert>
static unsigned received,delivered,failed;
static vp_frame_t last;
static void rx(const vp_frame_t &frame) {last=frame;++received;}
static void tx(uint8_t type,bool ok) {(void)type;if(ok)++delivered;else ++failed;}
static void reset() {
    test_runtime_reset();s_stats={};s_pending={};s_lastByteMs=0;
    received=delivered=failed=0;Serial1.incoming.clear();Serial1.outgoing.clear();
    link_init(rx,tx);
}
static void incoming(uint8_t type,const uint8_t *data,uint8_t size) {
    uint8_t bytes[VP_MAX_FRAME];size_t n=vp_encode(type,data,size,bytes,sizeof(bytes));assert(n);
    Serial1.incoming.insert(Serial1.incoming.end(),bytes,bytes+n);service_rx();
}
static vp_frame_t output() {
    vp_parser_t parser;vp_parser_reset(&parser);vp_frame_t frame{};bool got=false;
    for(auto byte:Serial1.outgoing)if(vp_parser_feed(&parser,byte,&frame)==VP_RX_OK)got=true;
    assert(got);Serial1.outgoing.clear();return frame;
}
int main() {
    reset();
    for(const auto &bytes:{std::vector<uint8_t>(golden_solved,golden_solved+sizeof(golden_solved)),
                          std::vector<uint8_t>(golden_tamper_alarm,golden_tamper_alarm+sizeof(golden_tamper_alarm)),
                          std::vector<uint8_t>(golden_attempt,golden_attempt+sizeof(golden_attempt))}) {
        Serial1.incoming.insert(Serial1.incoming.end(),bytes.begin(),bytes.end());service_rx();
        assert(output().type==VP_ACK);
        if(last.type==VP_SOLVED)assert(last.payload[0]==4 && vp_get_u16(last.payload+1)==0x1234);
        else if(last.type==VP_ATTEMPT)assert(last.len==8 && last.payload[5]==1 && last.payload[6]==1 && last.payload[7]==0);
        else assert(last.type==VP_ALARM && last.payload[0]==VP_CAUSE_TAMPER);
    }
    assert(received==3);
    uint8_t attempt[]={2,1,5,1,86,1,1,0};
    incoming(VP_ATTEMPT,attempt,VP_LEN_ATTEMPT_LEGACY);
    assert(last.len==5 && received==4 && output().type==VP_ACK);
    incoming(VP_ATTEMPT,attempt,7);assert(received==4 && output().type==VP_NAK);
    attempt[5]=3;incoming(VP_ATTEMPT,attempt,8);assert(received==4 && output().type==VP_NAK);
    uint8_t arm[]={0,0,7,0};assert(link_send(VP_ARM,arm,4));service_tx();
    assert(output().type==VP_ARM && s_pending.active);
    uint8_t wrong=VP_DISARM;incoming(VP_ACK,&wrong,1);assert(s_pending.active && delivered==0);
    test_now=99;service_tx();assert(s_stats.txRetries==0);
    test_now=100;service_tx();assert(s_stats.txRetries==1);output();
    incoming(VP_NAK,&wrong,1);assert(s_stats.txRetries==2);output();
    test_now+=100;service_tx();assert(s_stats.txRetries==3);output();
    test_now+=100;service_tx();assert(failed==1 && !s_pending.active && Serial1.outgoing.empty());
    assert(link_send(VP_DISARM,nullptr,0));service_tx();output();
    incoming(VP_ACK,&wrong,1);assert(delivered==1 && !s_pending.active);
    assert(!link_send(VP_ARM,arm,VP_MAX_PAYLOAD+1));
    for(unsigned i=0;i<LINK_TX_QUEUE_LEN;++i)assert(link_send(VP_DISARM,nullptr,0));
    assert(!link_send(VP_DISARM,nullptr,0));
    puts("PASS ESP32 golden MCU frames, ACK/NAK, retries, callbacks and queue backpressure");
    reset();test_now=UINT32_MAX-25;
    Serial1.incoming={VP_START,VP_STATUS};service_rx();assert(vp_parser_busy(&s_parser));
    test_now+=VP_INTERBYTE_TIMEOUT_MS+1;service_rx();assert(!vp_parser_busy(&s_parser) && s_stats.rxTimeouts==1);
    uint8_t status[VP_LEN_STATUS]={};incoming(VP_STATUS,status,VP_LEN_STATUS);assert(received==1 && output().type==VP_ACK);
    incoming(VP_STATUS,status,VP_LEN_STATUS-1);assert(received==1 && output().type==VP_NAK);
    uint8_t bytes[VP_MAX_FRAME];size_t n=vp_encode(VP_STATUS,status,VP_LEN_STATUS,bytes,sizeof(bytes));bytes[n-1]^=1;
    Serial1.incoming.insert(Serial1.incoming.end(),bytes,bytes+n);service_rx();assert(received==1 && output().type==VP_NAK);
    incoming(VP_ACK,nullptr,0);assert(Serial1.outgoing.empty()); // do not ACK/NAK malformed ACKs
    incoming(VP_STATUS,status,VP_LEN_STATUS);assert(received==2 && output().type==VP_ACK);
    puts("PASS ESP32 rollover, partial/corrupt frames, length validation and parser recovery");
    test_runtime_reset();
}
