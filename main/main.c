#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "adapter/lvgl_adapter.h"
#include "bsp/display_init.h"
#include "network/wifi_hosted.h"
#include "network/mqtt.h"
#include "gui_guider.h"
#include "custom.h"
#include "esp_sntp.h"

static const char *TAG = "main";
lv_ui guider_ui;

/* 网络初始化任务（在 LVGL 就绪后异步执行） */
static void network_init_task(void *arg)
{
    ESP_LOGI(TAG, "Network init starting...");

    /* 初始化 Hosted 传输层（不自动连接 WiFi） */
    bool transport_ok = wifi_hosted_init_transport(30000);
    if (!transport_ok) {
        ESP_LOGW(TAG, "Hosted transport init failed, retry in 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        transport_ok = wifi_hosted_init_transport(30000);
    }

    if (!transport_ok) {
        ESP_LOGE(TAG, "Hosted transport unavailable, no WiFi");
        vTaskDelete(NULL);
        return;
    }

    /* 尝试使用保存的凭证自动连接 */
    bool wifi_ok = wifi_hosted_auto_connect();
    if (!wifi_ok) {
        ESP_LOGI(TAG, "No saved WiFi credentials, waiting for user config");
    }

    vTaskDelete(NULL);
}

/* WiFi 连接成功回调：启动 MQTT + SNTP（在 WiFi 事件任务上下文调用） */
static void on_wifi_connected(void)
{
    ESP_LOGI(TAG, "WiFi connected, starting MQTT and SNTP");

    if (!mqtt_is_connected()) {
        mqtt_start();
    }

    static bool sntp_inited = false;
    if (!sntp_inited) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_init();
        custom_notify_sntp_inited();
        sntp_inited = true;
        ESP_LOGI(TAG, "SNTP started");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==== ESP32-P4 Clothing Store ====");

    /* ======================== 1. 先初始化 LVGL ======================== */
    lvgl_adapter_config_t cfg = {
        .hw_cfg = {
            .hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            },
        },
        .buffer_height = 10,
        .rotation      = 0,
    };

    lv_display_t *disp = lvgl_adapter_setup(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL init failed, restart in 3s...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    /* 开启背光 */
    display_backlight_on();

    /* 创建 GUI Guider UI */
    lvgl_lock();
    setup_ui(&guider_ui);
    custom_init(&guider_ui);
    ESP_LOGI(TAG, "UI OK");
    lvgl_unlock();

    /* ======================== 2. 再初始化网络（异步） ======================== */
    wifi_hosted_set_connected_cb(on_wifi_connected);
    xTaskCreate(network_init_task, "net_init", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "==== System Ready ====");
}
