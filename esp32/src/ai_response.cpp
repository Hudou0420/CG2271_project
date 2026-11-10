#include "ai_response.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
bool ai_parse_reply(const char *body, size_t size, const AiJob &job, AiMessage &out) {
    if (size > 8192) return false;
    JsonDocument doc, output;
    if (deserializeJson(doc, body, size)) return false;
    const char *finish = doc["candidates"][0]["finishReason"] | "";
    if (strcmp(finish, "STOP")) return false;
    JsonArrayConst parts = doc["candidates"][0]["content"]["parts"].as<JsonArrayConst>();
    if (parts.size() != 1 || !parts[0]["text"].is<const char *>()) return false;
    const char *text = parts[0]["text"];
    if (deserializeJson(output, text) || !output.is<JsonObject>() || output.size() != 2 ||
        !output["clueId"].is<int>() || !output["message"].is<const char *>()) return false;
    int selected = output["clueId"].as<int>();
    unsigned choices = ai_clue_count(job);
    if ((choices == 0 && selected != -1) ||
        (choices && (selected < 0 || (unsigned)selected >= choices))) return false;
    const char *message = output["message"];
    if (!*message || strlen(message) >= sizeof(out.message)) return false;
    char remainder[sizeof(out.message)];
    snprintf(remainder, sizeof(remainder), "%s", message);
    if (choices) {
        char clue[80];
        if (!ai_clue(job, (unsigned)selected, clue, sizeof(clue))) return false;
        char *found = strstr(remainder, clue);
        if (!found || strstr(found + strlen(clue), clue)) return false;
        memmove(found, found + strlen(clue), strlen(found + strlen(clue)) + 1);
    }
    bool hasReaction = false;
    unsigned words = 0;
    bool inWord = false;
    for (const unsigned char *p = (const unsigned char *)message; *p; ++p) {
        bool letter = isalpha(*p);
        if (letter && !inWord) ++words;
        inWord = letter;
    }
    if (words > 25) return false;
    unsigned sentences = 0;
    bool content = false;
    for (const char *p = message; *p; ++p) {
        if (isalpha((unsigned char)*p)) content = true;
        if (strchr(".!?", *p) && content) { ++sentences; content = false; }
    }
    if (content) ++sentences;
    if (sentences > 2) return false;
    for (const char *p = remainder; *p; ++p) if (!isspace((unsigned char)*p)) hasReaction = true;
    if ((!choices || hasReaction) && !ai_valid_reaction(remainder)) return false;
    out = {}; out.tag = job.tag;
    snprintf(out.message, sizeof(out.message), "%s", message);
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
