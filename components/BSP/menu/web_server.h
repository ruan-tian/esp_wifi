#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

// 启动 Web 服务器
esp_err_t start_webserver(void);

// 停止 Web 服务器
void stop_webserver(void);

#endif