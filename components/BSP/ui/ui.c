#include "ui.h"
#include "lvgl.h"
#include "wifi_scanner.h"
#include "wifi_connector.h"
#include "esp_log.h"
#include <string.h>
#include "lcd.h"

static const char *TAG = "UI";

LV_FONT_DECLARE(my_font_chinese_18);

// ======================= 前置声明 =======================
static void ui_style_init(void);
static void close_password_page(void);
static void open_password_page(int ap_index);
static void smartconfig_task(void *arg);
static void connect_task(void *arg);
static void list_btn_event_cb(lv_event_t * e);
static void keyboard_event_cb(lv_event_t * e);

// ======================= 全局变量 =======================
static lv_obj_t * main_screen;
static lv_obj_t * header;
static lv_obj_t * wifi_list;
static lv_obj_t * pwd_container = NULL;
static lv_obj_t * last_focused_btn = NULL;
static lv_group_t * input_group = NULL;

static lv_style_t style_screen;
static lv_style_t style_card;
static lv_style_t style_btn;
static lv_style_t style_focus;
static lv_style_t style_kb_focus;

// 用于保存密码（连接时使用）
static char g_password[64] = {0};

// ======================= 样式初始化 =======================
static void ui_style_init(void) {
    // 屏幕背景：白色
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_screen, &my_font_chinese_18);
    lv_style_set_text_color(&style_screen, lv_color_hex(0x000000));

    // 卡片背景（浅灰）
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0xF5F5F5));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_hex(0xDDDDDD));
    lv_style_set_radius(&style_card, 6);

    // 列表按钮默认样式：白底黑字，无边框
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0xFFFFFF));
    lv_style_set_text_color(&style_btn, lv_color_hex(0x222222));
    lv_style_set_border_width(&style_btn, 0);
    lv_style_set_pad_all(&style_btn, 10);
    lv_style_set_radius(&style_btn, 4);

    // 焦点样式：蓝色背景，白色字体（确保高亮明显）
    lv_style_init(&style_focus);
    lv_style_set_bg_color(&style_focus, lv_color_hex(0x1976D2));
    lv_style_set_text_color(&style_focus, lv_color_hex(0xFFFFFF));
    lv_style_set_outline_width(&style_focus, 0);
    lv_style_set_border_width(&style_focus, 0);

    // 键盘按键焦点样式：橙色背景
    lv_style_init(&style_kb_focus);
    lv_style_set_bg_color(&style_kb_focus, lv_color_hex(0xFF9800));
    lv_style_set_text_color(&style_kb_focus, lv_color_hex(0xFFFFFF));
}

// ======================= 关闭密码页面 =======================
static void close_password_page(void) {
    if (pwd_container != NULL) {
        lv_obj_del(pwd_container);
        pwd_container = NULL;
    }
    lv_obj_clear_flag(header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
    if (last_focused_btn != NULL) {
        lv_group_focus_obj(last_focused_btn);
        lv_obj_scroll_to_view(last_focused_btn, LV_ANIM_ON);
    }
}

// ======================= 打开密码输入页面 =======================
static void open_password_page(int ap_index) {
    lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);

    pwd_container = lv_obj_create(main_screen);
    lv_obj_set_size(pwd_container, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(pwd_container, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(pwd_container, 0, 0);
    lv_obj_set_style_pad_all(pwd_container, 0, 0);

    // 标题
    lv_obj_t *title = lv_label_create(pwd_container);
    lv_label_set_text_fmt(title, "连接: %s", g_ap_records[ap_index].ssid);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title, lv_color_hex(0x000000), 0);

    // 密码输入框
    lv_obj_t *ta = lv_textarea_create(pwd_container);
    lv_obj_set_size(ta, LCD_WIDTH - 20, 40);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 45);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "请输入密码");
    lv_obj_set_style_border_color(ta, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_textarea_set_max_length(ta, 63);

    // 键盘
    lv_obj_t *kb = lv_keyboard_create(pwd_container);
    lv_obj_set_size(kb, LCD_WIDTH, LCD_HEIGHT - 100);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xF0F0F0), 0);
    lv_obj_add_style(kb, &style_kb_focus, LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)ap_index);
    lv_group_add_obj(input_group, kb);
    lv_group_focus_obj(kb);
}

// ======================= 键盘事件回调 =======================
static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * kb = lv_event_get_target(e);
    int ap_index = (int)(uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_READY) {
        lv_obj_t * ta = lv_keyboard_get_textarea(kb);
        const char *pwd = lv_textarea_get_text(ta);
        strncpy(g_password, pwd, sizeof(g_password) - 1);
        ESP_LOGI(TAG, "准备连接: %s", g_ap_records[ap_index].ssid);
        // 启动连接任务
        xTaskCreate(connect_task, "wifi_conn", 4096, (void*)ap_index, 5, NULL);
        close_password_page();
    } else if (code == LV_EVENT_CANCEL) {
        close_password_page();
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            close_password_page();
        }
    }
}

// ======================= 列表按钮事件回调 =======================
static void list_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    int index = (int)(uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED) {
        // 确认键：进入密码页面
        last_focused_btn = btn;
        open_password_page(index);
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == LV_KEY_RIGHT) {
            // 右键：启动 SmartConfig 配网
            ESP_LOGI(TAG, "右键触发一键配网");
            xTaskCreate(smartconfig_task, "smartcfg", 4096, NULL, 5, NULL);
        }
        // 其他按键（上/下/左）由 LVGL 组导航自动处理
    }
}

