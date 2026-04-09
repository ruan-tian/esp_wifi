#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "lcd.h"
#include "wifi_scanner.h"
#include "wifi_connector.h"

static const char *TAG = "MENU";

// ===================== 5 按键引脚分配 =====================
#define KEY_UP_GPIO GPIO_NUM_5    // 上键GPIO引脚
#define KEY_DOWN_GPIO GPIO_NUM_6  // 下键GPIO引脚
#define KEY_RIGHT_GPIO GPIO_NUM_7 // 右键GPIO引脚
#define KEY_ENTER_GPIO GPIO_NUM_8 // 确认键GPIO引脚
#define KEY_BACK_GPIO GPIO_NUM_9  // 返回键GPIO引脚

/**
 * @brief 按钮事件枚举
 */
typedef enum
{
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_RIGHT,
    BUTTON_ENTER,
    BUTTON_BACK,
    BUTTON_REFRESH,     // 定时器触发的刷新事件
    BUTTON_RETURN_LIST, // 内部事件：安全返回列表页并强制重扫
} button_event_t;

// ===================== 状态机定义 =====================
/**
 * @brief 菜单状态枚举
 */
typedef enum
{
    STATE_LIST,        // 浏览WiFi列表
    STATE_DETAIL,      // WiFi详情页
    STATE_KEYBOARD,    // 虚拟键盘手动导航
    STATE_CONNECTING,  // 直连中
    STATE_SMARTCONFIG, // 一键配网中
} menu_state_t;

// 状态名称数组，用于调试日志
static const char *state_names[] = {
    [STATE_LIST] = "LIST",
    [STATE_DETAIL] = "DETAIL",
    [STATE_KEYBOARD] = "KEYBOARD",
    [STATE_CONNECTING] = "CONNECTING",
    [STATE_SMARTCONFIG] = "SMARTCONFIG",
};

// ===================== 状态机上下文 =====================
/**
 * @brief 菜单状态机上下文结构体
 */
typedef struct
{
    menu_state_t state;      // 当前状态
    uint16_t selected_index; // 当前选中的WiFi索引
    char password[65];       // 用户输入的密码
    int password_len;        // 密码长度
    int kb_cursor_row;       // 键盘光标行
    int kb_cursor_col;       // 键盘光标列
    int kb_last_row;         // 上一次光标位置（用于增量刷新）
    int kb_last_col;
} menu_context_t;

// 全局状态机实例
static menu_context_t sm = {
    .state = STATE_LIST,
    .selected_index = 0,
    .password = {0},
    .password_len = 0,
    .kb_cursor_row = 0,
    .kb_cursor_col = 0,
    .kb_last_row = -1,
    .kb_last_col = -1,
};

// ===================== 任务与同步机制 =====================
static QueueHandle_t s_button_queue = NULL;         // 按钮事件队列
static SemaphoreHandle_t s_connect_done_sem = NULL; // 连接完成信号量
static TaskHandle_t s_connect_task_handle = NULL;   // 连接任务句柄
static TaskHandle_t s_sc_task_handle = NULL;        // SmartConfig任务句柄
static TimerHandle_t s_refresh_timer = NULL;        // 自动刷新定时器

// ===================== 虚拟键盘定义 =====================
#define KB_ROWS 5  // 键盘行数
#define KB_COLS 10 // 键盘列数

