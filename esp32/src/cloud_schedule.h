// Fair scheduling for the single Firebase client. A failed event keeps its
// queue position and sub-request progress, but cannot monopolize the client.
#pragma once
#include <stdint.h>
enum CloudWork { CLOUD_STATUS, CLOUD_COMMANDS, CLOUD_EVENTS, CLOUD_AI, CLOUD_WAIT };
inline bool cloud_due(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
class CloudSchedule {
public:
    CloudWork next(const bool ready[4]) {
        for (unsigned n = 0; n < 4; ++n) {
            unsigned slot = (cursor_ + n) % 4;
            if (!ready[slot]) continue;
            cursor_ = (slot + 1) % 4;
            return (CloudWork)slot;
        }
        return CLOUD_WAIT;
    }
private:
    unsigned cursor_ = 0;
};
