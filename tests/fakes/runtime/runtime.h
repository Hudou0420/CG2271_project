#ifndef TEST_RUNTIME_H
#define TEST_RUNTIME_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1U
#define configTICK_RATE_HZ 1000U
#define configMINIMAL_STACK_SIZE 90U
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define taskDISABLE_INTERRUPTS() ((void)0)
#define portYIELD_FROM_ISR(...) ((void)0)
extern TickType_t test_now;
extern bool test_mutex_available;
extern bool test_allocation_failure;
extern int (*test_receive_hook)(QueueHandle_t, void *, TickType_t);
extern void (*test_delay_hook)(void);
extern void (*test_suspend_hook)(void);
void test_runtime_reset(void);
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void vTaskDelay(TickType_t delay);
void vTaskDelayUntil(TickType_t *previous, TickType_t delay);
void vTaskSuspend(TaskHandle_t task);
void vTaskStartScheduler(void);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *handle);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
#ifdef __cplusplus
}
template<class... Args> inline void test_critical(Args... args) {((void)args,...);}
#define taskENTER_CRITICAL(...) test_critical(__VA_ARGS__)
#define taskEXIT_CRITICAL(...) test_critical(__VA_ARGS__)
#else
#define taskENTER_CRITICAL(...) ((void)0)
#define taskEXIT_CRITICAL(...) ((void)0)
#endif
#endif
