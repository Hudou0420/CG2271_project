// imu.h - MPU-6050 tamper detection (polled at 20 Hz by the tImu task).
#pragma once

#include <stdint.h>

struct ImuReading {
    float accG;     // |acceleration| in g (about 1.0 at rest)
    float devG;     // |accG - resting baseline|
    float gyroDps;  // |angular rate| in deg/s
};

void       imu_init();
bool       imu_ok();                 // sensor found and calibrated
ImuReading imu_latest();
void       imu_set_stream(bool on);  // print every sample (threshold tuning)
