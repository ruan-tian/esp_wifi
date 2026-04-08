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

// ===================== 按键引脚 =====================
#define KEY_UP_GPIO      GPIO_NUM_5
#define KEY_DOWN_GPIO    GPIO_NUM_6
#define KEY_ENTER_GPIO   GPIO_NUM_7

typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_ENTER,
    BUTTON_REFRESH,     // 定时器触发的刷新事件
} button_event_t;

// ===================== 状态定义 =====================
typedef enum {
    STATE_LIST,         // 浏览WiFi列表
    STATE_DETAIL,       // WiFi详情页
    STATE_KEYBOARD,     // 虚拟键盘输入密码
    STATE_CONNECTING,   // 直连中
    STATE_SMARTCONFIG,  // 一键配网中
} menu_state_t;

static const char *state_names[] = {
    [STATE_LIST]        = "LIST",
    [STATE_DETAIL]      = "DETAIL",
    [STATE_KEYBOARD]    = "KEYBOARD",
    [STATE_CONNECTING]  = "CONNECTING",
    [STATE_SMARTCONFIG] = "SMARTCONFIG",
};


// ===================== 状态机上下文 =====================
typedef struct {
    menu_state_t state;
    uint16_t     selected_index;
    char         password[65];       // 用户输入的密码
    int          password_len;       // 当前密码长度
    int          kb_cursor_row;      // 键盘光标行
    int          kb_cursor_col;      // 键盘光标列
    int          kb_last_row;        // 上一次光标位置（用于增量刷新）
    int          kb_last_col;        // 上一次光标位置
} menu_context_t;

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


// ===================== 同步机制 =====================
static QueueHandle_t  s_button_queue      = NULL;
static SemaphoreHandle_t s_connect_done_sem = NULL;
static QueueHandle_t  s_keyboard_queue    = NULL;
static TaskHandle_t   s_keyboard_task_handle = NULL;
static TaskHandle_t   s_connect_task_handle  = NULL;
static TaskHandle_t   s_sc_task_handle       = NULL;
static TimerHandle_t  s_refresh_timer    = NULL;

// ===================== 虚拟键盘定义 =====================
#define KB_ROWS 5
#define KB_COLS 10

// 键盘布局：4行字母数字 + 1行功能键
static const char *kb_layout[KB_ROWS][KB_COLS] = {
    {"A","B","C","D","E","F","G","H","I","J"},
    {"K","L","M","N","O","P","Q","R","S","T"},
    {"U","V","W","X","Y","Z","0","1","2","3"},
    {"4","5","6","7","8","9","-","_",".","@"},
    {"DEL","CLR","SPC",  "","","","","","OK","ESC"},
};

typedef struct {
    int row;
    int col;
} kb_pos_t;

// ===================== 函数原型 =====================
static void display_wifi_list(void);
static void draw_wifi_line(int index, bool highlight);
static void enter_state(menu_state_t new_state);
static void connect_task(void *arg);
static void smartconfig_task(void *arg);
static void keyboard_task(void *arg);

// ===================== 信号强度绘制 =====================
// RSSI 转信号格数：-30满格，-80无信号
static void draw_signal_bars(int x, int y, int rssi, uint16_t color)
{
    int bars;
    if (rssi >= -30)      bars = 5;
    else if (rssi >= -45) bars = 4;
    else if (rssi >= -55) bars = 3;
    else if (rssi >= -65) bars = 2;
    else if (rssi >= -75) bars = 1;
    else                  bars = 0;

    // 5个竖条，从左到右逐渐增高
    for (int i = 0; i < 5; i++) {
        int h = 2 + i * 3;          // 高度: 2, 5, 8, 11, 14
        int bar_y = y + 14 - h;     // 底部对齐
        uint16_t c = (i < bars) ? color : LCD_COLOR_GRAY;

        for (int py = bar_y; py < y + 15; py++) {
            for (int px = 0; px < 3; px++) {
                ili9341_draw_pixel(x + i * 4 + px, py, c);
            }
        }
    }
}

