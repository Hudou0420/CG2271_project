#pragma once
#include "Arduino.h"
constexpr int ESP_OK=0,MALLOC_CAP_INTERNAL=1;
inline bool test_heap_monitor;
inline size_t heap_caps_get_free_size(int caps) {(void)caps;return ESP.free;}
inline size_t heap_caps_get_minimum_free_size(int caps) {(void)caps;return ESP.minimum;}
inline int heap_caps_monitor_local_minimum_free_size_start() {assert(!test_heap_monitor);test_heap_monitor=true;return 0;}
inline int heap_caps_monitor_local_minimum_free_size_stop() {assert(test_heap_monitor);test_heap_monitor=false;return 0;}
