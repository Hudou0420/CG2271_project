#include "../esp32/src/ai.cpp"
#include <esp_heap_caps.h>
#include <cassert>
static void reset() {
    https_client().stop();test_runtime_reset();https_guard_init();
    jobs=nullptr;view={};publishedTag={};cooldownUntil=nextRequest=0;quotaBackoff=60000;settingsLoaded=false;
    ESP={};WiFi.connection=WL_CONNECTED;test_http_calls.clear();test_http_plans.clear();test_http_begin_ok=true;
    auto &budget=const_cast<HttpsMemoryBudget &>(https_memory_budget());budget={};budget.observe(100000,50000,true);
    ai_init();ai_enabled(true);test_now=1000;
}
static AiTag alarm(bool publish=true) {
    vp_frame_t f{};f.type=VP_ALARM;f.len=VP_LEN_ALARM;f.payload[0]=VP_CAUSE_TAMPER;
    AiTag tag{};ai_frame(f,tag);if(publish)ai_published(tag);return tag;
}
static HttpPlan good() {
    return {200,R"({"candidates":[{"finishReason":"STOP","content":{"parts":[{"text":"{\"reaction\":\"Bold choice. Still locked.\"}"}]}}]})",-2,{}};
}
static void verify_cleanup() {
    assert(test_http_objects==0 && test_live_tls==0 && !test_heap_monitor);
    HttpsLease lease(0);assert(lease.acquired());
}
int main() {
    reset();AiTag tag=alarm(false);ai_poll();assert(test_http_calls.empty() && uxQueueMessagesWaiting(jobs)==1);
    ai_published(tag);test_http_plans.push_back(good());ai_poll();
    assert(test_http_calls.size()==1 && view.reaction.gemini && !strcmp(view.health,"ready"));
    assert(view.lastSuccessUptimeS==1 && test_http_calls[0].headers.at("x-goog-api-key")=="fixture-gemini-key");verify_cleanup();
    puts("PASS production AI defers without blocking publication, then accepts a verified Gemini response");
    reset();alarm();ESP.free=50000;ai_poll();assert(!strcmp(view.health,"low-memory") && test_http_calls.empty());
    assert(nextRequest==0 && ai_snapshot().cooldownSeconds==0);verify_cleanup();
    reset();alarm();ESP.largest=18000;ai_poll();assert(!strcmp(view.health,"low-memory") && test_http_calls.empty());verify_cleanup();
    reset();alarm();test_mutex_available=false;ai_poll();assert(!strcmp(view.health,"unavailable") && test_http_calls.empty());test_mutex_available=true;verify_cleanup();
    reset();alarm();WiFi.connection=WL_DISCONNECTED;ai_poll();assert(!strcmp(view.health,"offline") && test_http_calls.empty());verify_cleanup();
    reset();alarm();ai_cancel_context();ai_poll();assert(test_http_calls.empty());verify_cleanup();
    puts("PASS production low/fragmented heap, no false cooldown, busy lease, offline and stale queued job");
    for(auto pair:{std::pair<int,const char *>{429,"quota-cooldown"},{401,"key-rejected"},{403,"key-rejected"},{404,"model-unavailable"},{-11,"network-timeout"}}) {
        reset();alarm();test_http_plans.push_back({pair.first,"{}",-2,{}});ai_poll();
        assert(!strcmp(view.health,pair.second) && !view.reaction.gemini && view.lastSuccessUptimeS==0);
        size_t calls=test_http_calls.size();alarm();ai_poll();assert(test_http_calls.size()==calls);verify_cleanup();
    }
    reset();alarm();test_http_plans.push_back({200,"not JSON",-2,{}});ai_poll();assert(!strcmp(view.health,"invalid-response"));verify_cleanup();
    reset();alarm();test_http_plans.push_back({200,"{}",8193,{}});ai_poll();assert(!strcmp(view.health,"network-timeout"));verify_cleanup();
    reset();alarm();HttpPlan stale=good();stale.during=[] {ai_cancel_context();};test_http_plans.push_back(stale);ai_poll();
    assert(!view.reaction.gemini && !view.reaction.message[0]);verify_cleanup();
    reset();alarm();HttpPlan disabled=good();disabled.during=[] {ai_enabled(false);};test_http_plans.push_back(disabled);ai_poll();
    assert(!view.enabled && !view.reaction.gemini);verify_cleanup();
    reset();alarm();test_http_begin_ok=false;ai_poll();assert(!strcmp(view.health,"network-timeout"));verify_cleanup();
    assert(test_max_tls==1);puts("PASS production provider errors, cooldowns, invalid/oversized responses, stale in-flight results and cleanup");
    test_runtime_reset();
}
