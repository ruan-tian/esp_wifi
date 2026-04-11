#include "lcd.h"
#include "spi.h"        // SPI底层驱动依赖，负责硬件SPI数据发送
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  // FreeRTOS系统延时，用于LCD初始化时序
#include "string.h"
/**
 * @brief  LCD底层发送命令函数
 * @param  cmd: ILI9341命令码
 * @note   4线SPI规则：DC引脚=0 → 传输的是命令
 */
static void lcd_write_cmd(uint8_t cmd) {
    gpio_set_level(LCD_DC_PIN, 0);  // DC拉低，表示后续传输命令
    spi_send_byte(cmd);             // 通过SPI发送单字节命令
}

/**
 * @brief  LCD底层发送数据函数
 * @param  data: 要发送的8位数据
 * @note   4线SPI规则：DC引脚=1 → 传输的是数据/参数
 */
static void lcd_write_data(uint8_t data) {
    gpio_set_level(LCD_DC_PIN, 1);  // DC拉高，表示后续传输数据
    spi_send_byte(data);             // 通过SPI发送单字节数据
}

/**
 * @brief  设置LCD显示窗口（刷屏必备）
 * @param  x0: 起始X坐标
 * @param  y0: 起始Y坐标
 * @param  x1: 结束X坐标
 * @param  y1: 结束Y坐标
 * @note   坐标范围：竖屏模式 X:0~239 Y:0~319
 */
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // 0x2A：设置列地址X
    lcd_write_cmd(0x2A);
    lcd_write_data(x0 >> 8);   // 起始X 高8位
    lcd_write_data(x0 & 0xFF); // 起始X 低8位
    lcd_write_data(x1 >> 8);   // 结束X 高8位
    lcd_write_data(x1 & 0xFF); // 结束X 低8位

    // 0x2B：设置页地址Y
    lcd_write_cmd(0x2B);
    lcd_write_data(y0 >> 8);   // 起始Y 高8位
    lcd_write_data(y0 & 0xFF); // 起始Y 低8位
    lcd_write_data(y1 >> 8);   // 结束Y 高8位
    lcd_write_data(y1 & 0xFF); // 结束Y 低8位

    // 0x2C：写显存命令（发送完此命令后，直接发像素颜色数据）
    lcd_write_cmd(0x2C); 
}

/**
 * @brief  供LVGL调用的高速刷屏接口（核心渲染函数）
 * @param  x1/y1: 刷新区域左上角坐标
 * @param  x2/y2: 刷新区域右下角坐标
 * @param  color_data: RGB565颜色数据缓冲区指针
 * @note   1. 自动处理ESP32 SPI DMA传输大小限制，防止画面撕裂
 * @note   2. 1个像素 = 2字节(RGB565)
 */
void lcd_draw_color_buf(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *color_data) {
    // 第一步：设置要刷新的屏幕区域
    lcd_set_window(x1, y1, x2, y2);
    gpio_set_level(LCD_DC_PIN, 1); // DC持续拉高，准备连续发送像素数据
    
    // 计算总传输字节数：宽度*高度*2（RGB565格式）
    uint32_t total_bytes = (x2 - x1 + 1) * (y2 - y1 + 1) * 2;
    const uint8_t *buf = (const uint8_t *)color_data;  // 强制类型转换，按字节发送

    // ESP32 SPI DMA单次最大传输约4092字节，拆分4000字节发送，避免溢出
    uint32_t max_chunk = 4000;
    uint32_t sent = 0;  // 已发送字节数

    // 循环分片发送所有颜色数据
    while (sent < total_bytes) {
        uint32_t chunk_size = (total_bytes - sent > max_chunk) ? max_chunk : (total_bytes - sent);
        spi_send_buf(buf + sent, chunk_size);  // 调用底层SPI发送缓冲区
        sent += chunk_size;
    }
}

/**
 * @brief  ILI9341 LCD初始化函数（上电必须调用）
 * @note   包含：硬件初始化、复位、寄存器配置、清屏、背光开启
 */
