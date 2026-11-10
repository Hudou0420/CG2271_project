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
extern uint8_t test_rgb_red, test_rgb_green, test_rgb_blue;
extern uint32_t test_buzzer_hz;
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
    case 2: assert(gGame.state==ST_ALARM && wait==LOCKOUT_MS);test_now=base+2000;event.type=EV_DISARM;break;
    case 3: assert(gGame.state==ST_ALARM && wait==LOCKOUT_MS-1000);test_now=base+3000;event.type=EV_TAMPER;break;
    case 4: assert(gGame.state==ST_ALARM && wait==LOCKOUT_MS-2000);test_now=base+1000+LOCKOUT_MS;return pdFALSE;
    default: assert(gGame.state==ST_LOCKED && wait==portMAX_DELAY);longjmp(stop,1);
    }
    memcpy(item,&event,sizeof(event));return pdTRUE;
}
static int knocks(QueueHandle_t queue,void *item,TickType_t wait) {
    if(queue!=xInputQueue)return -1;
    VaultEvent event={EV_NONE,0,0};
    switch(stage++) {
    case 0: assert(wait==portMAX_DELAY);test_now=100;event=(VaultEvent){EV_ARM,123,100};break;
    case 1: assert(wait==portMAX_DELAY);test_now=1000;event=(VaultEvent){EV_KNOCK,0,1000};break;
    // An already queued input uses capture time even when game processing is delayed.
    case 2: assert(wait==2001);test_now=3000;event=(VaultEvent){EV_KNOCK,0,1300};break;
    case 3: assert(wait==301);test_now=3301;return pdFALSE;
    case 4: assert(wait==portMAX_DELAY && !gGame.knock_count && !gGame.attempts);
        test_now=3400;event=(VaultEvent){EV_KNOCK,0,3400};break;
    case 5: assert(wait==2001);test_now=3500;event=(VaultEvent){EV_DISARM,0,3500};break;
    default: assert(wait==portMAX_DELAY && gGame.state==ST_IDLE);longjmp(stop,1);
    }
    memcpy(item,&event,sizeof(event));return pdTRUE;
}
static int lid_events(QueueHandle_t queue,void *item,TickType_t wait) {
    if(queue!=xInputQueue)return -1;
    VaultEvent e={EV_NONE,0,0};
    switch(stage++) {
    case 0:test_now=100;e=(VaultEvent){EV_ARM,123,100};break;
    case 1:test_now=200;e.type=EV_LID_OPEN;break;
    case 2:assert(gGame.state==ST_ALARM && wait==portMAX_DELAY);
        test_now=300;e.type=EV_LID_CLOSED;break;
    case 3:assert(gGame.state==ST_LOCKED && wait==portMAX_DELAY);
        test_now=400;e.type=EV_LID_OPEN;break;
    case 4:assert(gGame.state==ST_ALARM && wait==portMAX_DELAY);
        test_now=20000;e.type=EV_NONE;break;
    case 5:assert(gGame.state==ST_ALARM && wait==portMAX_DELAY);
        test_now=21000;e.type=EV_LID_CLOSED;break;
    default:assert(gGame.state==ST_LOCKED && wait==portMAX_DELAY);longjmp(stop,1);
    }
    memcpy(item,&e,sizeof(e));return pdTRUE;
}
int main(void) {
    for(unsigned scenario=0;scenario<3;++scenario) {
        test_runtime_reset();stage=0;base=scenario==1?UINT32_MAX-1000:0;
        vault_init(&gGame);assert(vault_actuators_init());xInputQueue=xQueueCreate(INPUT_Q_LEN,sizeof(VaultEvent));
        xUartTxQueue=xQueueCreate(TX_Q_LEN,sizeof(TxMsg));xStateMutex=xSemaphoreCreateMutex();
        if(scenario==2) {TxMsg filler={0};while(xQueueSend(xUartTxQueue,&filler,0)){};}
        test_receive_hook=events;
        if(setjmp(stop)==0)tGame(NULL);
        vault_actuators_step(test_now);
        assert(test_rgb_red == VAULT_LED_BRIGHTNESS && test_rgb_green == 0 && test_rgb_blue == 0);
        assert(test_buzzer_hz == 0);
        assert(gGame.state==ST_LOCKED && gGame.alarm_cause==ALARM_NONE && gGame.start_ms==100);
        if(scenario!=2) {
            TxMsg message;assert(xQueueReceive(xUartTxQueue,&message,0));
            assert(message.type==PKT_ALARM && message.payload[0]==ALARM_TAMPER);
            assert(!uxQueueMessagesWaiting(xUartTxQueue));
        }
    }
    test_runtime_reset();stage=0;vault_init(&gGame);assert(vault_actuators_init());
    xInputQueue=xQueueCreate(INPUT_Q_LEN,sizeof(VaultEvent));
    xUartTxQueue=xQueueCreate(TX_Q_LEN,sizeof(TxMsg));xStateMutex=xSemaphoreCreateMutex();
    test_receive_hook=lid_events;
    if(setjmp(stop)==0)tGame(NULL);
    test_runtime_reset();stage=0;vault_init(&gGame);assert(vault_actuators_init());
    xInputQueue=xQueueCreate(INPUT_Q_LEN,sizeof(VaultEvent));
    xUartTxQueue=xQueueCreate(TX_Q_LEN,sizeof(TxMsg));xStateMutex=xSemaphoreCreateMutex();
    test_receive_hook=knocks;
    if(setjmp(stop)==0)tGame(NULL);
    TxMsg result;assert(xQueueReceive(xUartTxQueue,&result,0));
    assert(result.type==PKT_KNOCK_RESULT && result.payload[0]==KNOCK_FAIL_TIMEOUT &&
           result.payload[1]==KNOCK_GAP_OK && result.payload[2]==KNOCK_GAP_UNKNOWN);
    assert(!uxQueueMessagesWaiting(xUartTxQueue));
    test_runtime_reset();
    puts("PASS actual tGame: 10-second deadline, ignored disarm/repeated tamper, captured timestamp, rollover and full TX queue");
}
