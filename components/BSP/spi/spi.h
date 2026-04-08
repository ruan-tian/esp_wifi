#ifndef SPI_H
#define SPI_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

/************************* SPI 核心配置 *************************/
#define SPI_HOST          SPI2_HOST
#define SPI_SCLK_PIN      GPIO_NUM_16
#define SPI_MOSI_PIN      GPIO_NUM_17
#define SPI_MISO_PIN      GPIO_NUM_NC
#define SPI_CS_PIN        GPIO_NUM_19
#define SPI_CLK_SPEED     40*1000*1000
#define SPI_MODE          0
#define SPI_MAX_TRANSFER  240*320*2

/************************* 函数声明 *************************/
esp_err_t spi_master_init(void);
void spi_send_byte(uint8_t data);
void spi_send_16bit(uint16_t data);
void spi_send_buf(const uint8_t *buf, size_t len);

extern spi_device_handle_t g_spi_handle;

#endif
