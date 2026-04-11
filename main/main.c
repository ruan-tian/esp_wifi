#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// BSP 与硬件驱动
#include "lcd.h"
#include "wifi_scanner.h"

// LVGL 核心与移植接口
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

static const char *TAG = "MAIN_APP";

// -------------------------------------------------------------------------
// 1. 外部资源声明
// -------------------------------------------------------------------------
// 声明你生成的那个 18 号抗锯齿中文字体 (必须与网页转换时填写的 Name 一致)
LV_FONT_DECLARE(my_font_chinese_18);

// -------------------------------------------------------------------------
// 2. 全局 UI 对象与样式
// -------------------------------------------------------------------------
static lv_obj_t *wifi_list;             // WiFi 扫描列表对象
static lv_obj_t *pwd_container = NULL;  // 密码输入页面的容器 (图层管理)
static lv_obj_t *pwd_ta;                // 密码输入框
static lv_obj_t *kb;                    // 虚拟键盘
static char current_ssid[33];           // 记录当前点击的 WiFi SSID

static lv_style_t style_base;           // 全局基础样式 (负责字体、间距)
static lv_style_t style_focus;          // 焦点样式 (负责选中时的蓝色高亮，解决“丑陋方框”)

// -------------------------------------------------------------------------
// 3. UI 样式初始化 (彻底解决“难看”和“细字体”问题)
// -------------------------------------------------------------------------
void ui_style_init(void) {
    // --- 基础样式 (应用到全屏，解决方框格子和字体太细) ---
    lv_style_init(&style_base);
    lv_style_set_text_font(&style_base, &my_font_chinese_18); // 应用中文字库
    lv_style_set_text_line_space(&style_base, 4);           // 增加行间距，更美观
    lv_style_set_text_letter_space(&style_base, 1);         // 微调字间距
    
    // --- 焦点样式 (解决按键移动时出现的“莫名其妙的虚线方框”) ---
    lv_style_init(&style_focus);
    lv_style_set_outline_width(&style_focus, 0);            // 彻底去掉选中时的外围虚线框
    lv_style_set_border_width(&style_focus, 0);             // 去掉默认的控件边框
    lv_style_set_bg_color(&style_focus, lv_palette_main(LV_PALETTE_BLUE)); // 选中时背景变为纯蓝
    lv_style_set_bg_opa(&style_focus, LV_OPA_COVER);        // 背景全实色，遮盖底层
    lv_style_set_text_color(&style_focus, lv_color_white()); // 选中时文字变白，更有科技感
}

// -------------------------------------------------------------------------
// 4. 密码键盘页面事件回调
// -------------------------------------------------------------------------
static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    // 当用户按下虚拟键盘右下角的“确认/打勾”或者左下角的“隐藏”
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if(code == LV_EVENT_READY) {
            const char * pwd = lv_textarea_get_text(pwd_ta);
            ESP_LOGI(TAG, "用户确认连接! SSID: %s, Password: %s", current_ssid, pwd);
            // 这里可以添加逻辑：调用 wifi_connect_to_ap(current_ssid, pwd)
        } else {
            ESP_LOGI(TAG, "用户取消了输入");
        }

        // 1. 销毁整个密码层
        lv_obj_del(pwd_container);
        pwd_container = NULL;

        // 2. 重新显示背后的 WiFi 列表
        lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);

        // 3. 把物理按键的焦点还给列表，恢复滚动
        lv_group_focus_next(lv_group_get_default());
    }
}

