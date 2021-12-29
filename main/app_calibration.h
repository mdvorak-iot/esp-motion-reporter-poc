#pragma once

#include <mpu9250.h>

static calibration_t cal = {
    .mag_offset = {.x = -11.244141f, .y = 111.761719f, .z = -50.931641f},
    .mag_scale = {.x = 1.090747f, .y = 0.977191f, .z = 0.943525f},
    .gyro_bias_offset = {.x = -1.038590f, .y = -1.187172f, .z = -1.223160f},
    .accel_offset = {.x = 0.014761f, .y = 0.029747f, .z = -0.091939f},
    .accel_scale_lo = {.x = 0.996106f, .y = 1.009322f, .z = 0.941389f},
    .accel_scale_hi = {.x = -0.987365f, .y = -0.979035f, .z = -1.048373f},
};
