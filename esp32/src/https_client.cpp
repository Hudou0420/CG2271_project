#include "https_client.h"
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

static HttpsMemoryBudget budget;
NetworkClientSecure &https_client() {
    static NetworkClientSecure client;
    return client;
}
const HttpsMemoryBudget &https_memory_budget() { return budget; }
HttpsMemorySample::HttpsMemorySample(const char *label)
    : before_(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      active_(heap_caps_monitor_local_minimum_free_size_start() == ESP_OK), label_(label) {}
HttpsMemorySample::~HttpsMemorySample() { finish(false); }
void HttpsMemorySample::finish(bool success) {
    if (!active_) return;
    const size_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    heap_caps_monitor_local_minimum_free_size_stop();
    active_ = false;
    const size_t previous = budget.required();
    budget.observe(before_, minimum, success);
#ifdef ARDUINO
    if (budget.required() > previous) {
        Serial.printf("[https] budget raised by %s; before=%lu minimum=%lu required=%lu success=%d\n",
                      label_, (unsigned long)before_, (unsigned long)minimum,
                      (unsigned long)budget.required(), success);
    }
#else
    (void)previous;
    (void)label_;
#endif
}
