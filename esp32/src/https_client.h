#pragma once
#include "https_memory.h"
class NetworkClientSecure;
// Access only while holding HttpsLease; stop the client before releasing it.
NetworkClientSecure &https_client();
const HttpsMemoryBudget &https_memory_budget();

class HttpsMemorySample {
public:
    explicit HttpsMemorySample(const char *label = "HTTPS");
    ~HttpsMemorySample();
    void finish(bool success);
    HttpsMemorySample(const HttpsMemorySample &) = delete;
    HttpsMemorySample &operator=(const HttpsMemorySample &) = delete;
private:
    size_t before_;
    bool active_;
    const char *label_;
};
