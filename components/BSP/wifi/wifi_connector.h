#ifndef WIFI_CONNECTOR_H
#define WIFI_CONNECTOR_H

#include "esp_err.h"

/**
 * @brief 连接WiFi（阻塞直到连接成功或超时）
 * 
 * 该函数会配置并连接到指定的WiFi网络，内部使用事件组等待连接结果。
 * 
 * @param ssid     目标WiFi的SSID（字符串）
 * @param password 目标WiFi的密码（字符串）
 * @param timeout_ms 连接超时时间（毫秒），超过此时间仍未连接成功则返回失败
 * 
 * @return ESP_OK 连接成功；ESP_FAIL 连接失败（超时或认证错误等）
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password, int timeout_ms);

/**
 * @brief 初始化WiFi为Station模式（扫描+连接共用）
 * 
 * 该函数会初始化NVS、网络接口、事件循环和WiFi协议栈。
 * 如果已经初始化过，则直接返回，避免重复初始化。
 */
void wifi_connector_init(void);

esp_err_t wifi_smartconfig_start(int timeout_ms);

const char* wifi_get_connected_ssid(void);
#endif