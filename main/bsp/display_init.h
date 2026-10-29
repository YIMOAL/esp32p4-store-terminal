/*
 * BSP 显示初始化模块
 * 负责：MIPI DSI 总线创建、LCD 面板初始化、背光控制
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "bsp/display.h"

/**
 * @brief 显示硬件句柄结构体
 */
typedef struct {
    esp_lcd_panel_handle_t  panel;  /*!< LCD 面板句柄 */
    esp_lcd_panel_io_handle_t io;   /*!< LCD 面板 IO 句柄 */
} display_handles_t;

/**
 * @brief 初始化显示硬件（MIPI DSI + LCD 面板）
 *
 * @param cfg   BSP 显示配置（DSI 速率等）
 * @param out_handles  输出：面板和IO句柄
 * @return esp_err_t
 */
esp_err_t display_hw_init(const bsp_display_config_t *cfg, display_handles_t *out_handles);

/**
 * @brief 开启显示背光
 */
void display_backlight_on(void);

/**
 * @brief 关闭显示背光
 */
void display_backlight_off(void);

/**
 * @brief 设置屏幕亮度
 *
 * @param brightness_percent 亮度百分比 (0~100)
 * @return esp_err_t
 */
esp_err_t display_brightness_set(int brightness_percent);

/**
 * @brief 初始化屏幕亮度 PWM
 *
 * @return esp_err_t
 */
esp_err_t display_brightness_init(void);

/**
 * @brief 获取屏幕分辨率
 */
void display_get_resolution(uint32_t *hres, uint32_t *vres);

#ifdef __cplusplus
}
#endif
