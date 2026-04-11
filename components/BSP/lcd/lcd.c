#include "lcd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "LCD_HAL";

// ===================== 底层寄存器写入 =====================

static void lcd_write_cmd(uint8_t cmd) {
    gpio_set_level(LCD_DC_PIN, 0); // DC 拉低表示命令
    spi_send_byte(cmd);
}

static void lcd_write_data(uint8_t data) {
    gpio_set_level(LCD_DC_PIN, 1); // DC 拉高表示数据
    spi_send_byte(data);
}

// 供外部调用的窗口设置
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_write_cmd(0x2A); // Column Address Set
    lcd_write_data(x0 >> 8);
    lcd_write_data(x0 & 0xFF);
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);

    lcd_write_cmd(0x2B); // Page Address Set
    lcd_write_data(y0 >> 8);
    lcd_write_data(y0 & 0xFF);
    lcd_write_data(y1 >> 8);
    lcd_write_data(y1 & 0xFF);

    lcd_write_cmd(0x2C); // Memory Write
    gpio_set_level(LCD_DC_PIN, 1); // 准备开始发送像素
}

// ===================== ILI9341 核心初始化序列 =====================

void ili9341_init_sequence(void) {
    // 硬件复位
    gpio_set_level(LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 这里保留你之前代码中验证通过的完整初始化序列
    lcd_write_cmd(0xCF); lcd_write_data(0x00); lcd_write_data(0xC1); lcd_write_data(0x30);
    lcd_write_cmd(0xED); lcd_write_data(0x64); lcd_write_data(0x03); lcd_write_data(0x12); lcd_write_data(0x81);
    lcd_write_cmd(0xE8); lcd_write_data(0x85); lcd_write_data(0x00); lcd_write_data(0x78);
    lcd_write_cmd(0xCB); lcd_write_data(0x39); lcd_write_data(0x2C); lcd_write_data(0x00); lcd_write_data(0x34); lcd_write_data(0x02);
    lcd_write_cmd(0xF7); lcd_write_data(0x20);
    lcd_write_cmd(0xEA); lcd_write_data(0x00); lcd_write_data(0x00);
    
    lcd_write_cmd(0xC0); lcd_write_data(0x1D); // Power Control 1
    lcd_write_cmd(0xC1); lcd_write_data(0x12); // Power Control 2
    lcd_write_cmd(0xC5); lcd_write_data(0x33); lcd_write_data(0x34); // VCOM 1
    lcd_write_cmd(0xC7); lcd_write_data(0x92); // VCOM 2
    
    lcd_write_cmd(0x36); lcd_write_data(0x48); // Memory Access Control: 竖屏方向
    lcd_write_cmd(0x3A); lcd_write_data(0x55); // Pixel Format: 16-bit RGB565
    
    lcd_write_cmd(0xB1); lcd_write_data(0x00); lcd_write_data(0x12); // Frame Rate
    lcd_write_cmd(0xB6); lcd_write_data(0x0A); lcd_write_data(0xA2); // Display Function
    
    lcd_write_cmd(0x11); // Exit Sleep
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_write_cmd(0x29); // Display ON
}

// ===================== 顶层接口 =====================

void lcd_init(void) {
    // 1. 初始化引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_DC_PIN) | (1ULL << LCD_RST_PIN) | (1ULL << LCD_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    // 2. 初始化 SPI 总线 (调用你原有的 spi_master_init)
    spi_master_init();

    // 3. 执行屏幕初始化序列
    ili9341_init_sequence();

    // 4. 打开背光
    lcd_set_brightness(100);
    ESP_LOGI(TAG, "LCD 底层硬件驱动就绪");
}

void lcd_set_brightness(uint8_t level) {
    // 简单开关控制，如需渐变可改用 LEDC PWM
    gpio_set_level(LCD_BL_PIN, level > 0 ? 1 : 0);
}

void lcd_sleep(void) {
    lcd_write_cmd(0x10);
    vTaskDelay(pdMS_TO_TICKS(120));
}

void lcd_wakeup(void) {
    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
}