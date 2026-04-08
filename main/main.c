#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd.h"      // LCD驱动初始化
#include "menu.h"     // 菜单系统

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting WiFi Scanner with Menu...");

    // 1. 初始化LCD显示屏（必须最先调用，以便显示启动信息）
    lcd_init();

    // 2. 启动菜单系统（内部会创建菜单任务，并自动初始化WiFi、按键等）
    menu_start();

    // 3. 主循环可以空闲，或执行其他后台任务
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}