#include "app_motion.h"
#include "MadgwickAHRS.h"
#include "util_append.h"

#include <calibrate.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <mpu9250.h>

static const char TAG[] = "app_motion";

#define APP_REPORT_BATCH_SIZE (CONFIG_APP_REPORT_BATCH_SIZE)

static calibration_t cal = {
    .mag_offset = {.x = -11.244141f, .y = 111.761719f, .z = -50.931641f},
    .mag_scale = {.x = 1.090747f, .y = 0.977191f, .z = 0.943525f},
    .gyro_bias_offset = {.x = -1.038590f, .y = -1.187172f, .z = -1.223160f},
    .accel_offset = {.x = 0.014761f, .y = 0.029747f, .z = -0.091939f},
    .accel_scale_lo = {.x = 0.996106f, .y = 1.009322f, .z = 0.941389f},
    .accel_scale_hi = {.x = -0.987365f, .y = -0.979035f, .z = -1.048373f},
};

struct reading
{
    time_t time;
    vector_t va;
    vector_t vg;
    vector_t vm;
};

esp_err_t motion_sensors_init()
{
    // TODO
    //    calibrate_accel();
    //    calibrate_gyro();
    //    calibrate_mag();
    //    vTaskDelay(10000);

    // Devices
    esp_err_t err = i2c_mpu9250_init(&cal);
    if (err != ESP_OK)
    {
        return err;
    }

    //    MadgwickAHRSinit(CONFIG_SAMPLE_RATE_Hz, 0.8f);
    ESP_LOGI(TAG, "mpu initialized");
    return ESP_OK;
}

static void do_report(esp_websocket_client_handle_t client, struct reading *data, size_t len)
{
    //    float heading, pitch, roll;
    //    MadgwickGetEulerAnglesDegrees(&heading, &pitch, &roll);
    //    ESP_LOGI(TAG, "heading: %2.3f°, pitch: %2.3f°, roll: %2.3f°", heading, pitch, roll);

    {
        vector_t va;
        vector_t vg;
        vector_t vm;
        for (size_t i = 0; i < len; i++)
        {
            va.x += data[i].va.x;
            va.y += data[i].va.y;
            va.z += data[i].va.z;
            vg.x += data[i].vg.x;
            vg.y += data[i].vg.y;
            vg.z += data[i].vg.z;
            vm.x += data[i].vm.x;
            vm.y += data[i].vm.y;
            vm.z += data[i].vm.z;
        }

        vm.x = vm.x / (float)len;
        vm.y = vm.y / (float)len;
        vm.z = vm.z / (float)len;

        ESP_LOGI(TAG, "{\"t\":%ld,\"a\":[%.3g,%.3g,%.3g],\"g\":[%.3g,%.3g,%.3g],\"m\":[%.3g,%.3g,%.3g]}",
                 data[0].time,
                 va.x, va.y, va.z,
                 vg.x, vg.y, vg.z,
                 vm.x, vm.y, vm.z);
    }

    if (esp_websocket_client_is_connected(client))
    {
        // NOTE make it static to just reuse the buffer every time, no race-condition here, since it is running in single loop
        // Also, making it static allocates it on heap instead of stack
        static char json[APP_REPORT_BATCH_SIZE * 100] = {};

        char *ptr = json;
        const char *end = json + sizeof(json);

        *ptr = '\0';
        char sep = '[';

        for (size_t i = 0; i < len; i++)
        {
            ptr = util_append(ptr, end, "%c{\"t\":%ld,\"a\":[%.3g,%.3g,%.3g],\"g\":[%.3g,%.3g,%.3g],\"m\":[%.3g,%.3g,%.3g]}",
                              sep, data[i].time,
                              data[i].va.x, data[i].va.y, data[i].va.z,
                              data[i].vg.x, data[i].vg.y, data[i].vg.z,
                              data[i].vm.x, data[i].vm.y, data[i].vm.z);
            sep = ',';
        }
        ptr = util_append(ptr, end, "]");

        if (ptr != NULL)
        {
            int json_len = (int)(ptr - json);
            ESP_LOGD(TAG, "%.*s", json_len, json);

            esp_websocket_client_send_text(client, json, json_len, 1000 / portTICK_PERIOD_MS);
        }
        else
        {
            ESP_LOGE(TAG, "json buffer overflow");
        }
    }
}

void motion_sensors_loop(esp_websocket_client_handle_t client)
{
    static size_t readings = 0;
    static struct reading data[APP_REPORT_BATCH_SIZE] = {};

    // Get the Accelerometer, Gyroscope and Magnetometer values.
    esp_err_t err = get_accel_gyro_mag(&data[readings].va, &data[readings].vg, &data[readings].vm);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "get_accel_gyro_mag failed: %d %s", err, esp_err_to_name(err));
        return;
    }

    data[readings].time = time(NULL);

    // Apply the AHRS algorithm
    //    MadgwickAHRSupdate(DEG2RAD(data[readings].vg.x), DEG2RAD(data[readings].vg.y), DEG2RAD(data[readings].vg.z),
    //                       data[readings].va.x, data[readings].va.y, data[readings].va.z,
    //                       data[readings].vm.x, data[readings].vm.y, data[readings].vm.z);

    // Print the data every N ms
    if (readings >= APP_REPORT_BATCH_SIZE - 1)
    {
        //  ESP_LOGI(TAG, "accel: [%+6.2f %+6.2f %+6.2f ] (G)\tgyro: [%+7.2f %+7.2f %+7.2f ] (º/s)\tmag: [%7.2f %7.2f %7.2f]", vg.x, vg.y, vg.z, va.x, va.y, va.z, vm.x, vm.y, vm.z);
        do_report(client, data, readings);
        readings = 0;
    }
    else
    {
        // Advance in history
        readings++;
    }
}