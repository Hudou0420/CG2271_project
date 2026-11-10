#include "ai_policy.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void AiPolicy::init(uint32_t boot) {
    *this = AiPolicy{};
    boot_ = boot;
    invalidate();
}
void AiPolicy::invalidate() {
    hasCode = pending_ = ack_ = canConfirm_ = false;
    memset(code_, 0, sizeof(code_));
    memset(pendingCode_, 0, sizeof(pendingCode_));
    memset(history_, 0, sizeof(history_));
    count_ = hintPosition_ = 0;
    snprintf(tag.roundId, sizeof(tag.roundId), "%08lx-%lu", (unsigned long)boot_, (unsigned long)++round_);
    tag.eventId[0] = 0;
    ++tag.actionRevision;
}
void AiPolicy::prepareArm(const uint8_t *code, vp_state_t before, uint8_t attempts) {
    invalidate();
    pending_ = true;
    // A LOCKED->LOCKED arm with no prior attempts has no observable acceptance.
    canConfirm_ = before == VP_STATE_IDLE || before == VP_STATE_UNLOCKED ||
                  (before == VP_STATE_LOCKED && attempts > 0);
    for (int i = 0; i < 3; ++i) pendingCode_[i] = '0' + code[i];
}
void AiPolicy::delivery(uint8_t type, bool ok) {
    if (type == VP_DISARM || !ok || !pending_) { invalidate(); return; }
    ack_ = true;
}
void AiPolicy::status(vp_state_t state, uint8_t knocks, uint8_t attempts) {
    if (state == VP_STATE_IDLE && !pending_ && lastState_ != VP_STATE_IDLE) invalidate();
    if (pending_ && ack_) {
        if (state == VP_STATE_LOCKED && attempts == 0 && canConfirm_) {
            memcpy(code_, pendingCode_, sizeof(code_));
            hasCode = true;
        }
        // A post-ACK status is required. Ambiguous/rejected commands fail closed.
        pending_ = ack_ = false;
        memset(pendingCode_, 0, sizeof(pendingCode_));
    }
    if ((state == VP_STATE_LOCKED && knocks > 0 && knocks != lastKnocks_) ||
        (state == VP_STATE_DIALING && lastState_ != VP_STATE_DIALING) ||
        (lastState_ == VP_STATE_ALARM && state != VP_STATE_ALARM)) {
        ++tag.actionRevision;
        tag.eventId[0] = 0;
    }
    lastKnocks_ = knocks;
    lastState_ = state;
}
bool AiPolicy::event(const vp_frame_t &f, AiJob &job) {
    if (f.type != VP_ATTEMPT && f.type != VP_SOLVED && f.type != VP_ALARM && f.type != VP_KNOCK_RESULT) return false;
    job = {};
    ++tag.actionRevision;
    snprintf(tag.eventId, sizeof(tag.eventId), "%08lx-e%lu", (unsigned long)boot_, (unsigned long)++event_);
    job.tag = tag;
    job.type = f.type;
    job.hasCode = hasCode;
    memcpy(job.code, code_, sizeof(job.code));
    const uint8_t *p = f.payload;
    if (f.type == VP_ATTEMPT) {
        for (int i = 0; i < 3; ++i) job.guess[i] = '0' + p[i];
        job.attempt = p[3]; job.closeness = p[4];
        if (count_ == 16) { memmove(history_, history_ + 1, 15 * sizeof(history_[0])); --count_; }
        memcpy(history_[count_++], job.guess, sizeof(job.guess));
    } else if (f.type == VP_SOLVED) {
        job.attempts = p[0]; job.seconds = vp_get_u16(p + 1);
        hasCode = false;
        memset(code_, 0, sizeof(code_));
        memset(pendingCode_, 0, sizeof(pendingCode_));
        pending_ = ack_ = false;
    } else if (f.type == VP_KNOCK_RESULT) memcpy(job.knock, p, 3);
    else job.cause = p[0];
    unsigned choices = ai_clue_count(job);
    if (choices) {
        ai_clue(job, hintPosition_ % choices, job.clue, sizeof(job.clue));
        ++hintPosition_;
    }
    job.historyCount = count_;
    memcpy(job.history, history_, sizeof(history_));
    return true;
}
bool AiPolicy::current(const AiTag &other) const {
    return tag.actionRevision == other.actionRevision && !strcmp(tag.roundId, other.roundId) &&
           !strcmp(tag.eventId, other.eventId);
}
// Clue IDs are local to this immutable job; the model may select any valid ID.
bool ai_clue(const AiJob &job, unsigned index, char *out, unsigned capacity) {
    if (!out || !capacity) return false;
    out[0] = 0;
    const char *positions[] = {"Left", "Middle", "Right"};
    if (job.type == VP_ATTEMPT && job.hasCode) {
        for (int i = 0; i < 3; ++i) {
            if (job.code[i] == job.guess[i]) continue;
            if (index-- == 0) {
                snprintf(out, capacity, "%s digit: go %s.", positions[i],
                         job.code[i] > job.guess[i] ? "higher" : "lower");
                return true;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (job.code[i] == job.guess[i]) continue;
            if (index-- == 0) {
                snprintf(out, capacity, "%s digit is %s.", positions[i],
                         (job.code[i] - '0') % 2 ? "odd" : "even");
                return true;
            }
        }
    } else if (job.type == VP_KNOCK_RESULT) {
        for (int i = 0; i < 2; ++i) {
            uint8_t gap = job.knock[i + 1];
            if (gap != VP_GAP_SHORT && gap != VP_GAP_LONG) continue;
            if (index-- == 0) {
                snprintf(out, capacity, "%s the pause %s.", gap == VP_GAP_SHORT ? "Lengthen" : "Shorten",
                         i == 0 ? "after the opening tap" : "before the last tap");
                return true;
            }
        }
        if (index == 0) {
            snprintf(out, capacity, "%s", job.knock[0] == VP_KNOCK_TIMEOUT ?
                     "Finish the rhythm without such a long pause." :
                     "Keep the opening taps close, then leave a longer pause.");
            return true;
        }
    }
    return false;
}
unsigned ai_clue_count(const AiJob &job) {
    char text[80];
    unsigned count = 0;
    while (ai_clue(job, count, text, sizeof(text))) ++count;
    return count;
}

void ai_local(const AiJob &j, AiMessage &out) {
    out = {}; out.tag = j.tag;
    const char *text = j.type == VP_SOLVED ? "Fine. You earned this one." :
                       j.type == VP_ALARM ? "Subtle. Very subtle." :
                       j.type == VP_KNOCK_RESULT ? "Not quite." : "Still locked.";
    snprintf(out.message, sizeof(out.message), "%s%s%s", text, j.clue[0] ? " " : "", j.clue);
}
bool ai_valid_reaction(const char *text) {
    if (!text || !*text || strlen(text) > 120) return false;
    char word[128]{}; unsigned used = 0, words = 0;
    // Validate the reaction after removing the selected, approved clue.
    const char *blocked[] = {"zero","one","two","three","four","five","six","seven","eight","nine",
        "ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen",
        "nineteen","twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety",
        "hundred","thousand","million","billion","dozen","single","pair","odd","even","first","second","third","left","middle","right","higher","lower","up","down",
        "digit","code","password","combination","knock","rhythm","double","triple","half","twice"};
    for (const unsigned char *p = (const unsigned char *)text;; ++p) {
        if (*p && (isdigit(*p) || *p >= 127 || (!isalpha(*p) && !isspace(*p) && !strchr(".,!?'—-", *p)))) return false;
        if (isalpha(*p)) word[used++] = tolower(*p);
        else if (used) {
            word[used] = 0; used = 0;
            if (++words > 10) return false; // reserve words for the selected clue
            for (const char *bad : blocked) if (!strcmp(word, bad)) return false;
        }
        if (!*p) break;
    }
    return words > 0;
}

bool ai_publication_ready(const AiTag &candidate, const AiTag &message,
                          const AiTag &status, const char *writtenEvent) {
    return candidate.eventId[0] && !strcmp(candidate.eventId, writtenEvent) &&
           !strcmp(candidate.eventId, message.eventId) &&
           !strcmp(candidate.roundId, message.roundId) && !strcmp(candidate.roundId, status.roundId) &&
           candidate.actionRevision == message.actionRevision && candidate.actionRevision == status.actionRevision;
}
