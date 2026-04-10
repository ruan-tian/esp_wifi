#include "lv_port_disp.h"
#include "lcd.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_PORT";

// LVGL 刷屏回调函数声明
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

// 定时器回调：为 LVGL 提供时间心跳
static void lv_tick_task(void *arg) {
    lv_tick_inc(2); // 告诉 LVGL 过去了 2 毫秒
}

void lv_port_disp_init(void)
{
    // 1. 初始化 LVGL 核心库
    lv_init();

    // 2. 分配显示缓冲区 (分配 20 行，对于 240x320 屏幕大约占用 9.6KB 内存)
    // 如果你发现大面积刷新时有撕裂感，可以将 20 改成 40
    static lv_disp_draw_buf_t draw_buf_dsc;
    static lv_color_t buf_1[LCD_WIDTH * 20];   
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, NULL, LCD_WIDTH * 20);

    // 3. 注册显示驱动
    static lv_disp_drv_t disp_drv; 
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = LCD_WIDTH;       // LCD_WIDTH (240)
    disp_drv.ver_res = LCD_HEIGHT;      // LCD_HEIGHT (320)
    disp_drv.flush_cb = disp_flush;     // 绑定咱们的底层块写入函数
    disp_drv.draw_buf = &draw_buf_dsc;  // 绑定缓冲区

    lv_disp_drv_register(&disp_drv);

    // 4. 创建一个 2ms 的硬件定时器，给 LVGL 提供时间基准 (Tick)
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2000)); // 2000微秒 = 2毫秒
    
    ESP_LOGI(TAG, "LVGL 显示接口初始化完成!");
}

/* * LVGL 核心刷屏函数：
 * LVGL 会在内存里渲染好一块区域，然后把这块区域的像素数组交给你。
 * 你只需要调用底层的块发送函数，把数据推送到屏幕对应的窗口即可。
 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    // 调用你 lcd.c 里写好的图像发送函数
    ili9341_display_image(area->x1, area->y1, w, h, (const uint8_t *)color_p);

    // 重要：推送完数据后，必须通知 LVGL 这一块已经刷新完毕
    lv_disp_flush_ready(disp_drv);
}

// 5. LVGL 守护任务 (UI 线程)
void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL 守护任务已启动");
    while (1) {
        // 运行 LVGL 的内部任务 (处理动画、点击事件、计算界面刷新)
        lv_timer_handler();
        // 休息 10ms 释放 CPU 给别的任务
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}