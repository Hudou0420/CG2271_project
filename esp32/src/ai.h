#pragma once
#include "ai_policy.h"
struct AiSnapshot {
    AiTag tag;
    AiMessage reaction;
    uint32_t version, cooldownSeconds, lastSuccessUptimeS, freeHeap;
    bool enabled, configured, hasCode;
    char health[32];
};
void ai_init();
void ai_poll(); // call from tCloud; processes at most one pending reaction
void ai_prepare_arm(const uint8_t *code, vp_state_t before, uint8_t attempts);
void ai_cancel_context();
void ai_delivery(uint8_t type, bool ok);
void ai_frame(const vp_frame_t &frame, AiTag &tag);
// Called only after Firebase has accepted the local reaction.
void ai_published(const AiTag &tag);
void ai_enabled(bool enabled);
void ai_link_lost();
AiSnapshot ai_snapshot();
