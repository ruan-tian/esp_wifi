#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd.h"      // LCD驱动初始化
#include "menu.h"     // 菜单系统
#include "lvgl.h"
#include "lv_btn.h"
#include "lv_label.h"
#include "lv_obj.h"
#include "lv_port_disp.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting WiFi Scanner with Menu...");

    // 1. 初始化LCD显示屏（必须最先调用，以便显示启动信息）
    lcd_init();

    // 2. 启动菜单系统（内部会创建菜单任务，并自动初始化WiFi、按键等）
    //menu_start();
    // 2. 初始化 LVGL 和显示接口
    lv_port_disp_init();

    // 3. 启动 LVGL UI 守护任务
    xTaskCreate(lvgl_port_task, "lvgl_task", 4096, NULL, 5, NULL);

    // 4. 画一个 LVGL 按钮测试
    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_center(btn);
    lv_obj_set_size(btn, 120, 50);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "Hello LVGL!");
    lv_obj_center(label)
    // 3. 主循环可以空闲，或执行其他后台任务
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}