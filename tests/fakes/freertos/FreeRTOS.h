// Host-only shim for testing the actual HTTPS lease with competing threads.
#pragma once
#include <stdint.h>
using TickType_t = uint32_t;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr int pdTRUE = 1;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
