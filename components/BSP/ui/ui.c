#include "ui.h"
#include "lvgl.h"
#include "wifi_scanner.h"
#include "esp_log.h"
#include <string.h>
#include "lcd.h"
static const char *TAG = "UI";

// 声明字库
LV_FONT_DECLARE(my_font_chinese_18);

// ==========================================
// 状态机与全局 UI 对象
// ==========================================
typedef enum {
    UI_STATE_SCANNING,
    UI_STATE_WIFI_LIST,
    UI_STATE_PASSWORD_INPUT,
    UI_STATE_CONNECTING
} system_ui_state_t;

static system_ui_state_t current_state = UI_STATE_WIFI_LIST;

static lv_obj_t * main_screen;  
static lv_obj_t * header;       
static lv_obj_t * wifi_list;    
static lv_obj_t * pwd_container = NULL; 
static lv_obj_t * last_focused_btn = NULL; 

// 样式定义
static lv_style_t style_screen; 
static lv_style_t style_card;   
static lv_style_t style_btn;    
static lv_style_t style_focus;  
static lv_style_t style_kb_focus; // 专门用于键盘按键的高亮

// ==========================================
// 辅助：信号强度转中文
// ==========================================
static const char* get_rssi_level_str(int rssi) {
    if (rssi >= -60) return "强";
    if (rssi >= -75) return "中";
    if (rssi >= -85) return "弱";
    return "极弱";
}

// ==========================================
// 1. 全新清爽亮色主题 (Light Theme)
// ==========================================
static void ui_style_init(void) {
    // 【屏幕全局样式】纯白背景，黑字
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0xFFFFFF)); 
    lv_style_set_text_font(&style_screen, &my_font_chinese_18);  
    lv_style_set_text_color(&style_screen, lv_color_hex(0x000000)); 

    // 【卡片/标题背景】非常浅的灰色，区分层次
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0xF5F5F5)); 
    lv_style_set_border_width(&style_card, 1);                  
    lv_style_set_border_color(&style_card, lv_color_hex(0xE0E0E0));
    lv_style_set_radius(&style_card, 8);                        

    // 【列表按钮默认样式】白底黑字
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0xFFFFFF)); 
    lv_style_set_text_color(&style_btn, lv_color_hex(0x333333)); 
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, lv_color_hex(0xEEEEEE));
    lv_style_set_pad_all(&style_btn, 12);                      
    lv_style_set_translate_y(&style_btn, 0); 
    lv_style_set_transform_width(&style_btn, 0);

    // 【焦点选中样式】醒目的科技蓝背景，白字
    lv_style_init(&style_focus);
    lv_style_set_bg_color(&style_focus, lv_color_hex(0x2196F3)); 
    lv_style_set_text_color(&style_focus, lv_color_hex(0xFFFFFF)); 
    lv_style_set_outline_width(&style_focus, 0);                 
    lv_style_set_border_width(&style_focus, 0);
    lv_style_set_translate_y(&style_focus, 0); 
    lv_style_set_transform_width(&style_focus, 0);

    // 【键盘内部按键的高亮样式】
    lv_style_init(&style_kb_focus);
    lv_style_set_bg_color(&style_kb_focus, lv_color_hex(0xFF9800)); // 橙色高亮，方便在键盘中找位置
    lv_style_set_text_color(&style_kb_focus, lv_color_hex(0xFFFFFF));
}

// ==========================================
// 2. 页面流转逻辑
// ==========================================
static void close_password_page(void) {
    if (pwd_container != NULL) {
        lv_obj_del(pwd_container); 
        pwd_container = NULL;
    }
    
    lv_obj_clear_flag(header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
    
    current_state = UI_STATE_WIFI_LIST;

    if (last_focused_btn != NULL) {
        lv_group_focus_obj(last_focused_btn);
    }
}

static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    
    if(code == LV_EVENT_READY) { 
        const char * pwd = lv_textarea_get_text(lv_keyboard_get_textarea(target));
        ESP_LOGI(TAG, "获取到密码准备连接: %s", pwd);
        close_password_page();
    } else if(code == LV_EVENT_CANCEL) { 
        close_password_page();
    } else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if(key == LV_KEY_LEFT || key == LV_KEY_ESC) { 
            close_password_page();
        }
    }
}

