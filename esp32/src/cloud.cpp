// cloud.cpp - web dashboard link over Firebase Realtime Database (REST).
//
// tCloud (lowest priority) owns Wi-Fi and HTTPS, so a slow network can never
// delay tLink or tImu. Each pass it does at most one of, in this order:
//   1. send the oldest queued item (game event, command result), retrying it
//      until the database accepts it,
//   2. publish /vault/status when it changed (or as a heartbeat),
//   3. poll /vault/commands for arm/disarm requests from the dashboard.
// HTTPS certificates are checked against the core's built-in CA bundle, and
// one keep-alive connection to the database is reused between requests.
// Database layout: web/README.md.
#include "cloud.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "config.h"
#include "link.h"
#include "vault_state.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define CLOUD_CONFIGURED 1
#else
#define CLOUD_CONFIGURED 0
#endif

// ---- Queue items (from the link task) ----------------------------------------

enum ItemKind : uint8_t {
    ITEM_FRAME,      // ATTEMPT / SOLVED / ALARM from the MCU
    ITEM_DELIVERED,  // our ARM / DISARM was ACKed (ok) or given up on
    ITEM_MCU_LINK    // MCU went online / offline (ok = online)
};

struct CloudItem {
    ItemKind   kind;
    vp_frame_t frame;
    uint8_t    type;
    bool       ok;
};

enum ItemResult { ITEM_DONE, ITEM_RETRY };

static QueueHandle_t s_queue;
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static CloudStats    s_stats;

#if CLOUD_CONFIGURED

// ---- Task-private state (tCloud only) ------------------------------------------

struct Published {
    bool       known;
    vp_state_t state;
    bool       mcuOnline;
    McuSensors sensors;
};

static NetworkClientSecure s_tls; // keep-alive connection to the database
static HTTPClient          s_http;
static String              s_idToken;
static String              s_refreshToken;
static uint32_t            s_tokenExpiresMs;

static uint8_t  s_itemStep;          // sub-requests of the current item already done
static String   s_awaitKey[2];       // command waiting for ARM [0] / DISARM [1] delivery
static String   s_awaitPlayer;       // player named in the ARM being delivered
static String   s_roundPlayer = "console";
static String   s_lastCommandKey;    // already sent to the MCU: never send it again
static String   s_lastCommandAction;
static String   s_lastCommandResult;
static bool     s_resultPending;     // its /commandResults entry is not written yet
static uint32_t s_wifiBeginMs;
static uint32_t s_nextTryMs;         // back-off after a failure
static uint32_t s_lastPollMs;
static uint32_t s_lastStatusMs;
static Published s_published;
static bool      s_publishedOnce;

static void set_error(const char *fmt, ...)
{
    char    msg[sizeof(s_stats.lastError)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    bool changed;
    taskENTER_CRITICAL(&s_mux);
    changed = strcmp(msg, s_stats.lastError) != 0;
    strcpy(s_stats.lastError, msg);
    taskEXIT_CRITICAL(&s_mux);
    if (changed) { // don't repeat the same error every retry
        Serial.printf("[cloud] %s\n", msg);
    }
}

static void count_request(bool ok, int code)
{
    taskENTER_CRITICAL(&s_mux);
    if (ok) {
        s_stats.requestsOk++;
    } else {
        s_stats.requestsFailed++;
    }
    s_stats.lastHttpCode = code;
    taskEXIT_CRITICAL(&s_mux);
}

static void back_off()
{
    s_nextTryMs = millis() + CLOUD_RETRY_MS;
}

// Percent-encode a path segment (command keys come from the dashboard).
static String url_segment(const String &s)
{
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_') {
            out += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
            out += hex;
        }
    }
    return out;
}

// ---- Firebase anonymous sign-in --------------------------------------------------

