#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "lcd.h"
#include "wifi_scanner.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// ===================== HTML 前端页面 =====================
// 这是一个精简的现代风格 HTML 页面，包含 WiFi 列表请求和消息发送功能
const char* html_page = 
"<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 Dashboard</title>"
"<style>"
"body{font-family:Arial;margin:0;padding:20px;background:#f4f4f9;}"
".card{background:#fff;padding:20px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.1);margin-bottom:20px;}"
"button{background:#007bff;color:#fff;border:none;padding:10px 15px;border-radius:5px;cursor:pointer;width:100%;font-size:16px;margin-top:10px;}"
"input{width:100%;padding:10px;box-sizing:border-box;border:1px solid #ccc;border-radius:5px;font-size:16px;}"
"ul{list-style:none;padding:0;} li{padding:10px 0;border-bottom:1px solid #eee;}"
"</style></head><body>"
"<div class='card'><h2>📡 隔空传书</h2>"
"<input type='text' id='msg' placeholder='输入要显示在屏幕上的中文...'>"
"<button onclick='sendMsg()'>发送到屏幕</button></div>"
"<div class='card'><h2>📶 环境 WiFi 监控</h2>"
"<button onclick='getWiFi()'>刷新 WiFi 列表</button>"
"<ul id='wlist'></ul></div>"
"<script>"
"function sendMsg() {"
"  let txt = document.getElementById('msg').value;"
"  fetch('/api/msg', {method:'POST', body:txt}).then(()=>alert('已发送！'));"
"}"
"function getWiFi() {"
"  fetch('/api/wifi').then(r=>r.json()).then(d=>{"
"    let s='';"
"    d.forEach(w=> s+='<li><b>'+w.ssid+'</b><br><small>信号: '+w.rssi+' dBm</small></li>');"
"    document.getElementById('wlist').innerHTML=s;"
"  });"
"}"
"</script></body></html>";

// ===================== API 路由处理函数 =====================

// 1. GET "/" - 返回主页 HTML
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// 2. GET "/api/wifi" - 返回周围环境的 WiFi 列表 (JSON格式)
static esp_err_t wifi_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    
    // 手动拼接 JSON，避免引入庞大的 cJSON 库造成内存碎片
    char *buf = malloc(1024);
    if (!buf) return ESP_FAIL;
    
    strcpy(buf, "[");
    // 最多返回 10 个，防止 JSON 字符串超长
    int limit = (g_ap_count > 10) ? 10 : g_ap_count;
    for(int i = 0; i < limit; i++) {
        char item[128];
        snprintf(item, sizeof(item), "{\"ssid\":\"%s\",\"rssi\":%d}%s", 
                 g_ap_records[i].ssid, 
                 g_ap_records[i].rssi, 
                 (i == limit - 1) ? "" : ",");
        strcat(buf, item);
    }
    strcat(buf, "]");
    
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

// 3. POST "/api/msg" - 接收手机发来的文字并在 LCD 上弹出
static esp_err_t msg_post_handler(httpd_req_t *req) {
    char msg[128] = {0};
    int recv_len = httpd_req_recv(req, msg, MIN(req->content_len, sizeof(msg) - 1));
    
    if (recv_len > 0) {
        ESP_LOGI(TAG, "收到 Web 消息: %s", msg);
        
        // 【UI 交互】在屏幕中央画一个酷炫的蓝色弹窗，并用你优化的安全函数渲染中文
        ili9341_draw_round_rect(20, 180, LCD_WIDTH - 40, 50, 5, LCD_COLOR_BLUE);
        ili9341_draw_string_utf8_limit(30, 195, msg, LCD_COLOR_WHITE, LCD_COLOR_BLUE, LCD_WIDTH - 60);
    }
    
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

// ===================== 服务器控制 =====================
esp_err_t start_webserver(void) {
    if (server != NULL) return ESP_OK; // 已经启动

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4; // 分配足够的路由槽位

    esp_err_t ret = httpd_start(&server, &config);
    if (ret == ESP_OK) {
        httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_wifi  = { .uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_msg   = { .uri = "/api/msg", .method = HTTP_POST, .handler = msg_post_handler, .user_ctx = NULL };
        
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_wifi);
        httpd_register_uri_handler(server, &uri_msg);
        ESP_LOGI(TAG, "Web 服务器已在端口 80 启动");
    }
    return ret;
}

void stop_webserver(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web 服务器已停止");
    }
}