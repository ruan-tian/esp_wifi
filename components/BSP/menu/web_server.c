#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "lcd.h"
#include "wifi_scanner.h"

/**
 * @file web_server.c
 * @brief 设备端 HTTP 服务器实现
 *
 * 主要职责：
 * 1) 挂载 SPIFFS 并对外提供静态网页资源；
 * 2) 提供 WiFi 扫描结果查询接口；
 * 3) 提供前端消息下发接口，并将消息绘制到 LCD。
 *
 * 路由结构：
 * - GET  /api/wifi : 返回 AP 列表 JSON；
 * - POST /api/msg  : 接收文本消息并显示；
 * - GET  /*        : 静态文件（index.html/css/js 等）。
 */
static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/** 单次文件分块发送大小（字节） */
#define SCRATCH_BUFSIZE 4096

/**
 * @brief 初始化 SPIFFS 文件系统。
 *
 * 挂载分区表中名为 `storage` 的分区到 `/spiffs`，
 * 并打印总容量与已使用容量，便于调试存储占用情况。
 *
 * @return
 * - ESP_OK: 挂载成功
 * - 其他:   挂载失败（可通过日志查看具体错误码）
 */
static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "正在初始化 SPIFFS 文件系统...");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",        // 挂载路径：以后文件都放在 /spiffs/ 下面
      .partition_label = "storage", // 对应分区表中的名字
      .max_files = 5,               // 最大文件数
      .format_if_mount_failed = true // 如果挂载失败，则格式化
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);    // 把 SPIFFS 注册到系统（挂载到指定路径）
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 挂载失败 (%s)", esp_err_to_name(ret));// 打印错误信息
        return ret;
    }

    size_t total = 0, used = 0; // 获取文件系统总大小和已使用大小
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "文件系统状态: 总计 %d KB, 已用 %d KB", total / 1024, used / 1024); // 打印文件系统状态
    }
    return ESP_OK;
}

/**
 * @brief 静态文件分发处理器（GET）。
 *
 * 将浏览器请求 URI 映射到 `/spiffs` 下的目标文件并回传：
 * - 当 URI 为 `/` 时，默认返回 `index.html`；
 * - 其他 URI 直接拼接到 `/spiffs` 后；
 * - 使用 chunk 模式分块发送，降低内存峰值。
 *
 * @param req HTTP 请求上下文
 * @return
 * - ESP_OK: 发送成功
 * - ESP_FAIL: 文件不存在/打开失败/发送过程中断
 */
static esp_err_t static_file_get_handler(httpd_req_t *req)// ESP32 HTTP 服务器的请求结构体，包含了请求的 URI、方法、头信息、请求体等信息
{
    char filepath[520];
    // 根路径默认指向 index.html
    if (strcmp(req->uri, "/") == 0) {
        strcpy(filepath, "/spiffs/index.html"); // 根路径默认指向 index.html
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%s", req->uri); // 其他路径直接拼接到 /spiffs 后面 ，组成完整路径
    }

    // 检查文件是否存在
    struct stat st; // C 标准库用来获取文件信息的结构体，用来查：
                    // 文件是否存在
                    // 文件大小（字节）
                    // 文件权限
                    // 修改时间
    if (stat(filepath, &st) == -1) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "文件不存在");
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法读取文件");
        return ESP_FAIL;
    }

    // 根据扩展名设置 MIME，保证浏览器正确解析资源类型
    if (strstr(filepath, ".html")) httpd_resp_set_type(req, "text/html");//在一个字符串里，查找另一个字符串是否出现。如果出现，则返回该字符串的指针。
    else if (strstr(filepath, ".css")) httpd_resp_set_type(req, "text/css");
    else if (strstr(filepath, ".js")) httpd_resp_set_type(req, "application/javascript");

    // 分块读取并发送，适合在 MCU 场景中控制 RAM 使用
    char *chunk = malloc(SCRATCH_BUFSIZE); // 分配一块内存，用于存储文件内容
    size_t chunksize; // 存储文件内容的字节数
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd); // 从文件中读取一块内存，大小为 SCRATCH_BUFSIZE，存储到 chunk 中
        if (chunksize > 0) {
            // 任何一次 chunk 发送失败都立即中止，防止继续占用资源
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                free(chunk);
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    free(chunk);
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0); // 发送结束标志
    return ESP_OK;
}

