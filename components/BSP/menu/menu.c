#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "lcd.h"
#include "wifi_scanner.h"
#include "wifi_connector.h"

// 日志标签，用于标识此模块的日志输出
static const char *TAG = "MENU";

// ===================== 按键定义 =====================
#define KEY_UP_GPIO      GPIO_NUM_5    // 上键GPIO引脚号
#define KEY_DOWN_GPIO    GPIO_NUM_6    // 下键GPIO引脚号
#define KEY_ENTER_GPIO   GPIO_NUM_7    // 确认键GPIO引脚号

// 按钮事件枚举，定义三种按钮操作类型
typedef enum {
    BUTTON_UP,        // 上键事件
    BUTTON_DOWN,      // 下键事件
    BUTTON_ENTER      // 确认键事件
} button_event_t;

// ===================== 状态定义 =====================
// 菜单状态枚举，定义菜单的三种工作状态
typedef enum {
    STATE_LIST,         // 列表浏览状态：显示可连接的WiFi网络列表
    STATE_CONNECTING,   // 连接中状态：正在尝试连接选定的WiFi网络
    STATE_SMARTCONFIG,  // 智能配网状态：使用SmartConfig方式进行WiFi配置
} menu_state_t;

// 状态名称数组，用于日志输出状态转换信息
static const char *state_names[] = {
    [STATE_LIST]        = "LIST",        // 列表状态名称
    [STATE_CONNECTING]  = "CONNECTING",  // 连接中状态名称
    [STATE_SMARTCONFIG] = "SMARTCONFIG", // 智能配网状态名称
};

// ===================== 状态机上下文 =====================
// 菜单状态机上下文结构体，保存当前状态和选中项索引
typedef struct {
    menu_state_t state;          // 当前菜单状态
    uint16_t     selected_index; // 当前选中的WiFi列表项索引
} menu_context_t;

// 菜单状态机全局变量，初始化为列表状态，选中第一项
static menu_context_t sm = {
    .state = STATE_LIST,
    .selected_index = 0,
};

// ===================== 同步机制 =====================
static QueueHandle_t s_button_queue = NULL;      // 按钮事件队列，用于接收GPIO中断产生的按钮事件
static SemaphoreHandle_t s_connect_done_sem = NULL; // 连接完成信号量，用于在连接任务结束后返回主界面

// ===================== 函数原型 =====================
// 声明将在后面实现的函数原型
static void display_wifi_list(void);              // 显示WiFi列表界面
static void draw_wifi_line(int index, bool highlight); // 绘制WiFi列表中的单行
static void enter_state(menu_state_t new_state);  // 进入新状态的处理函数
static void connect_task(void *arg);              // WiFi连接任务
static void smartconfig_task(void *arg);          // SmartConfig配网任务

// ===================== 单行绘制 =====================
/**
 * @brief 绘制WiFi列表中的单行信息
 * 
 * @param index 列表项的索引
 * @param highlight 是否高亮显示（表示当前选中项）
 */
