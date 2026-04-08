/**
 * @file lcd.c
 * @brief LCD Display Driver - ILI9341 Implementation
 *
 * Features:
 * - ILI9341 SPI LCD driver
 * - Font rendering (8x16 ASCII, 16x16 Chinese)
 * - Image display from flash
 * - Backlight control
 * - Basic drawing primitives
 *
 * @author Optimized Version
 * @date 2024
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "lcd.h"
#include "font_picture.h"

// ===================== 日志配置 =====================
static const char *TAG = "LCD";

// ===================== 外部字库声明 =====================
extern const uint8_t Font_8x16[];               // 8x16 ASCII字库
extern const uint8_t Font_16x16_Chinese[][32];   // 16x16汉字库

// ===================== 静态变量 =====================
static bool s_lcd_initialized = false;
static uint8_t s_brightness = 100;
static bool s_display_on = true;
static lcd_orientation_t s_orientation = LCD_ORIENTATION_PORTRAIT;

// 滚动配置
static lcd_scroll_config_t s_scroll = {
    .scroll_start = 0,
    .scroll_end = LCD_HEIGHT,
    .scroll_offset = 0,
    .is_enabled = false
};

// ===================== GPIO/硬件层函数 =====================

void LCD_GPIO_Init(void)
{
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << LCD_DC_PIN) | (1ULL << LCD_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf);

    // 背光引脚初始化（使用GPIO）
    #ifndef LCD_BL_PIN
    #define LCD_BL_PIN GPIO_NUM_15
    #endif

    gpio_config_t bl_conf = {
        .pin_bit_mask = (1ULL << LCD_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_conf);

    gpio_set_level(LCD_RST_PIN, 1);
    gpio_set_level(LCD_DC_PIN, 0);
}

// ===================== 内部辅助函数 =====================

static void lcd_write_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_DC_PIN, 0);
    spi_send_byte(cmd);
}

static void lcd_write_data(uint8_t data)
{
    gpio_set_level(LCD_DC_PIN, 1);
    spi_send_byte(data);
}

static void lcd_write_data_bulk(const uint8_t *data, uint32_t len)
{
    gpio_set_level(LCD_DC_PIN, 1);
    spi_send_buf(data, len);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // 边界裁剪
    if (x0 >= LCD_WIDTH) x0 = LCD_WIDTH - 1;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y0 >= LCD_HEIGHT) y0 = LCD_HEIGHT - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    if (x0 > x1) { uint16_t tmp = x0; x0 = x1; x1 = tmp; }
    if (y0 > y1) { uint16_t tmp = y0; y0 = y1; y1 = tmp; }

    lcd_write_cmd(0x2A); // CASET: 列地址设置
    lcd_write_data(x0 >> 8);
    lcd_write_data(x0 & 0xFF);
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);

    lcd_write_cmd(0x2B); // RASET: 行地址设置
    lcd_write_data(y0 >> 8);
    lcd_write_data(y0 & 0xFF);
    lcd_write_data(y1 >> 8);
    lcd_write_data(y1 & 0xFF);

    lcd_write_cmd(0x2C); // RAMWR: 写GRAM
}

// ===================== ILI9341初始化 =====================

void ili9341_init(void)
{
    // 硬件复位
    gpio_set_level(LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 完整初始化序列
    lcd_write_cmd(0xCF);
    lcd_write_data(0x00);
    lcd_write_data(0xC1);
    lcd_write_data(0x30);

    lcd_write_cmd(0xED);
    lcd_write_data(0x64);
    lcd_write_data(0x03);
    lcd_write_data(0x12);
    lcd_write_data(0x81);

    lcd_write_cmd(0xE8);
    lcd_write_data(0x85);
    lcd_write_data(0x00);
    lcd_write_data(0x78);

    lcd_write_cmd(0xCB);
    lcd_write_data(0x39);
    lcd_write_data(0x2C);
    lcd_write_data(0x00);
    lcd_write_data(0x34);
    lcd_write_data(0x02);

    lcd_write_cmd(0xF7);
    lcd_write_data(0x20);

    lcd_write_cmd(0xEA);
    lcd_write_data(0x00);
    lcd_write_data(0x00);

    lcd_write_cmd(0xC0);  // Power Control 1
    lcd_write_data(0x1D);

    lcd_write_cmd(0xC1);  // Power Control 2
    lcd_write_data(0x12);

    lcd_write_cmd(0xC5);  // VCOM Control 1
    lcd_write_data(0x33);
    lcd_write_data(0x34);

    lcd_write_cmd(0xC7);  // VCOM Control 2
    lcd_write_data(0x92);

    lcd_write_cmd(0x3A);  // Pixel Format Set
    lcd_write_data(0x55); // 16 bits/pixel

    lcd_write_cmd(0xB1);  // Frame Rate Control
    lcd_write_data(0x00);
    lcd_write_data(0x12);

    lcd_write_cmd(0xB6);  // Display Function Control
    lcd_write_data(0x0A);
    lcd_write_data(0xA2);

    lcd_write_cmd(0xF2);  // Gamma Function Disable
    lcd_write_data(0x00);

    lcd_write_cmd(0x26);  // Gamma Set
    lcd_write_data(0x01);

    // Positive Gamma Correction
    lcd_write_cmd(0xE0);
    uint8_t gamma_pos[] = {0x0F, 0x22, 0x1F, 0x0B, 0x0E, 0x08, 0x4E, 0xF1,
                           0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    for (int i = 0; i < 15; i++) {
        lcd_write_data(gamma_pos[i]);
    }

    // Negative Gamma Correction
    lcd_write_cmd(0xE1);
    uint8_t gamma_neg[] = {0x00, 0x1D, 0x20, 0x04, 0x10, 0x08, 0x34, 0x32,
                           0x08, 0x06, 0x00, 0x1D, 0x24, 0x0F, 0x00};
    for (int i = 0; i < 15; i++) {
        lcd_write_data(gamma_neg[i]);
    }

    // Exit Sleep
    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Display ON
    lcd_write_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(50));

    // 默认竖屏
    ili9341_set_orientation(0);

    s_lcd_initialized = true;
    ESP_LOGI(TAG, "ILI9341 initialized");
}

void ili9341_set_orientation(uint8_t orient)
{
    uint8_t madctl;
    switch (orient) {
        case 0:  madctl = 0x48; break;  // 竖屏
        case 1:  madctl = 0xC8; break;  // 横屏
        case 2:  madctl = 0x88; break;  // 倒置竖屏
        case 3:  madctl = 0x08; break;  // 倒置横屏
        default: madctl = 0x48;
    }
    lcd_write_cmd(0x36);
    lcd_write_data(madctl);
    s_orientation = (lcd_orientation_t)orient;
}

// ===================== 像素和矩形绘制 =====================

void ili9341_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    lcd_set_window(x, y, x, y);
    uint8_t data[2] = {color >> 8, color & 0xFF};
    lcd_write_data_bulk(data, 2);
}

void ili9341_draw_rectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0 || h == 0) return;

    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;

    lcd_set_window(x, y, x1, y1);

    uint32_t pixels = (uint32_t)(x1 - x + 1) * (y1 - y + 1);
    uint8_t color_data[2] = {color >> 8, color & 0xFF};

    // 分块发送以减少内存使用
    #define BUF_SIZE 256
    uint8_t buf[BUF_SIZE * 2];
    for (uint32_t i = 0; i < BUF_SIZE; i++) {
        buf[i * 2] = color_data[0];
        buf[i * 2 + 1] = color_data[1];
    }

    gpio_set_level(LCD_DC_PIN, 1);
    uint32_t remaining = pixels;
    while (remaining > 0) {
        uint32_t send_len = (remaining > BUF_SIZE) ? BUF_SIZE : remaining;
        spi_send_buf(buf, send_len * 2);
        remaining -= send_len;
    }
}

void ili9341_fill_screen(uint16_t color)
{
    ili9341_draw_rectangle(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

// ===================== 图像显示 =====================

void ili9341_display_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *image_data)
{
    if (image_data == NULL || w == 0 || h == 0) return;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;

    lcd_set_window(x, y, x1, y1);
    uint32_t pixels = (uint32_t)(x1 - x + 1) * (y1 - y + 1);
    lcd_write_data_bulk(image_data, pixels * 2);
}

// ===================== 字符绘制 =====================

void ili9341_draw_char_8x16(uint16_t x, uint16_t y, uint8_t ch, uint16_t color, uint16_t bg_color)
{
    if (x + FONT_8X16_WIDTH > LCD_WIDTH || y + FONT_8X16_HEIGHT > LCD_HEIGHT) return;
    if (ch < 0x20 || ch > 0x7E) ch = 0x20;

    lcd_set_window(x, y, x + FONT_8X16_WIDTH - 1, y + FONT_8X16_HEIGHT - 1);

    uint8_t pixel_buf[FONT_8X16_HEIGHT * FONT_8X16_WIDTH * 2];
    uint8_t *ptr = pixel_buf;
    const uint8_t *font_ptr = &Font_8x16[(ch - 0x20) * FONT_8X16_HEIGHT];

    for (uint8_t row = 0; row < FONT_8X16_HEIGHT; row++) {
        uint8_t font_data = font_ptr[row];
        for (uint8_t col = 0; col < FONT_8X16_WIDTH; col++) {
            uint16_t pixel = (font_data & (0x80 >> col)) ? color : bg_color;
            *ptr++ = pixel >> 8;
            *ptr++ = pixel & 0xFF;
        }
    }
    lcd_write_data_bulk(pixel_buf, sizeof(pixel_buf));
}

void ili9341_draw_string_8x16(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color)
{
    if (str == NULL) return;

    uint16_t curr_x = x;
    while (*str) {
        // 自动换行
        if (curr_x + FONT_8X16_WIDTH > LCD_WIDTH) {
            curr_x = x;
            y += FONT_8X16_HEIGHT;
            if (y + FONT_8X16_HEIGHT > LCD_HEIGHT) break;
        }
        ili9341_draw_char_8x16(curr_x, y, *str, color, bg_color);
        curr_x += FONT_8X16_WIDTH;
        str++;
    }
}

// ===================== 中文绘制 =====================

void ili9341_draw_chinese_16x16(uint16_t x, uint16_t y, const uint8_t *font_buf,
                                uint16_t color, uint16_t bg_color)
{
    if (font_buf == NULL) return;
    if (x + FONT_16X16_WIDTH > LCD_WIDTH || y + FONT_16X16_HEIGHT > LCD_HEIGHT) return;

    lcd_set_window(x, y, x + FONT_16X16_WIDTH - 1, y + FONT_16X16_HEIGHT - 1);

    uint8_t pixel_buf[FONT_16X16_HEIGHT * FONT_16X16_WIDTH * 2];
    uint8_t *ptr = pixel_buf;

    for (uint8_t row = 0; row < FONT_16X16_HEIGHT; row++) {
        uint8_t high = font_buf[row * 2];
        uint8_t low = font_buf[row * 2 + 1];
        for (uint8_t col = 0; col < FONT_16X16_WIDTH; col++) {
            uint16_t pixel = bg_color;
            if (col < 8) {
                if (high & (0x80 >> col)) pixel = color;
            } else {
                if (low & (0x80 >> (col - 8))) pixel = color;
            }
            *ptr++ = pixel >> 8;
            *ptr++ = pixel & 0xFF;
        }
    }
    lcd_write_data_bulk(pixel_buf, sizeof(pixel_buf));
}

void ili9341_draw_mix_string_16x16(uint16_t x, uint16_t y, const char *str,
                                   const uint8_t *font_lib, uint16_t color, uint16_t bg_color)
{
    if (str == NULL || font_lib == NULL) return;

    uint16_t curr_x = x;
    while (*str) {
        if (curr_x + FONT_16X16_WIDTH > LCD_WIDTH) {
            curr_x = x;
            y += FONT_16X16_HEIGHT;
            if (y + FONT_16X16_HEIGHT > LCD_HEIGHT) break;
        }

        if (*str == '[') {
            // 提取汉字索引
            str++;
            uint16_t idx = 0;
            while (*str != ']' && *str != '\0') {
                if (*str >= '0' && *str <= '9') {
                    idx = idx * 10 + (*str - '0');
                }
                str++;
            }
            if (*str == ']') str++;

            const uint8_t *font_buf = &font_lib[idx * 32];
            ili9341_draw_chinese_16x16(curr_x, y, font_buf, color, bg_color);
            curr_x += FONT_16X16_WIDTH;
        } else {
            // ASCII字符
            ili9341_draw_char_8x16(curr_x + 4, y, *str, color, bg_color);
            curr_x += FONT_16X16_WIDTH;
            str++;
        }
    }
}

// ===================== 高级绘图函数 =====================

void ili9341_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        ili9341_draw_pixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ili9341_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        ili9341_draw_pixel(x0 + x, y0 + y, color);
        ili9341_draw_pixel(x0 + y, y0 + x, color);
        ili9341_draw_pixel(x0 - y, y0 + x, color);
        ili9341_draw_pixel(x0 - x, y0 + y, color);
        ili9341_draw_pixel(x0 - x, y0 - y, color);
        ili9341_draw_pixel(x0 - y, y0 - x, color);
        ili9341_draw_pixel(x0 + y, y0 - x, color);
        ili9341_draw_pixel(x0 + x, y0 - y, color);

        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

void ili9341_fill_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        ili9341_draw_line(x0 - x, y0 + y, x0 + x, y0 + y, color);
        ili9341_draw_line(x0 - y, y0 + x, x0 + y, y0 + x, color);
        ili9341_draw_line(x0 - y, y0 - x, x0 + y, y0 - x, color);
        ili9341_draw_line(x0 - x, y0 - y, x0 + x, y0 - y, color);

        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

void ili9341_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ili9341_draw_line(x, y, x + w - 1, y, color);
    ili9341_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    ili9341_draw_line(x, y, x, y + h - 1, color);
    ili9341_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void ili9341_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t r, uint16_t color)
{
    if (w < 2 * r || h < 2 * r) return;

    // 主体矩形
    ili9341_draw_rect(x + r, y, w - 2 * r, h, color);
    ili9341_draw_rect(x, y + r, w, h - 2 * r, color);

    // 四个角（简化为圆弧绘制）
    ili9341_draw_circle(x + r, y + r, r, color);
    ili9341_draw_circle(x + w - r - 1, y + r, r, color);
    ili9341_draw_circle(x + r, y + h - r - 1, r, color);
    ili9341_draw_circle(x + w - r - 1, y + h - r - 1, r, color);
}

// ===================== 滚动功能 =====================

void ili9341_set_scroll_area(uint16_t top_fixed, uint16_t bottom_fixed)
{
    if (top_fixed + bottom_fixed >= LCD_HEIGHT) return;

    s_scroll.scroll_start = top_fixed;
    s_scroll.scroll_end = LCD_HEIGHT - bottom_fixed;

    lcd_write_cmd(0x33);
    lcd_write_data(top_fixed >> 8);
    lcd_write_data(top_fixed & 0xFF);
    lcd_write_data((LCD_HEIGHT - bottom_fixed) >> 8);
    lcd_write_data((LCD_HEIGHT - bottom_fixed) & 0xFF);
}

void ili9341_scroll(uint16_t scroll_offset)
{
    if (!s_scroll.is_enabled) return;

    s_scroll.scroll_offset = scroll_offset % (s_scroll.scroll_end - s_scroll.scroll_start);

    lcd_write_cmd(0x37);
    lcd_write_data(s_scroll.scroll_offset >> 8);
    lcd_write_data(s_scroll.scroll_offset & 0xFF);
}

void ili9341_stop_scroll(void)
{
    lcd_write_cmd(0x37);
    lcd_write_data(0x00);
    lcd_write_data(0x00);
    s_scroll.is_enabled = false;
}

void ili9341_enable_scroll(bool enable)
{
    s_scroll.is_enabled = enable;
}

// ===================== 格式化字符串 =====================

void ili9341_printf(uint16_t x, uint16_t y, uint16_t color, uint16_t bg_color,
                    const char *fmt, ...)
{
    char buffer[128];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    ili9341_draw_string_8x16(x, y, buffer, color, bg_color);
}

// ===================== 位图绘制 =====================

void ili9341_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t *bitmap, uint16_t fg_color, uint16_t bg_color)
{
    if (bitmap == NULL) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    uint8_t pixel_buf[FONT_8X16_WIDTH * 2];
    uint8_t *ptr = pixel_buf;

    for (uint16_t byte_y = 0; byte_y < h; byte_y++) {
        for (uint16_t byte_x = 0; byte_x < (w + 7) / 8; byte_x++) {
            uint8_t b = bitmap[byte_y * ((w + 7) / 8) + byte_x];
            for (uint8_t bit = 0; bit < 8; bit++) {
                if ((byte_x * 8 + bit) >= w) break;
                uint16_t pixel = (b & (0x80 >> bit)) ? fg_color : bg_color;
                *ptr++ = pixel >> 8;
                *ptr++ = pixel & 0xFF;
                if (ptr >= pixel_buf + FONT_8X16_WIDTH * 2) {
                    lcd_write_data_bulk(pixel_buf, sizeof(pixel_buf));
                    ptr = pixel_buf;
                }
            }
        }
    }

    if (ptr > pixel_buf) {
        uint32_t remaining = ptr - pixel_buf;
        lcd_write_data_bulk(pixel_buf, remaining);
    }
}

// ===================== 进度条绘制 =====================

void ili9341_draw_progress_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint16_t progress, uint16_t fg_color, uint16_t bg_color)
{
    // progress: 0-100
    if (progress > 100) progress = 100;
    if (h < 4) h = 4;

    // 背景
    ili9341_draw_rectangle(x, y, w, h, bg_color);

    // 边框
    ili9341_draw_rect(x, y, w, h, fg_color);

    // 填充进度
    if (progress > 0) {
        uint16_t fill_w = (w - 2) * progress / 100;
        if (fill_w > 0) {
            ili9341_draw_rectangle(x + 1, y + 1, fill_w, h - 2, fg_color);
        }
    }
}

// ===================== Flash图像显示 =====================

void display_image_from_flash(uint16_t x, uint16_t y, uint32_t flash_addr)
{
    #define IMAGE_SIZE (120 * 160 * 2)
    extern void SPI_FLASH_ReadBuffer(uint32_t addr, uint8_t *buf, uint32_t len);

    uint32_t remaining = IMAGE_SIZE;
    uint32_t addr = flash_addr;
    uint8_t buffer[256];

    lcd_set_window(x, y, x + 120 - 1, y + 160 - 1);
    gpio_set_level(LCD_DC_PIN, 1);

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        SPI_FLASH_ReadBuffer(addr, buffer, chunk);
        spi_send_buf(buffer, chunk);
        addr += chunk;
        remaining -= chunk;
    }
}

// ===================== 背光控制 =====================

void lcd_backlight_on(void)
{
    #ifdef LCD_BL_PIN
    gpio_set_level(LCD_BL_PIN, 1);
    #endif
    s_display_on = true;
}

void lcd_backlight_off(void)
{
    #ifdef LCD_BL_PIN
    gpio_set_level(LCD_BL_PIN, 0);
    #endif
    s_display_on = false;
}

void lcd_set_brightness(uint8_t level)
{
    if (level > 100) level = 100;
    s_brightness = level;

    #ifdef LCD_BL_PIN
    // 简单PWM控制（可升级为LEDC）
    if (level == 0) {
        lcd_backlight_off();
    } else {
        gpio_set_level(LCD_BL_PIN, 1);
    }
    #endif

    ESP_LOGD(TAG, "Brightness set to %d%%", level);
}

uint8_t lcd_get_brightness(void)
{
    return s_brightness;
}

// ===================== 显示控制 =====================

void lcd_display_on(void)
{
    lcd_write_cmd(0x29);  // DISPON
    s_display_on = true;
    lcd_backlight_on();
}

void lcd_display_off(void)
{
    lcd_backlight_off();
    lcd_write_cmd(0x28);  // DISPOFF
    s_display_on = false;
}

void lcd_sleep(void)
{
    lcd_write_cmd(0x10);  // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "LCD entered sleep mode");
}

void lcd_wakeup(void)
{
    lcd_write_cmd(0x11);  // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "LCD wakeup");
}

// ===================== 状态查询 =====================

void lcd_get_status(lcd_status_t *status)
{
    if (status == NULL) return;

    memset(status, 0, sizeof(lcd_status_t));
    status->is_initialized = s_lcd_initialized;
    status->brightness = s_brightness;
    status->is_on = s_display_on;
    status->orientation = s_orientation;
    status->pixel_count = (uint32_t)LCD_WIDTH * LCD_HEIGHT;
}

bool lcd_is_initialized(void)
{
    return s_lcd_initialized;
}

// ===================== 总初始化 =====================

void lcd_init(void)
{
    LCD_GPIO_Init();
    spi_master_init();
    ili9341_init();
    ili9341_fill_screen(LCD_COLOR_BLACK);
    lcd_set_brightness(100);
    ESP_LOGI(TAG, "LCD driver initialized");
}

// ===================== 内存优化函数 =====================

void lcd_clear_buffer(void)
{
    // 如果实现了双缓冲，在此清空后缓冲区
    // 此处为占位实现
}

void lcd_flush_buffer(void)
{
    // 如果实现了双缓冲，在此将后缓冲区内容刷新到LCD
    // 此处为占位实现
}
