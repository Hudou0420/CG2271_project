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
    s_tls.useBuiltinCACertBundle();s_stats={};s_transportFailed=false;
    s_awaitKey[0]="";s_awaitKey[1]="";s_awaitPlayer="";s_lastCommandKey="";
    s_lastCommandAction="";s_lastCommandResult="";s_resultPending=false;
    s_idToken="fixture-token";s_refreshToken="";s_tokenExpiresMs=100000;
    s_publishedOnce=false;s_writtenReaction={};s_writtenEvent[0]=0;
    s_queue=xQueueCreate(CLOUD_EVENT_QUEUE_LEN,sizeof(CloudItem));
    state_set_status(VP_STATE_IDLE,{});sent.clear();link_ready=true;
}
static void plan(int code,const char *body="null") {test_http_plans.push_back({code,body,-2,{}});}
static void clean() {assert(test_live_tls==0 && test_http_objects==0 && test_http_plans.empty());HttpsLease lease(0);assert(lease.acquired());}
int main() {
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
    test_runtime_reset();
}