static void draw_wifi_line(int index, bool highlight)
{
    // 检查索引是否超出范围
    if (index >= g_ap_count) return;

    char buf[48];                           // 用于格式化输出的缓冲区
    int y = 30 + index * 16;                // 计算绘制的Y坐标位置
    uint16_t color = highlight ? LCD_COLOR_YELLOW : LCD_COLOR_WHITE; // 高亮时使用黄色，否则白色

    // 格式化输出WiFi信息：序号. SSID名称
    snprintf(buf, sizeof(buf), "%2d. %-16.16s",
             index + 1, (char *)g_ap_records[index].ssid);

    // 清空该行背景为黑色
    ili9341_draw_rectangle(0, y, LCD_WIDTH, 16, LCD_COLOR_BLACK);

    // 如果是高亮行，在前面绘制箭头指示符
    if (highlight) {
        ili9341_draw_string_8x16(0, y, ">", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    }
    // 绘制WiFi名称
    ili9341_draw_string_8x16(12, y, buf, color, LCD_COLOR_BLACK);

    // 绘制RSSI信号强度
    char rssi_buf[8];
    snprintf(rssi_buf, sizeof(rssi_buf), "%3d", g_ap_records[index].rssi);
    ili9341_draw_string_8x16(LCD_WIDTH - 40, y, rssi_buf, color, LCD_COLOR_BLACK);
}

// ===================== 全量绘制列表 =====================
/**
 * @brief 完整绘制WiFi列表界面
 * 
 * 包括标题、分隔线、WiFi列表项和底部提示信息
 */
static void display_wifi_list(void)
{
    // 清空屏幕为黑色背景
    ili9341_fill_screen(LCD_COLOR_BLACK);

    // 绘制标题和RSSI列标题
    ili9341_draw_string_8x16(10, 5, "WiFi List", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    ili9341_draw_string_8x16(LCD_WIDTH - 50, 5, "RSSI", LCD_COLOR_CYAN, LCD_COLOR_BLACK);

    // 绘制分隔线
    for (int x = 0; x < LCD_WIDTH; x += 2) {
        ili9341_draw_pixel(x, 22, LCD_COLOR_GRAY);
    }

    // 检查是否有发现WiFi网络
    if (g_ap_count == 0) {
        // 没有找到WiFi时显示提示信息
        ili9341_draw_string_8x16(10, 40, "No WiFi Found!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    } else {
        // 限制最多显示12个WiFi网络
        for (int i = 0; i < g_ap_count && i < 12; i++) {
            // 绘制每一行WiFi信息，当前选中项高亮显示
            draw_wifi_line(i, (i == sm.selected_index));
        }
    }

    // 底部提示：长按ENTER进入配网模式
    ili9341_draw_string_8x16(10, LCD_HEIGHT - 16,
        "Long ENTER: SmartConfig", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

// ===================== 状态转换 =====================
/**
 * @brief 状态转换函数，处理从一个状态切换到另一个状态的逻辑
 * 
 * @param new_state 要切换到的新状态
 */
static void enter_state(menu_state_t new_state)
{
    menu_state_t old_state = sm.state;
    // 记录状态转换日志
    ESP_LOGI(TAG, "State: %s -> %s", state_names[old_state], state_names[new_state]);

    sm.state = new_state;

    // 根据新状态执行相应操作
    switch (new_state) {
        case STATE_LIST:
            // 进入列表状态时重新绘制WiFi列表
            display_wifi_list();
            break;

        case STATE_CONNECTING:
            {
                // 获取选中的WiFi的SSID
                const char *ssid = (const char *)g_ap_records[sm.selected_index].ssid;
                
                // 清空屏幕并显示连接进度信息
                ili9341_fill_screen(LCD_COLOR_BLACK);
                char msg[64];
                snprintf(msg, sizeof(msg), "Connecting to %s...", ssid);
                ili9341_draw_string_8x16(10, 10, msg, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 30, "Please wait...", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

                // 创建WiFi连接任务
                xTaskCreate(connect_task, "connect_task", 4096, NULL, 5, NULL);
            }
            break;

        case STATE_SMARTCONFIG:
            {
                // 进入SmartConfig状态时显示配网提示信息
                ili9341_fill_screen(LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 10, "SmartConfig Mode", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 30, "Use ESP Touch App", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 50, "to send WiFi info...", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 80, "Waiting...", LCD_COLOR_CYAN, LCD_COLOR_BLACK);

                // 创建SmartConfig配网任务
                xTaskCreate(smartconfig_task, "sc_task", 4096, NULL, 5, NULL);
            }
            break;
    }
}

// ===================== 直连任务 =====================
/**
 * @brief WiFi连接任务，负责执行实际的WiFi连接操作
 * 
 * @param arg 任务参数（未使用）
 */
static void connect_task(void *arg)
{
    // 获取选中的WiFi的SSID
    const char *ssid = (const char *)g_ap_records[sm.selected_index].ssid;
    
    // 尝试连接WiFi，使用固定密码"725666666"，超时时间为30秒
    esp_err_t ret = wifi_connect_sta(ssid, "725666666", 30000);

    // 显示连接结果
    ili9341_fill_screen(LCD_COLOR_BLACK);
    if (ret == ESP_OK) {
        // 连接成功提示
        ili9341_draw_string_8x16(10, 10, "Connect Success!", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    } else {
        // 连接失败提示
        ili9341_draw_string_8x16(10, 10, "Connect Fail!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    }
    // 提示用户按ENTER返回
    ili9341_draw_string_8x16(10, 30, "Press ENTER to return", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    // 等待用户按ENTER键确认，此时会释放信号量
    xSemaphoreTake(s_connect_done_sem, portMAX_DELAY);

    // 返回列表状态
    enter_state(STATE_LIST);
    vTaskDelete(NULL);
}

// ===================== SmartConfig 配网任务 =====================
/**
 * @brief SmartConfig配网任务，负责执行智能配网操作
 * 
 * @param arg 任务参数（未使用）
 */
static void smartconfig_task(void *arg)
{
    // 启动SmartConfig配网，超时时间为60秒
    esp_err_t ret = wifi_smartconfig_start(60000);

    // 显示配网结果
    ili9341_fill_screen(LCD_COLOR_BLACK);

    if (ret == ESP_OK) {
        // 配网成功，获取并显示连接的WiFi名称
        const char *ssid = wifi_get_connected_ssid();
        char msg[64];
        snprintf(msg, sizeof(msg), "Connected to: %s", ssid);
        ili9341_draw_string_8x16(10, 10, "SmartConfig Success!", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
        ili9341_draw_string_8x16(10, 30, msg, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    } else {
        // 配网失败提示
        ili9341_draw_string_8x16(10, 10, "SmartConfig Failed!", LCD_COLOR_RED, LCD_COLOR_BLACK);
        ili9341_draw_string_8x16(10, 30, "Timeout or error", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    }
    // 提示用户按ENTER返回
    ili9341_draw_string_8x16(10, 60, "Press ENTER to return", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    // 等待用户按ENTER键确认
    xSemaphoreTake(s_connect_done_sem, portMAX_DELAY);

    // 返回列表状态
    enter_state(STATE_LIST);
    vTaskDelete(NULL);
}

// ===================== 按键处理（各状态）=====================

/**
 * @brief 处理列表状态下的按钮事件
 * 
 * @param event 按钮事件类型
 */
static void handle_list_event(button_event_t event)
{
    switch (event) {
        case BUTTON_UP:
            // 上键：移动选择光标向上
            if (g_ap_count == 0) return;  // 如果没有WiFi则不处理
            {
                uint16_t old = sm.selected_index;  // 保存之前的选择项
                // 循环选择，到达顶部时跳转到底部
                sm.selected_index = (sm.selected_index - 1 + g_ap_count) % g_ap_count;
                // 重新绘制旧选项（取消高亮）和新选项（添加高亮）
                draw_wifi_line(old, false);
                draw_wifi_line(sm.selected_index, true);
            }
            break;

        case BUTTON_DOWN:
            // 下键：移动选择光标向下
            if (g_ap_count == 0) return;  // 如果没有WiFi则不处理
            {
                uint16_t old = sm.selected_index;  // 保存之前的选择项
                // 循环选择，到达底部时跳转到顶部
                sm.selected_index = (sm.selected_index + 1) % g_ap_count;
                // 重新绘制旧选项（取消高亮）和新选项（添加高亮）
                draw_wifi_line(old, false);
                draw_wifi_line(sm.selected_index, true);
            }
            break;

        case BUTTON_ENTER:
            // 确认键：根据是否有WiFi可选决定行为
            if (g_ap_count > 0) {
                enter_state(STATE_CONNECTING);  // 有WiFi时：直接连接
            } else {
                enter_state(STATE_SMARTCONFIG); // 没WiFi时：进入配网模式
            }
            break;
    }
}

/**
 * @brief 处理连接和配网状态下的按钮事件
 * 
 * 在连接和配网状态下，只有ENTER键有效，用于返回主列表
 * 
 * @param event 按钮事件类型
 */
static void handle_connected_event(button_event_t event)
{
    if (event == BUTTON_ENTER) {
        // 释放信号量，使连接或配网任务能够结束并返回列表状态
        xSemaphoreGive(s_connect_done_sem);
    }
}

// ===================== 统一分发 =====================
/**
 * @brief 按钮事件统一分发函数
 * 
 * 根据当前状态调用相应的事件处理函数
 * 
 * @param event 按钮事件类型
 */
static void dispatch_button_event(button_event_t event)
{
    switch (sm.state) {
        case STATE_LIST:        handle_list_event(event);       break;  // 列表状态：处理列表事件
        case STATE_CONNECTING:  handle_connected_event(event);  break;  // 连接状态：处理连接事件
        case STATE_SMARTCONFIG: handle_connected_event(event);  break;  // 配网状态：处理配网事件
    }
}

// ===================== GPIO 中断 =====================
/**
 * @brief GPIO中断服务例程
 * 
 * 当按钮被按下时触发，将按钮事件发送到队列中
 * 
 * @param arg 中断关联的GPIO编号
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;  // 获取触发中断的GPIO编号
    button_event_t event;               // 定义按钮事件变量

    // 根据GPIO编号确定按钮类型
    if (gpio_num == KEY_UP_GPIO)         event = BUTTON_UP;    // 上键
    else if (gpio_num == KEY_DOWN_GPIO)  event = BUTTON_DOWN;  // 下键
    else if (gpio_num == KEY_ENTER_GPIO) event = BUTTON_ENTER; // 确认键
    else return;  // 无效GPIO编号则返回

    // 发送事件到队列，需要考虑中断上下文的特殊处理
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_button_queue, &event, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// ===================== 按键任务 =====================
/**
 * @brief 按键处理任务
 * 
 * 从队列中接收按钮事件，进行去抖处理和长按检测
 * 
 * @param arg 任务参数（未使用）
 */
static void button_task(void *arg)
{
    button_event_t event;           // 按钮事件变量
    TickType_t press_time = 0;      // 记录按键按下时间，用于长按检测

    while (1) {
        // 从队列中接收按钮事件
        if (xQueueReceive(s_button_queue, &event, portMAX_DELAY) == pdTRUE) {
            // 延迟50ms进行防抖处理
            vTaskDelay(pdMS_TO_TICKS(50));

            bool pressed = false;  // 按键状态变量
            // 检查对应GPIO的实际电平状态，确认按键确实被按下
            switch (event) {
                case BUTTON_UP:    pressed = (gpio_get_level(KEY_UP_GPIO) == 0);    break;
                case BUTTON_DOWN:  pressed = (gpio_get_level(KEY_DOWN_GPIO) == 0);  break;
                case BUTTON_ENTER: pressed = (gpio_get_level(KEY_ENTER_GPIO) == 0); break;
            }
            if (!pressed) continue;  // 如果按键实际上没按下，则跳过本次处理

            // ENTER长按检测（用于进入SmartConfig模式）
            if (event == BUTTON_ENTER && sm.state == STATE_LIST) {
                press_time = xTaskGetTickCount();  // 记录按键按下时刻

                // 等待按键释放，同时进行长按计时
                while (gpio_get_level(KEY_ENTER_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms检测一次
                    // 如果按下时间超过1秒，判定为长按
                    if ((xTaskGetTickCount() - press_time) > pdMS_TO_TICKS(1000)) {
                        ESP_LOGI(TAG, "Long press detected, entering SmartConfig");
                        enter_state(STATE_SMARTCONFIG);  // 进入SmartConfig状态

                        // 等待按键完全释放
                        while (gpio_get_level(KEY_ENTER_GPIO) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                        goto next_event;  // 跳过后续的普通事件分发
                    }
                }
                // 短按时，继续正常的事件分发流程
            }

            // 分发按钮事件到相应的处理函数
            dispatch_button_event(event);

            next_event:;  // 标记下一个事件处理的位置
        }
    }
}

// ===================== 初始化 =====================
/**
 * @brief 按钮初始化函数
 * 
 * 配置GPIO引脚，安装中断服务程序，创建按钮处理任务
 */
static void init_buttons(void)
{
    // 创建按钮事件队列，容量为10个事件
    s_button_queue = xQueueCreate(10, sizeof(button_event_t));
    // 创建二值信号量，用于连接完成后的同步
    s_connect_done_sem = xSemaphoreCreateBinary();

    // 配置GPIO引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) | (1ULL << KEY_ENTER_GPIO),  // 设置三个按钮引脚
        .mode = GPIO_MODE_INPUT,        // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,   // 启用内部上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 禁用内部下拉电阻
        .intr_type = GPIO_INTR_NEGEDGE,     // 下降沿触发中断（按钮按下时）
    };
    gpio_config(&io_conf);

    // 安装GPIO中断服务程序
    gpio_install_isr_service(0);
    // 为每个按钮添加中断处理程序
    gpio_isr_handler_add(KEY_UP_GPIO, gpio_isr_handler, (void *)KEY_UP_GPIO);
    gpio_isr_handler_add(KEY_DOWN_GPIO, gpio_isr_handler, (void *)KEY_DOWN_GPIO);
    gpio_isr_handler_add(KEY_ENTER_GPIO, gpio_isr_handler, (void *)KEY_ENTER_GPIO);

    // 创建按钮处理任务
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "Buttons initialized");
}

// ===================== 主任务 =====================
/**
 * @brief 菜单主任务
 * 
 * 初始化LCD显示屏、按钮和WiFi连接器，然后扫描WiFi并显示列表
 * 
 * @param arg 任务参数（未使用）
 */
void menu_task(void *arg)
{
    lcd_init();              // 初始化LCD显示屏
    init_buttons();          // 初始化按钮
    wifi_connector_init();   // 初始化WiFi连接器

    // 确保WiFi处于断开状态以便进行扫描
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));  // 等待500ms确保断开完成

    // 扫描并更新WiFi列表
    wifi_scan_and_update_list();
    // 进入列表状态
    enter_state(STATE_LIST);

    ESP_LOGI(TAG, "Menu started");

    // 主循环，定期刷新（虽然当前没有需要定时更新的内容）
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 每秒一次
    }
}

/**
 * @brief 启动菜单任务的外部接口函数
 * 
 * 创建菜单主任务
 */
void menu_start(void)
{
    xTaskCreate(menu_task, "menu_task", 4096, NULL, 5, NULL);
}