#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include "esp_wifi.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 全局变量：供 LVGL 列表读取扫描结果
extern wifi_ap_record_t *g_ap_records;
extern uint16_t g_ap_count;

/**
 * @brief 初始化 WiFi 扫描硬件
 */
void wifi_scanner_init(void);

/**
 * @brief 执行扫描并更新 g_ap_records 列表
 */
void wifi_scan_and_update_list(void);

#ifdef __cplusplus
}
#endif

#endif