#include "ai_policy.h"
#include "cloud_schedule.h"
#include "ai_response.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <string>
static vp_frame_t attempt(int n, int a = 0, int b = 0, int c = 0) {
    vp_frame_t f{}; f.type = VP_ATTEMPT;
    f.payload[0] = a; f.payload[1] = b; f.payload[2] = c; f.payload[3] = n; f.payload[4] = 30;
    return f;
}
static void arm(AiPolicy &p) {
    uint8_t code[] = {0, 4, 2};
    p.prepareArm(code, VP_STATE_IDLE, 0);
    assert(!p.hasCode);
    p.delivery(VP_ARM, true);
    assert(!p.hasCode); // ACK alone is insufficient
    p.status(VP_STATE_LOCKED, 0, 0);
    assert(p.hasCode);
}
static std::string reply(const char *content, const char *finish = "STOP") {
    return std::string("{\"candidates\":[{\"finishReason\":\"") + finish +
        "\",\"content\":{\"parts\":[{\"text\":\"" + content + "\"}]}}]}";
}
static void cloud_recovery_checks() {
    CloudSchedule schedule;
    bool ready[] = {true, true, true, true};
    // A permanently failing event remains ready. Status, commands and AI
    // settings must still run even if status changes on every pass too.
    unsigned counts[4] = {};
    for (int i = 0; i < 40; ++i) ++counts[schedule.next(ready)];
    for (auto n : counts) assert(n == 10);
    ready[CLOUD_EVENTS] = false; // event is waiting for its retry deadline
    for (int i = 0; i < 12; ++i) assert(schedule.next(ready) != CLOUD_EVENTS);
    ready[CLOUD_STATUS] = ready[CLOUD_COMMANDS] = ready[CLOUD_AI] = false;
    assert(schedule.next(ready) == CLOUD_WAIT);
    ready[CLOUD_EVENTS] = true;
    assert(schedule.next(ready) == CLOUD_EVENTS);
    assert(!cloud_due(1000, 3000));
    assert(cloud_due(3000, 3000));
    assert(cloud_due(3001, 3000));
    assert(!cloud_due(UINT32_MAX - 5, 10));
    assert(cloud_due(10, UINT32_MAX - 5));
}
int main() {
    cloud_recovery_checks();
    AiPolicy p; p.init(123); arm(p);
    AiJob j{}; AiMessage m{};
    for (int n = 1; n <= 9; ++n) {
        assert(p.event(attempt(n, 1, 0, 9), j));
        assert(!strcmp(j.code, "042"));
        if (n == 3) assert(!strcmp(j.clue, "Left digit: go lower."));
        else if (n == 6) assert(!strcmp(j.clue, "Middle digit: go higher."));
        else if (n == 9) assert(!strcmp(j.clue, "Right digit: go lower."));
        else assert(!j.clue[0]);
    }
    ai_local(j, m); assert(!m.gemini); assert(strstr(m.message, j.clue));
    assert(ai_publication_ready(j.tag, j.tag, j.tag, j.tag.eventId));
    AiTag stale = j.tag; ++stale.actionRevision;
    assert(!ai_publication_ready(j.tag, stale, j.tag, j.tag.eventId));
    assert(!ai_publication_ready(j.tag, j.tag, stale, j.tag.eventId));
    assert(!ai_publication_ready(j.tag, j.tag, j.tag, "not-published"));
    AiTag blank{};
    assert(!ai_publication_ready(j.tag, blank, j.tag, j.tag.eventId));
    AiTag old = j.tag;
    p.status(VP_STATE_LOCKED, 1, 9); assert(!p.current(old));
    p.event(attempt(10), j); old = j.tag;
    p.status(VP_STATE_DIALING, 0, 10); assert(!p.current(old));
    p.event(attempt(11), j); old = j.tag;
    p.invalidate(); assert(!p.hasCode && !p.current(old));
    p.event(attempt(12), j); assert(!j.hasCode && !j.clue[0]);
    arm(p);
    vp_frame_t solved{}; solved.type = VP_SOLVED; solved.payload[0] = 4;
    p.event(solved, j); assert(j.hasCode && !p.hasCode); // job receives context; active RAM cleared
    old = j.tag; arm(p); assert(!p.current(old));
    uint8_t code[] = {9,9,9};
    p.prepareArm(code, VP_STATE_LOCKED, 0); p.delivery(VP_ARM, true);
    p.status(VP_STATE_LOCKED, 0, 0); assert(!p.hasCode); // ambiguous re-arm
    p.prepareArm(code, VP_STATE_DIALING, 3); p.delivery(VP_ARM, true);
    p.status(VP_STATE_DIALING, 0, 3); assert(!p.hasCode); // rejected arm
    p.prepareArm(code, VP_STATE_IDLE, 0); p.delivery(VP_ARM, false);
    p.status(VP_STATE_LOCKED, 0, 0); assert(!p.hasCode); // failed delivery
    p.prepareArm(code, VP_STATE_LOCKED, 5); p.delivery(VP_ARM, true);
    p.status(VP_STATE_LOCKED, 0, 0); assert(p.hasCode); // observable reset
    p.delivery(VP_DISARM, true); assert(!p.hasCode);
    arm(p);
    for (int n = 1; n <= 20; ++n) p.event(attempt(n, n % 10, 1, 1), j);
    assert(j.historyCount == 16 && j.history[0][0] == '5' && j.history[15][0] == '0');
    for (const char *bad : {"042", "Go higher.", "Try four.", "The code is yours.", "First move.",
                           "Knock again.", "Try seventeen.", "THREE.", "<script>", "", "a b c d e f g h i j k"}) assert(!ai_valid_reaction(bad));
    assert(ai_valid_reaction("Bold choice. Still locked."));
    // Mock provider replies exercise actual production parser; no credentials/network.
    auto good = reply("{\\\"reaction\\\":\\\"Bold choice. Still locked.\\\"}");
    assert(ai_parse_reply(good.c_str(), good.size(), j, m)); assert(m.gemini);
    for (auto bad : {std::string("not json"), reply("{\\\"reaction\\\":\\\"Try four.\\\"}"),
                    reply("{\\\"reaction\\\":\\\"Nice.\\\",\\\"code\\\":\\\"042\\\"}"),
                    reply("{\\\"reaction\\\":\\\"Nice.\\\"}", "MAX_TOKENS"),
                    std::string("{\"candidates\":[]}"), std::string(8193, 'x')})
        assert(!ai_parse_reply(bad.c_str(), bad.size(), j, m));
    uint32_t backoff = 60000;
    assert(ai_retry_delay(429, false, backoff) == 60000);
    assert(ai_retry_delay(429, false, backoff) == 120000);
    for (int n = 0; n < 9; ++n) assert(ai_retry_delay(429, false, backoff) <= 600000);
    assert(ai_retry_delay(401, false, backoff) == 600000);
    assert(ai_retry_delay(404, false, backoff) == 600000);
    assert(ai_retry_delay(-1, false, backoff) == 15000);
    assert(ai_retry_delay(200, false, backoff) == 15000);
    assert(ai_retry_delay(200, true, backoff) == 0 && backoff == 60000);
    puts("AI policy, provider parsing, quota backoff and cloud recovery checks passed");
}
