#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

/**
 * @brief 启动 Web 服务器。
 *
 * 该接口会在内部完成文件系统初始化、HTTP 服务启动以及路由注册。
 * 重复调用时，若服务器已启动会直接返回成功。
 *
 * @return
 * - ESP_OK: 服务器启动成功或已经处于运行状态
 * - ESP_FAIL: 启动过程失败（例如 SPIFFS 初始化失败）
 */
esp_err_t start_webserver(void);

/**
 * @brief 停止 Web 服务器并释放相关资源。
 *
 * 在服务器已启动时会停止 HTTP 服务并注销 SPIFFS 挂载。
 * 若服务器未启动，函数直接返回且不产生副作用。
 */
void stop_webserver(void);

#endif