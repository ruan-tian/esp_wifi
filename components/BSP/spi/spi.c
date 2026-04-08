#include "spi.h"
#include "esp_log.h"

// 日志标签
static const char *TAG = "SPI_DRIVER";
// SPI设备句柄（全局唯一）
spi_device_handle_t g_spi_handle = NULL;

/**
 * @brief  SPI主机初始化（ESP-IDF5.4.1标准，DMA开启）
 * 对应STM32：MX_SPI_Init() + HAL_SPI_Start_DMA()
 * 配置说明：所有配置均为ESP32高速SPI最佳实践
 */
esp_err_t spi_master_init(void)
{
    esp_err_t ret;

    // 1. SPI总线配置
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_MOSI_PIN,      // 发送引脚
        .miso_io_num = SPI_MISO_PIN,      // 接收引脚（无效）
        .sclk_io_num = SPI_SCLK_PIN,      // 时钟引脚
        .quadwp_io_num = -1,              // 不使用四线SPI，禁用
        .quadhd_io_num = -1,              // 不使用四线SPI，禁用
        .max_transfer_sz = SPI_MAX_TRANSFER,// 最大传输大小
        .intr_flags = ESP_INTR_FLAG_IRAM, // 【配置】中断放IRAM，避免延迟（对应STM32中断优先级）
    };

    // 初始化SPI总线 + 自动分配DMA通道
    // 【配置】SPI_DMA_CH_AUTO = 开启DMA（对应STM32 SPI DMA使能）
    ret = spi_bus_initialize(SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if(ret != ESP_OK){
        ESP_LOGE(TAG,"SPI总线初始化失败");
        return ret;
    }

    // 2. SPI设备配置（ILI9341）
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLK_SPEED,  // 时钟频率
        .mode = SPI_MODE,                 // SPI模式0
        .spics_io_num = SPI_CS_PIN,       // 片选引脚
        .queue_size = 7,                  // 传输队列（官方默认值）
    };

    // 将LCD设备挂载到SPI总线
    ret = spi_bus_add_device(SPI_HOST, &dev_cfg, &g_spi_handle);
    if(ret != ESP_OK){
        ESP_LOGE(TAG,"SPI设备添加失败");
        return ret;
    }

    ESP_LOGI(TAG,"SPI初始化完成(DMA已开启)");
    return ESP_OK;
}

/**
 * @brief  SPI发送单字节（阻塞模式）
 * @param  data: 要发送的8位数据
 * 对应STM32：HAL_SPI_Transmit(&hspi, &data, 1, 100)
 */
void spi_send_byte(uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,          // 数据长度：8bit
        .tx_buffer = &data,   // 发送缓冲区
    };
    // 轮询发送（简单可靠，适合LCD命令）
    spi_device_polling_transmit(g_spi_handle, &t);
}

/**
 * @brief  SPI发送16位数据（RGB565颜色）
 * @param  data: 16位颜色数据
 * 对应STM32：SPI发送16bit数据
 */
void spi_send_16bit(uint16_t data)
{
    spi_transaction_t t = {
        .length = 16,         // 数据长度：16bit
        .tx_buffer = &data,   // 发送缓冲区
    };
    spi_device_polling_transmit(g_spi_handle, &t);
}

/**
 * @brief  SPI批量发送数据（DMA硬件加速）
 * @param  buf: 数据缓冲区
 * @param  len: 数据长度
 * 对应STM32：HAL_SPI_Transmit_DMA() 大批量刷屏
 */
void spi_send_buf(const uint8_t *buf, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,    // 转换为bit长度
        .tx_buffer = buf,     // 数据指针
    };
    spi_device_polling_transmit(g_spi_handle, &t);
}