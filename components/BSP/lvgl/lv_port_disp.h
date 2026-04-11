#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LVGL 显示层
 */
void lv_port_disp_init(void);

/**
 * @brief LVGL 守护任务 (需在 FreeRTOS 中创建)
 */
void lvgl_port_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif