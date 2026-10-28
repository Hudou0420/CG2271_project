#pragma once
#include <stddef.h>
constexpr int MALLOC_CAP_INTERNAL = 1;
constexpr int ESP_OK = 0;
size_t heap_caps_get_free_size(int);
size_t heap_caps_get_minimum_free_size(int);
int heap_caps_monitor_local_minimum_free_size_start();
int heap_caps_monitor_local_minimum_free_size_stop();
