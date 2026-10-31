/* Include the unchanged implementation to exercise its private state machine.
 * Only hardware/RTOS dependencies are fake; framing and conversion are real. */
#include "../source/vault_comms.c"
#include "vault_fsm.h"
#include "fixtures/wire_vectors.h"
#include <assert.h>
#include <stdio.h>
QueueHandle_t xInputQueue,xUartTxQueue,xUartRxQueue;
SemaphoreHandle_t xStateMutex;
GameState gGame;
static VaultSensorStats sensor_snapshot;
void vault_sensors_get_stats(VaultSensorStats *out) {*out=sensor_snapshot;}
static void reset(void) {
    test_runtime_reset();memset(&s_pending,0,sizeof(s_pending));vp_parser_reset(&s_parser);
    s_lastByteAt=0;s_statusSent=false;s_lastStatusAt=0;s_linkUp=false;s_uartErrors=0;s_rxDropped=0;
    test_uart_out_size=0;test_uart_flags=0;test_uart_cleared=0;test_uart_init_result=0;
    memset(&sensor_snapshot,0,sizeof(sensor_snapshot));memset(&gGame,0,sizeof(gGame));
    xInputQueue=xQueueCreate(INPUT_Q_LEN,sizeof(VaultEvent));xUartTxQueue=xQueueCreate(TX_Q_LEN,sizeof(TxMsg));
    xUartRxQueue=xQueueCreate(RX_Q_LEN,1);xStateMutex=xSemaphoreCreateMutex();
}
static vp_frame_t output(void) {
    vp_parser_t parser;vp_parser_reset(&parser);vp_frame_t f={0};bool got=false;
    for(size_t i=0;i<test_uart_out_size;++i)if(vp_parser_feed(&parser,test_uart_out[i],&f)==VP_RX_OK)got=true;
    assert(got);test_uart_out_size=0;return f;
}
static void feed(uint8_t type,const uint8_t *payload,uint8_t size) {
    uint8_t wire[VP_MAX_FRAME];size_t n=vp_encode(type,payload,size,wire,sizeof(wire));assert(n);
    for(size_t i=0;i<n;++i)handle_byte(wire[i]);
}
static void commands(void) {
    reset();assert(vault_comms_init());
    uint8_t arm[]={0,0,7,0};feed(VP_ARM,arm,4);
    vp_frame_t reply=output();assert(reply.type==VP_ACK && reply.payload[0]==VP_ARM);
    VaultEvent e;assert(xQueueReceive(xInputQueue,&e,0));assert(e.type==EV_ARM && e.value==7);
    arm[1]=10;feed(VP_ARM,arm,4);assert(output().type==VP_NAK);assert(!uxQueueMessagesWaiting(xInputQueue));
    arm[1]=0;feed(VP_ARM,arm,3);assert(output().type==VP_NAK);
    VaultEvent filler={EV_NONE,0,0};while(xQueueSend(xInputQueue,&filler,0)){}
    feed(VP_DISARM,NULL,0);assert(output().type==VP_NAK);
    uint8_t byte=0xaa;test_uart_byte=byte;
    test_uart_flags=kLPUART_RxOverrunFlag|kLPUART_RxDataRegFullFlag;
    COMMS_UART_IRQHandler();assert(s_uartErrors==1 && test_uart_cleared==kLPUART_RxOverrunFlag);
    assert(xQueueReceive(xUartRxQueue,&byte,0) && byte==0xaa);
    while(xQueueSend(xUartRxQueue,&byte,0)){}
    COMMS_UART_IRQHandler();assert(s_rxDropped==1);
    test_uart_init_result=-1;assert(!vault_comms_init());
    xInputQueue=NULL;assert(!vault_comms_init());
    puts("PASS MCU command validation, queue pressure, UART ISR errors and initialization failures");
}
static void feedback_packet(void) {
    reset();gGame.state=ST_LOCKED;gGame.attempts=1;gGame.closeness=86;
    const uint8_t code[]={1,2,3},guess[]={2,1,5};
    memcpy(gGame.code,code,3);memcpy(gGame.guess,guess,3);
    TxMsg tx;vault_make_packet(&gGame,PKT_ATTEMPT,&tx);
    assert(xQueueSend(xUartTxQueue,&tx,0));service_tx();
    assert(test_uart_out_size==sizeof(golden_attempt));
    assert(!memcmp(test_uart_out,golden_attempt,sizeof(golden_attempt)));
    puts("PASS MCU emits golden per-digit feedback without sending the code");
}
static void packets_and_status(void) {
    reset();gGame.state=ST_UNLOCKED;gGame.attempts=4;gGame.solve_time_s=0x1234;
    TxMsg tx;vault_make_packet(&gGame,PKT_SOLVED,&tx);assert(xQueueSend(xUartTxQueue,&tx,0));
    service_tx();assert(test_uart_out_size==sizeof(golden_solved));assert(!memcmp(test_uart_out,golden_solved,sizeof(golden_solved)));
    test_uart_out_size=0;uint8_t ack=VP_SOLVED;feed(VP_ACK,&ack,1);assert(!s_pending.active);
    gGame.alarm_cause=ALARM_TAMPER;vault_make_packet(&gGame,PKT_ALARM,&tx);xQueueSend(xUartTxQueue,&tx,0);
    service_tx();assert(test_uart_out_size==sizeof(golden_tamper_alarm));assert(!memcmp(test_uart_out,golden_tamper_alarm,sizeof(golden_tamper_alarm)));
    test_uart_out_size=0;ack=VP_ALARM;feed(VP_ACK,&ack,1);
    sensor_snapshot.encoder_steps=-2;sensor_snapshot.ldr_raw=0x123;
    sensor_snapshot.inputs_ready=true;sensor_snapshot.ldr_valid=true;
    service_tx();vp_frame_t status=output();assert(status.type==VP_STATUS && status.len==12);
    assert(vp_get_u16(status.payload+VP_ST_LIGHT)==0x123);
    assert((int16_t)vp_get_u16(status.payload+VP_ST_ENCODER)==-2);
    assert(status.payload[VP_ST_FLAGS]==(VP_STF_INPUTS_READY|VP_STF_LIGHT_VALID));
    ack=VP_STATUS;feed(VP_ACK,&ack,1);assert(vault_comms_link_up());
    test_now=999;service_tx();assert(!test_uart_out_size);
    test_now=1000;service_tx();assert(output().type==VP_STATUS);
    feed(VP_ACK,&ack,1);gGame.current_digit=3;
    test_now=1099;service_tx();assert(!test_uart_out_size);
    test_now=1100;service_tx();assert(output().payload[VP_ST_CURRENT_DIGIT]==3);
    puts("PASS MCU golden wire contract, endian/cause translation, sensor flags and heartbeat/change pacing");
}
static void retries_and_recovery(void) {
    reset();test_now=UINT32_MAX-50;
    TxMsg tx={PKT_ALARM,1,{ALARM_BEAM}};xQueueSend(xUartTxQueue,&tx,0);service_tx();output();
    uint8_t stray=VP_SOLVED;feed(VP_ACK,&stray,1);assert(s_pending.active);
    test_now+=VP_ACK_TIMEOUT_MS-1;service_tx();assert(s_pending.retries==0);
    ++test_now;service_tx();assert(s_pending.retries==1);output();
    feed(VP_NAK,&stray,1);assert(s_pending.retries==2);output();
    test_now+=VP_ACK_TIMEOUT_MS;service_tx();assert(s_pending.retries==3);output();
    test_now+=VP_ACK_TIMEOUT_MS;service_tx();assert(!s_pending.active && !test_uart_out_size);
    xQueueSend(xUartTxQueue,&tx,0);service_tx();assert(s_pending.active);output();
    uint8_t ack=VP_ALARM;feed(VP_ACK,&ack,1);assert(!s_pending.active);
    handle_byte(VP_START);handle_byte(VP_ARM);test_now+=VP_INTERBYTE_TIMEOUT_MS+1;
    feed(VP_DISARM,NULL,0);assert(output().type==VP_ACK);
    VaultEvent e;assert(xQueueReceive(xInputQueue,&e,0) && e.type==EV_DISARM);
    uint8_t wire[8],arm[]={1,2,3,0};vp_encode(VP_ARM,arm,4,wire,sizeof(wire));wire[7]^=1;
    for(unsigned i=0;i<8;++i) { handle_byte(wire[i]); }
    assert(output().type==VP_NAK);
    assert(!uxQueueMessagesWaiting(xInputQueue));
    puts("PASS MCU ACK/NAK, exact retry limit, rollover, partial frames and corruption recovery");
}
int main(void) {commands();feedback_packet();packets_and_status();retries_and_recovery();test_runtime_reset();}
