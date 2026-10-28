#pragma once
#include <stddef.h>
#include <stdint.h>

// Runtime estimate, not a guarantee: different servers may need more TLS heap.
// Keep headroom beyond the largest measured request and room for each 16 KiB
// TLS record buffer. Caller must serialize observation with the HTTPS lease.
class HttpsMemoryBudget {
public:
    void observe(size_t before, size_t minimum, bool success) {
        if (success) calibrated_ = true;
        const size_t used = before > minimum ? before - minimum : 0;
        if (used > peak_) peak_ = used;
    }
    size_t required() const {
        return peak_ > SIZE_MAX - reserve ? SIZE_MAX : peak_ + reserve;
    }
    bool permits(size_t free, size_t largest) const {
        return calibrated_ && free >= required() && largest >= minimumBlock;
    }
    static constexpr size_t reserve = 8192;
    static constexpr size_t minimumBlock = 18 * 1024;
private:
    size_t peak_ = 0;
    bool calibrated_ = false;
};
