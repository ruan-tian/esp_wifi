# ESP32-S3-WROOM-1U-N16R8 可用引脚总表（Markdown 标准版）
适用芯片：**ESP32-S3-WROOM-1U-N16R8**（16MB Flash + 8MB PSRAM）
说明：**3.3V 电平，不可直连 5V；所有引脚默认可做通用 GPIO，部分有硬件约束**

## 一、完全不可用引脚（硬件绑定 PSRAM，禁止使用）
| GPIO 编号 | 状态 | 原因 | 能否强行配置 |
|---------|------|------|-------------|
| GPIO35  | 不可用 | 模组内部接 PSRAM 数据线 | 否，硬件占用 |
| GPIO36  | 不可用 | 模组内部接 PSRAM 数据线 | 否，硬件占用 |
| GPIO37  | 不可用 | 模组内部接 PSRAM 数据线 | 否，硬件占用 |

## 二、慎用引脚（影响下载/启动/日志，非必要不使用）
| GPIO 编号 | 状态 | 特殊约束 | 推荐使用方式 | 注意事项 |
|---------|------|----------|-------------|----------|
| GPIO0   | 慎用 | 下载/启动模式选择脚 | 仅做输入，外部必须上拉 | 低电平开机进入下载模式，易导致无法启动 |
| GPIO43  | 慎用 | 默认 UART0 RX（下载串口） | 不接关键外设，避免烧录异常 | 占用后可能影响固件下载与串口打印 |
| GPIO44  | 慎用 | 默认 UART0 TX（下载串口） | 不接关键外设，避免烧录异常 | 占用后可能影响固件下载与串口打印 |
| GPIO46  | 慎用 | 默认日志打印输出 | 仅做普通输入/输出 | 占用后串口日志会消失，调试不便 |

## 三、通用推荐可用引脚（无特殊约束，优先选用）
| GPIO 编号 | 可用性 | 支持功能 | 最推荐用途 |
|---------|--------|----------|-----------|
| GPIO4   | 推荐 | GPIO、ADC1、Touch | LED、按键、普通传感器 |
| GPIO5   | 推荐 | GPIO、ADC1、Touch | LED、按键、继电器控制 |
| GPIO6   | 推荐 | GPIO、ADC1、Touch | 通用 IO、PWM 输出 |
| GPIO7   | 推荐 | GPIO、ADC1、Touch | 通用 IO、PWM 输出 |
| GPIO8   | 推荐 | GPIO、ADC1、Touch | 通用 IO、PWM 输出 |
| GPIO9   | 推荐 | GPIO、ADC1、Touch | 通用 IO、PWM 输出 |
| GPIO10  | 推荐 | GPIO、ADC1、Touch | 通用 IO、PWM 输出 |
| GPIO11  | 推荐 | GPIO、ADC1、Touch | 通用 IO、PWM 输出 |
| GPIO12  | 推荐 | GPIO、SPI、ADC1 | SPI MOSI（LCD/W25Q64） |
| GPIO13  | 推荐 | GPIO、SPI、ADC1 | SPI MISO（LCD/W25Q64） |
| GPIO14  | 推荐 | GPIO、SPI、ADC1 | SPI SCK（LCD/W25Q64） |
| GPIO15  | 推荐 | GPIO、SPI、ADC1 | SPI CS（片选信号） |
| GPIO16  | 推荐 | GPIO、ADC1 | 通用 IO、传感器控制 |
| GPIO17  | 推荐 | GPIO、ADC1 | 通用 IO、传感器控制 |
| GPIO18  | 推荐 | GPIO、ADC1 | 通用 IO、传感器控制 |
| GPIO19  | 推荐 | GPIO、USB D- | USB 设备 / 普通 IO |
| GPIO20  | 推荐 | GPIO、USB D+ | USB 设备 / 普通 IO |
| GPIO21  | 推荐 | GPIO、I2C SDA | I2C 传感器（MAX30102、OLED） |
| GPIO22  | 推荐 | GPIO、I2C SCL | I2C 传感器（MAX30102、OLED） |

## 四、次级可用引脚（可正常使用，部分带 ADC/DAC）
| GPIO 编号 | 可用性 | 支持功能 | 推荐用途 |
|---------|--------|----------|----------|
| GPIO26  | 可用 | GPIO、DAC、ADC2 | 音频、模拟电压输出 |
| GPIO27  | 可用 | GPIO、DAC、ADC2 | 音频、模拟电压输出 |
| GPIO32  | 可用 | GPIO、ADC1 | 高阻模拟信号采集 |
| GPIO33  | 可用 | GPIO、ADC1 | 高阻模拟信号采集 |
| GPIO34  | 可用 | GPIO、ADC1 | 高阻模拟信号采集 |
| GPIO38  | 可用 | GPIO | 通用 IO、PWM |
| GPIO39  | 可用 | GPIO | 通用 IO、PWM |
| GPIO40  | 可用 | GPIO | 通用 IO、PWM |
| GPIO41  | 可用 | GPIO | 通用 IO、PWM |
| GPIO42  | 可用 | GPIO | 通用 IO、PWM |

---

# 快速使用建议（直接照抄）
1. **做 LCD + SPI Flash 优先用**：GPIO12、13、14、15
2. **做 I2C 传感器（MAX30102）优先用**：GPIO21(SDA)、GPIO22(SCL)
3. **做按键/LED 优先用**：GPIO4 ~ GPIO11
4. **绝对不要碰**：GPIO35、36、37
5. **尽量别用**：GPIO0、43、44、46（影响调试与下载）