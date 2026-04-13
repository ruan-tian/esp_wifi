/**
 * @file ui.c
 * @brief 阶段一：WiFi 列表显示 (暗黑模式字体护眼 + 现代高亮线框)
 * 
 * @details 本模块实现了一个基于LVGL的WiFi网络列表界面，具有以下特点：
 *          - 暗黑模式设计，降低屏幕亮度对眼睛的刺激
 *          - 支持物理按键导航（上下键切换焦点）
 *          - 自动从wifi_scanner模块获取扫描结果并显示
 *          - 使用弹性布局(Flex)实现自适应滚动列表
 */

 #include "ui.h"
 #include "lvgl.h"
 #include "wifi_scanner.h"
 #include "esp_log.h"
 #include "lcd.h"       // 获取 LCD_WIDTH 和 LCD_HEIGHT 宏
 
 static const char *TAG = "UI_PHASE_1_DARK";  // ESP-IDF日志标签
 
 // 声明外部字体资源
 // LV_FONT_DECLARE是一个宏，用于引用在lv_conf.h或其他地方定义的字体
 // my_font_chinese_18应该是一个18号中文字体，需要在字体转换器中生成
 LV_FONT_DECLARE(my_font_chinese_18); 
 
 // ======================= 全局变量定义 =======================
 
 /**
  * @brief 输入焦点组指针
  * @details lv_group_t是LVGL的核心结构体之一，用于管理一组可聚焦的对象
  * 
  * 【lv_group_t结构体详解】
  * 作用：将多个UI对象组织成一个逻辑组，实现键盘/按键导航
  * 
  * 核心功能：
  * 1. 焦点管理：组内只有一个对象处于"焦点"状态
  * 2. 按键路由：将按键事件（上/下/确认/返回）分发给焦点对象
  * 3. 循环导航：支持wrap模式，到达末尾后回到开头
  * 
  * 内部主要成员（简化版）：
  * - obj_ll: 链表，存储组内所有对象的指针
  * - obj_focus: 当前获得焦点的对象指针
  * - focus_cb: 焦点变化时的回调函数
  * - wrap: 是否启用循环导航的标志
  * 
  * 使用场景：
  * - 菜单系统：上下键切换菜单项
  * - 表单填写：Tab键切换输入框
  * - 列表浏览：滚动查看大量数据
  */
 static lv_group_t * input_group = NULL;
 
 // ======================= 样式对象定义 =======================
 
 /**
  * @brief 屏幕基础样式
  * @details lv_style_t是LVGL的样式结构体，类似于CSS中的样式规则
  * 
  * 【lv_style_t结构体详解】
  * 作用：封装UI对象的外观属性集合
  * 
  * 设计理念：
  * - 样式与对象分离：一个样式可以被多个对象复用
  * - 状态机机制：可以为不同状态（默认、按下、聚焦等）设置不同样式
  * - 继承机制：子对象可以继承父对象的样式
  * 
  * 常见可配置属性：
  * - 背景：颜色(bg_color)、透明度(bg_opa)、渐变(bg_grad)
  * - 边框：宽度(border_width)、颜色(border_color)、圆角(radius)
  * - 轮廓：宽度(outline_width)、颜色(outline_color)、偏移(outline_pad)
  * - 阴影：宽度(Shadow_width)、颜色(shadow_color)、偏移(shadow_ofs_x/y)
  * - 文本：颜色(text_color)、字体(text_font)、对齐(text_align)
  * - 内边距：pad_top/bottom/left/right/all
  * 
  * 内存优化：
  * - 样式对象通常定义为static或全局变量，避免重复创建
  * - 使用lv_style_init()初始化后，可以多次set不同属性
  */
 static lv_style_t style_screen;   // 屏幕整体样式（背景色、字体等）
 
 /**
  * @brief 按钮默认样式
  * @details 定义WiFi列表项在未选中状态下的外观
  */
 static lv_style_t style_btn;      // 列表按钮的基础样式
 
 /**
  * @brief 焦点高亮样式
  * @details 定义按钮被选中（获得焦点）时的高亮效果
  */
 static lv_style_t style_focus;    // 焦点状态的样式（蓝色高亮框）
 
 // ======================= 1. 样式初始化函数 =======================
 
 /**
  * @brief 初始化所有UI样式
  * @details 配置三种样式的视觉效果，实现暗黑模式设计
  * 
  * @note 样式初始化只需执行一次，通常在应用启动时调用
  *       后续通过lv_obj_add_style()将样式应用到具体对象
  */
 static void ui_style_init(void) {
     
     // ------------------------------------------------------------------
     // 样式1：屏幕基础样式 - 极深灰背景 + 亮灰色文字（护眼设计）
     // ------------------------------------------------------------------
     lv_style_init(&style_screen);  // 初始化样式结构体，清零所有属性
     
     // 设置背景颜色为极深灰色 (#121212)
     // 选择理由：比纯黑(#000000)更柔和，减少OLED屏幕烧屏风险
     // 同时保持足够的对比度，适合长时间阅读
     lv_style_set_bg_color(&style_screen, lv_color_hex(0x121212)); 
     
     // 设置全局文本字体为18号中文字体
     // 注意：字体必须在编译前通过lv_font_conv工具生成
     lv_style_set_text_font(&style_screen, &my_font_chinese_18);
     
     // 设置文本颜色为亮灰色 (#E0E0E0)
     // 在深色背景上提供舒适的阅读体验，避免纯白刺眼
     lv_style_set_text_color(&style_screen, lv_color_hex(0xE0E0E0)); 
 
     // ------------------------------------------------------------------
     // 样式2：列表按钮默认样式 - 透明背景 + 无边框
     // ------------------------------------------------------------------
     lv_style_init(&style_btn);
     
     // 设置背景透明度为完全透明
     // LV_OPA_TRANSP = 0，表示完全透明
     // LV_OPA_COVER = 255，表示完全不透明
     lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP); 
     
     // 设置边框宽度为0，即不显示边框
     // 未选中状态下保持简洁，突出内容而非装饰
     lv_style_set_border_width(&style_btn, 0);       
     
     // 设置内边距（padding）为10像素
     // pad_all同时设置上下左右四个方向的内边距
     // 让按钮内容与边缘保持距离，提升点击区域的可识别性
     lv_style_set_pad_all(&style_btn, 10);           
     
     // 设置文本颜色与屏幕一致
     lv_style_set_text_color(&style_btn, lv_color_hex(0xE0E0E0));
 
     // ------------------------------------------------------------------
     // 样式3：焦点高亮样式 - 深海蓝背景 + 亮蓝色轮廓线（科技感）
     // ------------------------------------------------------------------
     lv_style_init(&style_focus);
     
     // 设置背景完全不透明（否则看不到背景色）
     lv_style_set_bg_opa(&style_focus, LV_OPA_COVER);
     
     // 设置背景颜色为深海蓝 (#1E293B)
     // 比全局背景稍亮，形成层次感
     // 这种颜色在现代UI设计中常用于卡片式设计的选中状态
     lv_style_set_bg_color(&style_focus, lv_color_hex(0x1E293B)); 
     
     // 设置选中时文字变为亮蓝色 (#38BDF8)
     // 通过颜色变化强化"已选中"的视觉反馈
     lv_style_set_text_color(&style_focus, lv_color_hex(0x38BDF8)); 
     
     // 设置轮廓线宽度为2像素
     // 轮廓线(outline)独立于边框(border)，不会占用布局空间
     lv_style_set_outline_width(&style_focus, 2);                   
     
     // 设置轮廓线颜色为亮蓝色 (#0EA5E9)
     // 与文字颜色形成呼应，统一视觉风格
     lv_style_set_outline_color(&style_focus, lv_color_hex(0x0EA5E9)); 
     
     // 设置轮廓线外扩2像素
     // outline_pad控制轮廓线与对象边缘的距离
     // 正值向外扩展，负值向内收缩
     lv_style_set_outline_pad(&style_focus, 2);                     
     
     // 设置圆角半径为6像素
     // 圆角设计让界面更加柔和，符合现代UI审美
     lv_style_set_radius(&style_focus, 6);                          
 }
 
 // ======================= 2. 页面构建函数 =======================
 
 /**
  * @brief 构建WiFi列表页面
  * @details 完整的UI层级结构：
  *          Screen (屏幕)
  *          ├── Header Label (标题文字)
  *          ├── Line Object (分割线)
  *          └── List Container (列表容器)
  *              ├── Button 1 (WiFi热点1)
  *              │   └── Label (SSID名称)
  *              ├── Button 2 (WiFi热点2)
  *              │   └── Label (SSID名称)
  *              └── ...
  * 
  * @note 此函数依赖全局变量g_ap_records和g_ap_count
  *       应在wifi_scan_and_update_list()之后调用
  */
 static void build_wifi_list_page(void) {
     
     // 获取当前活动屏幕对象
     // lv_scr_act()返回当前显示的屏幕指针
     // LVGL支持多屏幕切换，但本例只使用单屏幕
     lv_obj_t * scr = lv_scr_act();
     
     // 将屏幕样式应用到当前屏幕
     // 参数0表示应用到默认状态（LV_STATE_DEFAULT）
     lv_obj_add_style(scr, &style_screen, 0);
 
     // ==================================================================
     // 第一部分：顶部头部区（标题 + 分割线）
     // ==================================================================
     
     // --- 1.1 创建标题文字 ---
     /**
      * @brief lv_obj_t - LVGL通用对象结构体
      * 
      * 【lv_obj_t结构体详解】
      * 作用：LVGL中所有UI元素的基础类型（按钮、标签、容器等都是lv_obj_t）
      * 
      * 核心成员（简化版）：
      * - parent: 指向父对象的指针（形成树状结构）
      * - child_ll: 子对象链表
      * - coords: 对象的坐标和尺寸(x, y, width, height)
      * - style_list: 样式列表（可应用多个样式）
      * - group_p: 所属的焦点组指针
      * - flags: 标志位（如是否可点击、是否可见等）
      * - ext_attr: 扩展属性指针（用于存储自定义数据）
      * 
      * 对象树概念：
      * LVGL采用树形结构管理UI对象
      * - 根节点：Screen（屏幕）
      * - 中间节点：Container（容器）、Panel（面板）等
      * - 叶子节点：Button（按钮）、Label（标签）等
      * 
      * 渲染顺序：
      * 先创建的物体在下层，后创建的在上层（类似PS图层）
      */
     lv_obj_t * header_label = lv_label_create(scr);  // 在屏幕上创建标签对象
     
     // 设置标签文本
     // LV_SYMBOL_WIFI是LVGL内置的WiFi图标符号（Unicode字符）
     // 最终显示效果：📶 WiFi 连接
     lv_label_set_text(header_label, LV_SYMBOL_WIFI "  WiFi 连接");
     
     // 将标签对齐到屏幕顶部中央
     // 参数说明：
     // - LV_ALIGN_TOP_MID: 对齐锚点（顶部中间）
     // - 0: X轴偏移量（0表示居中）
     // - 10: Y轴偏移量（向下偏移10像素）
     lv_obj_align(header_label, LV_ALIGN_TOP_MID, 0, 10); 
     
     // 单独设置标题文字颜色为绿色 (#4CAF50)
     // 作为视觉点缀，突出主题
     // 参数0表示应用到默认状态
     lv_obj_set_style_text_color(header_label, lv_color_hex(0x4CAF50), 0); 
 
     // --- 1.2 创建分割线 ---
     // 由于lv_line_create可能需要额外配置，这里用扁平化的obj模拟分割线
     lv_obj_t * line = lv_obj_create(scr);  // 创建一个空容器对象
     
     // 设置分割线尺寸
     // LCD_WIDTH是lcd.h中定义的宏，表示屏幕宽度（如240或320）
     // 宽度比屏幕略窄30像素（左右各留15像素边距）
     // 高度设为2像素，形成细线效果
     lv_obj_set_size(line, LCD_WIDTH - 30, 2); 
     
     // 将分割线对齐到标题下方
     // Y轴偏移35像素（标题高度约25px + 间距10px）
     lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 35); 
     
     // 设置分割线背景颜色为深灰色 (#333333)
     // 在暗黑模式下提供微妙的分隔效果
     lv_obj_set_style_bg_color(line, lv_color_hex(0x333333), 0); 
     
     // 设置背景完全不透明（否则看不到颜色）
     lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
     
     // 移除边框（默认可能有1像素边框）
     lv_obj_set_style_border_width(line, 0, 0);
     
     // 设置圆角为0，形成直角矩形线条
     lv_obj_set_style_radius(line, 0, 0);
     
     // 清除可滚动标志
     // 防止这个细小的分割线意外捕获滚动手势
     lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
 
     // ==================================================================
     // 第二部分：列表容器区（Flex弹性布局）
     // ==================================================================
     
     // 创建列表容器
     lv_obj_t * list_cont = lv_obj_create(scr);
     
     // 设置容器尺寸
     // 宽度：屏幕宽度减去10像素（左右各5像素边距）
     // 高度：屏幕高度减去45像素（顶部标题区35px + 底部留白10px）
     lv_obj_set_size(list_cont, LCD_WIDTH - 10, LCD_HEIGHT - 45); 
     
     // 将容器对齐到屏幕底部中央
     // 这样即使列表内容很多，也能保证从顶部开始排列
     lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
     
     // 【关键】设置为弹性布局（Flex Layout）
     // Flex布局是LVGL v8引入的强大布局系统，类似CSS Flexbox
     // 
     // LV_FLEX_FLOW_COLUMN的含义：
     // - 主轴方向：垂直（从上到下）
     // - 交叉轴方向：水平（从左到右）
     // - 子对象会自动垂直堆叠排列
     // 
     // Flex布局的优势：
     // 1. 自动换行/滚动：当内容超出容器时，自动启用滚动条
     // 2. 自适应尺寸：子对象可以根据内容自动调整大小
     // 3. 灵活对齐：支持多种对齐方式（居左、居中、居右、分散对齐等）
     lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
     
     // 去除容器背景，使其融入暗黑环境
     // 容器本身不可见，只显示内部的按钮
     lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
     
     // 去除容器边框
     lv_obj_set_style_border_width(list_cont, 0, 0);
     
     // 设置容器内边距为5像素
     // 让列表内容与容器边缘保持距离
     lv_obj_set_style_pad_all(list_cont, 5, 0);
 
     // ==================================================================
     // 第三部分：填充WiFi数据（动态创建按钮）
     // ==================================================================
     
     // 用于记录第一个按钮，以便初始聚焦
     lv_obj_t * first_btn = NULL; 
 
     // 判断是否有扫描到的WiFi网络
     if (g_ap_count == 0) {
         // 情况1：没有扫描到任何网络
         
         // 创建提示标签
         lv_obj_t * empty_label = lv_label_create(list_cont);
         lv_label_set_text(empty_label, "未扫描到网络");
         
         // 将提示文字居中显示
         lv_obj_center(empty_label);
         
     } else {
         // 情况2：有扫描到的网络，逐个创建按钮
         
         for (int i = 0; i < g_ap_count; i++) {
             
             // --- 3.1 获取SSID名称 ---
             // 检查SSID是否为空字符串
             // 有些隐藏网络的SSID可能为空，需要特殊处理
             const char *ssid = (strlen((char *)g_ap_records[i].ssid) > 0) 
                              ? (char *)g_ap_records[i].ssid   // 正常SSID
                              : "[隐藏网络]";                    // 隐藏网络提示
             
             // --- 3.2 格式化显示文本 ---
             // 创建缓冲区存储格式化后的字符串
             char buf[64];
             
             // 使用snprintf安全地格式化字符串
             // 格式：📶  SSID名称
             // sizeof(buf)确保不会缓冲区溢出
             snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", ssid);
 
             // --- 3.3 创建按钮对象 ---
             // 在列表容器中创建按钮
             // 按钮会自动加入容器的Flex布局，垂直排列
             lv_obj_t * btn = lv_btn_create(list_cont);
             
             // 设置按钮宽度为容器的100%
             // LV_PCT(100)是LVGL的百分比宏，自动计算实际像素值
             // 这样无论屏幕宽度如何，按钮都能占满整行
             lv_obj_set_width(btn, LV_PCT(100)); 
             
             // 应用默认样式（透明背景、无边框）
             lv_obj_add_style(btn, &style_btn, 0); 
             
             // 【关键】应用焦点样式
             // LV_STATE_FOCUSED表示当按钮获得焦点时应用此样式
             // 这是实现"选中高亮"效果的核心机制
             // 
             // LVGL的状态机：
             // - LV_STATE_DEFAULT: 默认状态
             // - LV_STATE_CHECKED: 选中状态（如复选框）
             // - LV_STATE_FOCUSED: 焦点状态（键盘导航时）
             // - LV_STATE_PRESSED: 按下状态
             // - LV_STATE_DISABLED: 禁用状态
             // 可以同时应用多个状态，LVGL会自动合并样式
             lv_obj_add_style(btn, &style_focus, LV_STATE_FOCUSED); 
 
             // --- 3.4 创建按钮内的标签 ---
             // 在按钮内部创建标签，作为按钮的子对象
             lv_obj_t * label = lv_label_create(btn);
             
             // 设置标签文本为格式化后的SSID
             lv_label_set_text(label, buf);
             
             // 将标签对齐到按钮左侧中部
             // X轴偏移5像素，Y轴居中
             lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0); 
 
             // --- 3.5 将按钮加入焦点组 ---
             // 只有加入组的对象才能接收键盘事件
             // 用户可以通过上下键在这些按钮之间切换焦点
             lv_group_add_obj(input_group, btn);
 
             // --- 3.6 记录第一个按钮 ---
             // 用于后续设置初始焦点
             if (first_btn == NULL) {
                 first_btn = btn; 
             }
         }
     }
 
     // --- 3.7 设置初始焦点 ---
     // 如果有按钮存在，将焦点设置在第一个按钮上
     // 这样用户一进入页面就可以直接按上下键导航
     if (first_btn != NULL) {
         lv_group_focus_obj(first_btn);  // 强制将焦点移到指定对象
     }
 }
 
 // ======================= 3. UI初始化入口函数 =======================
 
 /**
  * @brief UI模块初始化主函数
  * @details 完成以下工作：
  *          1. 获取或创建输入焦点组
  *          2. 配置焦点组的循环导航特性
  *          3. 初始化所有UI样式
  *          4. 构建WiFi列表页面
  * 
  * @note 调用时机：
  *       - 必须在LVGL初始化之后调用
  *       - 必须在wifi_scanner_init()之后调用（因为依赖扫描结果）
  *       - 建议在main.c的任务中调用
  * 
  * @usage 典型调用流程：
  *        ```c
  *        lv_init();                        // 初始化LVGL核心
  *        lv_port_disp_init();              // 初始化显示驱动
  *        lv_port_indev_init();             // 初始化输入设备
  *        wifi_scanner_init();              // 初始化WiFi扫描
  *        wifi_scan_and_update_list();      // 执行扫描
  *        ui_init();                        // 初始化UI界面 ← 最后调用
  *        ```
  */
 void ui_init(void) {
     
     // ---- 第1步：获取或创建焦点组 ----
     
     // 尝试获取默认焦点组
     // lv_group_get_default()返回之前通过lv_group_set_default()设置的默认组
     // 在lv_port_indev.c中已经创建了默认组并关联了按键设备
     input_group = lv_group_get_default();
     
     // 如果默认组不存在（理论上不应该发生），则创建新组
     if (!input_group) {
         input_group = lv_group_create();           // 创建新的焦点组
         lv_group_set_default(input_group);         // 设为默认组
     }
     
     // 启用循环导航（Wrap模式）
     // true: 当焦点到达最后一个按钮时，再按"下键"会回到第一个按钮
     // false: 到达末尾后不再响应导航键
     // 对于列表浏览场景，循环导航能提供更好的用户体验
     lv_group_set_wrap(input_group, true); 
 
     // ---- 第2步：初始化样式 ----
     // 配置所有UI元素的视觉样式
     ui_style_init();
     
     // ---- 第3步：构建页面 ----
     // 根据当前扫描到的WiFi列表创建UI界面
     build_wifi_list_page();
     
     // 此时UI已经完全渲染到屏幕上
     // LVGL会在后台任务中持续刷新显示
 }