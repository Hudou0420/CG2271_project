#ifndef TEST_MCU_HARDWARE_H
#define TEST_MCU_HARDWARE_H
#include "runtime.h"
static inline int test_printf(const char *format, ...) {(void)format;return 0;}
#define PRINTF(...) test_printf(__VA_ARGS__)
#define LPUART1 ((void *)1)
#define PORTE ((void *)2)
#define LPUART1_IRQn 1
#define kCLOCK_PortE 1
#define kCLOCK_McgIrc48MClk 1
#define kPORT_MuxAlt3 3
#define kLPUART_RxOverrunFlag 1U
#define kLPUART_NoiseErrorFlag 2U
#define kLPUART_FramingErrorFlag 4U
#define kLPUART_RxDataRegFullFlag 8U
#define kLPUART_RxDataRegFullInterruptEnable 1U
#define kLPUART_RxOverrunInterruptEnable 2U
#define kStatus_Success 0
typedef struct {unsigned baudRate_Bps;bool enableTx,enableRx;} lpuart_config_t;
extern uint8_t test_uart_out[16384], test_uart_byte;
extern size_t test_uart_out_size;
extern uint32_t test_uart_flags, test_uart_cleared;
extern int test_uart_init_result;
static inline void CLOCK_EnableClock(int clock) {(void)clock;}
static inline void CLOCK_SetLpuart1Clock(unsigned clock) {(void)clock;}
static inline unsigned CLOCK_GetFreq(int clock) {(void)clock;return 48000000;}
static inline void PORT_SetPinMux(void *port,unsigned pin,int mux) {(void)port;(void)pin;(void)mux;}
static inline void LPUART_GetDefaultConfig(lpuart_config_t *c) {memset(c,0,sizeof(*c));}
static inline int LPUART_Init(void *uart,const lpuart_config_t *c,unsigned frequency) {
    (void)uart;assert(c->baudRate_Bps==115200 && c->enableTx && c->enableRx && frequency==48000000);return test_uart_init_result;
}
static inline void LPUART_EnableInterrupts(void *uart,unsigned flags) {(void)uart;(void)flags;}
static inline void EnableIRQ(int irq) {(void)irq;}
static inline uint32_t LPUART_GetStatusFlags(void *uart) {(void)uart;return test_uart_flags;}
static inline void LPUART_ClearStatusFlags(void *uart,uint32_t flags) {(void)uart;test_uart_cleared|=flags;}
static inline uint8_t LPUART_ReadByte(void *uart) {(void)uart;return test_uart_byte;}
static inline void LPUART_WriteBlocking(void *uart,const uint8_t *data,size_t size) {
    (void)uart;assert(test_uart_out_size+size<=sizeof(test_uart_out));
    memcpy(test_uart_out+test_uart_out_size,data,size);test_uart_out_size+=size;
}
#endif
