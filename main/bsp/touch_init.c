/*
 * BSP 触摸初始化模块
 * 负责：GT911 触摸 IC 初始化
 */
#include "touch_init.h"
#include "bsp/touch.h"
#include "esp_log.h"

static const char *TAG = "touch_init";

esp_err_t touch_hw_init(esp_lcd_touch_handle_t *out_handle)
{
    if (out_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bsp_touch_new(NULL, out_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Touch HW init OK (GT911)");
    return ESP_OK;
}
