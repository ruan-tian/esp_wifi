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
#include "lv_port_indev.h" // 如果你的按键还没修好，可以暂时不用管它

static const char *TAG = "MAIN_UI";

// 声明你的 18 号抗锯齿中文字体
LV_FONT_DECLARE(my_font_chinese_18);

// UI 核心对象
static lv_obj_t * main_screen;
static lv_obj_t * wifi_list;

// 全局样式定义
static lv_style_t style_screen; // 屏幕全局背景
static lv_style_t style_card;   // 卡片背景
static lv_style_t style_btn;    // 列表按钮默认样式
static lv_style_t style_focus;  // 列表按钮选中样式

// -------------------------------------------------------------------------
// 1. 初始化高级现代 UI 样式
// -------------------------------------------------------------------------
static void ui_style_init(void) {
    // 【屏幕全局样式】暗黑背景，全局默认中文字体
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0x121212)); // 极深灰背景 (RGB565自动转换)
    lv_style_set_text_font(&style_screen, &my_font_chinese_18);  // 全局抗锯齿中文
    lv_style_set_text_color(&style_screen, lv_color_hex(0xE0E0E0)); // 亮灰色文字，不刺眼

    // 【卡片样式】用于顶部状态栏和中间的列表容器
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x1E1E1E)); // 比背景稍亮的深灰
    lv_style_set_border_width(&style_card, 0);                  // 移除所有丑陋的默认边框
    lv_style_set_radius(&style_card, 8);                        // 现代感圆角

    // 【列表按钮样式】
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x1E1E1E)); // 按钮背景和卡片融为一体
    lv_style_set_border_width(&style_btn, 0);
    lv_style_set_pad_all(&style_btn, 12);                      // 增加内边距，让列表更舒展

    // 【焦点样式】物理按键选中时的蓝色高亮（彻底告别虚线框）
    lv_style_init(&style_focus);
    lv_style_set_bg_color(&style_focus, lv_color_hex(0x2196F3)); // 科技感亮蓝色
    lv_style_set_text_color(&style_focus, lv_color_hex(0xFFFFFF)); // 选中时文字变纯白
    lv_style_set_outline_width(&style_focus, 0);                 // 去掉外轮廓虚线
}

// -------------------------------------------------------------------------
// 2. 点击列表项的回调函数
// -------------------------------------------------------------------------
static void wifi_list_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // 获取点击的 WiFi 索引
        int index = (int)(uintptr_t)lv_event_get_user_data(e);
        ESP_LOGI(TAG, "用户选择了: %s", g_ap_records[index].ssid);
        
        // 此处可以接入你之前的密码键盘页面代码
        // show_password_keyboard((const char *)g_ap_records[index].ssid);
    }
}

// -------------------------------------------------------------------------
// 3. 构建现代仪表盘 UI
// -------------------------------------------------------------------------
static void build_dashboard_ui(void) {
    main_screen = lv_scr_act();
    lv_obj_add_style(main_screen, &style_screen, 0);

    // --- 顶部状态栏 (Header) ---
    lv_obj_t * header = lv_obj_create(main_screen);
    lv_obj_set_size(header, LCD_WIDTH, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(header, &style_card, 0);
    lv_obj_set_style_radius(header, 0, 0); // 顶部栏不需要圆角
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    lv_obj_t * title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_WIFI " WiFi 扫描终端"); // 图标+中文混排
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4CAF50), 0); // 标题文字用点缀的绿色

    // --- 中间列表卡片 ---
    wifi_list = lv_list_create(main_screen);
    lv_obj_set_size(wifi_list, LCD_WIDTH - 16, LCD_HEIGHT - 56); // 留出边缘间距
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_style(wifi_list, &style_card, 0);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF); // 隐藏难看的滚动条

    // --- 动态填充扫描到的 WiFi ---
    if (g_ap_count == 0) {
        lv_obj_t * no_data = lv_label_create(wifi_list);
        lv_label_set_text(no_data, "未发现周围网络");
        lv_obj_center(no_data);
    } else {
        lv_obj_t * first_btn = NULL; 

        for(int i = 0; i < g_ap_count; i++) {
            // 处理隐藏网络和名称长度
            const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) ? (char *)g_ap_records[i].ssid : "[隐藏网络]";
            
            // 组装带信号强度的字符串 (例如: "帅哥协会 [-45dBm]")
            char buf[64];
            snprintf(buf, sizeof(buf), "%s [%d]", ssid, g_ap_records[i].rssi);
            
            // 创建列表项
            lv_obj_t * btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, buf);
            lv_obj_add_style(btn, &style_btn, 0); // 添加基础样式
            lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED); // 添加选中时的高亮样式
            
            // 绑定点击事件和用户数据(索引)
            lv_obj_add_event_cb(btn, wifi_list_event_cb, LV_EVENT_ALL, (void*)(uintptr_t)i);
            
            // 将按钮加入按键控制组 (支持物理按键上下移动)
            lv_group_add_obj(lv_group_get_default(), btn);

            if(first_btn == NULL) first_btn = btn;
        }

        // 默认让物理焦点落在第一个按钮上
        if(first_btn != NULL) {
            lv_group_focus_obj(first_btn);
        }
    }
}

// -------------------------------------------------------------------------
// 4. 主程序入口
// -------------------------------------------------------------------------
void app_main(void) {
    // 1. 基础系统初始化
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    // 2. 底层驱动和 LVGL 桥接初始化
    lcd_init();
    lv_port_disp_init();
    
    // 【注意】如果你的物理按键连线还没修复导致疯狂乱跳，请注释掉下面这行：
    // lv_port_indev_init(); 

    // 3. 执行 WiFi 扫描 (在无 UI 状态下先拿到数据)
    ESP_LOGI(TAG, "正在扫描周围的 WiFi 网络...");
    wifi_scanner_init(); 
    wifi_scan_and_update_list(); 

    // 4. 初始化 UI 样式并构建界面
    ui_style_init();
    build_dashboard_ui();

    // 5. 挂载 LVGL 心跳任务
    xTaskCreate(lvgl_port_task, "lvgl_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "UI 渲染完成，系统就绪！");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // 挂起主任务，将算力全部交给 LVGL 任务
    }
}