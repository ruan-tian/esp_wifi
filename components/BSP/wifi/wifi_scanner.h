#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_wifi.h"
#include "esp_err.h"

// ===================== 全局变量声明（供menu.c调用） =====================
// 扫描到的WiFi列表
extern wifi_ap_record_t *g_ap_records;
// 扫描到的WiFi数量
extern uint16_t g_ap_count;

/**
 * @brief 初始化WiFi扫描功能（STA模式）
 */
void wifi_scanner_init(void);

/**
 * @brief 扫描WiFi，仅更新全局列表（不显示，给菜单用）
 */
void wifi_scan_and_update_list(void);

/**
 * @brief 扫描并直接在LCD显示
 */
void wifi_scan_and_display(void);

#ifdef __cplusplus
}
#endif

#endif