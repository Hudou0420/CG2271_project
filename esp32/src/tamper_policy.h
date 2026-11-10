// Pure detection policy, shared by the IMU task and host regression tests.
#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"
class TamperPolicy {
public:
    static bool over(float deviation, float rotation) {
        return deviation > TAMPER_ACC_DEV_G || rotation > TAMPER_GYRO_DPS;
    }
    void interrupted() { count_ = 0; peak_ = 0; }
    bool sample(float acceleration, float deviation, float rotation,
                bool armed, uint32_t now, float &peak) {
        if (!armed || !over(deviation, rotation)) { interrupted(); return false; }
        peak_ = fmaxf(peak_, acceleration);
        if (++count_ < TAMPER_SAMPLES) return false;
        peak = peak_;
        interrupted();
        if (sent_ && now - last_ < TAMPER_COOLDOWN_MS) return false;
        sent_ = true; last_ = now;
        return true;
    }
private:
    unsigned count_ = 0;
    float peak_ = 0;
    uint32_t last_ = 0;
    bool sent_ = false;
};
