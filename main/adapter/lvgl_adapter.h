/*
 * LVGL 适配器模块
 * 负责：LVGL 核心初始化、显示/触摸注册、任务启动
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "bsp/display_init.h"

/**
 * @brief LVGL 适配器初始化配置
 */
typedef struct {
    bsp_display_config_t hw_cfg;    /*!< 显示硬件配置（DSI速率等） */
    uint32_t buffer_height;         /*!< LVGL draw buffer 行数，默认20 */
    int rotation;                   /*!< 旋转角度：0/90/180/270 */
} lvgl_adapter_config_t;

/**
 * @brief 初始化整个 LVGL 系统
 *
 * 流程：显示硬件 → LVGL核心 → 注册显示 → 注册触摸 → 启动任务
 *
 * @param config  适配器配置，NULL 则使用默认值
 * @return lv_display_t*  LVGL 显示对象，失败返回 NULL
 */
lv_display_t *lvgl_adapter_setup(const lvgl_adapter_config_t *config);

/**
 * @brief 获取 LVGL 互斥锁
 */
void lvgl_lock(void);

/**
 * @brief 释放 LVGL 互斥锁
 */
void lvgl_unlock(void);

#ifdef __cplusplus
}
#endif
