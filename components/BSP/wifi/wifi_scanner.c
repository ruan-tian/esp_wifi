/**
 * @file wifi_scanner.c
 * @brief WiFi 扫描与管理模块 (已深度优化版)
 * * 包含优化点：
 * 1. 消除堆内存碎片：采用“一次分配，重复复用”策略管理扫描列表。
 * 2. 避免重复初始化：加入状态标志和 ESP_ERR_INVALID_STATE 兼容处理。
 */

 #include <string.h>
 #include <stdlib.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "esp_wifi.h"
 #include "esp_event.h"
 #include "esp_log.h"
 #include "nvs_flash.h"
 #include "lcd.h"
 #include "wifi_scanner.h"
 
 static const char *TAG = "WIFI_SCAN";
 
 // ===================== 全局变量定义 =====================
 wifi_ap_record_t *g_ap_records = NULL; // WiFi 列表堆内存指针
 uint16_t g_ap_count = 0;               // 实际扫描到的 WiFi 数量
 static bool s_scanner_inited = false;  // 防止重复初始化的安全标志位
 
 #define MAX_SCAN_AP 64                 // 最大扫描与存储数量
 
 /**
  * @brief 按WiFi信号强度(RSSI)降序排序
  */
 static int sort_by_rssi(const void *a, const void *b)
 {
     const wifi_ap_record_t *ap_a = (const wifi_ap_record_t *)a;
     const wifi_ap_record_t *ap_b = (const wifi_ap_record_t *)b;
     return ap_b->rssi - ap_a->rssi;
 }
 
 /**
  * @brief 初始化WiFi扫描模块
  */
 void wifi_scanner_init(void)
 {
     if (s_scanner_inited) return; // 【优化】防重复初始化拦截
 
     nvs_flash_init();
 
     // 【优化】安全初始化系统网络接口与事件循环（兼容已被 connector 初始化的情况）
     esp_err_t ret = esp_netif_init();
     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
         ESP_ERROR_CHECK(ret);
     }
     
     ret = esp_event_loop_create_default();
     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
         ESP_ERROR_CHECK(ret);
     }
 
     esp_netif_create_default_wifi_sta();
 
     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
     esp_wifi_init(&cfg);
     esp_wifi_set_mode(WIFI_MODE_STA);
     esp_wifi_start();
 
     s_scanner_inited = true;
     ESP_LOGI(TAG, "WiFi 扫描模块初始化完成");
 }
 
 /**
  * @brief 扫描周围环境的 WiFi 并更新到全局列表
  */
 void wifi_scan_and_update_list(void)
 {
     // 【优化】内存碎片拯救计划：不再频繁 free 和 malloc
     // 只在第一次运行时分配一块最大容量的内存，后续持续复用
     if (g_ap_records == NULL) {
         g_ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * MAX_SCAN_AP);
         if (g_ap_records == NULL) {
             ESP_LOGE(TAG, "FATAL: 无法为 AP 记录分配堆内存");
             return;
         }
     }
 
     wifi_scan_config_t scan_cfg = {
         .ssid = NULL,
         .bssid = NULL,
         .channel = 0,
         .show_hidden = true
     };
 
     // 启动扫描（阻塞模式）
     esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "WiFi 扫描启动失败: %s", esp_err_to_name(err));
         return;
     }
 
     uint16_t ap_num = 0;
     esp_wifi_scan_get_ap_num(&ap_num);
 
     if (ap_num == 0) {
         g_ap_count = 0;
         return;
     }
 
     // 限制最大读取数量，防止内存越界
     g_ap_count = (ap_num > MAX_SCAN_AP) ? MAX_SCAN_AP : ap_num;
 
     // 直接将新数据覆盖到复用的内存块中
     esp_wifi_scan_get_ap_records(&g_ap_count, g_ap_records);
     
     // 按信号强度排序
     qsort(g_ap_records, g_ap_count, sizeof(wifi_ap_record_t), sort_by_rssi);
 
     ESP_LOGI(TAG, "扫描完成，共找到 %d 个WiFi网络", g_ap_count);
 }
 
 /**
  * @brief 扫描并直接在 LCD 上简单显示 (主要用于调试或备用 UI)
  */
 void wifi_scan_and_display(void)
 {
     wifi_scan_and_update_list();
     ili9341_fill_screen(LCD_COLOR_BLACK);
 
     if (g_ap_count == 0) {
         ili9341_draw_string_8x16(10, 10, "No WiFi Found", LCD_COLOR_RED, LCD_COLOR_BLACK);
         return;
     }
 
     char buf[48];   // 使用 48 字节的宽裕缓冲区，避免 GCC 截断报错
     for (int i = 0; i < g_ap_count && i < 12; i++) {
         snprintf(buf, sizeof(buf), "%d. %.32s", i+1, g_ap_records[i].ssid);
         ili9341_draw_string_8x16(10, 10 + i*16, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
     }
 }