static bool auth_post(const String &url, const char *contentType, const String &body,
                      JsonDocument &out)
{
    NetworkClientSecure tls;
    HTTPClient          http;

    tls.useBuiltinCACertBundle();
    http.setConnectTimeout(CLOUD_HTTP_TIMEOUT_MS);
    http.setTimeout(CLOUD_HTTP_TIMEOUT_MS);
    if (!http.begin(tls, url)) {
        set_error("sign-in: bad URL");
        return false;
    }
    http.addHeader("Content-Type", contentType);
    int    code = http.POST(body);
    String resp = code > 0 ? http.getString() : String();
    http.end();
    count_request(code == 200, code);

    if (code != 200) {
        // Firebase explains itself, e.g. ADMIN_ONLY_OPERATION = anonymous sign-in disabled.
        JsonDocument err;
        const char  *why = "";
        if (deserializeJson(err, resp) == DeserializationError::Ok) {
            why = err["error"]["message"] | "";
        }
        set_error("sign-in failed: HTTP %d %s", code, why);
        return false;
    }
    if (deserializeJson(out, resp) != DeserializationError::Ok) {
        set_error("sign-in: bad reply");
        return false;
    }
    return true;
}

// Keep a valid ID token: refresh it a minute before it expires (1 h).
static bool ensure_signed_in()
{
    if (strlen(FIREBASE_API_KEY) == 0) {
        taskENTER_CRITICAL(&s_mux);
        s_stats.signedIn = true; // database rules allow access without signing in
        taskEXIT_CRITICAL(&s_mux);
        return true;
    }
    if (s_idToken.length() > 0 && (int32_t)(s_tokenExpiresMs - millis()) > 60000) {
        return true;
    }

    JsonDocument doc;
    const char  *idToken;
    const char  *refresh;
    const char  *expires;
    bool         fresh = s_refreshToken.length() == 0;

    if (fresh) {
        if (!auth_post(String("https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=") +
                           FIREBASE_API_KEY,
                       "application/json", "{\"returnSecureToken\":true}", doc)) {
            return false;
        }
        idToken = doc["idToken"] | "";
        refresh = doc["refreshToken"] | "";
        expires = doc["expiresIn"] | "3600";
    } else {
        if (!auth_post(String("https://securetoken.googleapis.com/v1/token?key=") +
                           FIREBASE_API_KEY,
                       "application/x-www-form-urlencoded",
                       "grant_type=refresh_token&refresh_token=" + s_refreshToken, doc)) {
            s_refreshToken = ""; // start over with a new sign-in next time
            return false;
        }
        idToken = doc["id_token"] | "";
        refresh = doc["refresh_token"] | "";
        expires = doc["expires_in"] | "3600";
    }

    s_idToken        = idToken;
    s_refreshToken   = refresh;
    s_tokenExpiresMs = millis() + (uint32_t)atoi(expires) * 1000U;
    if (fresh) {
        Serial.println("[cloud] signed in to Firebase (anonymous)");
    }
    bool signedIn = s_idToken.length() > 0;
    taskENTER_CRITICAL(&s_mux);
    s_stats.signedIn = signedIn;
    taskEXIT_CRITICAL(&s_mux);
    return signedIn;
}

// ---- Database requests ---------------------------------------------------------

// path is relative to CLOUD_ROOT, e.g. "/status". Returns the HTTP code
// (<= 0 for network errors) and stores the reply body in *reply if given.
static int db_request(const char *method, const String &path, const String &query,
                      const String &body, String *reply = nullptr)
{
    String url = String(FIREBASE_DB_URL) + CLOUD_ROOT + path + ".json";
    String q   = query;
    if (s_idToken.length() > 0) {
        q += (q.length() > 0 ? "&auth=" : "auth=") + s_idToken;
    }
    if (q.length() > 0) {
        url += "?" + q;
    }

    if (!s_http.begin(s_tls, url)) {
        set_error("bad database URL, check FIREBASE_DB_URL");
        return -1;
    }
    if (body.length() > 0) {
        s_http.addHeader("Content-Type", "application/json");
    }
    int    code = s_http.sendRequest(method, body);
    String resp = code > 0 ? s_http.getString() : String();
    s_http.end();

    bool ok = code == 200;
    count_request(ok, code);
    if (!ok) {
        if (code <= 0) {
            s_tls.stop(); // reconnect from scratch next time
            set_error("%s %s: %s", method, path.c_str(), HTTPClient::errorToString(code).c_str());
        } else {
            if (code == 401) {
                s_idToken = ""; // expired or revoked: sign in again
            }
            set_error("%s %s: HTTP %d %s", method, path.c_str(), code, resp.substring(0, 24).c_str());
        }
    }
    if (reply != nullptr) {
        *reply = resp;
    }
    return code;
}

