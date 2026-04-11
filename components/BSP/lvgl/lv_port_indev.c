#include "lv_port_indev.h"
#include "driver/gpio.h"
#include "esp_log.h"

// 复用你之前的引脚定义
#define KEY_UP_GPIO    GPIO_NUM_5
#define KEY_DOWN_GPIO  GPIO_NUM_6
#define KEY_RIGHT_GPIO GPIO_NUM_7
#define KEY_ENTER_GPIO GPIO_NUM_8
#define KEY_BACK_GPIO  GPIO_NUM_9

static const char *TAG = "LVGL_INDEV";

// ======================= 内部函数声明 =======================
static void keypad_init(void);
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static uint32_t keypad_get_key(void);

// 全局输入设备句柄
lv_indev_t * indev_keypad;

// ======================= 核心初始化 =======================
void lv_port_indev_init(void)
{
    // 1. 初始化 GPIO 引脚
    keypad_init();

    // 2. 注册 LVGL 键盘驱动
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    
    indev_drv.type = LV_INDEV_TYPE_KEYPAD; // 告诉 LVGL 这是一个键盘设备
    indev_drv.read_cb = keypad_read;       // 绑定读取函数
    
    indev_keypad = lv_indev_drv_register(&indev_drv);

    // 3. 【最关键的一步】：创建一个控制组（Group）并设为默认
    // LVGL 的按键必须要有个“焦点（Focus）”。设为默认后，你创建的按钮会自动加入这个组
    lv_group_t * g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(indev_keypad, g);

    ESP_LOGI(TAG, "LVGL 物理按键对接完成！");
}

// ======================= 底层读取逻辑 =======================

// 初始化 5 个按键为上拉输入
static void keypad_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_UP_GPIO) | (1ULL << KEY_DOWN_GPIO) | 
                        (1ULL << KEY_RIGHT_GPIO) | (1ULL << KEY_ENTER_GPIO) | (1ULL << KEY_BACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

// 翻译 GPIO 电平为 LVGL 键值
static uint32_t keypad_get_key(void)
{
    // 注意：拉低有效 (按下为 0)
    if (gpio_get_level(KEY_UP_GPIO) == 0)    return LV_KEY_PREV;  // 上一个
    if (gpio_get_level(KEY_DOWN_GPIO) == 0)  return LV_KEY_NEXT;  // 下一个
    if (gpio_get_level(KEY_RIGHT_GPIO) == 0) return LV_KEY_RIGHT; // 右
    if (gpio_get_level(KEY_BACK_GPIO) == 0)  return LV_KEY_ESC;  // 左 / 退格
    if (gpio_get_level(KEY_ENTER_GPIO) == 0) return LV_KEY_ENTER; // 确认

    return 0; // 没有按键按下
}

// LVGL 会以高频调用这个函数来获取当前按键状态（自带软件防抖）
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static uint32_t last_key = 0;
    uint32_t act_key = keypad_get_key();

    if(act_key != 0) {
        data->state = LV_INDEV_STATE_PR; 
        last_key = act_key;              
        
        // 【新增】：加一句底层打印！
        ESP_LOGI("LVGL_INDEV", "底层硬件检测到按键被按下! 键值: %d", (int)act_key); 
    } else {
        data->state = LV_INDEV_STATE_REL; 
    }
    
    data->key = last_key; 
}