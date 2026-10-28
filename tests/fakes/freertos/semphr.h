#pragma once
#include "FreeRTOS.h"
#include <mutex>
#include <chrono>
using SemaphoreHandle_t = std::timed_mutex *;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static std::timed_mutex gate;
    return &gate;
}
inline int xSemaphoreTake(SemaphoreHandle_t gate, TickType_t wait) {
    if (wait == portMAX_DELAY) { gate->lock(); return pdTRUE; }
    return gate->try_lock_for(std::chrono::milliseconds(wait)) ? pdTRUE : 0;
}
inline int xSemaphoreGive(SemaphoreHandle_t gate) { gate->unlock(); return pdTRUE; }
