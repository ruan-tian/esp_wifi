#include "lv_port_disp.h"
#include "lcd.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_PORT";

// 刷新回调函数：将 LVGL 缓冲区内容推送到屏幕
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    // 直接把颜色缓冲区丢给底层驱动
    lcd_draw_color_buf(area->x1, area->y1, area->x2, area->y2, (const uint16_t *)color_p);

    // 告诉 LVGL 刷屏完成
    lv_disp_flush_ready(disp_drv);
}

// 定时器回调：为 LVGL 提供 1 毫秒精度的系统心跳
static void lv_tick_task(void *arg) {
    lv_tick_inc(1); 
}

void lv_port_disp_init(void)
{
    // 1. 初始化 LVGL 核心
    lv_init();

    // 2. 分配显示缓冲区
    // 建议分配 20~40 行的缓冲区，平衡内存消耗和刷新速度
    static lv_disp_draw_buf_t draw_buf_dsc;
    uint32_t buffer_pixels = LCD_WIDTH * 30; 
    
    // 使用 MALLOC_CAP_DMA 确保内存可以被 SPI DMA 直接读取
    lv_color_t *buf_1 = (lv_color_t *)heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    if (buf_1 == NULL) {
        ESP_LOGE(TAG, "无法分配 DMA 显存!");
        return;
    }
    
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, NULL, buffer_pixels);

    // 3. 配置显示驱动
    static lv_disp_drv_t disp_drv; 
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = LCD_WIDTH;       
    disp_drv.ver_res = LCD_HEIGHT;      
    disp_drv.flush_cb = disp_flush;     // 绑定刚才重构的刷新回调
    disp_drv.draw_buf = &draw_buf_dsc;  

    lv_disp_drv_register(&disp_drv);

    // 4. 配置心跳定时器 (1ms 周期)
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    // 每 1000 微秒 (1ms) 触发一次回调
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000)); 
    
    ESP_LOGI(TAG, "LVGL 显示桥接完成，心跳已挂载");
}

// 守护任务：负责处理 UI 的动画、事件和重绘
void lvgl_port_task(void *arg)
{
    while (1) {
        // 让 LVGL 处理内部逻辑，返回值为下次调用的等待毫秒数
        uint32_t time_till_next = lv_timer_handler();
        
        // 动态调整休眠时间，节省功耗，最小休眠 5ms
        if (time_till_next < 5) time_till_next = 5;
        if (time_till_next > 50) time_till_next = 50;
        
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}