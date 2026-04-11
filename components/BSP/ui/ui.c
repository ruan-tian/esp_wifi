/**
 * @file ui.c
 * @brief 阶段一：WiFi 列表显示 (暗黑模式字体护眼 + 现代高亮线框)
 */

 #include "ui.h"
 #include "lvgl.h"
 #include "wifi_scanner.h"
 #include "esp_log.h"
 #include "lcd.h"       // 获取 LCD_WIDTH 和 LCD_HEIGHT 宏
 
 static const char *TAG = "UI_PHASE_1_DARK";
 
 LV_FONT_DECLARE(my_font_chinese_18); // 确保你的 18 号中文字体已注册
 
 // ======================= 全局变量 =======================
 static lv_group_t * input_group = NULL;
 
 // 样式定义
 static lv_style_t style_screen;
 static lv_style_t style_btn;
 static lv_style_t style_focus;
 
 // ======================= 1. 样式初始化 =======================
 static void ui_style_init(void) {
     // 1. 【保留老代码的清晰护眼】屏幕基础样式：极深灰背景，亮灰色文字
     lv_style_init(&style_screen);
     lv_style_set_bg_color(&style_screen, lv_color_hex(0x121212)); 
     lv_style_set_text_font(&style_screen, &my_font_chinese_18);
     lv_style_set_text_color(&style_screen, lv_color_hex(0xE0E0E0)); 
 
     // 2. 列表按钮默认样式：透明背景，无边框
     lv_style_init(&style_btn);
     lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP); 
     lv_style_set_border_width(&style_btn, 0);       
     lv_style_set_pad_all(&style_btn, 10);           
     lv_style_set_text_color(&style_btn, lv_color_hex(0xE0E0E0));
 
     // 3. 【你想要的新高亮框】焦点样式：深色科技蓝背景 + 亮蓝色轮廓线
     lv_style_init(&style_focus);
     lv_style_set_bg_opa(&style_focus, LV_OPA_COVER);
     lv_style_set_bg_color(&style_focus, lv_color_hex(0x1E293B)); // 深海蓝背景，比全局背景稍微亮一点
     lv_style_set_text_color(&style_focus, lv_color_hex(0x38BDF8)); // 选中时文字变亮蓝色
     lv_style_set_outline_width(&style_focus, 2);                   // 保留 2 像素的高亮轮廓线
     lv_style_set_outline_color(&style_focus, lv_color_hex(0x0EA5E9)); // 亮蓝色轮廓
     lv_style_set_outline_pad(&style_focus, 2);                     // 框与按钮的间隙
     lv_style_set_radius(&style_focus, 6);                          // 圆角
 }
 
 // ======================= 2. 页面构建 =======================
 static void build_wifi_list_page(void) {
     lv_obj_t * scr = lv_scr_act();
     lv_obj_add_style(scr, &style_screen, 0);
 
     // ------------- 1. 顶部头部区 -------------
     // 创建头部文字
     lv_obj_t * header_label = lv_label_create(scr);
     lv_label_set_text(header_label, LV_SYMBOL_WIFI "  WiFi 连接");
     lv_obj_align(header_label, LV_ALIGN_TOP_MID, 0, 10); 
     // 标题文字加亮一点，用纯白或者偏绿的颜色点缀
     lv_obj_set_style_text_color(header_label, lv_color_hex(0x4CAF50), 0); 
 
     // 创建普通色块充当分割线（解决没开启 lv_line_create 的报错问题）
     lv_obj_t * line = lv_obj_create(scr);
     lv_obj_set_size(line, LCD_WIDTH - 30, 2); // 宽度比屏幕略窄，高度2像素
     lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 35); 
     lv_obj_set_style_bg_color(line, lv_color_hex(0x333333), 0); // 暗黑模式下的深灰分割线
     lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
     lv_obj_set_style_border_width(line, 0, 0);
     lv_obj_set_style_radius(line, 0, 0);
     lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
 
     // ------------- 2. 列表容器区 -------------
     lv_obj_t * list_cont = lv_obj_create(scr);
     lv_obj_set_size(list_cont, LCD_WIDTH - 10, LCD_HEIGHT - 45); 
     lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
     
     // 设置为弹性布局 (Flex)，实现垂直排列
     lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
     // 去除容器背景和边框，融入暗黑环境
     lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
     lv_obj_set_style_border_width(list_cont, 0, 0);
     lv_obj_set_style_pad_all(list_cont, 5, 0);
 
     // ------------- 3. 填充 WiFi 数据 -------------
     lv_obj_t * first_btn = NULL; 
 
     if (g_ap_count == 0) {
         lv_obj_t * empty_label = lv_label_create(list_cont);
         lv_label_set_text(empty_label, "未扫描到网络");
         lv_obj_center(empty_label);
     } else {
         for (int i = 0; i < g_ap_count; i++) {
             const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) ? (char *)g_ap_records[i].ssid : "[隐藏网络]";
             
             char buf[64];
             snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", ssid);
 
             lv_obj_t * btn = lv_btn_create(list_cont);
             lv_obj_set_width(btn, LV_PCT(100)); 
             lv_obj_add_style(btn, &style_btn, 0); 
             lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED); // 触发焦点样式
 
             lv_obj_t * label = lv_label_create(btn);
             lv_label_set_text(label, buf);
             lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0); 
 
             // 将按钮加入按键焦点组
             lv_group_add_obj(input_group, btn);
 
             if (first_btn == NULL) first_btn = btn; 
         }
     }
 
     if (first_btn != NULL) {
         lv_group_focus_obj(first_btn);
     }
 }
 
 // ======================= 3. 初始化入口 =======================
 void ui_init(void) {
     input_group = lv_group_get_default();
     if (!input_group) {
         input_group = lv_group_create();
         lv_group_set_default(input_group);
     }
     lv_group_set_wrap(input_group, true); 
 
     ui_style_init();
     build_wifi_list_page();
 }