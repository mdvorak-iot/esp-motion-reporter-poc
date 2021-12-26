#pragma once

#include <mpu9250.h>

static calibration_t cal = {
    .mag_offset = {.x = 0, .y = 0, .z = 0},
    .mag_scale = {.x = 0, .y = 0, .z = 0},
    .gyro_bias_offset = {.x = 0, .y = 0, .z = 0},
    .accel_offset = {.x = 0.0, .y = 0.0, .z = 0.0},
    .accel_scale_lo = {.x = 0.0, .y = 0.0, .z = 0},
    .accel_scale_hi = {.x = 0.0, .y = 0.0, .z = 0.0},
};
