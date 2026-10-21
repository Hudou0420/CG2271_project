// cloud.h - link to the web dashboard through Firebase Realtime Database.
//
// One task (tCloud) owns Wi-Fi and HTTPS. It publishes the vault status,
// appends game events and leaderboard entries, and polls for arm/disarm
// commands written by the dashboard. The database layout is in web/README.md.
// The other tasks only call the cloud_on_*() hooks, which never block.
#pragma once

#include <stdint.h>
#include "vault_protocol.h"

struct CloudStats {
    bool     configured;     // secrets.h present
    bool     wifiUp;
    bool     signedIn;       // have a Firebase ID token (or no API key needed)
    uint32_t requestsOk;
    uint32_t requestsFailed;
    uint32_t commandsHandled;
    uint32_t eventsDropped;  // hook queue was full
    int      lastHttpCode;
    char     lastError[64];
};

void       cloud_init();
CloudStats cloud_stats();

// Hooks for the link task (same arguments as its callbacks).
void cloud_on_message(const vp_frame_t &frame);   // ATTEMPT / SOLVED / ALARM
void cloud_on_delivered(uint8_t type, bool ok);   // ARM / DISARM results
