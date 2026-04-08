#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "nvs_flash.h"
#include "wifi_connector.h"

// 日志标签，用于标识此模块的日志输出
static const char *TAG = "WIFI_CONN";

// 定义事件组中的位标志
#define WIFI_CONNECTED_BIT  BIT0      // WiFi连接成功标志位
#define WIFI_FAIL_BIT       BIT1      // WiFi连接失败标志位
#define SMARTCONFIG_DONE    BIT2      // SmartConfig完成标志位
#define MAX_RETRY           5         // 最大重试连接次数

// 全局变量定义
static EventGroupHandle_t s_wifi_event_group;     // WiFi事件组句柄，用于同步WiFi状态
static int s_retry_num = 0;                      // 连接重试计数器
static bool s_wifi_inited = false;               // WiFi初始化状态标志
static char s_sc_ssid[33] = {0};                 // 存储SmartConfig获取的SSID
static char s_sc_password[65] = {0};             // 存储SmartConfig获取的密码

/**
 * @brief WiFi和SmartConfig事件处理回调函数
 * 
 * @param arg 传递给事件处理程序的参数
 * @param event_base 事件基类型
 * @param event_id 事件ID
 * @param event_data 事件数据指针
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    // 处理WiFi事件：当STA模式启动时
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started");
    }
    // 处理WiFi断开连接事件
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // 如果当前重试次数大于0且小于最大重试次数，则继续尝试连接
        if (s_retry_num > 0 && s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting (%d/%d)", s_retry_num, MAX_RETRY);
        } 
        // 如果已达到最大重试次数，则标记连接失败
        else if (s_retry_num >= MAX_RETRY) {
            ESP_LOGE(TAG, "Connect failed after %d retries", MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    // 处理IP获取事件：当获取到IP地址时
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;  // 获取IP成功后，重置重试计数器
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);  // 设置连接成功标志位
    }
    // 处理SmartConfig事件：获取到SSID和密码
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        smartconfig_event_got_ssid_pswd_t *evt =
            (smartconfig_event_got_ssid_pswd_t *)event_data;

        // 清空存储的SSID缓冲区并复制新的SSID
        memset(s_sc_ssid, 0, sizeof(s_sc_ssid));
        memcpy(s_sc_ssid, evt->ssid, sizeof(evt->ssid));

        // 清空存储的密码缓冲区并复制新的密码
        memset(s_sc_password, 0, sizeof(s_sc_password));
        memcpy(s_sc_password, evt->password, sizeof(evt->password));

        ESP_LOGI(TAG, "SmartConfig got SSID: %s", s_sc_ssid);

        // 停止SmartConfig过程
        esp_smartconfig_stop();

        // 配置WiFi连接参数
        wifi_config_t wifi_config = {0};
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(evt->password));

        // 如果是ESPTouch V2类型，设置相应的认证模式
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        }

        // 应用WiFi配置并开始连接
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();

        // 设置SmartConfig完成标志位
        xEventGroupSetBits(s_wifi_event_group, SMARTCONFIG_DONE);
    }
}

/**
 * @brief 初始化WiFi连接器
 * 
 * 初始化NVS闪存、网络接口、WiFi驱动，并注册事件处理程序
 */
void wifi_connector_init(void)
{
    // 检查是否已经初始化过，避免重复初始化
    if (s_wifi_inited) return;

    // 初始化NVS闪存，用于存储WiFi配置信息
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化TCP/IP网络适配层
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 创建默认STA模式的网络接口
    esp_netif_create_default_wifi_sta();

    // 初始化WiFi配置结构体为默认值
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 定义事件处理实例句柄
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_sc;

    // 注册WiFi事件处理程序，处理所有WiFi事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &event_handler, NULL, &instance_any_id));

    // 注册IP获取事件处理程序，处理IP获取事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &event_handler, NULL, &instance_got_ip));

    // 注册SmartConfig事件处理程序，处理SmartConfig相关事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        SC_EVENT, ESP_EVENT_ANY_ID,
        &event_handler, NULL, &instance_sc));

    // 设置WiFi工作模式为STA（Station）模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 创建事件组，用于同步WiFi连接状态
    s_wifi_event_group = xEventGroupCreate();
    s_wifi_inited = true;
    ESP_LOGI(TAG, "WiFi connector initialized");
}

/**
 * @brief 使用指定的SSID和密码连接到WiFi网络
 * 
 * @param ssid 要连接的WiFi网络名称
 * @param password WiFi网络密码
 * @param timeout_ms 连接超时时间（毫秒）
 * @return esp_err_t 成功返回ESP_OK，失败返回ESP_FAIL
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password, int timeout_ms)
{
    // 如果WiFi未初始化，则先进行初始化
    if (!s_wifi_inited) wifi_connector_init();

    // 清除事件组中的所有标志位
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | SMARTCONFIG_DONE);
    s_retry_num = 0;  // 重置重试计数器

    // 断开当前WiFi连接（如果有）
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));  // 短暂延迟，确保断开完成

    // 配置WiFi连接参数
    wifi_config_t wifi_config = {0};
    // 复制SSID，注意防止字符串溢出
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    // 复制密码，注意防止字符串溢出
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    // 设置认证模式为WPA2-PSK
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Connecting to: %s", ssid);
    // 设置WiFi配置并开始连接
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // 等待连接结果，检查是否成功或失败
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    // 再次清除事件组标志位
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | SMARTCONFIG_DONE);

    // 检查连接结果
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to %s", ssid);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to connect to %s", ssid);
    return ESP_FAIL;
}

/**
 * @brief 启动SmartConfig配网功能
 * 
 * @param timeout_ms SmartConfig超时时间（毫秒）
 * @return esp_err_t 成功返回ESP_OK，失败返回ESP_FAIL
 */
esp_err_t wifi_smartconfig_start(int timeout_ms)
{
    // 如果WiFi未初始化，则先进行初始化
    if (!s_wifi_inited) wifi_connector_init();

    // 清除事件组中的所有标志位
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | SMARTCONFIG_DONE);
    s_retry_num = 0;  // 重置重试计数器

    // 断开当前WiFi连接（如果有）
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));  // 短暂延迟，确保断开完成

    ESP_LOGI(TAG, "Starting SmartConfig...");

    // 配置SmartConfig启动参数
    smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    // 设置SmartConfig类型为ESPTouch V2
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2));
    // 启动SmartConfig
    ESP_ERROR_CHECK(esp_smartconfig_start(&sc_cfg));

    // 等待SmartConfig完成事件
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                        SMARTCONFIG_DONE,
                        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    // 检查SmartConfig是否超时
    if (!(bits & SMARTCONFIG_DONE)) {
        esp_smartconfig_stop();  // 停止SmartConfig
        ESP_LOGE(TAG, "SmartConfig timeout");
        return ESP_FAIL;
    }

    // SmartConfig完成后，等待WiFi连接结果
    bits = xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));  // 给予30秒时间连接

    // 清除事件组标志位
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | SMARTCONFIG_DONE);

    // 检查WiFi连接结果
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "SmartConfig connected to %s", s_sc_ssid);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "SmartConfig connection failed");
    return ESP_FAIL;
}

/**
 * @brief 获取当前连接的WiFi网络的SSID
 * 
 * @return const char* 当前连接的WiFi网络的SSID，如果是通过SmartConfig连接的话
 */
const char *wifi_get_connected_ssid(void)
{
    return s_sc_ssid;
}