// -------------------------------------------------------------------------
// 5. 弹出密码输入界面 (解决页面重叠与难看问题)
// -------------------------------------------------------------------------
static void show_password_keyboard(const char * ssid) {
    if(pwd_container != NULL) return; // 防止连按导致的多次创建

    strncpy(current_ssid, ssid, sizeof(current_ssid));

    // 先隐藏主列表，防止透视和重叠干扰视觉
    lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);

    // 创建一个全屏覆盖层
    pwd_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pwd_container, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_center(pwd_container);
    
    // 美化容器：去除边框和圆角，铺满屏幕
    lv_obj_set_style_pad_all(pwd_container, 0, 0);
    lv_obj_set_style_border_width(pwd_container, 0, 0);
    lv_obj_set_style_radius(pwd_container, 0, 0);
    lv_obj_add_style(pwd_container, &style_base, 0); // 应用中文字体

    // 标题提示
    lv_obj_t * label = lv_label_create(pwd_container);
    lv_label_set_text_fmt(label, "请输入密码连接到:\n%s", ssid); // 支持中文显示了！
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    // 密码文本框
    pwd_ta = lv_textarea_create(pwd_container);
    lv_textarea_set_password_mode(pwd_ta, true); // 星号隐藏
    lv_textarea_set_one_line(pwd_ta, true);
    lv_obj_set_width(pwd_ta, LCD_WIDTH - 20);
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 60);

    // 全功能虚拟键盘
    kb = lv_keyboard_create(pwd_container);
    lv_keyboard_set_textarea(kb, pwd_ta); // 键盘输入内容关联到文本框
    lv_obj_set_size(kb, LCD_WIDTH, LCD_HEIGHT / 2 + 50); // 占屏幕下半部
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);

    // 关键：将键盘也加入物理按键控制组，并赋予美颜焦点样式
    lv_group_add_obj(lv_group_get_default(), kb);
    lv_obj_add_style(kb, &style_focus, LV_STATE_FOCUSED | LV_PART_ITEMS);
    lv_group_focus_obj(kb); // 强行让按键控制键盘
}

// -------------------------------------------------------------------------
// 6. WiFi 列表项点击回调
// -------------------------------------------------------------------------
static void wifi_list_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // 这里的 i 是创建按钮时存入的 user_data
        int index = (int)(uintptr_t)lv_event_get_user_data(e);
        ESP_LOGI(TAG, "选中 WiFi: %s", g_ap_records[index].ssid);
        
        // 弹出密码界面
        show_password_keyboard((const char *)g_ap_records[index].ssid);
    }
}

// -------------------------------------------------------------------------
// 7. 主程序入口
// -------------------------------------------------------------------------
void app_main(void) {
    // 初始化存储和硬件驱动
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }
    lcd_init();

    // 初始化 LVGL 及其接口
    lv_port_disp_init();
    lv_port_indev_init();

    // 初始化 UI 样式
    ui_style_init();
    
    // 给当前屏幕应用全局中文字体样式 (这样后续创建的 Label/List 都会默认用 18 号字)
    lv_obj_add_style(lv_scr_act(), &style_base, 0);

    // 执行一次底层的 WiFi 扫描
    wifi_scanner_init(); 
    wifi_scan_and_update_list(); 

    // 创建主界面的 WiFi 列表
    wifi_list = lv_list_create(lv_scr_act()); 
    lv_obj_set_size(wifi_list, LCD_WIDTH, LCD_HEIGHT);   
    lv_obj_center(wifi_list);
    lv_obj_set_style_border_width(wifi_list, 0, 0);       // 去掉列表的大外框
    lv_obj_set_style_radius(wifi_list, 0, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF); // 去掉丑陋的滚动条

    lv_list_add_text(wifi_list, "附近的无线网络 (WiFi List)");

    lv_obj_t * first_btn = NULL; 

    // 将扫描结果逐一显示在列表中
    for(int i = 0; i < g_ap_count; i++) {
        const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) ? (char *)g_ap_records[i].ssid : "[隐藏网络]";
        char buf[64];
        snprintf(buf, sizeof(buf), "%s (%d dBm)", ssid, g_ap_records[i].rssi);
        
        // 创建列表按钮
        lv_obj_t * btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, buf);
        lv_obj_add_event_cb(btn, wifi_list_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)i);
        
        // 给按钮应用按键选中样式 (变蓝 + 去方框)
        lv_group_add_obj(lv_group_get_default(), btn);
        lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED);

        if(first_btn == NULL) first_btn = btn;
    }

    // 默认让焦点落在第一个 WiFi 按钮上，这样物理按键一按就有反应
    if(first_btn != NULL) {
        lv_group_focus_obj(first_btn);
    }

    // 启动 LVGL 核心心跳任务
    xTaskCreate(lvgl_port_task, "lvgl_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "系统启动完毕，开始操作吧！");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}