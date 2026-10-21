// vault_state.cpp - mutex-protected shared state.
//
// Written by the link task (STATUS / delivered ARM), read by the IMU task
// (is the vault armed?) and the console.
#include "vault_state.h"

#include <Arduino.h>
#include "config.h"

static SemaphoreHandle_t s_mutex;
static VaultState        s_state = {VP_STATE_IDLE, false, 0, {}};

void state_init()
{
    s_mutex = xSemaphoreCreateMutex();
}

void state_set_mcu(vp_state_t s, bool fromHeartbeat)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.mcuState   = s;
    s_state.stateKnown = true;
    if (fromHeartbeat) {
        s_state.lastStatusMs = millis();
    }
    xSemaphoreGive(s_mutex);
}

void state_set_status(vp_state_t s, const McuSensors &sensors)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.mcuState     = s;
    s_state.stateKnown   = true;
    s_state.lastStatusMs = millis();
    s_state.sensors      = sensors;
    xSemaphoreGive(s_mutex);
}

VaultState state_get()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    VaultState copy = s_state;
    xSemaphoreGive(s_mutex);
    return copy;
}

bool state_is_armed()
{
    VaultState s = state_get();
    return s.stateKnown &&
           (s.mcuState == VP_STATE_LOCKED || s.mcuState == VP_STATE_DIALING);
}

bool state_mcu_online()
{
    VaultState s = state_get();
    return s.lastStatusMs != 0 && millis() - s.lastStatusMs < MCU_OFFLINE_MS;
}

const char *state_name(vp_state_t s)
{
    switch (s) {
    case VP_STATE_IDLE:     return "IDLE";
    case VP_STATE_LOCKED:   return "LOCKED";
    case VP_STATE_DIALING:  return "DIALING";
    case VP_STATE_UNLOCKED: return "UNLOCKED";
    case VP_STATE_ALARM:    return "ALARM";
    default:                return "?";
    }
}