// 布局优化：去掉了原有的 DEL/ESC 软按键，交由物理 BACK 键处理
static const char *kb_layout[KB_ROWS][KB_COLS] = {
    {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"},
    {"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"},
    {"U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3"},
    {"4", "5", "6", "7", "8", "9", "-", "_", ".", "@"},
    {"CLR", "SPC", "", "", "", "", "", "", "", "OK"}, // CLR:清空 SPC:空格 OK:确定
};

// ===================== 函数原型 =====================
static void display_wifi_list(void);
static void draw_wifi_line(int index, bool highlight);
static void enter_state(menu_state_t new_state);
static void connect_task(void *arg);
static void smartconfig_task(void *arg);
static void update_keyboard_cursor(void);

// ===================== UI: 信号强度绘制 =====================
/**
 * @brief 绘制信号强度条
 * @param x X坐标
 * @param y Y坐标
 * @param rssi RSSI值
 * @param color 颜色
 */
static void draw_signal_bars(int x, int y, int rssi, uint16_t color)
{
    int bars = 0;
    // 根据RSSI值计算信号强度等级
    if (rssi >= -30)
        bars = 5;
    else if (rssi >= -45)
        bars = 4;
    else if (rssi >= -55)
        bars = 3;
    else if (rssi >= -65)
        bars = 2;
    else if (rssi >= -75)
        bars = 1;

    for (int i = 0; i < 5; i++)
    {
        int h = 2 + i * 3;
        int bar_y = y + 14 - h;
        // 如果信号格在范围内则使用传入颜色，否则使用暗灰色
        uint16_t c = (i < bars) ? color : 0x4208; // 0x4208 为暗灰色

        for (int py = bar_y; py < y + 15; py++)
        {
            for (int px = 0; px < 3; px++)
            {
                ili9341_draw_pixel(x + i * 4 + px, py, c);
            }
        }
    }
}

// ===================== UI: 单行列表绘制 =====================
/**
 * @brief 绘制WiFi列表中的单行
 * @param index WiFi索引
 * @param highlight 是否高亮显示
 */
static void draw_wifi_line(int index, bool highlight)
{
    if (index >= g_ap_count)
        return;

    int y = 30 + index * 20;

    // 1. 每次画之前，干净彻底地擦除整行的文字区（不碰信号格区）
    ili9341_draw_rectangle(0, y - 2, LCD_WIDTH - 30, 20, LCD_COLOR_BLACK);

    if (highlight)
    {
        ili9341_draw_round_rect(4, y - 2, LCD_WIDTH - 36, 18, 4, LCD_COLOR_BLUE);
    }

    // 2. 拦截并处理“隐藏网络”造成的空白
    const char *ssid_name = (const char *)g_ap_records[index].ssid;
    if (strlen(ssid_name) == 0)
    {
        ssid_name = "[隐藏网络]";
    }

    // 3. 放弃 %-14.14s 截断！改用最简单的 %s
    char buf[64];
    snprintf(buf, sizeof(buf), "%2d. %s", index + 1, ssid_name);

    uint16_t fg_color = LCD_COLOR_WHITE;
    uint16_t bg_color = highlight ? LCD_COLOR_BLUE : LCD_COLOR_BLACK;

    // 4. 调用新函数，强行限制绘制宽度不能超过 (LCD_WIDTH - 44)，绝对不会侵占信号格地盘
    ili9341_draw_string_utf8_limit(8, y, buf, fg_color, bg_color, LCD_WIDTH - 44);

    // 5. 信号格绘制保持不变
    draw_signal_bars(LCD_WIDTH - 28, y, g_ap_records[index].rssi, highlight ? LCD_COLOR_WHITE : LCD_COLOR_GREEN);
}
// ===================== UI: 列表页 =====================
/**
 * @brief 显示WiFi列表页面
 */
static void display_wifi_list(void)
{
    ili9341_fill_screen(LCD_COLOR_BLACK);

    // 美化：顶部深灰色状态栏
    ili9341_draw_rectangle(0, 0, LCD_WIDTH, 20, 0x3186);
    ili9341_draw_string_8x16(5, 2, "WiFi Scanner V2.0", LCD_COLOR_WHITE, 0x3186);

    if (g_ap_count == 0)
    {
        ili9341_draw_string_8x16(10, 40, "No WiFi Found!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    }
    else
    {
        // 最多显示12个WiFi网络
        for (int i = 0; i < g_ap_count && i < 12; i++)
        {
            draw_wifi_line(i, (i == sm.selected_index));
        }
    }

    // 底部按键提示更新
    ili9341_draw_string_8x16(2, LCD_HEIGHT - 16,
                             "ENT:Info  RGT:SmartCFG  BCK:Scan", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

// ===================== UI: 详情页 =====================
/**
 * @brief 显示WiFi详情页面
 */
static void display_detail(void)
{
    ili9341_fill_screen(LCD_COLOR_BLACK);
    ili9341_draw_string_8x16(10, 10, "WiFi Details", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    
    // 修复：替换为系统原有的画线函数
    ili9341_draw_line(0, 30, LCD_WIDTH, 30, LCD_COLOR_WHITE);

    int y = 45;
    char buf[64];

    // 1. 安全获取 SSID（处理隐藏网络）
    const char *ssid_name = (const char *)g_ap_records[sm.selected_index].ssid;
    if (strlen(ssid_name) == 0) {
        ssid_name = "[隐藏网络]";
    }

    // 2. 拼接并使用安全函数打印 SSID
    snprintf(buf, sizeof(buf), "SSID: %s", ssid_name);
    ili9341_draw_string_utf8_limit(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK, LCD_WIDTH - 20);
    y += 25;

    // 3. 打印信号强度
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", g_ap_records[sm.selected_index].rssi);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    y += 25;

    // 4. 打印加密方式
    snprintf(buf, sizeof(buf), "Auth: %d", g_ap_records[sm.selected_index].authmode);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    y += 25;

    // 5. 底部操作提示
    ili9341_draw_string_8x16(10, 290, "[OK] Connect   [BACK] Return", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
}

// ===================== UI: 虚拟键盘绘制 =====================
/**
 * @brief 绘制密码输入框
 */
static void draw_password_box(void)
{
    ili9341_draw_rectangle(50, 24, LCD_WIDTH - 50, 16, LCD_COLOR_BLACK);
    char pw_display[32];
    int len = sm.password_len < 20 ? sm.password_len : 20;
    memset(pw_display, '*', len);
    pw_display[len] = '\0';
    ili9341_draw_string_8x16(50, 24, pw_display, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
}

/**
 * @brief 显示虚拟键盘页面
 */
static void display_keyboard(void)
{
    ili9341_fill_screen(LCD_COLOR_BLACK);

    // 1. 顶部标题区域（安全解析 SSID）
    const char *ssid_name = (const char *)g_ap_records[sm.selected_index].ssid;
    if (strlen(ssid_name) == 0) ssid_name = "[隐藏网络]";
    
    char title[64];
    snprintf(title, sizeof(title), "Connect: %s", ssid_name);
    ili9341_draw_string_utf8_limit(5, 5, title, LCD_COLOR_CYAN, LCD_COLOR_BLACK, LCD_WIDTH - 10);

    ili9341_draw_line(0, 25, LCD_WIDTH, 25, LCD_COLOR_WHITE);

    // 2. 当前已输入密码显示 (修复：使用你结构体里的 sm.password)
    char pwd_buf[64];
    snprintf(pwd_buf, sizeof(pwd_buf), "Pwd: %s", sm.password);
    ili9341_draw_string_8x16(5, 30, pwd_buf, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);

    // 3. 渲染虚拟键盘按键矩阵 (修复：使用你代码中原本的 KB_ROWS 和 kb_layout)
    for (int i = 0; i < KB_ROWS; i++) {
        for (int j = 0; j < KB_COLS; j++) {
            int x = 4 + j * 23;
            int y = 46 + i * 18;
            
            // 判断当前按键是否被光标选中
            bool highlight = (i == sm.kb_cursor_row && j == sm.kb_cursor_col);
            uint16_t bg_color = highlight ? LCD_COLOR_WHITE : LCD_COLOR_BLACK;
            uint16_t fg_color = highlight ? LCD_COLOR_BLACK : LCD_COLOR_WHITE;
            
            ili9341_draw_rectangle(x, y, 21, 16, bg_color);
            
            // 将字符画在按键框内
            const char* key_str = kb_layout[i][j];
            if(strlen(key_str) > 0) {
                int tx = x + (21 - strlen(key_str) * 8) / 2;
                ili9341_draw_string_8x16(tx, y, key_str, fg_color, bg_color);
            }
        }
    }

    // 4. 底部操作提示
    ili9341_draw_string_8x16(5, 290, "[OK] Select  [BACK] Del/Ret", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
}

/**
 * @brief 增量刷新键盘光标
 */
static void update_keyboard_cursor(void)
{
    // 清除上一个光标位置
    if (sm.kb_last_row >= 0 && sm.kb_last_col >= 0)
    {
        int lx = 4 + sm.kb_last_col * 23;
        int ly = 46 + sm.kb_last_row * 18;
        ili9341_draw_rectangle(lx, ly, 21, 16, LCD_COLOR_BLACK);
        int tx = lx + (21 - strlen(kb_layout[sm.kb_last_row][sm.kb_last_col]) * 8) / 2;
        ili9341_draw_string_8x16(tx, ly, kb_layout[sm.kb_last_row][sm.kb_last_col], LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    }

    // 绘制新光标位置
    int nx = 4 + sm.kb_cursor_col * 23;
    int ny = 46 + sm.kb_cursor_row * 18;
    ili9341_draw_rectangle(nx, ny, 21, 16, LCD_COLOR_WHITE);
    int tx = nx + (21 - strlen(kb_layout[sm.kb_cursor_row][sm.kb_cursor_col]) * 8) / 2;
    ili9341_draw_string_8x16(tx, ny, kb_layout[sm.kb_cursor_row][sm.kb_cursor_col], LCD_COLOR_BLACK, LCD_COLOR_WHITE);
}

// ===================== 后台网络任务 =====================
/**
 * @brief WiFi连接任务
 * @param arg 参数
 */
static void connect_task(void *arg)
{
    const char *ssid = (const char *)g_ap_records[sm.selected_index].ssid;
    ESP_LOGI(TAG, "Connecting to %s", ssid);

    // 尝试连接WiFi
    esp_err_t ret = wifi_connect_sta(ssid, sm.password, 30000);

    ili9341_fill_screen(LCD_COLOR_BLACK);
    if (ret == ESP_OK)
    {
        ili9341_draw_string_8x16(10, 10, "Connect Success!", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    }
    else
    {
        ili9341_draw_string_8x16(10, 10, "Connect Fail!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    }
    ili9341_draw_string_8x16(10, 30, "Press ENTER to return", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    // 等待用户按下回车键返回
    xSemaphoreTake(s_connect_done_sem, portMAX_DELAY);

    s_connect_task_handle = NULL;

    // Bug修复：不直接调用 enter_state(STATE_LIST)，发送内部事件交由状态机安全切屏
    button_event_t ev = BUTTON_RETURN_LIST;
    xQueueSend(s_button_queue, &ev, 0);
    vTaskDelete(NULL);
}

/**
 * @brief SmartConfig任务
 * @param arg 参数
 */
static void smartconfig_task(void *arg)
{
    // 启动SmartConfig配网
    esp_err_t ret = wifi_smartconfig_start(60000);

    ili9341_fill_screen(LCD_COLOR_BLACK);
    if (ret == ESP_OK)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "Connected: %s", wifi_get_connected_ssid());
        ili9341_draw_string_8x16(10, 10, "SmartConfig OK!", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
        ili9341_draw_string_8x16(10, 30, msg, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    }
    else
    {
        ili9341_draw_string_8x16(10, 10, "SmartConfig Failed!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    }
    ili9341_draw_string_8x16(10, 60, "Press ENTER to return", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    xSemaphoreTake(s_connect_done_sem, portMAX_DELAY);

    s_sc_task_handle = NULL;
    button_event_t ev = BUTTON_RETURN_LIST;
    xQueueSend(s_button_queue, &ev, 0);
    vTaskDelete(NULL);
}

// ===================== 状态机切屏逻辑 =====================
/**
 * @brief 进入指定状态
 * @param new_state 新状态
 */
static void enter_state(menu_state_t new_state)
{
    // 修复：使用你定义的 sm.state
    sm.state = new_state;

    switch (new_state) {
        case STATE_LIST:
            // 修复：使用你代码中的 display_wifi_list
            display_wifi_list();
            break;

        case STATE_DETAIL:
            display_detail();
            break;

        case STATE_KEYBOARD:
            display_keyboard();
            break;

        case STATE_CONNECTING:
            {
                // 1. 安全获取 SSID
                const char *ssid = (const char *)g_ap_records[sm.selected_index].ssid;
                if (strlen(ssid) == 0) ssid = "[隐藏网络]"; 
                
                // 2. 画弹窗深色背景和边框
                ili9341_fill_screen(LCD_COLOR_BLACK);
                ili9341_draw_round_rect(20, 80, LCD_WIDTH - 40, 100, 5, 0x2104);
                
                // 3. 打印提示头
                ili9341_draw_string_8x16(30, 100, "Connecting to:", LCD_COLOR_YELLOW, 0x2104);
                
                // 4. 安全打印超长 SSID
                char msg[128];
                snprintf(msg, sizeof(msg), "%s", ssid);
                ili9341_draw_string_utf8_limit(30, 125, msg, LCD_COLOR_WHITE, 0x2104, LCD_WIDTH - 60);

                // 5. 打印正在使用的密码 (修复：使用 sm.password)
                char pwd_msg[128];
                snprintf(pwd_msg, sizeof(pwd_msg), "Pwd: %s", sm.password);
                ili9341_draw_string_8x16(30, 150, pwd_msg, LCD_COLOR_CYAN, 0x2104);
            }
            break;

        case STATE_SMARTCONFIG:
            ili9341_fill_screen(LCD_COLOR_BLACK);
            ili9341_draw_string_8x16(30, 100, "SmartConfig Ready...", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
            break;

        default:
            break;
    }
}

// ===================== 事件分发逻辑 =====================
/**
 * @brief 处理列表页面事件
 * @param event 按钮事件
 */
static void handle_list_event(button_event_t event)
{
    if (g_ap_count == 0 && event != BUTTON_BACK && event != BUTTON_RIGHT)
        return;

    uint16_t old = sm.selected_index;
    switch (event)
    {
    case BUTTON_UP:
        // 循环选择，向上滚动
        sm.selected_index = (sm.selected_index - 1 + g_ap_count) % g_ap_count;
        draw_wifi_line(old, false);
        draw_wifi_line(sm.selected_index, true);
        break;
    case BUTTON_DOWN:
        // 循环选择，向下滚动
        sm.selected_index = (sm.selected_index + 1) % g_ap_count;
        draw_wifi_line(old, false);
        draw_wifi_line(sm.selected_index, true);
        break;
    case BUTTON_ENTER:
        enter_state(STATE_DETAIL);
        break;
    case BUTTON_RIGHT: // 快捷触发 SmartConfig
        enter_state(STATE_SMARTCONFIG);
        break;
    case BUTTON_BACK: // 手动强制重新扫描
        ili9341_draw_string_8x16(10, LCD_HEIGHT / 2, "Scanning...", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
        wifi_scan_and_update_list();
        display_wifi_list();
        break;
    default:
        break;
    }
}

/**
 * @brief 处理键盘页面事件
 * @param event 按钮事件
 */
static void handle_keyboard_event(button_event_t event)
{
    // 保存当前光标位置，用于后续增量刷新
    sm.kb_last_row = sm.kb_cursor_row;
    sm.kb_last_col = sm.kb_cursor_col;

    switch (event)
    {
    case BUTTON_UP:
        // 在键盘上向上移动，跳过空按钮
        do
        {
            sm.kb_cursor_row = (sm.kb_cursor_row - 1 + KB_ROWS) % KB_ROWS;
        } while (kb_layout[sm.kb_cursor_row][sm.kb_cursor_col][0] == '\0');
        update_keyboard_cursor();
        break;
    case BUTTON_DOWN:
        // 在键盘上向下移动，跳过空按钮
        do
        {
            sm.kb_cursor_row = (sm.kb_cursor_row + 1) % KB_ROWS;
        } while (kb_layout[sm.kb_cursor_row][sm.kb_cursor_col][0] == '\0');
        update_keyboard_cursor();
        break;
    case BUTTON_RIGHT:
        // 在键盘上向右移动，跳过空按钮
        do
        {
            sm.kb_cursor_col = (sm.kb_cursor_col + 1) % KB_COLS;
        } while (kb_layout[sm.kb_cursor_row][sm.kb_cursor_col][0] == '\0');
        update_keyboard_cursor();
        break;
    case BUTTON_BACK:
        // 物理返回键作为退格或退出
        if (sm.password_len > 0)
        {
            sm.password[--sm.password_len] = '\0';
            draw_password_box();
        }
        else
        {
            enter_state(STATE_DETAIL);
        }
        break;
    case BUTTON_ENTER:
    {
        const char *key = kb_layout[sm.kb_cursor_row][sm.kb_cursor_col];
        if (strcmp(key, "CLR") == 0)
        {
            // 清空密码
            sm.password_len = 0;
            sm.password[0] = '\0';
        }
        else if (strcmp(key, "SPC") == 0)
        {
            // 添加空格
            if (sm.password_len < 63)
                sm.password[sm.password_len++] = ' ';
        }
        else if (strcmp(key, "OK") == 0)
        {
            // 确认连接
            enter_state(STATE_CONNECTING);
            return;
        }
        else
        {
            // 添加字符到密码
            if (sm.password_len < 63)
            {
                sm.password[sm.password_len++] = key[0];
            }
        }
        sm.password[sm.password_len] = '\0';
        draw_password_box();
    }
    break;
    default:
        break;
    }
}

/**
 * @brief 分发按钮事件
 * @param event 按钮事件
 */
static void dispatch_button_event(button_event_t event)
{
    // 处理定时器自动刷新
    if (event == BUTTON_REFRESH)
    {
        if (sm.state == STATE_LIST)
        {
            wifi_scan_and_update_list();
            display_wifi_list();
        }
        return;
    }

    switch (sm.state)
    {
    case STATE_LIST:
        handle_list_event(event);
        break;
    case STATE_DETAIL:
        if (event == BUTTON_BACK)
            enter_state(STATE_LIST);
        if (event == BUTTON_ENTER)
            enter_state(STATE_KEYBOARD);
        break;
    case STATE_KEYBOARD:
        handle_keyboard_event(event);
        break;
    case STATE_CONNECTING:
    case STATE_SMARTCONFIG:
        // 按 ENTER 确认并退出结果提示页
        if (event == BUTTON_ENTER)
        {
            xSemaphoreGive(s_connect_done_sem);
        }
        // 按 BACK 强行中止配网/连接，并立刻返回
        if (event == BUTTON_BACK)
        {
            wifi_cancel();                      // 1. 打断底层长达 30-60 秒的阻塞等待
            xSemaphoreGive(s_connect_done_sem); // 2. 释放信号量，防止网络任务卡在最后的显示结果页
        }
        break;
    }
}

// ===================== 中断与主循环 =====================
/**
 * @brief GPIO中断处理函数
 * @param arg GPIO编号
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    button_event_t event;

    // 根据GPIO编号确定按钮事件
    if (gpio_num == KEY_UP_GPIO)
        event = BUTTON_UP;
    else if (gpio_num == KEY_DOWN_GPIO)
        event = BUTTON_DOWN;
    else if (gpio_num == KEY_RIGHT_GPIO)
        event = BUTTON_RIGHT;
    else if (gpio_num == KEY_ENTER_GPIO)
        event = BUTTON_ENTER;
    else if (gpio_num == KEY_BACK_GPIO)
        event = BUTTON_BACK;
    else
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // 发送事件到队列
    xQueueSendFromISR(s_button_queue, &event, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

/**
 * @brief 按钮任务，处理按钮事件
 * @param arg 参数
 */
static void button_task(void *arg)
{
    button_event_t event;
    while (1)
    {
        if (xQueueReceive(s_button_queue, &event, portMAX_DELAY) == pdTRUE)
        {

            // Bug修复核心：接收网络任务发来的返回列表指令，并在此处安全重扫、切屏
            if (event == BUTTON_RETURN_LIST)
            {
                ESP_LOGI(TAG, "Re-scanning to refresh g_ap_count...");
                vTaskDelay(pdMS_TO_TICKS(500));
                wifi_scan_and_update_list();
                enter_state(STATE_LIST);
                continue;
            }

            if (event == BUTTON_REFRESH)
            {
                dispatch_button_event(event);
                continue;
            }

            // 简单消抖：延迟50ms检测是否真的按下
            vTaskDelay(pdMS_TO_TICKS(50));
            bool pressed = false;
            switch (event)
            {
            case BUTTON_UP:
                pressed = (gpio_get_level(KEY_UP_GPIO) == 0);
                break;
            case BUTTON_DOWN:
                pressed = (gpio_get_level(KEY_DOWN_GPIO) == 0);
                break;
            case BUTTON_RIGHT:
                pressed = (gpio_get_level(KEY_RIGHT_GPIO) == 0);
                break;
            case BUTTON_ENTER:
                pressed = (gpio_get_level(KEY_ENTER_GPIO) == 0);
                break;
            case BUTTON_BACK:
                pressed = (gpio_get_level(KEY_BACK_GPIO) == 0);
                break;
            default:
                break;
            }
            if (!pressed)
                continue;

            dispatch_button_event(event);
        }
    }
}

/**
 * @brief 初始化按钮GPIO
 */
static void init_buttons(void)
{
    // 创建事件队列和信号量
    s_button_queue = xQueueCreate(10, sizeof(button_event_t));
    s_connect_done_sem = xSemaphoreCreateBinary();

    // 配置GPIO为输入模式，启用上拉电阻
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) |
                        (1ULL << KEY_RIGHT_GPIO) | (1ULL << KEY_ENTER_GPIO) | (1ULL << KEY_BACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // 下降沿触发中断
    };
    gpio_config(&io_conf);

    // 安装GPIO中断服务
    gpio_install_isr_service(0);
    gpio_isr_handler_add(KEY_UP_GPIO, gpio_isr_handler, (void *)KEY_UP_GPIO);
    gpio_isr_handler_add(KEY_DOWN_GPIO, gpio_isr_handler, (void *)KEY_DOWN_GPIO);
    gpio_isr_handler_add(KEY_RIGHT_GPIO, gpio_isr_handler, (void *)KEY_RIGHT_GPIO);
    gpio_isr_handler_add(KEY_ENTER_GPIO, gpio_isr_handler, (void *)KEY_ENTER_GPIO);
    gpio_isr_handler_add(KEY_BACK_GPIO, gpio_isr_handler, (void *)KEY_BACK_GPIO);

    // 创建按钮处理任务
    xTaskCreate(button_task, "button_task", 3072, NULL, 10, NULL);
}

/**
 * @brief 定时器回调函数，用于定期刷新WiFi列表
 * @param timer 定时器句柄
 */
static void refresh_timer_callback(TimerHandle_t timer)
{
    if (sm.state == STATE_LIST)
    {
        button_event_t event = BUTTON_REFRESH;
        xQueueSend(s_button_queue, &event, 0);
    }
}

/**
 * @brief 菜单主任务
 * @param arg 参数
 */
void menu_task(void *arg)
{
    lcd_init();            // 初始化LCD
    init_buttons();        // 初始化按钮
    wifi_connector_init(); // 初始化WiFi连接器

    // 断开当前WiFi连接，准备扫描
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 执行首次WiFi扫描
    wifi_scan_and_update_list();

    // 创建30秒刷新间隔的定时器
    s_refresh_timer = xTimerCreate("wifi_refresh", pdMS_TO_TICKS(30000), pdTRUE, NULL, refresh_timer_callback);
    xTimerStart(s_refresh_timer, 0);

    // 进入初始状态（列表页面）
    enter_state(STATE_LIST);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒延时，保持任务运行
    }
}

/**
 * @brief 启动菜单功能
 */
void menu_start(void)
{
    xTaskCreate(menu_task, "menu_task", 4096, NULL, 5, NULL);
}