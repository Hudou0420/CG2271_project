#include "runtime.h"
#include <stdlib.h>
typedef struct TestQueue {
    size_t capacity, size, count;
    unsigned char *data;
    struct TestQueue *next;
} TestQueue;
static TestQueue *queues;
typedef struct TestMutex {bool held;struct TestMutex *next;} TestMutex;
static TestMutex *mutexes;
TickType_t test_now;
bool test_mutex_available = true, test_allocation_failure;
int (*test_receive_hook)(QueueHandle_t, void *, TickType_t);
void (*test_delay_hook)(void);
void (*test_suspend_hook)(void);
void test_runtime_reset(void) {
    while(queues) {TestQueue *next=queues->next;free(queues->data);free(queues);queues=next;}
    while(mutexes) {TestMutex *next=mutexes->next;free(mutexes);mutexes=next;}
    test_now=0;test_mutex_available=true;test_allocation_failure=false;
    test_receive_hook=NULL;test_delay_hook=NULL;test_suspend_hook=NULL;
}
QueueHandle_t xQueueCreate(UBaseType_t capacity,UBaseType_t size) {
    if(test_allocation_failure)return NULL;
    TestQueue *q=(TestQueue *)calloc(1,sizeof(*q));assert(q);
    q->data=(unsigned char *)calloc(capacity,size);assert(q->data);
    q->capacity=capacity;q->size=size;q->next=queues;queues=q;return q;
}
BaseType_t xQueueSend(QueueHandle_t queue,const void *item,TickType_t wait) {
    (void)wait;TestQueue *q=(TestQueue *)queue;
    if(!q || q->count==q->capacity)return pdFALSE;
    memcpy(q->data+q->count*q->size,item,q->size);++q->count;return pdTRUE;
}
BaseType_t xQueueSendToFront(QueueHandle_t queue,const void *item,TickType_t wait) {
    (void)wait;TestQueue *q=(TestQueue *)queue;
    if(!q || q->count==q->capacity)return pdFALSE;
    memmove(q->data+q->size,q->data,q->count*q->size);memcpy(q->data,item,q->size);++q->count;return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue,const void *item) {
    TestQueue *q=(TestQueue *)queue;if(!q)return pdFALSE;assert(q->capacity==1);
    memcpy(q->data,item,q->size);q->count=1;return pdTRUE;
}
BaseType_t xQueueSendFromISR(QueueHandle_t queue,const void *item,BaseType_t *woken) {
    if(woken)*woken=pdFALSE;
    return xQueueSend(queue,item,0);
}
BaseType_t xQueueReceive(QueueHandle_t queue,void *item,TickType_t wait) {
    if(test_receive_hook) {int r=test_receive_hook(queue,item,wait);if(r!=-1)return r;}
    TestQueue *q=(TestQueue *)queue;if(!q || !q->count)return pdFALSE;
    memcpy(item,q->data,q->size);--q->count;memmove(q->data,q->data+q->size,q->count*q->size);return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t queue,void *item,TickType_t wait) {
    (void)wait;TestQueue *q=(TestQueue *)queue;if(!q || !q->count)return pdFALSE;
    memcpy(item,q->data,q->size);return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {return queue?(unsigned)((TestQueue *)queue)->count:0;}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if(test_allocation_failure)return NULL;
    TestMutex *m=(TestMutex *)calloc(1,sizeof(*m));assert(m);m->next=mutexes;mutexes=m;return m;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore,TickType_t wait) {
    (void)wait;TestMutex *m=(TestMutex *)semaphore;if(!m || !test_mutex_available || m->held)return pdFALSE;
    m->held=true;return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    TestMutex *m=(TestMutex *)semaphore;if(!m || !m->held)return pdFALSE;m->held=false;return pdTRUE;
}
TickType_t xTaskGetTickCount(void) {return test_now;}
TickType_t xTaskGetTickCountFromISR(void) {return test_now;}
void vTaskDelay(TickType_t delay) {test_now+=delay;if(test_delay_hook)test_delay_hook();}
void vTaskDelayUntil(TickType_t *previous,TickType_t delay) {*previous+=delay;test_now=*previous;if(test_delay_hook)test_delay_hook();}
void vTaskSuspend(TaskHandle_t task) {(void)task;if(test_suspend_hook)test_suspend_hook();assert(!"Unexpected suspended task");}
void vTaskStartScheduler(void) {}
BaseType_t xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;return !test_allocation_failure;
}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) {(void)task;return 6000;}
