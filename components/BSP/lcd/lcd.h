/**
 * @file lcd.h
 * @brief 优化版 ILI9341 硬件抽象层
 * * 修改说明：移除了冗余的字库和绘图函数，专为 LVGL 提供底层支撑。
 */

 #ifndef LCD_H__
 #define LCD_H__
 
 #include "driver/gpio.h"
 #include "esp_err.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 // ===================== 硬件引脚配置 =====================
 #define LCD_DC_PIN          GPIO_NUM_2
 #define LCD_RST_PIN         GPIO_NUM_4
 #define LCD_BL_PIN          GPIO_NUM_15
 
 // ===================== 屏幕尺寸 =====================
 #define LCD_WIDTH           240
 #define LCD_HEIGHT          320
 
 // ===================== 核心驱动接口 =====================
 /**
  * @brief LCD 总初始化
  * 包含 GPIO、SPI 协议栈以及 ILI9341 寄存器初始化。
  */
 void lcd_init(void);
 
 /**
  * @brief 设置显示窗口 (供 LVGL 刷新调用)
  */
 void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
 
 /**
  * @brief 发送大批量颜色数据 (供 LVGL 刷新调用)
  */
 void spi_send_buf(const uint8_t *data, uint32_t len);
 
 /**
  * @brief 设置背光亮度 (0-100)
  */
 void lcd_set_brightness(uint8_t level);
 
 /**
  * @brief 进入/退出休眠
  */
 void lcd_sleep(void);
 void lcd_wakeup(void);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* LCD_H__ */