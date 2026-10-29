/*
 * BSP 触摸初始化模块
 * 负责：GT911 触摸 IC 初始化
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_lcd_touch.h"

/**
 * @brief 初始化触摸硬件
 *
 * @param out_handle  输出：触摸句柄
 * @return esp_err_t
 */
esp_err_t touch_hw_init(esp_lcd_touch_handle_t *out_handle);

#ifdef __cplusplus
}
#endif
