#include "wifi_scanner.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_SCAN";

wifi_ap_record_t *g_ap_records = NULL;
uint16_t g_ap_count = 0;
static bool s_scanner_inited = false;
#define MAX_SCAN_AP 64

static int sort_by_rssi(const void *a, const void *b) {
    return ((wifi_ap_record_t *)b)->rssi - ((wifi_ap_record_t *)a)->rssi;
}

void wifi_scanner_init(void) {
    if (s_scanner_inited) return;

    // 基础系统组件初始化
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(ret);
    
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(ret);

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_scanner_inited = true;
    ESP_LOGI(TAG, "WiFi 扫描模块已就绪 (无 UI 依赖)");
}

void wifi_scan_and_update_list(void) {
    if (g_ap_records == NULL) {
        g_ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * MAX_SCAN_AP);
    }

    wifi_scan_config_t scan_cfg = { .show_hidden = true };
    esp_wifi_scan_start(&scan_cfg, true); // 阻塞扫描

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    g_ap_count = (ap_num > MAX_SCAN_AP) ? MAX_SCAN_AP : ap_num;

    esp_wifi_scan_get_ap_records(&g_ap_count, g_ap_records);
    qsort(g_ap_records, g_ap_count, sizeof(wifi_ap_record_t), sort_by_rssi);
    
    ESP_LOGI(TAG, "扫描完成，找到 %d 个网络", g_ap_count);
}