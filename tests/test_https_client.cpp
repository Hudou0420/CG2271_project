#include "https_client.h"
#include <cassert>
#include <cstdio>
#include <limits>

static size_t freeHeap = 74000, lowHeap = 20000;
static bool monitoring;
size_t heap_caps_get_free_size(int) { return freeHeap; }
size_t heap_caps_get_minimum_free_size(int) { return lowHeap; }
int heap_caps_monitor_local_minimum_free_size_start() {
    if (monitoring) return -1;
    monitoring = true; return 0;
}
int heap_caps_monitor_local_minimum_free_size_stop() {
    assert(monitoring); monitoring = false; return 0;
}
int main() {
    HttpsMemoryBudget policy;
    assert(!policy.permits(100000, 50000)); // no successful TLS baseline yet
    policy.observe(74000, 20000, false);
    assert(!policy.permits(100000, 50000));
    policy.observe(74000, 21000, true);
    assert(policy.required() == 54000 + 8192); // failed request peak retained too
    assert(!policy.permits(policy.required() - 1, 50000));
    assert(!policy.permits(100000, HttpsMemoryBudget::minimumBlock - 1));
    assert(policy.permits(policy.required(), HttpsMemoryBudget::minimumBlock));
    policy.observe(100, 200, true); // concurrent frees must not underflow
    assert(policy.required() == 62192);
    policy.observe(80000, 15000, false); // more costly TLS raises future reserve
    assert(policy.required() == 73192);
    policy.observe(std::numeric_limits<size_t>::max(), 0, false);
    assert(policy.required() == std::numeric_limits<size_t>::max());

    assert(&https_client() == &https_client()); // all callers share one context
    {
        HttpsMemorySample sample;
        assert(monitoring);
        sample.finish(true);
        assert(!monitoring);
        assert(https_memory_budget().permits(74000, 22000));
    } // no double finish
    freeHeap = 80000; lowHeap = 10000;
    { HttpsMemorySample abandoned; } // early return still records peak, stops monitor
    assert(!monitoring);
    assert(https_memory_budget().required() == 78192);
    monitoring = true;
    { HttpsMemorySample unavailable; unavailable.finish(true); }
    assert(monitoring); // cannot stop a monitor owned by someone else
    monitoring = false;
    puts("HTTPS memory calibration, reserve, fragmentation, cleanup and shared client passed");
}