void lcd_init(void) {
    // ====================== 1. GPIO引脚初始化 ======================
    // 配置DC(命令/数据)、RST(复位)、BL(背光)引脚为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_DC_PIN) | (1ULL << LCD_RST_PIN) | (1ULL << LCD_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    // ====================== 2. SPI总线初始化 ======================
    spi_master_init();  // 初始化硬件SPI，匹配LCD通信时序

    // ====================== 3. LCD硬件复位 ======================
    gpio_set_level(LCD_RST_PIN, 0);  // 拉低复位引脚，开始复位
    vTaskDelay(pdMS_TO_TICKS(20));   // 保持低电平20ms
    gpio_set_level(LCD_RST_PIN, 1);  // 拉高复位引脚，结束复位
    vTaskDelay(pdMS_TO_TICKS(120));  // 等待LCD内部电路稳定

    // ====================== 4. ILI9341寄存器配置 ======================
    // 以下为ILI9341官方标准电源/时序初始化序列
    lcd_write_cmd(0xCF); lcd_write_data(0x00); lcd_write_data(0xC1); lcd_write_data(0x30);
    lcd_write_cmd(0xED); lcd_write_data(0x64); lcd_write_data(0x03); lcd_write_data(0x12); lcd_write_data(0x81);
    lcd_write_cmd(0xE8); lcd_write_data(0x85); lcd_write_data(0x00); lcd_write_data(0x78);
    lcd_write_cmd(0xCB); lcd_write_data(0x39); lcd_write_data(0x2C); lcd_write_data(0x00); lcd_write_data(0x34); lcd_write_data(0x02);
    lcd_write_cmd(0xF7); lcd_write_data(0x20);
    lcd_write_cmd(0xEA); lcd_write_data(0x00); lcd_write_data(0x00);
    
    lcd_write_cmd(0xC0); lcd_write_data(0x1D); // 电源控制1
    lcd_write_cmd(0xC1); lcd_write_data(0x12); // 电源控制2
    lcd_write_cmd(0xC5); lcd_write_data(0x33); lcd_write_data(0x34); // VCOM控制1
    lcd_write_cmd(0xC7); lcd_write_data(0x92); // VCOM控制2
    
    // ====================== 核心配置：屏幕方向+颜色顺序 ======================
    // 0x36：内存访问控制寄存器
    // 0x40：标准竖屏 + RGB颜色正常(无反转) + 无水平镜像(字体正常)
    lcd_write_cmd(0x36); 
    lcd_write_data(0x40);
    
    // ====================== 像素格式设置 ======================
    // 0x3A：设置像素格式
    // 0x55 = RGB565 16位色（嵌入式最常用，2字节/像素）
    lcd_write_cmd(0x3A); lcd_write_data(0x55); 
    
    // ====================== 显示参数配置 ======================
    lcd_write_cmd(0xB1); lcd_write_data(0x00); lcd_write_data(0x12); // 设置屏幕帧率
    lcd_write_cmd(0xB6); lcd_write_data(0x0A); lcd_write_data(0xA2); // 显示功能配置
    
    // ====================== Gamma色彩校准 ======================
    lcd_write_cmd(0xF2); lcd_write_data(0x00); // 关闭Gamma功能
    lcd_write_cmd(0x26); lcd_write_data(0x01); // 设置Gamma曲线
    
    // 正向Gamma校正参数
    lcd_write_cmd(0xE0);
    uint8_t gamma_pos[] = {0x0F, 0x22, 0x1F, 0x0B, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    for (int i = 0; i < 15; i++) lcd_write_data(gamma_pos[i]);
    
    // 负向Gamma校正参数
    lcd_write_cmd(0xE1);
    uint8_t gamma_neg[] = {0x00, 0x1D, 0x20, 0x04, 0x10, 0x08, 0x34, 0x32, 0x08, 0x06, 0x00, 0x1D, 0x24, 0x0F, 0x00};
    for (int i = 0; i < 15; i++) lcd_write_data(gamma_neg[i]);

    // ====================== 5. 启动LCD显示 ======================
    lcd_write_cmd(0x11); // 0x11：退出睡眠模式
    vTaskDelay(pdMS_TO_TICKS(120)); // 退出睡眠后必须延时120ms以上
    lcd_write_cmd(0x29); // 0x29：开启屏幕显示
    vTaskDelay(pdMS_TO_TICKS(50));

    // ====================== 6. 全屏清屏（消除开机花屏） ======================
    // 定义一行黑色数据，逐行刷新全屏，清除显存随机噪点
    uint16_t *black_buf = (uint16_t *)heap_caps_malloc(240 * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (black_buf != NULL) {
        memset(black_buf, 0, 240 * sizeof(uint16_t)); // 填充黑色
        for(int i = 0; i < 320; i++) {
            lcd_draw_color_buf(0, i, 239, i, black_buf);
        }
        heap_caps_free(black_buf); // 用完释放
    }

    // ====================== 7. 开启屏幕背光 ======================
    gpio_set_level(LCD_BL_PIN, 1);
}