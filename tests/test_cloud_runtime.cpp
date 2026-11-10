#include "../esp32/src/cloud.cpp"
#include <cassert>
static bool link_ready=true;
static std::vector<std::pair<uint8_t,std::vector<uint8_t>>> sent;
bool link_send(uint8_t type,const uint8_t *payload,uint8_t len) {
    if(!link_ready)return false;
    sent.push_back({type,len?std::vector<uint8_t>(payload,payload+len):std::vector<uint8_t>{}});return true;
}
static void reset() {
    s_tls.stop();test_runtime_reset();https_guard_init();state_init();ai_init();ai_enabled(true);
    test_http_plans.clear();test_http_calls.clear();test_http_begin_ok=true;ESP={};test_now=1000;
    s_tls.useBuiltinCACertBundle();s_stats={};s_haveAttempt=false;s_transportFailed=false;
    s_awaitKey[0]="";s_awaitKey[1]="";s_awaitPlayer="";s_lastCommandKey="";
    s_lastCommandAction="";s_lastCommandResult="";s_resultPending=false;
    s_idToken="fixture-token";s_refreshToken="";s_tokenExpiresMs=100000;
    s_publishedOnce=false;s_writtenReaction={};s_writtenEvent[0]=0;
    s_queue=xQueueCreate(CLOUD_EVENT_QUEUE_LEN,sizeof(CloudItem));
    state_set_status(VP_STATE_IDLE,{});sent.clear();link_ready=true;
}
static void plan(int code,const char *body="null") {test_http_plans.push_back({code,body,-2,{}});}
static void clean() {assert(test_live_tls==0 && test_http_objects==0 && test_http_plans.empty());HttpsLease lease(0);assert(lease.acquired());}
static void attempt_retries() {
    reset();vp_frame_t frame{};frame.type=VP_ATTEMPT;frame.len=VP_LEN_ATTEMPT;
    const uint8_t payload[]={3,4,5,2,50,0,0,0};memcpy(frame.payload,payload,sizeof(payload));
    cloud_on_message(frame);auto first=ai_snapshot();
    assert(uxQueueMessagesWaiting(s_queue)==1 && first.reaction.message[0]);
    // Heartbeats and unrelated input between retries do not erase the identity.
    vp_frame_t status{};status.type=VP_STATUS;status.len=VP_LEN_STATUS;
    status.payload[VP_ST_STATE]=VP_STATE_LOCKED;cloud_on_message(status);
    auto before=ai_snapshot();cloud_on_message(frame);auto after=ai_snapshot();
    assert(uxQueueMessagesWaiting(s_queue)==1 && after.version==before.version);
    assert(!strcmp(after.tag.eventId,before.tag.eventId));
    CloudItem item{};assert(xQueueReceive(s_queue,&item,0));
    uint8_t step=0;plan(200);assert(send_item(item,step)==ITEM_DONE);clean();
    assert(test_http_calls.size()==1);cloud_on_message(frame);
    assert(!uxQueueMessagesWaiting(s_queue));
    // A genuine later attempt may repeat exactly the same guessed digits.
    ++frame.payload[3];cloud_on_message(frame);assert(uxQueueMessagesWaiting(s_queue)==1);
    // A fresh round accepts the same packet bytes again.
    ai_cancel_context();cloud_on_message(frame);assert(uxQueueMessagesWaiting(s_queue)==2);
    reset();CloudItem filler{};while(xQueueSend(s_queue,&filler,0)){}
    cloud_on_message(frame);assert(!s_haveAttempt && s_stats.eventsDropped==1);
    assert(xQueueReceive(s_queue,&item,0));cloud_on_message(frame);
    assert(s_haveAttempt && uxQueueMessagesWaiting(s_queue)==CLOUD_EVENT_QUEUE_LEN);
    before=ai_snapshot();cloud_on_message(frame);
    assert(ai_snapshot().version==before.version && s_stats.eventsDropped==1);
    // Legacy five-byte attempt packets are also retry-safe.
    reset();frame.len=VP_LEN_ATTEMPT_LEGACY;cloud_on_message(frame);cloud_on_message(frame);
    assert(uxQueueMessagesWaiting(s_queue)==1);
    puts("PASS duplicate UART attempts produce one event/AI action; later attempts, new rounds and queue recovery remain valid");
}
int main() {
    attempt_retries();
    reset();plan(200,R"({"state":"LOCKED"})");String reply;
    assert(db_request("GET","/status","","",&reply)==200 && reply==R"({"state":"LOCKED"})");clean();
    assert(test_http_calls[0].url=="https://vault.invalid/vault/status.json?auth=fixture-token");
    plan(-11);assert(db_request("PUT","/status","","{}")==-11 && s_transportFailed);clean();
    plan(200);assert(db_request("PUT","/status","","{}")==200 && !s_transportFailed);clean();
    plan(401);assert(db_request("GET","/commands","","")==401 && s_idToken.empty());clean();
    assert(worth_retrying(401) && worth_retrying(503) && worth_retrying(-1) && !worth_retrying(403));
    assert(s_stats.requestsOk==2 && s_stats.requestsFailed==2);
    puts("PASS production Firebase transport failures/recovery, auth invalidation, lease and HTTP-object cleanup");
    reset();s_idToken="";
    plan(200,R"({"idToken":"new-token","refreshToken":"refresh-token","expiresIn":"3600"})");
    assert(ensure_signed_in() && s_idToken=="new-token" && s_refreshToken=="refresh-token");clean();
    s_tokenExpiresMs=test_now+50000;
    plan(200,R"({"id_token":"renewed","refresh_token":"refresh-again","expires_in":"3600"})");
    assert(ensure_signed_in() && s_idToken=="renewed");clean();
    s_idToken="";s_refreshToken="";plan(200,"not JSON");assert(!ensure_signed_in());clean();
    puts("PASS production Firebase device sign-in, token refresh and invalid auth reply");
    reset();JsonDocument command;command["action"]="arm";command["code"]="007";command["player"]="Test";
    assert(run_command("leading-zero",command.as<JsonObjectConst>())=="sent");
    assert(sent.size()==1 && sent[0].first==VP_ARM && sent[0].second==std::vector<uint8_t>({0,0,7,0}));
    assert(run_command("other",command.as<JsonObjectConst>()).startsWith("rejected:"));
    s_awaitKey[0]="";command["code"]="07x";
    assert(run_command("bad",command.as<JsonObjectConst>()).startsWith("rejected:") && sent.size()==1);
    command["code"]="123";state_set_status(VP_STATE_ALARM,{});
    assert(run_command("alarm",command.as<JsonObjectConst>()).startsWith("rejected:"));
    state_set_status(VP_STATE_DIALING,{});assert(run_command("dialing",command.as<JsonObjectConst>()).startsWith("rejected:"));
    state_set_status(VP_STATE_IDLE,{});link_ready=false;assert(run_command("busy",command.as<JsonObjectConst>())=="rejected: link busy");
    link_ready=true;test_now+=MCU_OFFLINE_MS;assert(run_command("offline",command.as<JsonObjectConst>())=="rejected: MCU offline");
    puts("PASS production leading-zero command, pending-command exclusion, invalid code, alarm/dialing/offline/link-busy gates");
    reset();const char *queued=R"({"command-key":{"action":"arm","code":"042","player":"Test"}})";
    plan(200,queued);plan(-11);assert(!poll_commands());assert(sent.size()==1 && s_resultPending);clean();
    plan(200,queued);plan(200);plan(-11);assert(!poll_commands());assert(sent.size()==1 && !s_resultPending);clean();
    plan(200,queued);plan(200);assert(poll_commands());assert(sent.size()==1 && s_stats.commandsHandled==1);clean();
    assert(test_http_calls.back().method=="DELETE");
    puts("PASS production uncertain command-result/delete writes never resend MCU commands");
    reset();vp_frame_t frame{};frame.type=VP_SOLVED;frame.len=VP_LEN_SOLVED;frame.payload[0]=2;vp_put_u16(frame.payload+1,42);
    AiTag tag{};strlcpy(tag.roundId,"round",sizeof(tag.roundId));strlcpy(tag.eventId,"stable-event",sizeof(tag.eventId));tag.actionRevision=3;
    uint8_t step=0;plan(200);plan(503);assert(send_frame(frame,tag,step)==ITEM_RETRY && step==1);clean();
    plan(200);assert(send_frame(frame,tag,step)==ITEM_DONE);clean();
    assert(test_http_calls.size()==3);
    assert(test_http_calls[0].method=="PUT" && test_http_calls[0].url.find("/leaderboard/stable-event.json")!=String::npos);
    assert(test_http_calls[1].url==test_http_calls[2].url && test_http_calls[2].method=="PUT");
    puts("PASS production multi-step retry progress and stable event/leaderboard IDs");
    reset();vp_frame_t attempt{};attempt.type=VP_ATTEMPT;attempt.len=VP_LEN_ATTEMPT;
    const uint8_t data[]={2,1,5,1,86,1,1,0};memcpy(attempt.payload,data,sizeof(data));
    step=0;plan(200);assert(send_frame(attempt,tag,step)==ITEM_DONE);clean();
    JsonDocument record;assert(!deserializeJson(record,test_http_calls.back().body));
    assert(record["guess"].as<String>()=="215" && record["feedback"].as<String>()=="110");
    assert(record["code"].isNull());
    attempt.len=VP_LEN_ATTEMPT_LEGACY;step=0;plan(200);
    assert(send_frame(attempt,tag,step)==ITEM_DONE);clean();
    assert(!deserializeJson(record,test_http_calls.back().body) && record["feedback"].isNull());
    puts("PASS cloud publishes per-digit feedback, omits legacy feedback, and never publishes code");
    reset();vp_frame_t knock{};knock.type=VP_KNOCK_RESULT;knock.len=VP_LEN_KNOCK_RESULT;
    knock.payload[0]=VP_KNOCK_TIMEOUT;knock.payload[1]=VP_GAP_OK;
    step=0;plan(200);assert(send_frame(knock,tag,step)==ITEM_DONE);clean();
    assert(!deserializeJson(record,test_http_calls.back().body));
    assert(record["type"].as<String>()=="knock_failed" && record["reason"].as<String>()=="timeout" && record["gaps"].as<String>()=="20");
    assert(record["code"].isNull() && record["guess"].isNull() && record["targetTimings"].isNull());
    assert(!strcmp(s_writtenEvent,tag.eventId));
    cloud_on_message(knock);assert(uxQueueMessagesWaiting(s_queue)==1);
    puts("PASS failed knocks publish qualitative feedback and participate in AI publication ordering");
    test_runtime_reset();
}
