#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// 初始化 LVGL 显示接口和心跳定时器
void lv_port_disp_init(void);

// LVGL 的守护任务，需要交给 FreeRTOS 运行
void lvgl_port_task(void *arg);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_DISP_H*/