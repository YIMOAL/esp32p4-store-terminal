/*
 * BSP SD卡模块
 * 负责：SD卡挂载、状态检测
 */
#include "SD.h"
#include "esp_log.h"
static const char *TAG = "SD";
static bool s_sd_inserted = false;
void sd_start(void)
{
    ESP_LOGI(TAG, "Mounting SD card...");
    esp_err_t err = bsp_sdcard_mount();
    if (err == ESP_OK) {
        s_sd_inserted = true;
        sdmmc_card_t *card = bsp_sdcard_get_handle();
        if (card) {
            ESP_LOGI(TAG, "SD card mounted: %s, size: %lluMB",
                     card->cid.name, ((uint64_t)card->csd.capacity) * 512 / (1024 * 1024));
        }
    } else {
        s_sd_inserted = false;
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(err));
    }
}
bool sd_is_inserted(void)
{
    return s_sd_inserted;
}