// vault_state.h - the ESP32's view of the vault, shared between tasks.
#pragma once

#include <stdint.h>
#include "vault_protocol.h"

// Game progress and sensor snapshot from the MCU's STATUS (VP_ST_* fields).
struct McuSensors {
    uint8_t  digitIndex;   // digits confirmed so far
    uint8_t  currentDigit; // digit on the dial
    uint8_t  knocks;       // knocks counted towards the rhythm
    uint8_t  attempts;
    uint8_t  closeness;    // last attempt, 0..100
    uint16_t light;        // raw ADC 0..4095
    int16_t  encoder;      // raw encoder steps
    uint8_t  events;       // sensor events sent to the game (wraps)
    uint8_t  flags;        // VP_STF_*
};

struct VaultState {
    vp_state_t mcuState;     // last state reported by the MCU
    bool       stateKnown;   // false until the first STATUS / delivered ARM
    uint32_t   lastStatusMs; // millis() of the last STATUS heartbeat
    McuSensors sensors;      // valid once lastStatusMs != 0
};

void       state_init();
void       state_set_mcu(vp_state_t s, bool fromHeartbeat);
void       state_set_status(vp_state_t s, const McuSensors &sensors); // from STATUS
VaultState state_get();
bool       state_is_armed();   // LOCKED or DIALING
bool       state_mcu_online(); // a STATUS arrived within MCU_OFFLINE_MS
const char *state_name(vp_state_t s);
