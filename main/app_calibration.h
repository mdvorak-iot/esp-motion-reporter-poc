#pragma once

#include <mpu9250.h>

static calibration_t cal = {
    .mag_offset = {.x = 0, .y = 0, .z = 0},
    .mag_scale = {.x = 1, .y = 1, .z = 1},
    .gyro_bias_offset = {.x = 0, .y = 0, .z = 0},
    .accel_offset = {.x = 0, .y = 0, .z = 0},
    .accel_scale_lo = {.x = 1, .y = 1, .z = 1},
    .accel_scale_hi = {.x = 1, .y = 1, .z = 1},
};
