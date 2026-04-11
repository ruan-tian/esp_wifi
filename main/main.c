#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// BSP 与硬件驱动
#include "lcd.h"
#include "wifi_scanner.h"

// LVGL 桥接
#include "lv_port_disp.h"
#include "lv_port_indev.h" 

// 引入我们刚刚抽离的 UI 模块
#include "ui.h"

static const char *TAG = "MAIN";

void app_main(void) {
    // 1. 基础系统与 NVS 初始化
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    // 2. 底层驱动和 LVGL 桥接初始化
    lcd_init();
    lv_port_disp_init();
    lv_port_indev_init(); 

    // 3. 执行 WiFi 扫描获取底层数据
    ESP_LOGI(TAG, "正在扫描周围的 WiFi 网络...");
    wifi_scanner_init(); 
    wifi_scan_and_update_list(); 

    // 4. 【核心分离】调用独立 UI 模块构建界面
    ui_init();

    // 5. 挂载 LVGL 渲染心跳任务
    xTaskCreate(lvgl_port_task, "lvgl_task", 10240, NULL, 5, NULL);
    ESP_LOGI(TAG, "系统初始化完成，UI 任务就绪！");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}