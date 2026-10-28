#include "https_guard.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t gate;
void https_guard_init() {
    // Called once in setup, before link/cloud/AI tasks are started.
    gate = xSemaphoreCreateMutex();
}
HttpsLease::HttpsLease(uint32_t waitMs) : held_(false) {
    if (gate) held_ = xSemaphoreTake(gate, waitMs == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(waitMs)) == pdTRUE;
}
HttpsLease::~HttpsLease() {
    if (held_) xSemaphoreGive(gate);
}
