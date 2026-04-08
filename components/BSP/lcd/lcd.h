/**
 * @file lcd.h
 * @brief LCD Display Driver - ILI9341 with Hardware Abstraction
 *
 * Features:
 * - ILI9341 SPI LCD driver
 * - Font rendering (8x16 ASCII, 16x16 Chinese)
 * - Image display from flash
 * - Backlight control
 * - Display abstraction layer
 *
 * @author Optimized Version
 * @date 2024
 */

#ifndef LCD_H__
#define LCD_H__

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===================== 硬件配置 =====================
#define LCD_DC_PIN          GPIO_NUM_2
#define LCD_RST_PIN         GPIO_NUM_4
#define LCD_BL_PIN          GPIO_NUM_15   // 背光控制引脚

// ===================== 屏幕尺寸 =====================
#define LCD_WIDTH           240
#define LCD_HEIGHT          320

// ===================== 颜色定义（RGB565）====================//
#define LCD_COLOR_BLACK     0x0000
#define LCD_COLOR_WHITE     0xFFFF
#define LCD_COLOR_RED       0xF800
#define LCD_COLOR_GREEN     0x07E0
#define LCD_COLOR_BLUE      0x001F
#define LCD_COLOR_YELLOW    0xFFE0
#define LCD_COLOR_CYAN      0x07FF
#define LCD_COLOR_MAGENTA   0xF81F
#define LCD_COLOR_GRAY      0x8410
#define LCD_COLOR_ORANGE    0xFD20
#define LCD_COLOR_PINK      0xF81F
#define LCD_COLOR_PURPLE    0x8010
#define LCD_COLOR_NAVY       0x000F
#define LCD_COLOR_TEAL      0x0410
#define LCD_COLOR_MAROON    0x7800
#define LCD_COLOR_OLIVE     0x7BE0
#define LCD_COLOR_LIME      0x07E0
#define LCD_COLOR_AQUA     0x07FF
#define LCD_COLOR_MAROON    0x7800

// ===================== 字体尺寸 =====================
#define FONT_8X16_WIDTH     8
#define FONT_8X16_HEIGHT    16
#define FONT_16X16_WIDTH    16
#define FONT_16X16_HEIGHT   16
#define FONT_12X12_WIDTH    12
#define FONT_12X12_HEIGHT   12

// ===================== 滚动配置 =====================
#define LCD_SCROLL_MAX_LINES    100

// ===================== 显示方向 =====================
typedef enum {
    LCD_ORIENTATION_PORTRAIT = 0,      // 竖屏
    LCD_ORIENTATION_LANDSCAPE,          // 横屏
    LCD_ORIENTATION_PORTRAIT_INV,       // 竖屏倒置
    LCD_ORIENTATION_LANDSCAPE_INV        // 横屏倒置
} lcd_orientation_t;

// ===================== 显示状态 =====================
typedef struct {
    bool is_initialized;
    uint8_t brightness;        // 亮度 0-100
    bool is_on;              // 显示开关
    lcd_orientation_t orientation;
    uint32_t pixel_count;    // 总像素数
} lcd_status_t;

// ===================== 滚动控制 =====================
typedef struct {
    uint16_t scroll_start;    // 起始行
    uint16_t scroll_end;      // 结束行
    uint16_t scroll_offset;   // 滚动偏移
    bool is_enabled;         // 是否启用
} lcd_scroll_config_t;

// ===================== 驱动接口（抽象层）====================//
typedef struct {
    void (*init)(void);
    void (*set_window)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*fill_rect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void (*draw_pixel)(uint16_t x, uint16_t y, uint16_t color);
    void (*set_orientation)(lcd_orientation_t orient);
    void (*set_brightness)(uint8_t level);
    void (*sleep)(void);
    void (*wakeup)(void);
} lcd_driver_t;

// ===================== GPIO函数声明 =====================
void LCD_GPIO_Init(void);

// ===================== SPI函数声明 =====================
void spi_master_init(void);
void spi_send_byte(uint8_t data);
void spi_send_buf(const uint8_t *data, uint32_t len);

// ===================== ILI9341基础函数 =====================
void ili9341_init(void);
void ili9341_set_orientation(uint8_t orient);
void ili9341_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void ili9341_draw_rectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ili9341_fill_screen(uint16_t color);
void ili9341_display_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *image_data);

// ===================== 字符和字符串绘制 =====================
void ili9341_draw_char_8x16(uint16_t x, uint16_t y, uint8_t ch, uint16_t color, uint16_t bg_color);
void ili9341_draw_string_8x16(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);
void ili9341_draw_chinese_16x16(uint16_t x, uint16_t y, const uint8_t *font_buf, uint16_t color, uint16_t bg_color);
void ili9341_draw_mix_string_16x16(uint16_t x, uint16_t y, const char *str, const uint8_t *font_lib, uint16_t color, uint16_t bg_color);

// ===================== 高级绘制函数 =====================
void ili9341_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ili9341_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void ili9341_fill_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void ili9341_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ili9341_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color);

// ===================== 格式化字符串绘制 =====================
/**
 * @brief 带格式的字符串绘制
 * @note 简化版，只支持基本格式：%s, %d, %c
 */
void ili9341_printf(uint16_t x, uint16_t y, uint16_t color, uint16_t bg_color,
                    const char *fmt, ...);

// ===================== 图形绘制 =====================
void ili9341_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t *bitmap, uint16_t fg_color, uint16_t bg_color);

// ===================== 进度条绘制 =====================
void ili9341_draw_progress_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint16_t progress, uint16_t fg_color, uint16_t bg_color);

// ===================== 滚动功能 =====================
void ili9341_set_scroll_area(uint16_t top_fixed, uint16_t bottom_fixed);
void ili9341_scroll(uint16_t scroll_offset);
void ili9341_stop_scroll(void);
void ili9341_enable_scroll(bool enable);

// ===================== 高级功能 =====================
void display_image_from_flash(uint16_t x, uint16_t y, uint32_t flash_addr);

// ===================== 背光控制 =====================
void lcd_set_brightness(uint8_t level);
uint8_t lcd_get_brightness(void);
void lcd_backlight_on(void);
void lcd_backlight_off(void);

// ===================== 显示控制 =====================
void lcd_display_on(void);
void lcd_display_off(void);
void lcd_sleep(void);
void lcd_wakeup(void);

// ===================== 状态查询 =====================
void lcd_get_status(lcd_status_t *status);
bool lcd_is_initialized(void);

// ===================== 总初始化函数 =====================
void lcd_init(void);

// ===================== 内存优化 =====================
/**
 * @brief 清空LCD缓冲区（如果使用双缓冲）
 */
void lcd_clear_buffer(void);

/**
 * @brief 刷新缓冲区到LCD
 */
void lcd_flush_buffer(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_H__ */