// Retry on network errors, 5xx and 401 (after signing in again); a request the
// database rejects for good (other 4xx) is dropped so it can't block the queue.
static bool worth_retrying(int code)
{
    return code <= 0 || code >= 500 || code == 401;
}

static int write_json(const char *method, const String &path, JsonDocument &doc,
                      const char *timeField)
{
    String body;
    if (timeField != nullptr) {
        doc[timeField][".sv"] = "timestamp"; // filled in by the server
    }
    serializeJson(doc, body);
    return db_request(method, path, "", body);
}

// One sub-request of a multi-request item. Skips steps already done on an
// earlier try, so a retry never duplicates an append.
#define ITEM_STEP(n, call)                         \
    if (step == (n)) {                             \
        int code_ = (call);                        \
        if (code_ != 200 && worth_retrying(code_)) \
            return ITEM_RETRY;                     \
        step++;                                    \
    }

// ---- Queue items -> database writes ------------------------------------------------

static ItemResult send_frame(const vp_frame_t &f, uint8_t &step)
{
    const uint8_t *p = f.payload;
    JsonDocument   ev;

    switch (f.type) {
    case VP_ATTEMPT: {
        char guess[4] = {(char)('0' + p[0]), (char)('0' + p[1]), (char)('0' + p[2]), '\0'};
        ev["type"]      = "attempt";
        ev["guess"]     = guess;
        ev["attempt"]   = p[3];
        ev["closeness"] = p[4];
        ITEM_STEP(0, write_json("POST", "/events", ev, "at"));
        break;
    }
    case VP_SOLVED: {
        JsonDocument entry;
        entry["player"]   = s_roundPlayer;
        entry["attempts"] = p[0];
        entry["seconds"]  = vp_get_u16(&p[1]);
        ev["type"]        = "solved";
        ev["player"]      = s_roundPlayer;
        ev["attempts"]    = p[0];
        ev["seconds"]     = vp_get_u16(&p[1]);
        ITEM_STEP(0, write_json("POST", "/leaderboard", entry, "at"));
        ITEM_STEP(1, write_json("POST", "/events", ev, "at"));
        break;
    }
    case VP_ALARM:
        ev["type"]  = "alarm";
        ev["cause"] = p[0] == VP_CAUSE_TAMPER ? "tamper" : "beam";
        ITEM_STEP(0, write_json("POST", "/events", ev, "at"));
        break;
    default:
        break;
    }
    return ITEM_DONE;
}

static ItemResult send_delivered(uint8_t type, bool ok, uint8_t &step)
{
    int     slot = type == VP_ARM ? 0 : 1;
    String &key  = s_awaitKey[slot];

    if (step == 0) {
        // First try only: update the round's player before SOLVED can arrive.
        if (type == VP_ARM && ok) {
            s_roundPlayer = key.length() > 0 ? s_awaitPlayer : String("console");
        }
    }
    if (key.length() > 0) {
        JsonDocument res;
        res["result"] = ok ? "delivered" : "failed: MCU did not acknowledge";
        ITEM_STEP(0, write_json("PATCH", "/commandResults/" + url_segment(key), res, "at"));
    } else if (step == 0) {
        step++; // sent from the console: no command to report back to
    }
    if (ok) {
        JsonDocument ev;
        ev["type"] = type == VP_ARM ? "armed" : "disarmed";
        if (type == VP_ARM) {
            ev["player"] = s_roundPlayer; // never the code: every dashboard user can read events
        }
        ITEM_STEP(1, write_json("POST", "/events", ev, "at"));
    }
    key = "";
    return ITEM_DONE;
}

