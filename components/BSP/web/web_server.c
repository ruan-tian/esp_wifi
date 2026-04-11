#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "ui.h"            // 【修改点】：引入 UI 模块，替代原来的 lcd.h
#include "wifi_scanner.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

#define SCRATCH_BUFSIZE 4096

static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "正在初始化 SPIFFS 文件系统...");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 挂载失败 (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "文件系统状态: 总计 %d KB, 已用 %d KB", total / 1024, used / 1024);
    }
    return ESP_OK;
}

static esp_err_t static_file_get_handler(httpd_req_t *req)
{
    char filepath[520];
    if (strcmp(req->uri, "/") == 0) {
        strcpy(filepath, "/spiffs/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%s", req->uri);
    }

    struct stat st;
    if (stat(filepath, &st) == -1) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "文件不存在");
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法读取文件");
        return ESP_FAIL;
    }

    if (strstr(filepath, ".html")) httpd_resp_set_type(req, "text/html");
    else if (strstr(filepath, ".css")) httpd_resp_set_type(req, "text/css");
    else if (strstr(filepath, ".js")) httpd_resp_set_type(req, "application/javascript");

    char *chunk = malloc(SCRATCH_BUFSIZE);
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                free(chunk);
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    free(chunk);
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t wifi_api_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    char *json_out = malloc(2048);
    strcpy(json_out, "[");
    
    int count = (g_ap_count > 15) ? 15 : g_ap_count;
    for (int i = 0; i < count; i++) {
        char entry[128];
        snprintf(entry, sizeof(entry), "{\"ssid\":\"%s\",\"rssi\":%d}%s",
                 (char*)g_ap_records[i].ssid, g_ap_records[i].rssi,
                 (i == count - 1) ? "" : ",");
        strcat(json_out, entry);
    }
    strcat(json_out, "]");
    
    httpd_resp_send(req, json_out, strlen(json_out));
    free(json_out);
    return ESP_OK;
}

static esp_err_t msg_api_handler(httpd_req_t *req)
{
    char content[128] = {0};
    int ret = httpd_req_recv(req, content, req->content_len);
    if (ret <= 0) return ESP_FAIL;

    ESP_LOGI(TAG, "Web 收到指令: %s", content);
    
    // 【修改点】：调用 LVGL 的弹窗接口，替代底层的画框逻辑
    ui_show_web_message(content);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t start_webserver(void)
{
    if (server) return ESP_OK;

    if (init_spiffs() != ESP_OK) return ESP_FAIL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t api_wifi = { .uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_api_handler };
        httpd_uri_t api_msg  = { .uri = "/api/msg",  .method = HTTP_POST, .handler = msg_api_handler };
        httpd_register_uri_handler(server, &api_wifi);
        httpd_register_uri_handler(server, &api_msg);

        httpd_uri_t static_files = { .uri = "/*", .method = HTTP_GET, .handler = static_file_get_handler };
        httpd_register_uri_handler(server, &static_files);
    }
    return ESP_OK;
}

void stop_webserver(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        esp_vfs_spiffs_unregister("storage");
    }
}