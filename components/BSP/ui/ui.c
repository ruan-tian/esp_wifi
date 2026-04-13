/**
 * @file ui.c
 * @brief 阶段三：清新亮色主题 + 密码键盘输入 + 智能回退与连接反馈
 */

 #include "ui.h"
 #include "lvgl.h"
 #include "wifi_scanner.h"
 #include "wifi_connector.h"
 #include "esp_log.h"
 #include "lcd.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include <string.h>
 
 static const char *TAG = "UI_PHASE_3_LIGHT";
 
 LV_FONT_DECLARE(my_font_chinese_18);
 
 // 声明生成的 5 个 WiFi 图片数组
 LV_IMG_DECLARE(WIFI);   
 LV_IMG_DECLARE(WIFI1);  
 LV_IMG_DECLARE(WIFI2);  
 LV_IMG_DECLARE(WIFI3);  
 LV_IMG_DECLARE(WIFI4);  
 
 // ======================= 全局变量 =======================
 static lv_group_t * input_group = NULL;
 static char g_password[64] = {0};
 
 // 扩充状态机
 typedef enum {
     STATE_WIFI_LIST,        
     STATE_SCANNING,         
     STATE_PASSWORD_INPUT,   // 密码输入页
     STATE_CONNECTING,       // 连接中
     STATE_CONNECT_RESULT    // 连接结果页 (成功/失败)
 } menu_state_t;
 
 static menu_state_t current_state = STATE_WIFI_LIST;
 
 static lv_style_t style_screen, style_card, style_btn, style_focus, style_kb_focus;
 
 // ======================= 5x10 定制键盘 =======================
 static const char * kb_map_custom[] = {
     "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
     "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
     "a", "s", "d", "f", "g", "h", "j", "k", "l", ",", "\n",
     "z", "x", "c", "v", "b", "n", "m", ".", "?", "!", "\n",
     LV_SYMBOL_BACKSPACE, " ", LV_SYMBOL_OK, ""
 };
 static const lv_btnmatrix_ctrl_t kb_ctrl_custom[] = {
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     2, 6, 2 
 };
 
 // ======================= 前置声明 =======================
 static void enter_state(menu_state_t new_state, int param);
 static void build_wifi_list_page(void);
 static void build_password_page(int ap_index);
 static void build_loading_page(const char * title_text, const char * sub_text, lv_color_t color);
 static void list_btn_event_cb(lv_event_t * e);
 static void keyboard_event_cb(lv_event_t * e);
 
 // ======================= 1. 亮色样式初始化 =======================
 static void ui_style_init(void) {
     // 1. 屏幕基础：白底，黑字
     lv_style_init(&style_screen);
     lv_style_set_bg_color(&style_screen, lv_color_hex(0xFFFFFF)); 
     lv_style_set_text_font(&style_screen, &my_font_chinese_18);
     lv_style_set_text_color(&style_screen, lv_color_hex(0x111111)); 

     // 2. 卡片与顶栏：白底，浅灰边框
     lv_style_init(&style_card);
     lv_style_set_bg_color(&style_card, lv_color_hex(0xFFFFFF)); 
     lv_style_set_border_width(&style_card, 1);                  
     lv_style_set_border_color(&style_card, lv_color_hex(0xE5E7EB)); 
     lv_style_set_radius(&style_card, 8);                        

     // 3. 列表按钮：白底，无边框
     lv_style_init(&style_btn);
     lv_style_set_bg_opa(&style_btn, LV_OPA_COVER); 
     lv_style_set_bg_color(&style_btn, lv_color_hex(0xFFFFFF)); 
     lv_style_set_border_width(&style_btn, 0);       
     lv_style_set_pad_all(&style_btn, 12);           
     lv_style_set_text_color(&style_btn, lv_color_hex(0x333333)); 

     // 4. 高亮焦点框：灰色系主题 (修改点)
     lv_style_init(&style_focus);
     lv_style_set_bg_opa(&style_focus, LV_OPA_COVER);
     lv_style_set_bg_color(&style_focus, lv_color_hex(0xF3F4F6)); // 浅灰背景
     lv_style_set_text_color(&style_focus, lv_color_hex(0x374151)); // 深灰文字
     lv_style_set_outline_width(&style_focus, 2);                   
     lv_style_set_outline_color(&style_focus, lv_color_hex(0x9CA3AF)); // 中灰边框
     lv_style_set_outline_pad(&style_focus, 1);                     
     lv_style_set_radius(&style_focus, 6);                          

     // 5. 键盘按键高亮：灰色反馈 (修改点)
     lv_style_init(&style_kb_focus);
     lv_style_set_bg_color(&style_kb_focus, lv_color_hex(0xD1D5DB)); // 中灰背景
     lv_style_set_text_color(&style_kb_focus, lv_color_hex(0x111827)); // 近黑文字
     lv_style_set_outline_width(&style_kb_focus, 2);
     lv_style_set_outline_color(&style_kb_focus, lv_color_hex(0x6B7280)); // 深灰边框
}

 // ======================= 2. 后台异步任务 =======================
 static void scan_done_cb(void * arg) {
     enter_state(STATE_WIFI_LIST, -1);
 }
 
 static void scan_task(void *arg) {
     wifi_scan_and_update_list();
     lv_async_call(scan_done_cb, NULL);
     vTaskDelete(NULL);
 }
 
 static void connect_result_cb(void * arg) {
     int success = (int)(uintptr_t)arg;
     enter_state(STATE_CONNECT_RESULT, success);
 }
 
 static void connect_task(void *arg) {
     int ap_index = (int)(uintptr_t)arg;
     // 调用底层连接逻辑（阻塞等待15秒）
     esp_err_t ret = wifi_connect_sta((const char *)g_ap_records[ap_index].ssid, g_password, 15000);
     
     // 安全地通知 UI 连接结果
     lv_async_call(connect_result_cb, (void*)(uintptr_t)(ret == ESP_OK));
     vTaskDelete(NULL);
 }
 
 // 结果页面展示完毕后的延迟返回任务
 static void return_list_task(void *arg) {
     vTaskDelay(pdMS_TO_TICKS(2000)); // 停顿2秒让用户看清结果
     lv_async_call(scan_done_cb, NULL); // 复用回调切回列表
     vTaskDelete(NULL);
 }
 
 // ======================= 3. 状态调度器 =======================
 static void enter_state(menu_state_t new_state, int param) {
     current_state = new_state;
     lv_obj_clean(lv_scr_act()); 
     if (input_group != NULL) lv_group_remove_all_objs(input_group);
 
     switch (current_state) {
         case STATE_WIFI_LIST:
             build_wifi_list_page();
             break;
         case STATE_SCANNING:
             build_loading_page("扫描网络", "正在寻找附近的 WiFi...", lv_color_hex(0x0EA5E9));
             xTaskCreate(scan_task, "scan", 4096, NULL, 5, NULL);
             break;
         case STATE_PASSWORD_INPUT:
             build_password_page(param);
             break;
         case STATE_CONNECTING:
             build_loading_page("正在连接网络", "设备配网中，请稍候...", lv_color_hex(0x0EA5E9));
             xTaskCreate(connect_task, "conn", 4096, (void*)(uintptr_t)param, 5, NULL);
             break;
         case STATE_CONNECT_RESULT:
             if (param == 1) {
                 build_loading_page("连接成功!", "已接入互联网", lv_color_hex(0x4CAF50)); // 绿色成功
             } else {
                 build_loading_page("连接失败", "密码错误或信号差", lv_color_hex(0xF44336)); // 红色失败
             }
             xTaskCreate(return_list_task, "ret", 2048, NULL, 5, NULL);
             break;
     }
 }
 
 // ======================= 4. 页面构建 =======================
 
 // 通用加载/结果页
 static void build_loading_page(const char * title_text, const char * sub_text, lv_color_t color) {
     lv_obj_t * scr = lv_scr_act();
     lv_obj_add_style(scr, &style_screen, 0);
 
     lv_obj_t * title = lv_label_create(scr);
     lv_label_set_text(title, title_text);
     lv_obj_center(title);
     lv_obj_set_style_text_color(title, color, 0);
 
     lv_obj_t * sub = lv_label_create(scr);
     lv_label_set_text(sub, sub_text);
     lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
     lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
 }
 
 // 核心列表页
 static void build_wifi_list_page(void) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_add_style(scr, &style_screen, 0);

    // --- 顶部状态栏 ---
    lv_obj_t * header_label = lv_label_create(scr);
    lv_label_set_text(header_label, "WiFi 网络连接"); 
    lv_obj_align(header_label, LV_ALIGN_TOP_MID, 0, 10); 
    lv_obj_set_style_text_color(header_label, lv_color_hex(0x111111), 0); 

    lv_obj_t * line = lv_obj_create(scr);
    lv_obj_set_size(line, LCD_WIDTH - 30, 2); 
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 35); 
    lv_obj_set_style_bg_color(line, lv_color_hex(0xEEEEEE), 0); // 浅灰分割线
    lv_obj_set_style_border_width(line, 0, 0);

    // --- 列表容器 ---
    lv_obj_t * wifi_list = lv_list_create(scr);
    lv_obj_set_size(wifi_list, LCD_WIDTH - 16, LCD_HEIGHT - 45); 
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(wifi_list, &style_card, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF);

    if (g_ap_count == 0) {
        lv_obj_t * empty_btn = lv_btn_create(wifi_list);
        lv_obj_set_width(empty_btn, LV_PCT(100));
        lv_obj_add_style(empty_btn, &style_btn, 0);
        lv_obj_add_style(empty_btn, &style_focus, LV_STATE_FOCUSED);
        
        lv_obj_t * empty_label = lv_label_create(empty_btn);
        lv_label_set_text(empty_label, "未发现网络，按 [右键] 刷新");
        lv_obj_center(empty_label);

        lv_obj_add_event_cb(empty_btn, list_btn_event_cb, LV_EVENT_ALL, (void*)-1);
        lv_group_add_obj(input_group, empty_btn);
        lv_group_focus_obj(empty_btn);
    } else {
        lv_obj_t * first_btn = NULL; 

        for (int i = 0; i < g_ap_count; i++) {
            const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) ? (char *)g_ap_records[i].ssid : "[隐藏网络]";
            int rssi = g_ap_records[i].rssi;

            const void * src_img;
            if (rssi >= -55)      src_img = &WIFI4;
            else if (rssi >= -70) src_img = &WIFI3;
            else if (rssi >= -80) src_img = &WIFI2;
            else if (rssi >= -90) src_img = &WIFI1;
            else                  src_img = &WIFI;

            lv_obj_t * btn = lv_list_add_btn(wifi_list, src_img, ssid);
            lv_obj_add_style(btn, &style_btn, 0); 
            lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED); 

            // 【黑科技】：强制把图片当蒙版染色！防止白色图标在白底上隐形
            lv_obj_t * icon = lv_obj_get_child(btn, 0); 
            lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, LV_STATE_FOCUSED);
            // 默认深灰色，选中时变为黑色/深灰，配合灰色主题
            lv_obj_set_style_img_recolor(icon, lv_color_hex(0x6B7280), LV_STATE_DEFAULT); 
            lv_obj_set_style_img_recolor(icon, lv_color_hex(0x111827), LV_STATE_FOCUSED);

            lv_obj_t * name_label = lv_obj_get_child(btn, 1); 
            lv_obj_set_style_pad_left(name_label, 12, 0); 

            lv_obj_add_event_cb(btn, list_btn_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)i);
            lv_group_add_obj(input_group, btn);
            if (first_btn == NULL) first_btn = btn; 
        }
        if (first_btn != NULL) lv_group_focus_obj(first_btn);
    }
}

 // 密码输入键盘页
 static void build_password_page(int ap_index) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_add_style(scr, &style_screen, 0);

    // 顶部标题
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text_fmt(title, "连接: %s", g_ap_records[ap_index].ssid);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    // 标题颜色改为深灰，保持整体黑白灰风格
    lv_obj_set_style_text_color(title, lv_color_hex(0x374151), 0);

    // 输入框
    lv_obj_t * ta = lv_textarea_create(scr);
    lv_obj_set_size(ta, LCD_WIDTH - 20, 40);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 35);
    lv_textarea_set_password_mode(ta, true); // 密码星号隐藏
    lv_textarea_set_one_line(ta, true);
    // 强制清除光标，因为我们靠键盘输入，不需要屏幕光标闪烁
    // 边框颜色改为灰色系
    lv_obj_set_style_border_color(ta, lv_color_hex(0x9CA3AF), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);

    // 虚拟键盘
    lv_obj_t * kb = lv_keyboard_create(scr);
    lv_obj_set_size(kb, LCD_WIDTH, LCD_HEIGHT - 80);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    
    // 应用定制布局与样式
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_custom, kb_ctrl_custom);
    lv_obj_add_style(kb, &style_kb_focus, LV_PART_ITEMS | LV_STATE_FOCUSED);
    
    // 将键盘绑定事件并加入焦点组
    lv_obj_add_event_cb(kb, keyboard_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)ap_index);
    lv_group_add_obj(input_group, kb);
    lv_group_focus_obj(kb); // 焦点直接给键盘
}

 // ======================= 5. 事件回调与按键逻辑 =======================
 
 static void list_btn_event_cb(lv_event_t * e) {
     lv_event_code_t code = lv_event_get_code(e);
     
     // 短按 ENTER 进入密码页
     if (code == LV_EVENT_CLICKED) {
         int index = (int)(uintptr_t)lv_event_get_user_data(e);
         if (index >= 0) {
             enter_state(STATE_PASSWORD_INPUT, index);
         }
     }
     // 短按右键刷新
     else if (code == LV_EVENT_KEY) {
         uint32_t key = *((uint32_t *)lv_event_get_param(e)); 
         if (key == LV_KEY_RIGHT) {
             enter_state(STATE_SCANNING, -1); 
         }
     }
 }
 
 static void keyboard_event_cb(lv_event_t * e) {
     lv_event_code_t code = lv_event_get_code(e);
     lv_obj_t * kb = lv_event_get_target(e);
     int ap_index = (int)(uintptr_t)lv_event_get_user_data(e);

     // 1. 屏幕键盘按下了 OK 键
     if (code == LV_EVENT_READY) { 
         lv_obj_t * ta = lv_keyboard_get_textarea(kb);
         strncpy(g_password, lv_textarea_get_text(ta), sizeof(g_password) - 1);
         enter_state(STATE_CONNECTING, ap_index);
     } 
     // 2. 智能拦截物理 BACK 键 (LV_KEY_ESC)
     else if (code == LV_EVENT_KEY) {
         uint32_t key = *((uint32_t *)lv_event_get_param(e));
         if (key == LV_KEY_ESC || key == LV_KEY_LEFT) { 
             lv_obj_t * ta = lv_keyboard_get_textarea(kb);
             const char * txt = lv_textarea_get_text(ta);
             
             // 如果框里有密码，物理 BACK 键作为“退格删除”使用
             if (strlen(txt) > 0) {
                 lv_textarea_del_char(ta);
             } 
             // 如果密码已经删空了，物理 BACK 键作为“退出当前页”使用
             else {
                 enter_state(STATE_WIFI_LIST, -1);
             }
         }
     }
 }
 
 // ======================= 6. 初始化入口 =======================
 void ui_init(void) {
     input_group = lv_group_get_default();
     if (!input_group) {
         input_group = lv_group_create();
         lv_group_set_default(input_group);
     }
     lv_group_set_wrap(input_group, true); 
 
     ui_style_init();
     
     enter_state(STATE_WIFI_LIST, -1);
 }