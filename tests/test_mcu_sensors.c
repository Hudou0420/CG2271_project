#include "../source/vault_sensors.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
QueueHandle_t xInputQueue,xUartTxQueue,xUartRxQueue;
SemaphoreHandle_t xStateMutex;
GameState gGame;
static jmp_buf stop;
static BspInputLevels levels={true,true,true,true};
static BspInputCallback callback;
static BspStatus configuration_status=BSP_OK,adc_status=BSP_OK;
static uint16_t adc_value=1000;
static unsigned cycles,stop_after;
static bool drain_full_queue,disarm_on_read;
static RawInputEvent script[32];
static size_t script_count,script_next;
BspStatus bsp_inputs_configure(BspInput input,BspPull pull,BspEdge edge) {(void)input;(void)pull;(void)edge;return configuration_status;}
BspStatus bsp_inputs_read_all(BspInputLevels *out) {*out=levels;return configuration_status;}
BspStatus bsp_inputs_read(BspInput input,bool *high) {(void)input;*high=levels.sw;return BSP_OK;}
void bsp_inputs_set_callback(BspInputCallback cb,void *context) {(void)context;callback=cb;}
BspStatus bsp_light_start(void) {return adc_status;}
BspStatus bsp_light_try_read(uint16_t *out) {
    *out=adc_value;
    if(disarm_on_read && cycles==2)gGame.state=ST_IDLE;
    return adc_status;
}
void bsp_light_cancel(void) {}
static void reset(void) {
    test_runtime_reset();raw_queue=NULL;callback=NULL;configuration_status=BSP_OK;adc_status=BSP_OK;
    raw_drops=0;memset(&stats,0,sizeof(stats));memset(&gGame,0,sizeof(gGame));
    levels=(BspInputLevels){true,true,true,true};cycles=0;stop_after=4;drain_full_queue=false;disarm_on_read=false;
    script_count=script_next=0;
    xInputQueue=xQueueCreate(INPUT_Q_LEN,sizeof(VaultEvent));xStateMutex=xSemaphoreCreateMutex();
    assert(vault_sensors_init());assert(!vault_sensors_init());
}
static void suspended(void) {longjmp(stop,1);}
static int receive(QueueHandle_t queue,void *item,TickType_t wait) {
    (void)wait;if(queue!=raw_queue)return -1;
    if(script_next==script_count)longjmp(stop,1);
    RawInputEvent raw=script[script_next++];test_now=raw.tick;levels=raw.levels;
    if(raw.input==BSP_INPUT_COUNT)return pdFALSE;
    memcpy(item,&raw,sizeof(raw));return pdTRUE;
}
static void add(BspInput input,uint32_t tick,bool sw,uint32_t generation) {
    script[script_count++]=(RawInputEvent){input,{true,true,true,sw},tick,generation};
}
static void run_input(void) {
    test_receive_hook=receive;test_suspend_hook=suspended;
    if(setjmp(stop)==0)vault_input_task(NULL);
    test_receive_hook=NULL;
}
static void delay_period(void) {
    ++cycles;
    if(drain_full_queue && cycles==3) {VaultEvent e;assert(xQueueReceive(xInputQueue,&e,0));}
    if(cycles==stop_after)longjmp(stop,1);
}
static void run_tripwire(void) {
    test_delay_hook=delay_period;
    if(setjmp(stop)==0)vault_tripwire_task(NULL);
    test_delay_hook=NULL;
}
static void taps_and_button(void) {
    reset();add(BSP_INPUT_TAP,100,true,0);add(BSP_INPUT_TAP,139,true,0);add(BSP_INPUT_TAP,140,true,0);
    add(BSP_INPUT_TAP,179,true,0);add(BSP_INPUT_TAP,180,true,0);run_input();
    VaultEvent e;for(unsigned i=0;i<3;++i){assert(xQueueReceive(xInputQueue,&e,0));assert(e.type==EV_KNOCK && e.tick_ms==100+40*i);}
    assert(!uxQueueMessagesWaiting(xInputQueue) && stats.inputs_ready && stats.game_events_sent==3);
    reset();add(BSP_INPUT_ENCODER_SW,100,false,0);add(BSP_INPUT_ENCODER_SW,105,true,0);
    add(BSP_INPUT_ENCODER_SW,110,false,0);add(BSP_INPUT_COUNT,129,false,0);add(BSP_INPUT_COUNT,130,false,0);
    add(BSP_INPUT_ENCODER_SW,150,false,0);add(BSP_INPUT_COUNT,170,false,0);
    add(BSP_INPUT_ENCODER_SW,200,true,0);add(BSP_INPUT_COUNT,220,true,0);
    add(BSP_INPUT_ENCODER_SW,300,false,0);add(BSP_INPUT_COUNT,320,false,0);run_input();
    assert(xQueueReceive(xInputQueue,&e,0) && e.type==EV_PRESS && e.tick_ms==110);
    assert(xQueueReceive(xInputQueue,&e,0) && e.type==EV_PRESS && e.tick_ms==300);
    assert(!uxQueueMessagesWaiting(xInputQueue));
    puts("PASS production tap debounce boundary, captured times, button bounce/release/held press");
}
static void overflow_and_failure(void) {
    reset();
    for(unsigned i=0;i<VAULT_RAW_INPUT_QUEUE_LENGTH+1;++i)capture_edge(BSP_INPUT_TAP,&levels,NULL);
    assert(raw_drops==1);
    script[0]=(RawInputEvent){BSP_INPUT_ENCODER_CLK,{true,false,true,true},100,0};
    script[1]=(RawInputEvent){BSP_INPUT_ENCODER_DT,{true,false,false,true},101,1};
    script[2]=(RawInputEvent){BSP_INPUT_ENCODER_CLK,{true,true,false,true},102,1};
    script[3]=(RawInputEvent){BSP_INPUT_ENCODER_DT,{true,true,true,true},103,1};script_count=4;
    run_input();
    VaultEvent e;assert(xQueueReceive(xInputQueue,&e,0) && e.type==EV_DIAL && e.value==1 && e.tick_ms==103);
    assert(!uxQueueMessagesWaiting(xInputQueue) && stats.encoder_steps==1);
    VaultSensorStats snapshot;vault_sensors_get_stats(&snapshot);assert(snapshot.raw_queue_drops==1);
    vault_sensors_get_stats(NULL);
    reset();configuration_status=BSP_NOT_READY;run_input();assert(!stats.inputs_ready && !callback);
    reset();test_allocation_failure=true;raw_queue=NULL;assert(!vault_sensors_init());
    puts("PASS production capture overflow, encoder resynchronization, diagnostics and startup failures");
}
static void beam_service(void) {
    reset();gGame.state=ST_LOCKED;gGame.start_ms=1;run_tripwire();
    assert(stats.ldr_valid && stats.ldr_raw==1000);
#if VAULT_LDR_BEAM_DETECTION_ENABLED
    VaultEvent e;assert(xQueueReceive(xInputQueue,&e,0) && e.type==EV_BEAM_BROKEN && e.tick_ms==100);
    assert(!uxQueueMessagesWaiting(xInputQueue));
    reset();gGame.state=ST_LOCKED;gGame.start_ms=1;drain_full_queue=true;
    VaultEvent filler={EV_NONE,0,0};while(xQueueSend(xInputQueue,&filler,0)){}
    run_tripwire();assert(stats.beam_queue_retries==1);
    bool found=false;while(xQueueReceive(xInputQueue,&e,0))if(e.type==EV_BEAM_BROKEN){found=true;assert(e.tick_ms==100);}
    assert(found);
    reset();gGame.state=ST_LOCKED;gGame.start_ms=1;disarm_on_read=true;run_tripwire();
    assert(!uxQueueMessagesWaiting(xInputQueue));
#else
    assert(!uxQueueMessagesWaiting(xInputQueue));
#endif
    reset();adc_status=BSP_NOT_READY;run_tripwire();assert(!stats.ldr_valid && stats.adc_errors==4);
    puts("PASS production ADC failures, beam enable gate, one-shot event, retry and disarm cancellation");
}
int main(void){taps_and_button();overflow_and_failure();beam_service();test_runtime_reset();}