static ItemResult send_item(const CloudItem &item, uint8_t &step)
{
    switch (item.kind) {
    case ITEM_FRAME:
        return send_frame(item.frame, step);
    case ITEM_DELIVERED:
        return send_delivered(item.type, item.ok, step);
    case ITEM_MCU_LINK: {
        JsonDocument ev;
        ev["type"] = item.ok ? "mcu_online" : "mcu_offline";
        ITEM_STEP(0, write_json("POST", "/events", ev, "at"));
        return ITEM_DONE;
    }
    }
    return ITEM_DONE;
}

// ---- Status ----------------------------------------------------------------------

static Published snapshot()
{
    VaultState vs = state_get();
    return {vs.stateKnown, vs.mcuState, state_mcu_online(), vs.sensors};
}

// What the dashboard shows; raw encoder steps and event counts are left out.
static bool status_changed(const Published &a, const Published &b)
{
    const McuSensors &x = a.sensors;
    const McuSensors &y = b.sensors;
    return a.known != b.known || a.state != b.state || a.mcuOnline != b.mcuOnline ||
           x.digitIndex != y.digitIndex || x.currentDigit != y.currentDigit ||
           x.knocks != y.knocks || x.attempts != y.attempts || x.closeness != y.closeness ||
           x.flags != y.flags || abs((int)x.light - (int)y.light) >= CLOUD_LIGHT_DELTA;
}

static bool status_due(const Published &now)
{
    uint32_t since = millis() - s_lastStatusMs;
    if (!s_publishedOnce || since >= CLOUD_STATUS_HEARTBEAT_MS) {
        return true;
    }
    return since >= CLOUD_STATUS_MIN_MS && status_changed(now, s_published);
}

static bool publish_status(const Published &now)
{
    const McuSensors &s = now.sensors;
    JsonDocument      doc;

    doc["state"]         = now.known ? state_name(now.state) : "UNKNOWN";
    doc["mcuOnline"]     = now.mcuOnline;
    doc["digitsEntered"] = s.digitIndex;
    doc["dial"]          = s.currentDigit;
    doc["knocks"]        = s.knocks;
    doc["attempts"]      = s.attempts;
    doc["closeness"]     = s.closeness;
    doc["light"]         = s.light;
    doc["lightValid"]    = (s.flags & VP_STF_LIGHT_VALID) != 0;
    doc["beamEnabled"]   = (s.flags & VP_STF_BEAM_ENABLED) != 0;
    doc["inputsReady"]   = (s.flags & VP_STF_INPUTS_READY) != 0;
    doc["uptimeS"]       = millis() / 1000;

    int code = write_json("PUT", "/status", doc, "updatedAt");
    s_lastStatusMs = millis(); // also after a failure: don't hammer the database
    if (code != 200) {
        return false;
    }
    s_published     = now;
    s_publishedOnce = true;
    return true;
}

// ---- Commands from the dashboard -----------------------------------------------------

static String parse_code(JsonVariantConst v, uint8_t digits[3])
{
    char buf[8];
    if (v.is<const char *>()) {
        strlcpy(buf, v.as<const char *>(), sizeof(buf));
    } else if (v.is<int>() && v.as<int>() >= 0 && v.as<int>() <= 999) {
        snprintf(buf, sizeof(buf), "%03d", v.as<int>());
    } else {
        return "rejected: code must be 3 digits";
    }
    if (strlen(buf) != 3 || !isdigit(buf[0]) || !isdigit(buf[1]) || !isdigit(buf[2])) {
        return "rejected: code must be 3 digits";
    }
    for (int i = 0; i < 3; i++) {
        digits[i] = (uint8_t)(buf[i] - '0');
    }
    return "";
}