static void wifi_list_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED) {
        int index = (int)(uintptr_t)lv_event_get_user_data(e);
        last_focused_btn = btn; 
        
        lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);

        pwd_container = lv_obj_create(main_screen);
        lv_obj_set_size(pwd_container, LCD_WIDTH, LCD_HEIGHT);
        lv_obj_set_style_bg_color(pwd_container, lv_color_hex(0xFFFFFF), 0); // 密码页白底
        lv_obj_set_style_border_width(pwd_container, 0, 0);
        lv_obj_set_style_pad_all(pwd_container, 0, 0);

        lv_obj_t * title = lv_label_create(pwd_container);
        lv_label_set_text_fmt(title, "连接: %s", g_ap_records[index].ssid);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_text_color(title, lv_color_hex(0x000000), 0); // 黑字标题

        lv_obj_t * ta = lv_textarea_create(pwd_container);
        lv_obj_set_size(ta, LCD_WIDTH - 20, 40);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 40);
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_placeholder_text(ta, "请输入密码");

        // 优化键盘布局
        lv_obj_t * kb = lv_keyboard_create(pwd_container);
        lv_obj_set_size(kb, LCD_WIDTH, LCD_HEIGHT - 90); 
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(kb, ta);
        
        // 关键修复：让键盘内部的按键(LV_PART_ITEMS)也能响应焦点高亮
        lv_obj_add_style(kb, &style_kb_focus, LV_PART_ITEMS | LV_STATE_FOCUSED);
        
        lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_ALL, NULL);
        lv_group_add_obj(lv_group_get_default(), kb);
        lv_group_focus_obj(kb);
        
        current_state = UI_STATE_PASSWORD_INPUT;
    }
}

// ==========================================
// 3. 对外暴露的初始化接口
// ==========================================
void ui_init(void) {
    ui_style_init();

    main_screen = lv_scr_act();
    lv_obj_add_style(main_screen, &style_screen, 0);

    header = lv_obj_create(main_screen);
    lv_obj_set_size(header, LCD_WIDTH, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(header, &style_card, 0);
    lv_obj_set_style_radius(header, 0, 0); 
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE); 

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, "WiFi 扫描终端"); 
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x2196F3), 0); // 标题用蓝色点缀

    wifi_list = lv_list_create(main_screen);
    lv_obj_set_size(wifi_list, LCD_WIDTH - 16, LCD_HEIGHT - 56); 
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_style(wifi_list, &style_card, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_AUTO); 

    if (g_ap_count == 0) {
        lv_obj_t * no_data = lv_label_create(wifi_list);
        lv_label_set_text(no_data, "未发现周围网络");
        lv_obj_center(no_data);
    } else {
        lv_obj_t * first_btn = NULL; 

        for(int i = 0; i < g_ap_count; i++) {
            const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) ? (char *)g_ap_records[i].ssid : "[隐藏]";
            int rssi = g_ap_records[i].rssi;
            
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%d) [%s]", ssid, rssi, get_rssi_level_str(rssi));
            
            lv_obj_t * btn = lv_list_add_btn(wifi_list, "", buf);
            lv_obj_add_style(btn, &style_btn, 0); 
            // 绑定蓝色高亮
            lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED | LV_STATE_PRESSED); 
            
            lv_obj_add_event_cb(btn, wifi_list_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)i);
            lv_group_add_obj(lv_group_get_default(), btn);

            // 修正子标签颜色，确保变蓝底时文字变白
            lv_obj_t * label = lv_obj_get_child(btn, 1); 
            if (label) {
                lv_obj_add_style(label, &style_focus, LV_STATE_FOCUSED | LV_STATE_PRESSED);
            }

            if(first_btn == NULL) first_btn = btn;
        }

        if(first_btn != NULL) {
            lv_group_focus_obj(first_btn);
        }
    }
}