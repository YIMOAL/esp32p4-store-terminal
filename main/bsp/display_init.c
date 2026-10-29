/*
 * BSP 显示初始化模块
 * 负责：MIPI DSI 总线创建、LCD 面板初始化、背光控制
 */
#include "display_init.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "display_init";

esp_err_t display_hw_init(const bsp_display_config_t *cfg, display_handles_t *out_handles)
{
    if (cfg == NULL || out_handles == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    bsp_lcd_handles_t handles = {0};
    esp_err_t err = bsp_display_new_with_handles(cfg, &handles);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP display init failed: %s", esp_err_to_name(err));
        return err;
    }

    out_handles->panel = handles.panel;
    out_handles->io    = handles.io;

    ESP_LOGI(TAG, "Display HW init OK (%lux%lu)",
             (unsigned long)BSP_LCD_H_RES, (unsigned long)BSP_LCD_V_RES);
    return ESP_OK;
}

void display_backlight_on(void)
{
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "Backlight ON");
}

void display_backlight_off(void)
{
    bsp_display_backlight_off();
    ESP_LOGI(TAG, "Backlight OFF");
}

esp_err_t display_brightness_init(void)
{
    esp_err_t err = bsp_display_brightness_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Brightness init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Brightness PWM init OK");
    }
    return err;
}

esp_err_t display_brightness_set(int brightness_percent)
{
    if (brightness_percent < 20) brightness_percent = 20; // 最小亮度限制，避免完全黑屏
    if (brightness_percent > 100) brightness_percent = 100;

    esp_err_t err = bsp_display_brightness_set(brightness_percent);
    if (err != ESP_OK) 
	{
        ESP_LOGE(TAG, "Set brightness %d%% failed: %s", brightness_percent, esp_err_to_name(err));
    } 
	else 
	{
        ESP_LOGI(TAG, "Brightness set to %d%%", brightness_percent);
    }
    return err;
}

void display_get_resolution(uint32_t *hres, uint32_t *vres)
{
    if (hres) *hres = BSP_LCD_H_RES;
    if (vres) *vres = BSP_LCD_V_RES;
}
