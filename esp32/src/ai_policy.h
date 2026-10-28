// Pure game-master policy: no networking, credentials or Arduino dependencies.
#pragma once
#include <stdint.h>
#include "vault_protocol.h"

struct AiTag { char roundId[32]; char eventId[48]; uint32_t actionRevision; };
struct AiJob {
    AiTag tag;
    uint8_t type, attempt, closeness, cause, attempts;
    uint16_t seconds;
    bool hasCode;
    char code[4], guess[4], history[16][4], clue[64];
    uint8_t historyCount;
};
struct AiMessage { AiTag tag; char message[192]; bool gemini; };
class AiPolicy {
public:
    void init(uint32_t boot);
    void prepareArm(const uint8_t *code, vp_state_t before, uint8_t attempts);
    void delivery(uint8_t type, bool ok);
    void invalidate();
    void status(vp_state_t state, uint8_t knocks, uint8_t attempts);
    bool event(const vp_frame_t &frame, AiJob &job);
    bool current(const AiTag &tag) const;
    AiTag tag{};
    bool hasCode = false;
private:
    uint32_t boot_ = 0, round_ = 0, event_ = 0;
    bool pending_ = false, ack_ = false, canConfirm_ = false;
    vp_state_t lastState_ = VP_STATE_IDLE;
    uint8_t lastKnocks_ = 0, hintPosition_ = 0, count_ = 0;
    char code_[4]{}, pendingCode_[4]{}, history_[16][4]{};
};
void ai_local(const AiJob &job, AiMessage &out);
bool ai_valid_reaction(const char *text);

// The local reaction, its source event and current status must all be public
// before Gemini can occupy the board's sole TLS budget.
bool ai_publication_ready(const AiTag &candidate, const AiTag &message,
                          const AiTag &status, const char *writtenEvent);
