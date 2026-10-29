/*
 * LVGL 适配器模块
 * 负责：LVGL 核心初始化、显示/触摸注册、任务启动
 */
#include "lvgl_adapter.h"
#include "esp_lv_adapter.h"
#include "bsp/display_init.h"
#include "bsp/touch_init.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "lvgl_adapter";

#define DEFAULT_BUFFER_HEIGHT   20
#define DEFAULT_ROTATION        0

lv_display_t *lvgl_adapter_setup(const lvgl_adapter_config_t *config)
{
    /* ---------- 1. 默认配置 ---------- */
    bsp_display_config_t hw_cfg = {
        .hdmi_resolution = BSP_HDMI_RES_NONE,
        .dsi_bus = {
            .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        },
    };
    uint32_t buf_height = DEFAULT_BUFFER_HEIGHT;
    int rotation = DEFAULT_ROTATION;

    if (config) {
        hw_cfg     = config->hw_cfg;
        buf_height = config->buffer_height > 0 ? config->buffer_height : DEFAULT_BUFFER_HEIGHT;
        rotation   = config->rotation;
    }

    /* ---------- 2. 初始化显示硬件 ---------- */
    display_handles_t disp_hw = {0};
    esp_err_t err = display_hw_init(&hw_cfg, &disp_hw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display HW init failed");
        return NULL;
    }

    /* ---------- 3. 初始化 LVGL 核心 ---------- */
    const esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    err = esp_lv_adapter_init(&adapter_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    /* ---------- 4. 注册 MIPI DSI 显示 ---------- */
    uint32_t hres = 0, vres = 0;
    display_get_resolution(&hres, &vres);

    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        disp_hw.panel, disp_hw.io, hres, vres, rotation);

    disp_cfg.profile.buffer_height = buf_height;
    disp_cfg.profile.use_psram     = true;  // 使用 PSRAM 存放 LVGL 缓冲区

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Register display failed");
        return NULL;
    }
    ESP_LOGI(TAG, "Display registered (%lux%lu, buf=%lu lines, rot=%d)",
             (unsigned long)hres, (unsigned long)vres,
             (unsigned long)buf_height, rotation);

    /* ---------- 5. 初始化并注册触摸 ---------- */
    esp_lcd_touch_handle_t touch = NULL;
    err = touch_hw_init(&touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed, continuing without touch");
        /* 触摸失败不阻止启动 */
    } else {
        const esp_lv_adapter_touch_config_t touch_cfg =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
        lv_indev_t *indev = esp_lv_adapter_register_touch(&touch_cfg);
        if (indev == NULL) {
            ESP_LOGW(TAG, "Register touch failed");
        } else {
            ESP_LOGI(TAG, "Touch registered (GT911)");
        }
    }

    /* ---------- 6. 启动 LVGL 任务循环 ---------- */
    err = esp_lv_adapter_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed: %s", esp_err_to_name(err));
        return NULL;
    }
    ESP_LOGI(TAG, "LVGL task started");

    return disp;
}

void lvgl_lock(void)
{
    esp_lv_adapter_lock(-1);
}

void lvgl_unlock(void)
{
    esp_lv_adapter_unlock();
}
