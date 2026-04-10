#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lcd.h"

#include "wifi_scanner.h"
#include "wifi_connector.h"

static const char *TAG = "MENU";

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PWD "pwd"

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
    BUTTON_ENTER_LONG,  // 新增：长按确认键
    BUTTON_BACK_LONG,   // 新增：长按返回键
    BUTTON_REFRESH,     
    BUTTON_RETURN_LIST,
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
    STATE_INFO,        // 信息页
} menu_state_t;

// 状态名称数组，用于调试日志
static const char *state_names[] = {
    [STATE_LIST] = "LIST",
    [STATE_DETAIL] = "DETAIL",
    [STATE_KEYBOARD] = "KEYBOARD",
    [STATE_CONNECTING] = "CONNECTING",
    [STATE_SMARTCONFIG] = "SMARTCONFIG",
    [STATE_INFO] = "INFO", 
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
// ===================== UI: 高级网络信息页 =====================
static void display_info(void)
{
    ili9341_fill_screen(LCD_COLOR_BLACK);
    ili9341_draw_string_8x16(10, 10, "Device Network Info", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    ili9341_draw_line(0, 30, LCD_WIDTH, 30, LCD_COLOR_WHITE);

    wifi_ap_record_t ap_info;
    // 获取当前连接的 AP 信息，如果没连上会返回非 ESP_OK
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err != ESP_OK) {
        ili9341_draw_string_8x16(10, 60, "Status: Not Connected", LCD_COLOR_RED, LCD_COLOR_BLACK);
        ili9341_draw_string_8x16(10, 90, "Please connect first.", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    } else {
        char buf[64];
        int y = 45;

        // 1. 打印当前连接的 SSID
        snprintf(buf, sizeof(buf), "SSID: %s", ap_info.ssid);
        ili9341_draw_string_utf8_limit(10, y, buf, LCD_COLOR_YELLOW, LCD_COLOR_BLACK, LCD_WIDTH - 20);
        y += 25;

        // 2. 获取并打印 IP 和 网关
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(buf, sizeof(buf), "IP: " IPSTR, IP2STR(&ip_info.ip));
            ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
            y += 25;

            snprintf(buf, sizeof(buf), "GW: " IPSTR, IP2STR(&ip_info.gw));
            ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
            y += 25;
        }

        // 3. 获取并打印本机 MAC 地址
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(buf, sizeof(buf), "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ili9341_draw_string_8x16(10, y, buf, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    }

    // 底部返回提示
    ili9341_draw_string_8x16(10, 290, "[BACK] Return to List", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
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
    char pwd_buf[128];
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

// ===================== NVS：WiFi 凭据存取 =====================
/**
 * @brief 从 NVS 读取上次保存的 WiFi SSID 与密码。
 *
 * 数据保存在命名空间 @c NVS_NAMESPACE 下，键名为 @c NVS_KEY_SSID、@c NVS_KEY_PWD。
 * 调用 @c nvs_get_str 时传入的缓冲区长度需包含结尾 @c '\\0'；函数内部用长度参数同时作为
 * 输入（容量）与输出（实际写入长度，含 @c '\\0'）。
 *
 * @param[out] ssid           成功时写入以 @c '\\0' 结尾的 SSID；失败时内容未定义。
 * @param[in]  max_ssid_len   @a ssid 缓冲区总字节数，建议 ≥ 33（32 字符 SSID + NUL）。
 * @param[out] pwd            成功时写入以 @c '\\0' 结尾的密码；失败时内容未定义。
 * @param[in]  max_pwd_len    @a pwd 缓冲区总字节数，需能容纳最长密码加 NUL。
 *
 * @retval true  命名空间打开成功，且 SSID、密码两个键均读取成功。
 * @retval false 命名空间不存在/打开失败，或缺少任一键，或缓冲区不足以容纳存储的字符串。
 */
static bool load_wifi_credentials(char *ssid, size_t max_ssid_len, char *pwd, size_t max_pwd_len)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_len = max_ssid_len;
    err = nvs_get_str(my_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return false;
    }

    size_t pwd_len = max_pwd_len;
    err = nvs_get_str(my_handle, NVS_KEY_PWD, pwd, &pwd_len);
    nvs_close(my_handle);
    return (err == ESP_OK);
}

/**
 * @brief 将当前 WiFi SSID 与密码写入 NVS 并提交到 Flash。
 *
 * 在 STA 连接成功后调用，用于下次上电时由 @c load_wifi_credentials 读出并尝试自动连接。
 * 流程为：以读写模式打开命名空间 → 写入两个字符串键 → @c nvs_commit → @c nvs_close；
 * 任一步失败会记录错误日志并尽快关闭句柄，避免泄漏。
 *
 * @param[in] ssid 要保存的网络名称（与 @c wifi_connect_sta 等使用的 SSID 一致）。
 * @param[in] pwd  要保存的密码；开放网络可为空字符串，但指针须有效。
 */
static void save_wifi_credentials(const char *ssid, const char *pwd)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(my_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving SSID to NVS!", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    err = nvs_set_str(my_handle, NVS_KEY_PWD, pwd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) saving password to NVS!", esp_err_to_name(err));
        nvs_close(my_handle);
        return;
    }

    err = nvs_commit(my_handle);
    nvs_close(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing NVS!", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "WiFi credentials saved to NVS: %s", ssid);
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
        save_wifi_credentials(ssid, sm.password);
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
    // 状态机监控日志
    ESP_LOGI(TAG, "State transitioning to: %s", state_names[new_state]);
    
    sm.state = new_state;

    switch (new_state) {
        case STATE_LIST:
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

                // 5. 打印正在使用的密码
                char pwd_msg[128];
                snprintf(pwd_msg, sizeof(pwd_msg), "Pwd: %s", sm.password);
                ili9341_draw_string_8x16(30, 150, pwd_msg, LCD_COLOR_CYAN, 0x2104);

                // 启动实际的网络连接任务
                if (s_connect_task_handle == NULL) {
                    xTaskCreate(connect_task, "connect_task", 4096, NULL, 5, &s_connect_task_handle);
                }
            }
            break;

        case STATE_SMARTCONFIG:
            ili9341_fill_screen(LCD_COLOR_BLACK);
            ili9341_draw_string_8x16(30, 100, "SmartConfig Ready...", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
            
            // 启动实际的一键配网任务
            if (s_sc_task_handle == NULL) {
                xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 5, &s_sc_task_handle);
            }
            break;

        case STATE_INFO:
            display_info();
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
    // 【新增】：全局高级操作拦截
    if (event == BUTTON_BACK_LONG) {
        ESP_LOGW(TAG, "LONG PRESS BACK! Erasing NVS and Restarting...");
        ili9341_fill_screen(LCD_COLOR_RED);
        ili9341_draw_string_8x16(20, 100, "Factory Reset...", LCD_COLOR_WHITE, LCD_COLOR_RED);
        ili9341_draw_string_8x16(20, 120, "Erasing WiFi Data", LCD_COLOR_WHITE, LCD_COLOR_RED);
        
        // 抹除 NVS 分区并重启
        nvs_flash_erase();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart(); 
        return;
    }
    
    
    if (event == BUTTON_ENTER_LONG) {
        if (sm.state == STATE_LIST) {
            ESP_LOGI(TAG, "LONG PRESS ENTER triggers! Showing Net Info...");
            enter_state(STATE_INFO);
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
    case STATE_INFO:
        if (event == BUTTON_BACK || event == BUTTON_ENTER) {
            enter_state(STATE_LIST);
        }
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

/**
 * @brief 按钮任务，处理按钮事件
 * @param arg 参数
 */
 static void button_task(void *arg)
 {
     const gpio_num_t pins[5] = {KEY_UP_GPIO, KEY_DOWN_GPIO, KEY_RIGHT_GPIO, KEY_ENTER_GPIO, KEY_BACK_GPIO};
     const button_event_t short_evts[5] = {BUTTON_UP, BUTTON_DOWN, BUTTON_RIGHT, BUTTON_ENTER, BUTTON_BACK};
     const button_event_t long_evts[5]  = {BUTTON_UP, BUTTON_DOWN, BUTTON_RIGHT, BUTTON_ENTER_LONG, BUTTON_BACK_LONG};
 
     bool last_state[5] = {1, 1, 1, 1, 1}; // 默认上拉为高电平
     TickType_t press_tick[5] = {0};
 
     while (1)
     {
         button_event_t event;
         // 【巧妙设计】：将原有的死等 portMAX_DELAY 改为 20ms 超时。
         // 这既能接收软件发送的消息，又充当了 20ms 的物理按键轮询节拍！
         if (xQueueReceive(s_button_queue, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
             if (event == BUTTON_RETURN_LIST) {
                 ESP_LOGI(TAG, "Re-scanning to refresh g_ap_count...");
                 vTaskDelay(pdMS_TO_TICKS(500));
                 wifi_scan_and_update_list();
                 enter_state(STATE_LIST);
                 continue;
             }
             if (event == BUTTON_REFRESH) {
                 dispatch_button_event(event);
                 continue;
             }
         }
 
         // 轮询物理按键状态
         for (int i = 0; i < 5; i++) {
             bool current_state = gpio_get_level(pins[i]);
             
             if (last_state[i] == 1 && current_state == 0) {
                 // 按下瞬间，记录时间戳
                 press_tick[i] = xTaskGetTickCount();
             } 
             else if (last_state[i] == 0 && current_state == 1) {
                 // 松开瞬间，计算按压时长
                 uint32_t duration_ms = (xTaskGetTickCount() - press_tick[i]) * portTICK_PERIOD_MS;
                 
                 if (duration_ms >= 1000) {
                     // 大于1秒判定为长按
                     dispatch_button_event(long_evts[i]);
                 } else if (duration_ms >= 30) {
                     // 30ms ~ 1000ms 判定为短按 (自带完美的 30ms 软件消抖)
                     dispatch_button_event(short_evts[i]);
                 }
             }
             last_state[i] = current_state;
         }
     }
 }

/**
 * @brief 初始化按钮GPIO
 */
static void init_buttons(void)
{
    s_button_queue = xQueueCreate(10, sizeof(button_event_t));
    s_connect_done_sem = xSemaphoreCreateBinary();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) |
                        (1ULL << KEY_RIGHT_GPIO) | (1ULL << KEY_ENTER_GPIO) | (1ULL << KEY_BACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE, // 【关键修改】：彻底禁用中断，解放 IRAM
    };
    gpio_config(&io_conf);

    // 取消了 gpio_install_isr_service 的调用
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
     
     // 【新增】：确保 NVS 正常初始化，以防底层的 wifi_connector_init 遗漏
     esp_err_t err = nvs_flash_init();
     if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
         ESP_ERROR_CHECK(nvs_flash_erase());
         err = nvs_flash_init();
     }
     
     wifi_connector_init(); // 初始化WiFi底层
 
     // 绘制加载界面
     ili9341_fill_screen(LCD_COLOR_BLACK);
     ili9341_draw_string_8x16(10, LCD_HEIGHT / 2, "Scanning Environment...", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
 
     esp_wifi_disconnect();
     vTaskDelay(pdMS_TO_TICKS(500));
 
     // 执行首次WiFi扫描
     wifi_scan_and_update_list();
 
     // 创建自动刷新定时器
     s_refresh_timer = xTimerCreate("wifi_refresh", pdMS_TO_TICKS(30000), pdTRUE, NULL, refresh_timer_callback);
     xTimerStart(s_refresh_timer, 0);
 
     // 【新增核心逻辑】：检查 NVS 历史记录并尝试自动连接
     char saved_ssid[33] = {0};
     char saved_pwd[65] = {0};
     bool auto_connected = false;
 
     if (load_wifi_credentials(saved_ssid, sizeof(saved_ssid), saved_pwd, sizeof(saved_pwd))) {
         ESP_LOGI(TAG, "Found saved NVS WiFi: %s", saved_ssid);
         
         // 遍历刚才扫描到的列表，看这个保存的 WiFi 在不在附近
         for (int i = 0; i < g_ap_count; i++) {
             if (strcmp((const char *)g_ap_records[i].ssid, saved_ssid) == 0) {
                 ESP_LOGI(TAG, "Match found in scan list! Auto-connecting...");
                 
                 // 伪装用户操作：选中该项、填入密码、进入连接状态
                 sm.selected_index = i;
                 snprintf(sm.password, sizeof(sm.password), "%s", saved_pwd);
                 sm.password_len = strlen(sm.password);
                 
                 enter_state(STATE_CONNECTING);
                 auto_connected = true;
                 break; // 找到就退出循环
             }
         }
     }
 
     // 如果没找到保存的 WiFi，或者保存的 WiFi 不在附近信号里，就进入常规列表页
     if (!auto_connected) {
         enter_state(STATE_LIST);
     }
 
     while (1)
     {
         vTaskDelay(pdMS_TO_TICKS(1000));
     }
 }

/**
 * @brief 启动菜单功能
 */
void menu_start(void)
{
    xTaskCreate(menu_task, "menu_task", 4096, NULL, 5, NULL);
}