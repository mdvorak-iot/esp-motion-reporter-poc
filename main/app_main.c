#include "app_calibration.h"
#include "app_status.h"
#include <MadgwickAHRS.h>
#include <app_wifi.h>
#include <calibrate.h>
#include <double_reset.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <math.h>
#include <mpu9250.h>
#include <nvs_flash.h>
#include <status_led.h>
#include <wifi_reconnect.h>

static const char TAG[] = "app_main";

#define DEG2RAD(deg) (deg * M_PI / 180.0f)

static esp_websocket_client_handle_t client = NULL;

static void websocket_event_handler(__unused void *handler_args, __unused esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_DATA && data->op_code == 0x1)
    {
        ESP_LOGI(TAG, "received data: %.*s", data->data_len, data->data_ptr);
    }
}

static void do_report()
{
    float temp;
    ESP_ERROR_CHECK(get_temperature_celsius(&temp));

    float heading, pitch, roll;
    MadgwickGetEulerAnglesDegrees(&heading, &pitch, &roll);
    ESP_LOGI(TAG, "heading: %2.3f°, pitch: %2.3f°, roll: %2.3f°, Temp %2.3f°C", heading, pitch, roll, temp);

    if (esp_websocket_client_is_connected(client))
    {
        char json[1024] = {};
        int len = snprintf(json, sizeof(json), "{\"h\":%.3f,\"p\":%.3f,\"r\":%.3f,\"t\":%.3f}", heading, pitch, roll, temp);
        if (len >= 0 && len < sizeof(json))
        {
            esp_websocket_client_send_text(client, json, len, 1000 / portTICK_PERIOD_MS);
        }
        else
        {
            ESP_LOGE(TAG, "json json overflow, needed %d bytes", len);
        }
    }
}

void app_main()
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
        .security = WIFI_PROV_SECURITY_0,
        .service_name = app_info.project_name,
        .pop = NULL,
        .hostname = app_info.project_name,
        .wifi_connect = wifi_reconnect_resume,
    };
    ESP_ERROR_CHECK(app_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(wifi_reconnect_start());

    // Devices
    ESP_ERROR_CHECK(i2c_mpu9250_init(&cal));
    MadgwickAHRSinit(200, 0.8);
    ESP_LOGI(TAG, "MPU initialized");

    // Start
    ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_set_interval(STATUS_LED_DEFAULT, STATUS_LED_CONNECTING_INTERVAL, true));
    ESP_ERROR_CHECK(app_wifi_start(reconfigure));
    ESP_LOGI(TAG, "starting");

    // Wait for WiFi
    wifi_reconnect_wait_for_connection(CONFIG_APP_WIFI_PROV_TIMEOUT_S * 1000 + CONFIG_WIFI_RECONNECT_CONNECT_TIMEOUT_MS);

    ESP_ERROR_CHECK(esp_websocket_client_start(client));
    while (!esp_websocket_client_is_connected(client))
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Report loop
    int64_t last = esp_timer_get_time();
    while (true)
    {
        // Get the Accelerometer, Gyroscope and Magnetometer values.
        vector_t va, vg, vm;
        ESP_ERROR_CHECK(get_accel_gyro_mag(&va, &vg, &vm));

        // Apply the AHRS algorithm
        MadgwickAHRSupdate(DEG2RAD(vg.x), DEG2RAD(vg.y), DEG2RAD(vg.z),
                           va.x, va.y, va.z,
                           vm.x, vm.y, vm.z);

        // Print the data every N ms
        int64_t now = esp_timer_get_time();
        if (now - last >= CONFIG_APP_REPORT_INTERVAL_MS * 1000L)
        {
            do_report();
        }

        vTaskDelay(1);
    }
}
