/* Only rename the entry point in this test translation unit. Firmware is unchanged. */
#define main unused_application_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#if defined(__GNUC__) && !defined(__clang__)
/* Existing MCUXpresso example main uses volatile static; keep production unchanged. */
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#endif
#include "../source/CG2271_Project.c"
#pragma GCC diagnostic pop
#undef main
#include <assert.h>
#include <setjmp.h>
static jmp_buf stop;
static unsigned stage;
static uint32_t base;
BspStatus bsp_init(const BspOutputConfig *config) {(void)config;return BSP_OK;}
bool vault_sensors_init(void) {return true;}
bool vault_comms_init(void) {return true;}
void vault_comms_task(void *arg) {(void)arg;}
void vault_input_task(void *arg) {(void)arg;}
void vault_tripwire_task(void *arg) {(void)arg;}
static int events(QueueHandle_t queue,void *item,TickType_t wait) {
    if(queue!=xInputQueue)return -1;
    VaultEvent event={EV_NONE,0,0};
    switch(stage++) {
    case 0: assert(wait==portMAX_DELAY);test_now=base+500;event=(VaultEvent){EV_ARM,123,100};break;
    case 1: assert(wait==portMAX_DELAY);test_now=base+1000;event.type=EV_TAMPER;break;
    case 2: assert(gGame.state==ST_ALARM && wait==30000);test_now=base+2000;event.type=EV_DISARM;break;
    case 3: assert(gGame.state==ST_ALARM && wait==29000);test_now=base+3000;event.type=EV_TAMPER;break;
    case 4: assert(gGame.state==ST_ALARM && wait==28000);test_now=base+31000;return pdFALSE;
    default: assert(gGame.state==ST_LOCKED && wait==portMAX_DELAY);longjmp(stop,1);
    }
    memcpy(item,&event,sizeof(event));return pdTRUE;
}
int main(void) {
    for(unsigned scenario=0;scenario<3;++scenario) {
        test_runtime_reset();stage=0;base=scenario==1?UINT32_MAX-1000:0;
        vault_init(&gGame);xInputQueue=xQueueCreate(INPUT_Q_LEN,sizeof(VaultEvent));
        xUartTxQueue=xQueueCreate(TX_Q_LEN,sizeof(TxMsg));xStateMutex=xSemaphoreCreateMutex();
        if(scenario==2) {TxMsg filler={0};while(xQueueSend(xUartTxQueue,&filler,0)){};}
        test_receive_hook=events;
        if(setjmp(stop)==0)tGame(NULL);
        assert(gGame.state==ST_LOCKED && gGame.alarm_cause==ALARM_NONE && gGame.start_ms==100);
        if(scenario!=2) {
            TxMsg message;assert(xQueueReceive(xUartTxQueue,&message,0));
            assert(message.type==PKT_ALARM && message.payload[0]==ALARM_TAMPER);
            assert(!uxQueueMessagesWaiting(xUartTxQueue));
        }
    }
    test_runtime_reset();
    puts("PASS actual tGame: 30-second deadline, ignored disarm/repeated tamper, captured timestamp, rollover and full TX queue");
}
