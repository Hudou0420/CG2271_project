#include "ai_policy.h"
#include "cloud_schedule.h"
#include "ai_response.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <ArduinoJson.h>
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
static std::string hint_reply(int id, const char *message) {
    JsonDocument output,provider;output["clueId"]=id;output["message"]=message;
    std::string text;serializeJson(output,text);
    provider["candidates"][0]["finishReason"]="STOP";
    provider["candidates"][0]["content"]["parts"][0]["text"]=text;
    std::string body;serializeJson(provider,body);return body;
}
static void hint_checks() {
    AiPolicy policy;policy.init(7);arm(policy);
    AiJob job{};AiMessage message{};
    assert(policy.event(attempt(1,1,0,9),job));
    assert(ai_clue_count(job)==6 && job.clue[0]);
    for (unsigned i=0;i<ai_clue_count(job);++i) {
        char clue[80];assert(ai_clue(job,i,clue,sizeof(clue)));
        std::string text=std::string("Nice try. ")+clue;
        auto body=hint_reply((int)i,text.c_str());
        assert(ai_parse_reply(body.c_str(),body.size(),job,message) && message.gemini);
        assert(!strcmp(message.message,text.c_str()));
    }
    for (auto body : {hint_reply(99,"Nice try."),hint_reply(-1,"Nice try."),
                     hint_reply(0,"Nice try."),hint_reply(0,"Left digit: go higher."),
                     hint_reply(0,"Try zero. Left digit: go lower."),
                     hint_reply(0,"042. Left digit: go lower."),
                     hint_reply(0,"Nice. Still locked. Left digit: go lower."),
                     hint_reply(0,"Left digit: go lower. Left digit: go lower.")})
        assert(!ai_parse_reply(body.c_str(),body.size(),job,message));
    char property[80];assert(!ai_clue(job,6,property,sizeof(property)));
    memcpy(job.code,"112",4);memcpy(job.guess,"221",4);
    assert(ai_clue(job,3,property,sizeof(property)) && !strcmp(property,"Left digit is odd."));
    assert(!ai_clue(job,6,property,sizeof(property)));
    memcpy(job.guess,"111",4);assert(ai_clue_count(job)==2);
    assert(ai_clue(job,0,property,sizeof(property)) && !strcmp(property,"Right digit: go higher."));
    assert(ai_clue(job,1,property,sizeof(property)) && !strcmp(property,"Right digit is even."));
    vp_frame_t knock{};knock.type=VP_KNOCK_RESULT;knock.len=3;
    knock.payload[1]=VP_GAP_SHORT;knock.payload[2]=VP_GAP_LONG;
    assert(policy.event(knock,job) && ai_clue_count(job)==3);
    char clue[80];assert(ai_clue(job,0,clue,sizeof(clue)));
    assert(!strcmp(clue,"Lengthen the pause after the opening tap."));
    assert(ai_clue(job,1,clue,sizeof(clue)) && !strcmp(clue,"Shorten the pause before the last tap."));
    ai_local(job,message);assert(message.message[0] && !message.gemini);
    auto body=hint_reply(1,clue);assert(ai_parse_reply(body.c_str(),body.size(),job,message));
    AiTag old=job.tag;policy.status(VP_STATE_LOCKED,1,0);assert(!policy.current(old));
    knock.payload[0]=VP_KNOCK_TIMEOUT;knock.payload[1]=VP_GAP_OK;knock.payload[2]=VP_GAP_UNKNOWN;
    assert(policy.event(knock,job) && ai_clue_count(job)==1);
    assert(!strcmp(job.clue,"Finish the rhythm without such a long pause."));
    policy.invalidate();assert(policy.event(attempt(2),job) && ai_clue_count(job)==0);
    body=hint_reply(-1," ");assert(!ai_parse_reply(body.c_str(),body.size(),job,message));
    body=hint_reply(0,"Left digit: go lower.");assert(!ai_parse_reply(body.c_str(),body.size(),job,message));
    puts("PASS every-failure clues, Gemini selection, exact wording, secret rejection and rhythm coaching");
}
static void latest_guess_checks() {
    const struct {const char *code;const char *guess;} cases[] = {
        {"123","423"}, {"123","143"}, {"123","124"}, {"123","443"},
        {"123","000"}, {"112","111"}, {"112","211"},
        {"007","107"}, {"007","070"}, {"007","007"}
    };
    const char *positions[] = {"Left", "Middle", "Right"};
    for (auto scenario : cases) {
        AiPolicy policy;policy.init(10);
        uint8_t code[3];for(int i=0;i<3;++i)code[i]=scenario.code[i]-'0';
        policy.prepareArm(code,VP_STATE_IDLE,0);policy.delivery(VP_ARM,true);
        policy.status(VP_STATE_LOCKED,0,0);assert(policy.hasCode);
        unsigned wrong=0;for(int i=0;i<3;++i)wrong+=scenario.code[i]!=scenario.guess[i];
        for(int n=1;n<=12;++n) {
            AiJob job{};AiMessage message{};
            assert(policy.event(attempt(n,scenario.guess[0]-'0',scenario.guess[1]-'0',scenario.guess[2]-'0'),job));
            assert(ai_clue_count(job)==wrong*2);
            for(unsigned choice=0;choice<ai_clue_count(job);++choice) {
                char clue[80];assert(ai_clue(job,choice,clue,sizeof(clue)));
                for(int i=0;i<3;++i)if(scenario.code[i]==scenario.guess[i])assert(!strstr(clue,positions[i]));
                assert(!strstr(clue,"combination"));
                auto body=hint_reply(choice,clue);
                assert(ai_parse_reply(body.c_str(),body.size(),job,message));
            }
            ai_local(job,message);
            for(int i=0;i<3;++i)if(scenario.code[i]==scenario.guess[i])assert(!strstr(message.message,positions[i]));
            if(!strcmp(scenario.code,"123") && !strcmp(scenario.guess,"423")) {
                assert(!strcmp(job.clue,"Left digit: go lower.") || !strcmp(job.clue,"Left digit is odd."));
                for(auto text : {"Right digit is odd.", "Middle digit is even.",
                     "All digits in the combination are different."}) {
                    for(int id=0;id<=2;++id) {
                        auto body=hint_reply(id,text);
                        assert(!ai_parse_reply(body.c_str(),body.size(),job,message));
                    }
                }
            }
        }
        // A new attempt must replace the previous guess when choosing clues.
        AiJob next{};auto changed=attempt(13,code[0],code[1],(code[2]+1)%10);
        assert(policy.event(changed,next) && ai_clue_count(next)==2);
        assert(!strncmp(next.clue,"Right digit",11));
        policy.invalidate();assert(policy.event(changed,next) && !next.clue[0]);
        AiMessage fallback{};ai_local(next,fallback);assert(!strcmp(fallback.message,"Still locked."));
    }
    puts("PASS hints target only latest incorrect positions, including 123/423, repeated digits and leading zeroes");
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
    hint_checks();
    latest_guess_checks();
    AiPolicy p; p.init(123); arm(p);
    AiJob j{}; AiMessage m{};
    for (int n = 1; n <= 9; ++n) {
        assert(p.event(attempt(n, 1, 0, 9), j));
        assert(!strcmp(j.code, "042"));
        assert(j.clue[0] && ai_clue_count(j) >= 4);

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
    j = {}; // A reaction-only event has no eligible clue.
    auto good = reply("{\\\"clueId\\\":-1,\\\"message\\\":\\\"Bold choice. Still locked.\\\"}");
    assert(ai_parse_reply(good.c_str(), good.size(), j, m)); assert(m.gemini);
    for (auto bad : {std::string("not json"), reply("{\\\"clueId\\\":-1,\\\"message\\\":\\\"Try four.\\\"}"),
                    reply("{\\\"clueId\\\":-1,\\\"message\\\":\\\"Nice.\\\",\\\"code\\\":\\\"042\\\"}"),
                    reply("{\\\"clueId\\\":-1,\\\"message\\\":\\\"Nice.\\\"}", "MAX_TOKENS"),
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
