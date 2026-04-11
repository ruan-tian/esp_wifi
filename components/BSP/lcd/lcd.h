#ifndef LCD_H__
#define LCD_H__

#include <stdint.h>
#include "driver/gpio.h"

#define LCD_DC_PIN          GPIO_NUM_2
#define LCD_RST_PIN         GPIO_NUM_4
#define LCD_BL_PIN          GPIO_NUM_15
#define LCD_WIDTH           240
#define LCD_HEIGHT          320

// 初始化屏幕和底层 SPI
void lcd_init(void);

// LVGL 专用的底层刷屏接口：给指定区域填充颜色数组
void lcd_draw_color_buf(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *color_data);

#endif