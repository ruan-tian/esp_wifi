#include "lv_port_indev.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ======================= 硬件引脚定义 =======================
// 定义5个物理按键对应的GPIO引脚号
// 这些引脚需要根据实际硬件电路连接进行配置
#define KEY_UP_GPIO    GPIO_NUM_5   // 上键 - GPIO5
#define KEY_DOWN_GPIO  GPIO_NUM_6   // 下键 - GPIO6
#define KEY_RIGHT_GPIO GPIO_NUM_7   // 右键 - GPIO7
#define KEY_ENTER_GPIO GPIO_NUM_8   // 确认键 - GPIO8
#define KEY_BACK_GPIO  GPIO_NUM_9   // 返回/ESC键 - GPIO9

static const char *TAG = "LVGL_INDEV";  // ESP-IDF日志标签，用于调试输出

// ======================= 内部函数声明 =======================
/**
 * @brief 初始化按键的GPIO引脚配置
 * @details 将所有按键引脚配置为上拉输入模式
 */
static void keypad_init(void);

/**
 * @brief LVGL输入设备读取回调函数
 * @param indev_drv 输入设备驱动结构体指针
 * @param data 输入设备数据指针，用于返回按键状态和键值
 * @details 该函数会被LVGL周期性调用，用于检测当前按键状态
 */
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

/**
 * @brief 获取当前按下的按键对应的LVGL键值
 * @return 返回LVGL定义的键值常量（如LV_KEY_ENTER），无按键按下时返回0
 * @details 将GPIO电平状态转换为LVGL可识别的键值
 */
static uint32_t keypad_get_key(void);

// 全局输入设备句柄，用于后续操作（如设置焦点组）
lv_indev_t * indev_keypad;

// ======================= 核心初始化函数 =======================
/**
 * @brief LVGL输入设备初始化主函数
 * @details 完成以下工作：
 *          1. 初始化底层GPIO硬件
 *          2. 注册LVGL键盘输入设备驱动
 *          3. 创建并设置默认的焦点组（Group）
 * 
 * @note 必须在LVGL初始化之后、创建UI控件之前调用此函数
 */
void lv_port_indev_init(void)
{
    // 第1步：初始化底层GPIO硬件
    // 配置所有按键引脚为上拉输入模式
    keypad_init();

    // 第2步：注册LVGL键盘输入设备驱动
    static lv_indev_drv_t indev_drv;  // 静态分配驱动结构体，避免栈溢出
    
    lv_indev_drv_init(&indev_drv);    // 使用默认值初始化驱动结构体
    
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;  // 设置设备类型为键盘类型
                                            // LVGL支持多种输入类型：指针、键盘、编码器等
                                            
    indev_drv.read_cb = keypad_read;        // 绑定按键读取回调函数
                                            // LVGL会周期性调用此函数获取按键状态
    
    indev_keypad = lv_indev_drv_register(&indev_drv);  // 向LVGL注册输入设备
                                                       // 返回设备句柄，用于后续管理

    // 第3步：创建焦点组（Group）并设为默认
    // 【关键概念】LVGL的键盘导航需要"焦点"机制
    // Group用于管理一组可聚焦的控件（如按钮、列表项等）
    // 键盘事件只会发送给当前获得焦点的控件
    
    lv_group_t * g = lv_group_create();      // 创建一个新的焦点组
    lv_group_set_default(g);                 // 将此组设为默认组
                                             // 之后创建的控件会自动加入此组
                                             
    lv_group_set_wrap(g, true);              // 启用循环导航
                                             // 当到达最后一个控件时，再按"下一个"会回到第一个
                                             
    lv_indev_set_group(indev_keypad, g);     // 将输入设备与焦点组关联
                                             // 这样按键事件才能正确传递给组内的控件

    ESP_LOGI(TAG, "LVGL 物理按键对接完成！");  // 输出初始化成功日志
}

// ======================= 底层GPIO初始化 =======================
/**
 * @brief 配置按键GPIO引脚
 * @details 将5个按键引脚统一配置为：
 *          - 输入模式
 *          - 使能内部上拉电阻
 *          - 禁用中断（采用轮询方式）
 * 
 * @note 使用上拉电阻的原因：
 *       按键未按下时，引脚通过上拉电阻保持高电平(1)
 *       按键按下时，引脚接地变为低电平(0)
 *       这样可以避免引脚悬空导致的不稳定状态
 */
