#include "ai_response.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
bool ai_parse_reply(const char *body, size_t size, const AiJob &job, AiMessage &out) {
    if (size > 8192) return false;
    JsonDocument doc, output;
    if (deserializeJson(doc, body, size)) return false;
    const char *finish = doc["candidates"][0]["finishReason"] | "";
    if (strcmp(finish, "STOP")) return false;
    JsonArrayConst parts = doc["candidates"][0]["content"]["parts"].as<JsonArrayConst>();
    if (parts.size() != 1 || !parts[0]["text"].is<const char *>()) return false;
    const char *text = parts[0]["text"];
    if (deserializeJson(output, text) || !output.is<JsonObject>() || output.size() != 1 ||
        !output["reaction"].is<const char *>() || !ai_valid_reaction(output["reaction"])) return false;
    ai_local(job, out);
    snprintf(out.message, sizeof(out.message), "%s%s%s", output["reaction"].as<const char *>(),
             job.clue[0] ? " " : "", job.clue);
    out.gemini = true;
    return true;
}
uint32_t ai_retry_delay(int code, bool valid, uint32_t &backoff) {
    if (code == 429) {
        uint32_t delay = backoff;
        backoff = backoff >= 300000 ? 600000 : backoff * 2;
        return delay;
    }
    if (valid) { backoff = 60000; return 0; }
    return code == 401 || code == 403 || code == 404 ? 600000 : 15000;
}