/**
 * @brief API：返回 WiFi 扫描结果 JSON 数组（GET /api/wifi）。
 *
 * 从全局扫描结果缓存 `g_ap_records / g_ap_count` 构建 JSON：
 * `[{ "ssid":"xxx", "rssi":-40 }, ...]`
 * 为了限制响应体大小，最多返回前 15 条记录。
 *
 * @param req HTTP 请求上下文
 * @return ESP_OK（正常发送）
 */
static esp_err_t wifi_api_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");//设置响应类型为 JSON
    
    // 动态拼接 JSON 字符串（固定 2KB 缓冲）
    char *json_out = malloc(2048); // 分配一块内存，用于存储 JSON 字符串
    strcpy(json_out, "[");//将 [ 复制到 json_out 中
    
    // 仅返回最多 15 条，避免前端解析和网络传输过重
    int count = (g_ap_count > 15) ? 15 : g_ap_count; // 如果 g_ap_count 大于 15，则 count 为 15，否则为 g_ap_count
    for (int i = 0; i < count; i++) {
        char entry[128];
        snprintf(entry, sizeof(entry), "{\"ssid\":\"%s\",\"rssi\":%d}%s",//将 ssid 和 rssi 复制到 entry 中
                 (char*)g_ap_records[i].ssid, g_ap_records[i].rssi,
                 (i == count - 1) ? "" : ",");
        strcat(json_out, entry);
    }
    strcat(json_out, "]");
    
    httpd_resp_send(req, json_out, strlen(json_out));
    free(json_out);
    return ESP_OK;
}

/**
 * @brief API：接收前端消息并绘制到 LCD（POST /api/msg）。
 *
 * 请求体按原样读取为文本（长度取 `content_len`），
 * 随后在 LCD 指定区域绘制弹窗与文字提示，并返回 "OK"。
 *
 * @param req HTTP 请求上下文
 * @return
 * - ESP_OK: 处理并响应成功
 * - ESP_FAIL: 请求体读取失败
 */
static esp_err_t msg_api_handler(httpd_req_t *req)
{
    char content[128] = {0};
    int ret = httpd_req_recv(req, content, req->content_len);
    if (ret <= 0) return ESP_FAIL;

    ESP_LOGI(TAG, "Web 收到指令: %s", content);
    
    // UI 刷新逻辑：先画背景框，再叠加文字
    ili9341_draw_round_rect(20, 180, 280, 50, 5, 0x001F); // 蓝色弹窗
    ili9341_draw_string_utf8_limit(30, 195, content, 0xFFFF, 0x001F, 260);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/**
 * @brief 启动 Web 服务器并注册路由。
 *
 * 启动流程：
 * 1) 若已启动则直接返回；
 * 2) 初始化 SPIFFS；
 * 3) 启动 httpd；
 * 4) 注册 API 与静态文件路由（API 优先）。
 *
 * @return
 * - ESP_OK: 启动成功（或已在运行）
 * - ESP_FAIL: SPIFFS 初始化失败
 */
esp_err_t start_webserver(void)
{
    if (server) return ESP_OK;

    // 1. 先启动文件系统
    if (init_spiffs() != ESP_OK) return ESP_FAIL;

    // 2. 配置服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard; // 支持通配符匹配

    if (httpd_start(&server, &config) == ESP_OK) {
        // API 路由（优先注册，避免被通配符静态路由吞掉）
        httpd_uri_t api_wifi = { .uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_api_handler };
        httpd_uri_t api_msg  = { .uri = "/api/msg",  .method = HTTP_POST, .handler = msg_api_handler };
        httpd_register_uri_handler(server, &api_wifi);
        httpd_register_uri_handler(server, &api_msg);

        // 静态文件路由：匹配所有剩余 GET 请求
        httpd_uri_t static_files = { .uri = "/*", .method = HTTP_GET, .handler = static_file_get_handler };
        httpd_register_uri_handler(server, &static_files);
    }
    return ESP_OK;
}

/**
 * @brief 停止 Web 服务器并卸载 SPIFFS。
 *
 * 在服务器句柄有效时执行：
 * - 停止 httpd；
 * - 清空句柄；
 * - 注销 `storage` 分区挂载。
 */
void stop_webserver(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        esp_vfs_spiffs_unregister("storage");
    }
}