static void keypad_init(void)
{
    gpio_config_t io_conf = {
        // 使用位掩码同时选择5个GPIO引脚
        // (1ULL << GPIO_NUM_X) 将第X位设置为1
        // 通过位或运算组合多个引脚
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) | 
                        (1ULL << KEY_RIGHT_GPIO) | (1ULL << KEY_ENTER_GPIO) | (1ULL << KEY_BACK_GPIO),
        
        .mode = GPIO_MODE_INPUT,           // 设置为输入模式
        
        .pull_up_en = GPIO_PULLUP_ENABLE,  // 使能内部上拉电阻
                                           // 按键未按下时为高电平，按下时为低电平
                                           
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 禁用下拉电阻（与上拉互斥）
        
        .intr_type = GPIO_INTR_DISABLE,    // 禁用硬件中断
                                           // 采用软件轮询方式检测按键
                                           // LVGL会在每个刷新周期调用读取函数
    };
    
    gpio_config(&io_conf);  // 应用GPIO配置到硬件寄存器
}

// ======================= 按键扫描与转换 =======================
/**
 * @brief 扫描所有按键并返回对应的LVGL键值
 * @return LVGL键值常量或0（无按键）
 * 
 * @retval LV_KEY_PREV   上一个/向上键被按下
 * @retval LV_KEY_NEXT   下一个/向下键被按下
 * @retval LV_KEY_RIGHT  向右键被按下
 * @retval LV_KEY_ESC    返回/退出键被按下
 * @retval LV_KEY_ENTER  确认键被按下
 * @retval 0             没有按键被按下
 * 
 * @note 按键逻辑：低电平有效（按下=0，释放=1）
 *       采用优先级扫描：同时按下多个键时，只返回最先检测到的那个
 */
static uint32_t keypad_get_key(void)
{
    // 依次检查每个按键的电平状态
    // gpio_get_level() 返回0表示按键按下（低电平），返回1表示未按下（高电平）
    
    if (gpio_get_level(KEY_UP_GPIO) == 0)    return LV_KEY_PREV;   // 上键 → 上一个控件
    if (gpio_get_level(KEY_DOWN_GPIO) == 0)  return LV_KEY_NEXT;   // 下键 → 下一个控件
    if (gpio_get_level(KEY_RIGHT_GPIO) == 0) return LV_KEY_RIGHT;  // 右键 → 向右移动
    if (gpio_get_level(KEY_BACK_GPIO) == 0)  return LV_KEY_ESC;    // 返回键 → 退出/返回
    if (gpio_get_level(KEY_ENTER_GPIO) == 0) return LV_KEY_ENTER;  // 确认键 → 确认/选择

    return 0;  // 所有按键都未按下，返回0
}

// ======================= LVGL读取回调实现 =======================
/**
 * @brief LVGL输入设备读取回调函数
 * @param indev_drv 输入设备驱动结构体（未使用，但必须保留参数）
 * @param data 输出参数，用于向LVGL报告按键状态
 * 
 * @details 工作流程：
 *          1. 调用keypad_get_key()获取当前按键状态
 *          2. 如果有按键按下，记录键值并标记为"按下"状态
 *          3. 如果没有按键按下，标记为"释放"状态
 *          4. 保持最后一次按下的键值，直到新按键出现
 * 
 * @note LVGL内置软件防抖机制：
 *       - 短按：按下→释放，触发一次事件
 *       - 长按：持续按下，会重复触发事件（用于列表滚动等场景）
 *       - 防抖时间可在lv_conf.h中配置
 * 
 * @warning 此函数会被LVGL高频调用（通常每5-30ms一次）
 *          因此函数执行必须快速，不能包含延时或阻塞操作
 */
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    // 使用静态变量保存上一次按下的键值
    // 这样可以在按键释放后仍然保持键值，直到新按键出现
    static uint32_t last_key = 0;
    
    // 获取当前实时的按键状态
    uint32_t act_key = keypad_get_key();

    if(act_key != 0) {
        // 有按键被按下
        data->state = LV_INDEV_STATE_PR;  // 设置状态为"按下"（Pressed）
                                          // LVGL会根据此状态判断是按下还是释放
                                          
        last_key = act_key;               // 更新最后按下的键值
                                          // 即使后续按键释放，仍保留此值
                                          
        // 调试日志：输出底层硬件检测到的按键事件
        // 可用于验证硬件连接和GPIO配置是否正确
        ESP_LOGI("LVGL_INDEV", "底层硬件检测到按键被按下! 键值: %d", (int)act_key); 
        
    } else {
        // 没有按键被按下（所有按键都处于释放状态）
        data->state = LV_INDEV_STATE_REL;  // 设置状态为"释放"（Released）
                                           // LVGL收到此状态后，会认为按键已松开
    }
    
    // 将键值写入data结构体
    // 即使按键已释放，仍上报last_key，这样LVGL可以知道是哪个键被释放了
    data->key = last_key; 
    
    // LVGL内部处理逻辑：
    // 1. 如果 state=PR 且 key=X，触发"X键按下"事件
    // 2. 如果 state=REL 且 key=X，触发"X键释放"事件
    // 3. 如果连续多次 state=PR，视为长按，会重复触发事件
}