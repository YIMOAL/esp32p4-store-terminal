/*
 * UI 管理模块
 * 负责：UI 页面管理、页面切换、UI 资源管理
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief 初始化 UI 管理器
 * @return lv_obj_t*  主屏幕对象
 */
lv_obj_t *ui_manager_init(void);

/**
 * @brief UI主入口
 */
void ui_init(void);

#ifdef __cplusplus
}
#endif
