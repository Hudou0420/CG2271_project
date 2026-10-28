#pragma once
#include <stddef.h>
#include "ai_policy.h"
// Provider parsing and backoff are host-testable without network access.
bool ai_parse_reply(const char *body, size_t size, const AiJob &job, AiMessage &out);
uint32_t ai_retry_delay(int httpCode, bool valid, uint32_t &quotaBackoff);
