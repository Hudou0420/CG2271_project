// One TLS session at a time on the ESP32-S2. Acquire before constructing a
// secure client; destroy/stop that client before releasing the lease.
#pragma once
#include <stdint.h>
void https_guard_init();
class HttpsLease {
public:
    explicit HttpsLease(uint32_t waitMs = UINT32_MAX);
    ~HttpsLease();
    HttpsLease(const HttpsLease &) = delete;
    HttpsLease &operator=(const HttpsLease &) = delete;
    bool acquired() const { return held_; }
private:
    bool held_;
};
