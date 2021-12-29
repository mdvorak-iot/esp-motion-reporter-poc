#include "app_calibration.h"
#include "app_status.h"
#include "util_append.h"
#include <MadgwickAHRS.h>
#include <app_wifi.h>
#include <calibrate.h>
#include <double_reset.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <math.h>
#include <mpu9250.h>
#include <nvs_flash.h>
#include <status_led.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

#define SAMPLE_INTERVAL_MS (1000 / CONFIG_SAMPLE_RATE_Hz) // Sample interval in milliseconds
#define APP_REPORT_BATCH_SIZE (CONFIG_APP_REPORT_BATCH_SIZE)

static esp_websocket_client_handle_t client = NULL;

struct reading
{
    time_t time;
    vector_t va;
    vector_t vg;
    vector_t vm;
};

// DEBUG
static void websocket_event_handler(__unused void *handler_args, __unused esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_DATA && data->op_code == 0x1)
    {
        ESP_LOGI(TAG, "received data: %.*s", data->data_len, data->data_ptr);
    }
}

static void do_report(struct reading *data, size_t len)
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

_Noreturn void app_main()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // System services
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Check double reset
    // NOTE this should be called as soon as possible, ideally right after nvs init
    bool reconfigure = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(double_reset_start(&reconfigure, DOUBLE_RESET_DEFAULT_TIMEOUT));

    // App info
    esp_app_desc_t app_info = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_get_partition_description(esp_ota_get_running_partition(), &app_info));

    // WebSocket client
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = CONFIG_WEBSOCKET_SERVER_URL;

    client = esp_websocket_client_init(&websocket_cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL));

    // Status
    app_status_init(client);

    // WiFi
    struct app_wifi_config wifi_cfg = {
        .security = WIFI_PROV_SECURITY_1,
        .service_name = NULL,
        .pop = NULL,
        .hostname = NULL,
        .wifi_connect = wifi_reconnect_resume,
    };
    ESP_ERROR_CHECK(app_wifi_print_qr_code_handler_register(NULL));
    ESP_ERROR_CHECK(app_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // NTP
#ifdef LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(1); // accept NTP offers from DHCP server, if any
#endif
#if LWIP_DHCP_GET_NTP_SRV && SNTP_MAX_SERVERS > 1
    sntp_setservername(1, "pool.ntp.org");
#else
    // otherwise, use DNS address from a pool
    sntp_setservername(0, "pool.ntp.org");
#endif

    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();

    // TODO
    //    calibrate_accel();
    //    calibrate_gyro();
    //    calibrate_mag();
    //    vTaskDelay(10000);

    // Devices
    ESP_ERROR_CHECK(i2c_mpu9250_init(&cal));
    ESP_ERROR_CHECK(set_accel_filter(5));
    MadgwickAHRSinit(CONFIG_SAMPLE_RATE_Hz, 0.8f);
    ESP_LOGI(TAG, "mpu initialized");

    // Start
    ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(STATUS_LED_DEFAULT, STATUS_LED_CONNECTING_INTERVAL, true));
    ESP_ERROR_CHECK(app_wifi_start(reconfigure));
    ESP_LOGI(TAG, "starting");

    // Wait for Wi-Fi
    wifi_reconnect_wait_for_connection(CONFIG_APP_WIFI_PROV_TIMEOUT_S * 1000 + CONFIG_WIFI_RECONNECT_CONNECT_TIMEOUT_MS);

    ESP_ERROR_CHECK(esp_websocket_client_start(client));
    // while (!esp_websocket_client_is_connected(client))
    // {
    //     vTaskDelay(100 / portTICK_PERIOD_MS);
    // }

    // Wait for NTP for 10 sec (after boot)
    ESP_LOGI(TAG, "waiting for NTP");
    while (time(NULL) < 10)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // Log start time
    {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char now_str[30] = {};

        strftime(now_str, sizeof(now_str), "%x %X %Z", &tm);
        ESP_LOGI(TAG, "running at %s", now_str);
    }

    // Report loop
    TickType_t last_collect = xTaskGetTickCount();
    size_t readings = 0;
    struct reading *data = (struct reading *)malloc(APP_REPORT_BATCH_SIZE * sizeof(struct reading));

    while (true)
    {
        // Get the Accelerometer, Gyroscope and Magnetometer values.
        err = get_accel_gyro_mag(&data[readings].va, &data[readings].vg, &data[readings].vm);
        if (err != ESP_OK)
        {
            memset(&data[readings], 0, sizeof(struct reading));
            ESP_LOGE(TAG, "get_accel_gyro_mag failed: %d %s", err, esp_err_to_name(err));
        }

        data[readings].time = time(NULL);

        // Apply the AHRS algorithm
        MadgwickAHRSupdate(DEG2RAD(data[readings].vg.x), DEG2RAD(data[readings].vg.y), DEG2RAD(data[readings].vg.z),
                           data[readings].va.x, data[readings].va.y, data[readings].va.z,
                           data[readings].vm.x, data[readings].vm.y, data[readings].vm.z);

        // Print the data every N ms
        if (readings >= APP_REPORT_BATCH_SIZE - 1)
        {
            //  ESP_LOGI(TAG, "accel: [%+6.2f %+6.2f %+6.2f ] (G)\tgyro: [%+7.2f %+7.2f %+7.2f ] (º/s)\tmag: [%7.2f %7.2f %7.2f]", vg.x, vg.y, vg.z, va.x, va.y, va.z, vm.x, vm.y, vm.z);
            do_report(data, readings);
            readings = 0;
        }
        else
        {
            // Advance in history
            readings++;
        }

        // Throttle
        vTaskDelayUntil(&last_collect, SAMPLE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}
