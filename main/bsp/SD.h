#ifndef __SD_H
#define __SD_H

#include "esp_err.h"
#include "bsp/esp-bsp.h"

/**
 * @brief 启动SD卡
 */
void sd_start(void);

/**
 * @brief 检查SD卡是否已插入
 */
bool sd_is_inserted(void);

#endif