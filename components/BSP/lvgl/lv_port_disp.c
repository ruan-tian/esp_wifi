#include "lv_port_disp.h"
#include "lcd.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_PORT";

/**
 * @brief LVGL 显示刷新回调函数 (The Flush Callback)
 * 
 * 【核心逻辑】：这是 LVGL 与你的物理屏幕（LCD）沟通的唯一桥梁。
 * 当 LVGL 渲染完一帧画面后，它不会直接操作硬件，而是调用这个函数，把渲染好的像素数据“丢”给你。
 * 
 * 【执行流程】：
 * 1. LVGL 将屏幕划分为若干个区域（Area），通常是一行或多行。
 * 2. LVGL 准备好这些区域的颜色数据，存放在 color_p 指向的缓冲区中。
 * 3. 你在这里调用底层 LCD 驱动（如 spi_write），将数据发送给屏幕。
 * 4. **关键步骤**：数据发送完毕后，必须调用 lv_disp_flush_ready()，告诉 LVGL：“这块区域刷完了，你可以继续处理下一块了”。
 *    如果不调用它，LVGL 会一直卡在这里等待，导致界面死机。
 * 
 * @param disp_drv 显示驱动对象指针，用于后续调用 lv_disp_flush_ready 汇报状态
 * @param area     需要刷新的矩形区域坐标 {x1, y1, x2, y2}
 * @param color_p  指向颜色数据的指针。注意：这里的颜色格式必须与你在 lv_conf.h 中配置的 LV_COLOR_16_SWAP 等设置匹配
 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    // 直接把颜色缓冲区丢给底层驱动
    lcd_draw_color_buf(area->x1, area->y1, area->x2, area->y2, (const uint16_t *)color_p);

    // 告诉 LVGL 刷屏完成
    lv_disp_flush_ready(disp_drv); 
}

/**
 * @brief LVGL 系统心跳定时器回调 (The Heartbeat)
 * 
 * 【核心逻辑】：LVGL 是一个基于时间轴的 UI 库。动画、按钮长按检测、自动隐藏光标等功能都依赖一个精确的“系统时间”。
 * 
 * 【为什么需要它】：
 * LVGL 自身不产生时间，它需要你每隔固定时间（通常是 1ms ~ 5ms）喂给它一个 tick。
 * 如果心跳停止，LVGL 的所有动画和交互都会冻结。
 * 
 * @param arg 定时器回调参数指针（此处未使用）
 */
static void lv_tick_task(void *arg) {
    lv_tick_inc(1); 
}

/**
 * @brief 初始化 LVGL 显示端口 (Port Initialization)
 * 
 * 【新手必读 - 移植四部曲】：
 * 1. lv_init(): 启动 LVGL 引擎，初始化内部链表和内存池。
 * 2. 分配缓冲区 (Draw Buffer): LVGL 需要一个地方暂存渲染好的像素。
 *    - 为什么用 heap_caps_malloc(MALLOC_CAP_DMA)? 
 *      因为 ESP32 的 SPI 控制器通过 DMA 传输数据时，要求源内存必须是连续的且位于内部 RAM (SRAM)。
 *    - 缓冲区大小决定了刷新效率。太小会导致频繁中断 CPU，太大会占用过多 SRAM。
 * 3. 注册驱动 (Register Driver): 告诉 LVGL 你的屏幕分辨率是多少，以及上面写的 disp_flush 函数在哪里。
 * 4. 启动心跳 (Start Tick): 开启一个高精度定时器，确保 LVGL 能感知时间的流逝。
 */
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
    
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, NULL, buffer_pixels);// 分配缓冲区

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

/**
 * @brief LVGL 端口守护任务 (The Handler Task)
 * 
 * 【核心逻辑】：LVGL 不是多线程安全的，它所有的 UI 操作必须在同一个任务（线程）中完成。
 * 这个任务就是 LVGL 的“大脑”，负责调度一切。
 * 
 * 【循环逻辑】：
 * 1. lv_timer_handler(): 处理所有待办的 UI 任务（重绘、动画计算、事件回调）。
 *    它会返回一个建议值，告诉你“再过多少毫秒我又有新任务要处理了”。
 * 2. vTaskDelay(): 根据建议值让出 CPU。
 *    - 为什么要动态休眠？如果一直不休眠，CPU 占用率会是 100%；如果休眠太久，界面会卡顿。
 *    - 限制在 5ms~50ms 之间是为了兼顾流畅度和功耗。
 * 
 * @param arg 任务创建时传入的参数指针（此处未使用）
 */
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