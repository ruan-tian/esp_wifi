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
wifi_ap_record_t *g_ap_records = NULL;
uint16_t g_ap_count = 0;

#define MAX_SCAN_AP 64

/**
 * @brief 按WiFi信号强度排序
 */
static int sort_by_rssi(const void *a, const void *b)
{
    const wifi_ap_record_t *ap_a = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *ap_b = (const wifi_ap_record_t *)b;
    return ap_b->rssi - ap_a->rssi;
}

/**
 * @brief 初始化WiFi扫描
 */
void wifi_scanner_init(void)
{
    // 注意：如果已由其他模块（如wifi_connector）初始化过，需避免重复初始化
    // 简单起见，这里依然执行初始化，ESP-IDF会处理重复调用（返回错误但不会崩溃）
    // 更健壮的做法是检查标志位，但为保持简洁，此处保持原样。
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi扫描初始化完成");
}

/**
 * @brief 扫描并更新全局列表
 */
void wifi_scan_and_update_list(void)
{
    if (g_ap_records) {
        free(g_ap_records);
        g_ap_records = NULL;
        g_ap_count = 0;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    esp_wifi_scan_start(&scan_cfg, true);
    esp_wifi_scan_get_ap_num(&g_ap_count);

    if (g_ap_count == 0) return;

    if (g_ap_count > MAX_SCAN_AP) g_ap_count = MAX_SCAN_AP;

    g_ap_records = malloc(sizeof(wifi_ap_record_t) * g_ap_count);
    esp_wifi_scan_get_ap_records(&g_ap_count, g_ap_records);
    qsort(g_ap_records, g_ap_count, sizeof(wifi_ap_record_t), sort_by_rssi);

    ESP_LOGI(TAG, "扫描完成，共找到 %d 个WiFi", g_ap_count);
}

/**
 * @brief 扫描并直接在LCD显示（修复格式截断问题）
 */
void wifi_scan_and_display(void)
{
    wifi_scan_and_update_list();
    ili9341_fill_screen(LCD_COLOR_BLACK);

    if (g_ap_count == 0) {
        ili9341_draw_string_8x16(10, 10, "No WiFi", LCD_COLOR_RED, LCD_COLOR_BLACK);
        return;
    }

    char buf[48];   // 增大缓冲区，避免截断
    for (int i = 0; i < g_ap_count && i < 12; i++) {
        // 限制SSID最多显示32个字符，防止溢出
        snprintf(buf, sizeof(buf), "%d. %.32s", i+1, g_ap_records[i].ssid);
        ili9341_draw_string_8x16(10, 10 + i*16, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    }
}