// ======================= SmartConfig 任务 =======================
static void smartconfig_task(void *arg) {
    // 显示提示
    lv_obj_t *msg = lv_label_create(lv_scr_act());
    lv_label_set_text(msg, "SmartConfig 配网中...\n请使用 ESPTouch 发送密码");
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_bg_color(msg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(msg, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(msg, 20, 0);
    lv_obj_set_style_radius(msg, 8, 0);

    esp_err_t ret = wifi_smartconfig_start(60000); // 60秒超时
    lv_obj_del(msg);

    if (ret == ESP_OK) {
        // 配网成功，刷新列表
        lv_label_set_text(lv_obj_get_child(header, 0), LV_SYMBOL_WIFI "  已连接");
        wifi_scan_and_update_list();
        // 简单重建 UI（可优化为局部刷新，此处重建整个界面）
        lv_obj_clean(lv_scr_act());
        ui_init();
    } else {
        lv_obj_t *fail = lv_label_create(lv_scr_act());
        lv_label_set_text(fail, "配网失败/超时");
        lv_obj_align(fail, LV_ALIGN_CENTER, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
        lv_obj_del(fail);
    }
    vTaskDelete(NULL);
}

// ======================= WiFi 连接任务 =======================
static void connect_task(void *arg) {
    int ap_index = (int)arg;
    const char *ssid = (const char *)g_ap_records[ap_index].ssid;
    
    lv_obj_t *msg = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(msg, "正在连接 %s ...", ssid);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0x1976D2), 0);
    
    esp_err_t ret = wifi_connect_sta(ssid, g_password, 15000);
    lv_obj_del(msg);
    
    if (ret == ESP_OK) {
        lv_obj_t *ok = lv_label_create(lv_scr_act());
        lv_label_set_text(ok, "连接成功！");
        lv_obj_align(ok, LV_ALIGN_CENTER, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
        lv_obj_del(ok);
        // 更新标题
        lv_label_set_text_fmt(lv_obj_get_child(header, 0), LV_SYMBOL_WIFI "  %s", ssid);
    } else {
        lv_obj_t *err = lv_label_create(lv_scr_act());
        lv_label_set_text(err, "连接失败，请检查密码");
        lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
        lv_obj_del(err);
    }
    vTaskDelete(NULL);
}

// ======================= 主 UI 初始化 =======================
void ui_init(void) {
    ui_style_init();
    main_screen = lv_scr_act();
    lv_obj_add_style(main_screen, &style_screen, 0);

    // 创建输入设备组（启用循环导航）
    input_group = lv_group_get_default();
    if (input_group == NULL) {
        input_group = lv_group_create();
        lv_group_set_default(input_group);
    }
    lv_group_set_wrap(input_group, true); // 循环导航

    // 顶部标题栏
    header = lv_obj_create(main_screen);
    lv_obj_set_size(header, LCD_WIDTH, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(header, &style_card, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WiFi 列表");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1976D2), 0);

    // 列表容器（使用普通对象，不用 lv_list 以避免复杂的默认行为）
    wifi_list = lv_obj_create(main_screen);
    lv_obj_set_size(wifi_list, LCD_WIDTH - 10, LCD_HEIGHT - 55);
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(wifi_list, &style_card, 0);
    lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);   // 垂直排列
    lv_obj_set_flex_align(wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(wifi_list, 5, 0);
    lv_obj_set_style_pad_gap(wifi_list, 4, 0);  // 子项间距

    if (g_ap_count == 0) {
        lv_obj_t *no_data = lv_label_create(wifi_list);
        lv_label_set_text(no_data, "未发现网络\n按下右键启动 SmartConfig");
        lv_obj_set_style_text_align(no_data, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(no_data, LCD_WIDTH - 20);
    } else {
        lv_obj_t *first_btn = NULL;
        for (int i = 0; i < g_ap_count; i++) {
            const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) 
                               ? (char *)g_ap_records[i].ssid : "[隐藏]";
            int rssi = g_ap_records[i].rssi;

            char buf[64];
            snprintf(buf, sizeof(buf), "%s  (%d dBm)", ssid, rssi);

            lv_obj_t *btn = lv_btn_create(wifi_list);
            lv_obj_set_width(btn, LV_PCT(100));
            lv_obj_add_style(btn, &style_btn, 0);
            lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED);
            lv_obj_add_style(btn, &style_focus, LV_STATE_PRESSED);

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, buf);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_center(label);
            lv_obj_set_align(label, LV_ALIGN_LEFT_MID);
            lv_obj_set_style_pad_left(label, 10, 0);

            lv_obj_add_event_cb(btn, list_btn_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)i);
            lv_group_add_obj(input_group, btn);

            if (first_btn == NULL) first_btn = btn;
        }
        if (first_btn != NULL) {
            lv_group_focus_obj(first_btn);
            lv_obj_scroll_to_view(first_btn, LV_ANIM_OFF);
        }
    }

    // 底部提示栏
    lv_obj_t *hint = lv_label_create(main_screen);
    lv_label_set_text(hint, LV_SYMBOL_UP LV_SYMBOL_DOWN "选择  " LV_SYMBOL_RIGHT "配网  " LV_SYMBOL_OK "连接");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
}