// ===================== 单行绘制（带信号格）=====================
static void draw_wifi_line(int index, bool highlight)
{
    if (index >= g_ap_count) return;

    char buf[48];
    int y = 30 + index * 16;
    uint16_t color = highlight ? LCD_COLOR_YELLOW : LCD_COLOR_WHITE;

    snprintf(buf, sizeof(buf), "%2d. %-16.16s",
             index + 1, (char *)g_ap_records[index].ssid);

    // 擦除该行
    ili9341_draw_rectangle(0, y, LCD_WIDTH, 16, LCD_COLOR_BLACK);

    // 高亮箭头
    if (highlight) {
        ili9341_draw_string_8x16(0, y, ">", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    }

    // SSID 文字
    ili9341_draw_string_8x16(12, y, buf, color, LCD_COLOR_BLACK);

    // 右侧信号格（替换原来的数字RSSI）
    draw_signal_bars(LCD_WIDTH - 28, y, g_ap_records[index].rssi, color);
}

// ===================== 全量绘制列表 =====================
static void display_wifi_list(void)
{
    ili9341_fill_screen(LCD_COLOR_BLACK);

    // 标题
    ili9341_draw_string_8x16(10, 5, "WiFi List", LCD_COLOR_CYAN, LCD_COLOR_BLACK);

    // 分割线
    for (int x = 0; x < LCD_WIDTH; x += 2) {
        ili9341_draw_pixel(x, 22, LCD_COLOR_GRAY);
    }

    // 列表
    if (g_ap_count == 0) {
        ili9341_draw_string_8x16(10, 40, "No WiFi Found!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    } else {
        for (int i = 0; i < g_ap_count && i < 12; i++) {
            draw_wifi_line(i, (i == sm.selected_index));
        }
    }

    // 底部提示
    ili9341_draw_string_8x16(10, LCD_HEIGHT - 16,
        "ENTER:Detail  LONG:SmartConfig", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

// ===================== 详情页绘制 =====================
static void display_detail(void)
{
    if (sm.selected_index >= g_ap_count) return;

    ili9341_fill_screen(LCD_COLOR_BLACK);

    // 标题
    ili9341_draw_string_8x16(10, 5, "WiFi Detail", LCD_COLOR_CYAN, LCD_COLOR_BLACK);

    for (int x = 0; x < LCD_WIDTH; x += 2) {
        ili9341_draw_pixel(x, 22, LCD_COLOR_GRAY);
    }

    int y = 30;
    char buf[64];

    // SSID
    snprintf(buf, sizeof(buf), "SSID: %s", (char *)g_ap_records[sm.selected_index].ssid);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    y += 20;

    // RSSI
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", g_ap_records[sm.selected_index].rssi);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    // 信号格
    draw_signal_bars(160, y, g_ap_records[sm.selected_index].rssi, LCD_COLOR_GREEN);
    y += 20;

    // 信道
    snprintf(buf, sizeof(buf), "Channel: %d", g_ap_records[sm.selected_index].primary);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    y += 20;

    // 加密方式
    const char *auth_str;
    switch (g_ap_records[sm.selected_index].authmode) {
        case WIFI_AUTH_OPEN:            auth_str = "Open";          break;
        case WIFI_AUTH_WEP:             auth_str = "WEP";           break;
        case WIFI_AUTH_WPA_PSK:         auth_str = "WPA-PSK";       break;
        case WIFI_AUTH_WPA2_PSK:        auth_str = "WPA2-PSK";      break;
        case WIFI_AUTH_WPA_WPA2_PSK:    auth_str = "WPA/WPA2-PSK";  break;
        case WIFI_AUTH_WPA3_PSK:        auth_str = "WPA3-PSK";      break;
        default:                        auth_str = "Unknown";       break;
    }
    snprintf(buf, sizeof(buf), "Auth: %s", auth_str);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    y += 20;

    // BSSID (MAC地址)
    snprintf(buf, sizeof(buf), "BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
             g_ap_records[sm.selected_index].bssid[0],
             g_ap_records[sm.selected_index].bssid[1],
             g_ap_records[sm.selected_index].bssid[2],
             g_ap_records[sm.selected_index].bssid[3],
             g_ap_records[sm.selected_index].bssid[4],
             g_ap_records[sm.selected_index].bssid[5]);
    ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    // 底部操作提示
    ili9341_draw_string_8x16(10, LCD_HEIGHT - 32,
        "ENTER: Connect", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    ili9341_draw_string_8x16(10, LCD_HEIGHT - 16,
        "UP: Back", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

// ===================== 虚拟键盘绘制 =====================
static void display_keyboard(void)
{
    if (sm.selected_index >= g_ap_count) return;

    ili9341_fill_screen(LCD_COLOR_BLACK);

    // 标题：正在连接的WiFi名称
    char title[64];
    snprintf(title, sizeof(title), "Connect: %-16.16s",
             (char *)g_ap_records[sm.selected_index].ssid);
    ili9341_draw_string_8x16(10, 2, title, LCD_COLOR_CYAN, LCD_COLOR_BLACK);

    // 密码显示区域
    ili9341_draw_rectangle(0, 20, LCD_WIDTH, 18, LCD_COLOR_BLACK);
    char pw_display[32];
    int len = sm.password_len < 20 ? sm.password_len : 20;
    memset(pw_display, '*', len);
    pw_display[len] = '\0';
    ili9341_draw_string_8x16(10, 24, "PWD:", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    ili9341_draw_string_8x16(50, 24, pw_display, LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    // 键盘区域
    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
            if (kb_layout[r][c][0] == '\0') continue;

            int px = 4 + c * 23;
            int py = 46 + r * 18;

            // 按键背景
            ili9341_draw_rectangle(px, py, 21, 16, LCD_COLOR_BLACK);

            // 按键文字
            uint16_t fg = LCD_COLOR_WHITE;
            uint16_t bg = LCD_COLOR_BLACK;

            if (r == sm.kb_cursor_row && c == sm.kb_cursor_col) {
                // 当前光标位置：反色高亮
                fg = LCD_COLOR_BLACK;
                bg = LCD_COLOR_WHITE;
                ili9341_draw_rectangle(px, py, 21, 16, bg);
            }

            // 居中显示按键文字
            int text_x = px + (21 - strlen(kb_layout[r][c]) * 8) / 2;
            ili9341_draw_string_8x16(text_x, py, kb_layout[r][c], fg, bg);
        }
    }
}

// ===================== 键盘自动轮转任务 =====================
// 光标自动在按键间跳转，用户按ENTER选中当前高亮的键
static void keyboard_task(void *arg)
{
    s_keyboard_queue = xQueueCreate(10, sizeof(int));

    sm.password[0] = '\0';
    sm.password_len = 0;
    sm.kb_cursor_row = 0;
    sm.kb_cursor_col = 0;
    sm.kb_last_row = -1;
    sm.kb_last_col = -1;

    // 首次绘制
    display_keyboard();

    TickType_t last_advance = xTaskGetTickCount();
    int cmd;

    while (1) {
        // 每500ms光标自动跳到下一个按键
        if (xTaskGetTickCount() - last_advance > pdMS_TO_TICKS(500)) {
            // 跳过空格位
            do {
                sm.kb_cursor_col++;
                if (sm.kb_cursor_col >= KB_COLS) {
                    sm.kb_cursor_col = 0;
                    sm.kb_cursor_row++;
                    if (sm.kb_cursor_row >= KB_ROWS) {
                        sm.kb_cursor_row = 0;
                    }
                }
            } while (kb_layout[sm.kb_cursor_row][sm.kb_cursor_col][0] == '\0');

            last_advance = xTaskGetTickCount();

            // 增量刷新：只重绘旧位置和新位置
            if (sm.kb_last_row >= 0) {
                int lx = 4 + sm.kb_last_col * 23;
                int ly = 46 + sm.kb_last_row * 18;
                ili9341_draw_rectangle(lx, ly, 21, 16, LCD_COLOR_BLACK);
                int tx = lx + (21 - strlen(kb_layout[sm.kb_last_row][sm.kb_last_col]) * 8) / 2;
                ili9341_draw_string_8x16(tx, ly,
                    kb_layout[sm.kb_last_row][sm.kb_last_col],
                    LCD_COLOR_WHITE, LCD_COLOR_BLACK);
            }

            // 绘制新位置
            {
                int nx = 4 + sm.kb_cursor_col * 23;
                int ny = 46 + sm.kb_cursor_row * 18;
                ili9341_draw_rectangle(nx, ny, 21, 16, LCD_COLOR_WHITE);
                int tx = nx + (21 - strlen(kb_layout[sm.kb_cursor_row][sm.kb_cursor_col]) * 8) / 2;
                ili9341_draw_string_8x16(tx, ny,
                    kb_layout[sm.kb_cursor_row][sm.kb_cursor_col],
                    LCD_COLOR_BLACK, LCD_COLOR_WHITE);
            }

            sm.kb_last_row = sm.kb_cursor_row;
            sm.kb_last_col = sm.kb_cursor_col;
        }

        // 检查是否有按键命令
        if (xQueueReceive(s_keyboard_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (cmd == 1) {
                // ===== ENTER：选中当前高亮键 =====
                const char *key = kb_layout[sm.kb_cursor_row][sm.kb_cursor_col];

                if (strcmp(key, "DEL") == 0) {
                    // 退格
                    if (sm.password_len > 0) {
                        sm.password_len--;
                        sm.password[sm.password_len] = '\0';
                    }
                }
                else if (strcmp(key, "CLR") == 0) {
                    // 清空
                    sm.password_len = 0;
                    sm.password[0] = '\0';
                }
                else if (strcmp(key, "SPC") == 0) {
                    // 空格
                    if (sm.password_len < 63) {
                        sm.password[sm.password_len++] = ' ';
                        sm.password[sm.password_len] = '\0';
                    }
                }
                else if (strcmp(key, "OK") == 0) {
                    // 确认连接
                    QueueHandle_t q = s_keyboard_queue;
                    s_keyboard_queue = NULL;
                    vQueueDelete(q);
                    s_keyboard_task_handle = NULL;
                    enter_state(STATE_CONNECTING);
                    vTaskDelete(NULL);
                    return;
                }
                else if (strcmp(key, "ESC") == 0) {
                    // 取消，返回详情页
                    QueueHandle_t q = s_keyboard_queue;
                    s_keyboard_queue = NULL;
                    vQueueDelete(q);
                    s_keyboard_task_handle = NULL;
                    enter_state(STATE_DETAIL);
                    vTaskDelete(NULL);
                    return;
                }
                else {
                    // 普通字符
                    if (sm.password_len < 63) {
                        sm.password[sm.password_len++] = key[0];
                        sm.password[sm.password_len] = '\0';
                    }
                }

                // 刷新密码显示
                ili9341_draw_rectangle(50, 24, LCD_WIDTH - 50, 16, LCD_COLOR_BLACK);
                char pw_display[32];
                int len = sm.password_len < 20 ? sm.password_len : 20;
                memset(pw_display, '*', len);
                pw_display[len] = '\0';
                ili9341_draw_string_8x16(50, 24, pw_display, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
            }
            else if (cmd == 2) {
                // 取消（由button_task长按ENTER触发）
                QueueHandle_t q = s_keyboard_queue;
                s_keyboard_queue = NULL;
                vQueueDelete(q);
                s_keyboard_task_handle = NULL;
                enter_state(STATE_LIST);
                vTaskDelete(NULL);
                return;
            }
        }
    }
}

// ===================== 连接任务 =====================
static void connect_task(void *arg)
{
    const char *ssid = (const char *)g_ap_records[sm.selected_index].ssid;

    ESP_LOGI(TAG, "Connecting to %s with password [%s]", ssid, sm.password);

    esp_err_t ret = wifi_connect_sta(ssid, sm.password, 30000);

    // 显示结果
    ili9341_fill_screen(LCD_COLOR_BLACK);
    if (ret == ESP_OK) {
        ili9341_draw_string_8x16(10, 10, "Connect Success!", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    } else {
        ili9341_draw_string_8x16(10, 10, "Connect Fail!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    }
    ili9341_draw_string_8x16(10, 30, "Press ENTER to return", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    xSemaphoreTake(s_connect_done_sem, portMAX_DELAY);

    s_connect_task_handle = NULL;
    enter_state(STATE_LIST);
    vTaskDelete(NULL);
}

// ===================== SmartConfig 任务 =====================
static void smartconfig_task(void *arg)
{
    esp_err_t ret = wifi_smartconfig_start(60000);

    ili9341_fill_screen(LCD_COLOR_BLACK);
    if (ret == ESP_OK) {
        const char *ssid = wifi_get_connected_ssid();
        char msg[64];
        snprintf(msg, sizeof(msg), "Connected: %s", ssid);
        ili9341_draw_string_8x16(10, 10, "SmartConfig OK!", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
        ili9341_draw_string_8x16(10, 30, msg, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    } else {
        ili9341_draw_string_8x16(10, 10, "SmartConfig Failed!", LCD_COLOR_RED, LCD_COLOR_BLACK);
    }
    ili9341_draw_string_8x16(10, 60, "Press ENTER to return", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

    xSemaphoreTake(s_connect_done_sem, portMAX_DELAY);

    s_sc_task_handle = NULL;
    enter_state(STATE_LIST);
    vTaskDelete(NULL);
}

// ===================== 状态转换 =====================
static void enter_state(menu_state_t new_state)
{
    menu_state_t old_state = sm.state;
    ESP_LOGI(TAG, "State: %s -> %s", state_names[old_state], state_names[new_state]);

    sm.state = new_state;

    switch (new_state) {
        case STATE_LIST:
            display_wifi_list();
            break;

        case STATE_DETAIL:
            display_detail();
            break;

        case STATE_KEYBOARD:
            // 防止重复创建
            if (s_keyboard_task_handle == NULL) {
                xTaskCreate(keyboard_task, "kb_task", 4096, NULL, 5, &s_keyboard_task_handle);
            }
            break;

        case STATE_CONNECTING:
            {
                const char *ssid = (const char *)g_ap_records[sm.selected_index].ssid;
                ili9341_fill_screen(LCD_COLOR_BLACK);
                char msg[64];
                snprintf(msg, sizeof(msg), "Connecting to %s...", ssid);
                ili9341_draw_string_8x16(10, 10, msg, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 30, "Please wait...", LCD_COLOR_WHITE, LCD_COLOR_BLACK);

                if (s_connect_task_handle == NULL) {
                    xTaskCreate(connect_task, "conn_task", 4096, NULL, 5, &s_connect_task_handle);
                }
            }
            break;

        case STATE_SMARTCONFIG:
            {
                ili9341_fill_screen(LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 10, "SmartConfig Mode", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 30, "Use ESP Touch App", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 50, "to send WiFi info...", LCD_COLOR_WHITE, LCD_COLOR_BLACK);
                ili9341_draw_string_8x16(10, 80, "Waiting...", LCD_COLOR_CYAN, LCD_COLOR_BLACK);

                if (s_sc_task_handle == NULL) {
                    xTaskCreate(smartconfig_task, "sc_task", 4096, NULL, 5, &s_sc_task_handle);
                }
            }
            break;
    }
}

// ===================== 各状态按键处理 =====================

// LIST 状态
static void handle_list_event(button_event_t event)
{
    switch (event) {
        case BUTTON_UP:
            if (g_ap_count == 0) return;
            {
                uint16_t old = sm.selected_index;
                sm.selected_index = (sm.selected_index - 1 + g_ap_count) % g_ap_count;
                draw_wifi_line(old, false);
                draw_wifi_line(sm.selected_index, true);
            }
            break;

        case BUTTON_DOWN:
            if (g_ap_count == 0) return;
            {
                uint16_t old = sm.selected_index;
                sm.selected_index = (sm.selected_index + 1) % g_ap_count;
                draw_wifi_line(old, false);
                draw_wifi_line(sm.selected_index, true);
            }
            break;

        case BUTTON_ENTER:
            if (g_ap_count > 0) {
                enter_state(STATE_DETAIL);
            } else {
                enter_state(STATE_SMARTCONFIG);
            }
            break;

        case BUTTON_REFRESH:
            // 定时器触发的刷新
            ESP_LOGI(TAG, "Auto refreshing WiFi list...");
            wifi_scan_and_update_list();
            if (sm.state == STATE_LIST) {
                display_wifi_list();
            }
            break;

        default:
            break;
    }
}

// DETAIL 状态
static void handle_detail_event(button_event_t event)
{
    switch (event) {
        case BUTTON_UP:
            enter_state(STATE_LIST);    // UP 返回列表
            break;

        case BUTTON_ENTER:
            enter_state(STATE_KEYBOARD); // ENTER 进入键盘输密码
            break;

        default:
            break;
    }
}

// KEYBOARD 状态：把按键转发给键盘任务
static void handle_keyboard_event(button_event_t event)
{
    if (s_keyboard_queue == NULL) return;

    int cmd = 0;
    switch (event) {
        case BUTTON_ENTER:
            cmd = 1;    // 选中当前高亮键
            xQueueSend(s_keyboard_queue, &cmd, 0);
            break;

        case BUTTON_UP:
        case BUTTON_DOWN:
            // UP/DOWN 在键盘中暂不处理
            break;

        default:
            break;
    }
}

// CONNECTING 状态
static void handle_connecting_event(button_event_t event)
{
    if (event == BUTTON_ENTER) {
        xSemaphoreGive(s_connect_done_sem);
    }
}

// SMARTCONFIG 状态
static void handle_sc_event(button_event_t event)
{
    if (event == BUTTON_ENTER) {
        xSemaphoreGive(s_connect_done_sem);
    }
}

// ===================== 统一分发 =====================
static void dispatch_button_event(button_event_t event)
{
    switch (sm.state) {
        case STATE_LIST:        handle_list_event(event);        break;
        case STATE_DETAIL:      handle_detail_event(event);      break;
        case STATE_KEYBOARD:    handle_keyboard_event(event);    break;
        case STATE_CONNECTING:  handle_connecting_event(event);  break;
        case STATE_SMARTCONFIG: handle_sc_event(event);          break;
    }
}

// ===================== 自动刷新回调 =====================
static void refresh_timer_callback(TimerHandle_t timer)
{
    // 只在 LIST 状态时触发刷新
    if (sm.state == STATE_LIST) {
        button_event_t event = BUTTON_REFRESH;
        xQueueSend(s_button_queue, &event, 0);
    }
}

// ===================== GPIO中断 =====================
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    button_event_t event;

    if (gpio_num == KEY_UP_GPIO)         event = BUTTON_UP;
    else if (gpio_num == KEY_DOWN_GPIO)  event = BUTTON_DOWN;
    else if (gpio_num == KEY_ENTER_GPIO) event = BUTTON_ENTER;
    else return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_button_queue, &event, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// ===================== 按键任务 =====================
static void button_task(void *arg)
{
    button_event_t event;
    TickType_t press_time = 0;

    while (1) {
        if (xQueueReceive(s_button_queue, &event, portMAX_DELAY) == pdTRUE) {

            // TIMER 事件不需要消抖
            if (event == BUTTON_REFRESH) {
                dispatch_button_event(event);
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(50));

            bool pressed = false;
            switch (event) {
                case BUTTON_UP:    pressed = (gpio_get_level(KEY_UP_GPIO) == 0);    break;
                case BUTTON_DOWN:  pressed = (gpio_get_level(KEY_DOWN_GPIO) == 0);  break;
                case BUTTON_ENTER: pressed = (gpio_get_level(KEY_ENTER_GPIO) == 0); break;
                default: continue;
            }
            if (!pressed) continue;

            // ENTER 长按检测（在 LIST 或 KEYBOARD 状态下进入 SmartConfig）
            if (event == BUTTON_ENTER &&
                (sm.state == STATE_LIST || sm.state == STATE_KEYBOARD)) {
                press_time = xTaskGetTickCount();

                while (gpio_get_level(KEY_ENTER_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    if ((xTaskGetTickCount() - press_time) > pdMS_TO_TICKS(1000)) {
                        ESP_LOGI(TAG, "Long press -> SmartConfig");

                        // 如果在键盘状态，通知键盘任务退出
                        if (sm.state == STATE_KEYBOARD && s_keyboard_queue) {
                            int cmd = 2;  // 取消命令
                            xQueueSend(s_keyboard_queue, &cmd, 0);
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }

                        enter_state(STATE_SMARTCONFIG);

                        while (gpio_get_level(KEY_ENTER_GPIO) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                        goto next_event;
                    }
                }
            }

            dispatch_button_event(event);
            next_event:;
        }
    }
}

// ===================== 按键初始化 =====================
static void init_buttons(void)
{
    s_button_queue = xQueueCreate(10, sizeof(button_event_t));
    s_connect_done_sem = xSemaphoreCreateBinary();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) | (1ULL << KEY_ENTER_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(KEY_UP_GPIO, gpio_isr_handler, (void *)KEY_UP_GPIO);
    gpio_isr_handler_add(KEY_DOWN_GPIO, gpio_isr_handler, (void *)KEY_DOWN_GPIO);
    gpio_isr_handler_add(KEY_ENTER_GPIO, gpio_isr_handler, (void *)KEY_ENTER_GPIO);

    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "Buttons initialized");
}

// ===================== 主任务 =====================
void menu_task(void *arg)
{
    lcd_init();
    init_buttons();
    wifi_connector_init();

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_scan_and_update_list();
    ESP_LOGI(TAG, "Scan done, found %d APs", g_ap_count);

    // 创建自动刷新定时器（30秒周期）
    s_refresh_timer = xTimerCreate(
        "wifi_refresh",
        pdMS_TO_TICKS(30000),   // 30秒
        pdTRUE,                  // 自动重载
        NULL,
        refresh_timer_callback
    );
    xTimerStart(s_refresh_timer, 0);

    enter_state(STATE_LIST);
    ESP_LOGI(TAG, "Menu started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void menu_start(void)
{
    xTaskCreate(menu_task, "menu_task", 4096, NULL, 5, NULL);
}