// Hand one command to the MCU link. Returns the result for /commandResults.
static String run_command(const String &key, JsonObjectConst cmd)
{
    const char *action = cmd["action"] | "";

    if (strcmp(action, "arm") != 0 && strcmp(action, "disarm") != 0) {
        return "rejected: unknown action";
    }
    if (!state_mcu_online()) {
        return "rejected: MCU offline";
    }

    if (strcmp(action, "disarm") == 0) {
        if (!link_send(VP_DISARM, nullptr, 0)) {
            return "rejected: link busy";
        }
        s_awaitKey[1] = key;
        Serial.println("[cloud] dashboard: disarm");
        return "sent";
    }

    uint8_t payload[VP_LEN_ARM];
    String  err = parse_code(cmd["code"], payload);
    if (err.length() > 0) {
        return err;
    }
    int pattern = cmd["pattern"] | 0;
    payload[3]  = (uint8_t)constrain(pattern, 0, 255);

    String player = cmd["player"] | "anonymous";
    player.trim();
    if (player.length() == 0) {
        player = "anonymous";
    }
    if (player.length() > CLOUD_PLAYER_MAX) {
        player = player.substring(0, CLOUD_PLAYER_MAX);
    }

    if (!link_send(VP_ARM, payload, sizeof(payload))) {
        return "rejected: link busy";
    }
    s_awaitKey[0] = key;
    s_awaitPlayer = player;
    Serial.printf("[cloud] dashboard: arm (player %s)\n", player.c_str());
    return "sent";
}

// Oldest command first. Each one is sent to the MCU once, its result written,
// and then it is deleted; if a write fails, the next poll finishes the job
// without sending the command again.
static bool poll_commands()
{
    String reply;
    int    code = db_request("GET", "/commands", "orderBy=%22%24key%22&limitToFirst=1", "",
                             &reply);
    if (code != 200) {
        return false;
    }
    if (reply == "null") {
        return true; // nothing waiting
    }

    JsonDocument doc;
    if (deserializeJson(doc, reply) != DeserializationError::Ok) {
        set_error("GET /commands: bad JSON");
        return false;
    }
    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
        String key = kv.key().c_str();

        if (key != s_lastCommandKey) {
            JsonObjectConst cmd = kv.value().as<JsonObjectConst>();
            s_lastCommandKey    = key;
            s_lastCommandAction = cmd["action"] | "";
            s_lastCommandResult = cmd.isNull() ? String("rejected: not an object")
                                               : run_command(key, cmd);
            s_resultPending     = true;
            taskENTER_CRITICAL(&s_mux);
            s_stats.commandsHandled++;
            taskEXIT_CRITICAL(&s_mux);
        }
        if (s_resultPending) {
            JsonDocument res;
            res["action"] = s_lastCommandAction;
            res["result"] = s_lastCommandResult;
            code          = write_json("PUT", "/commandResults/" + url_segment(key), res, "at");
            if (code != 200 && worth_retrying(code)) {
                return false;
            }
            s_resultPending = false;
        }
        code = db_request("DELETE", "/commands/" + url_segment(key), "", "");
        return code == 200 || !worth_retrying(code);
    }
    return true;
}

// ---- Wi-Fi -----------------------------------------------------------------------------

static const char *wifi_reason(wl_status_t s)
{
    switch (s) {
    case WL_NO_SSID_AVAIL:  return "network not found (2.4 GHz only)";
    case WL_CONNECT_FAILED: return "connection failed (password?)";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:   return "disconnected";
    default:                return "connecting";
    }
}

static bool wifi_ready()
{
    static bool        wasUp;
    static wl_status_t lastStatus = WL_IDLE_STATUS;

    wl_status_t st = WiFi.status();
    bool        up = st == WL_CONNECTED;

    if (up != wasUp) {
        wasUp = up;
        s_tls.stop();
        if (up) {
            Serial.printf("[cloud] Wi-Fi connected to %s, IP %s, RSSI %d dBm\n", WIFI_SSID,
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        taskENTER_CRITICAL(&s_mux);
        s_stats.wifiUp = up;
        taskEXIT_CRITICAL(&s_mux);
    }
    if (!up) {
        if (st != lastStatus) {
            Serial.printf("[cloud] Wi-Fi %s: %s\n", WIFI_SSID, wifi_reason(st));
        }
        if (millis() - s_wifiBeginMs > 15000) { // kick it if it stays down
            s_wifiBeginMs = millis();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }
    lastStatus = st;
    return up;
}

// ---- Task ------------------------------------------------------------------------------

static void track_mcu_link()
{
    static bool wasOnline;
    bool        online = state_mcu_online();

    if (online != wasOnline) {
        wasOnline      = online;
        CloudItem item = {};
        item.kind      = ITEM_MCU_LINK;
        item.ok        = online;
        xQueueSend(s_queue, &item, 0);
    }
}

static void cloud_task(void *)
{
    bool staleCleared = false;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_wifiBeginMs = millis();
    s_tls.useBuiltinCACertBundle();
    s_http.setReuse(true);
    s_http.setConnectTimeout(CLOUD_HTTP_TIMEOUT_MS);
    s_http.setTimeout(CLOUD_HTTP_TIMEOUT_MS);

    for (;;) {
        CloudItem item;
        bool      haveItem = xQueuePeek(s_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE;

        track_mcu_link();
        if (!wifi_ready() || (int32_t)(millis() - s_nextTryMs) < 0) {
            continue;
        }
        if (!ensure_signed_in()) {
            back_off();
            continue;
        }

        // Commands left from before this boot are stale: never act on them.
        if (!staleCleared) {
            int code = db_request("DELETE", "/commands", "", "");
            if (code != 200) {
                back_off();
                continue;
            }
            staleCleared = true;
            Serial.println("[cloud] connected to the database");
        }

        if (haveItem) {
            if (send_item(item, s_itemStep) == ITEM_DONE) {
                xQueueReceive(s_queue, &item, 0);
                s_itemStep = 0;
            } else {
                back_off();
            }
            continue;
        }

        Published now = snapshot();
        if (status_due(now)) {
            if (!publish_status(now)) {
                back_off();
            }
            continue;
        }

        if (millis() - s_lastPollMs >= CLOUD_COMMAND_POLL_MS) {
            s_lastPollMs = millis();
            if (!poll_commands()) {
                back_off();
            }
        }
    }
}

#endif // CLOUD_CONFIGURED

// ---- Public API ----------------------------------------------------------------------

static void enqueue(const CloudItem &item)
{
    if (s_queue == nullptr) {
        return; // cloud link off
    }
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
        taskENTER_CRITICAL(&s_mux);
        s_stats.eventsDropped++;
        taskEXIT_CRITICAL(&s_mux);
    }
}

void cloud_init()
{
#if CLOUD_CONFIGURED
    s_stats.configured = true;
    s_queue            = xQueueCreate(CLOUD_EVENT_QUEUE_LEN, sizeof(CloudItem));
    xTaskCreate(cloud_task, "tCloud", CLOUD_TASK_STACK, nullptr, CLOUD_TASK_PRIO, nullptr);
    Serial.printf("Cloud: Wi-Fi '%s', database %s%s\n", WIFI_SSID, FIREBASE_DB_URL, CLOUD_ROOT);
#else
    Serial.println("Cloud: off (copy include/secrets.example.h to include/secrets.h)");
#endif
}

CloudStats cloud_stats()
{
    taskENTER_CRITICAL(&s_mux);
    CloudStats copy = s_stats;
    taskEXIT_CRITICAL(&s_mux);
    return copy;
}

void cloud_on_message(const vp_frame_t &frame)
{
    if (frame.type != VP_ATTEMPT && frame.type != VP_SOLVED && frame.type != VP_ALARM) {
        return;
    }
    CloudItem item = {};
    item.kind      = ITEM_FRAME;
    item.frame     = frame;
    enqueue(item);
}

void cloud_on_delivered(uint8_t type, bool ok)
{
    if (type != VP_ARM && type != VP_DISARM) {
        return;
    }
    CloudItem item = {};
    item.kind      = ITEM_DELIVERED;
    item.type      = type;
    item.ok        = ok;
    enqueue(item);
}
