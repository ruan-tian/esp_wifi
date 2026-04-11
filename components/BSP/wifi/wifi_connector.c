/**
 * @file wifi_connector.c
 * @brief WiFi 连接与配网核心模块 (已深度优化版)
 * * 包含优化点：
 * 1. 彻底防泄漏：SmartConfig 获取密码后从内存中安全擦除。
 * 2. 智能退避重连：引入 2 秒延时防止高频重试引发路由器防火墙拦截。
 * 3. 健壮性初始化：兼容 ESP_ERR_INVALID_STATE。
 */

 #include <string.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/event_groups.h"
 #include "esp_wifi.h"
 #include "esp_event.h"
 #include "esp_log.h"
 #include "esp_smartconfig.h"
 #include "nvs_flash.h"
 #include "wifi_connector.h"
 
 static const char *TAG = "WIFI_CONN";
 
 // ===================== 事件标志位与宏 =====================
 #define WIFI_CONNECTED_BIT  BIT0      // WiFi 连接成功标志位
 #define WIFI_FAIL_BIT       BIT1      // WiFi 连接失败标志位
 #define SMARTCONFIG_DONE    BIT2      // SmartConfig 配网完成标志位
 #define WIFI_CANCEL_BIT     BIT3      // 操作被用户强行取消标志位
 #define MAX_RETRY           5         // 失败后的最大重试次数
 
 // ===================== 内部状态机变量 =====================
 static EventGroupHandle_t s_wifi_event_group;
 static int s_retry_num = 0;                      
 static bool s_wifi_inited = false;               
 
 // 【优化】删除了明文保存密码的 s_sc_password 数组，保障设备安全
 static char s_sc_ssid[33] = {0};                 // 仅保留 SSID 供 UI 读取展示
 
 /**
  * @brief 系统 WiFi 底层事件路由与处理回调
  */
 static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
 {
     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
         ESP_LOGI(TAG, "WiFi STA 模式已启动");
     }
     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
         if (s_retry_num > 0 && s_retry_num < MAX_RETRY) {
             ESP_LOGW(TAG, "连接断开，2秒后进行第 %d/%d 次重试...", s_retry_num, MAX_RETRY);
             
             // 【优化】防止网络雪崩：引入退避重连机制
             // 路由器拒绝连接时，不立刻高频重发，延缓 2 秒释放系统资源并防拉黑
             vTaskDelay(pdMS_TO_TICKS(2000)); 
             
             esp_wifi_connect();
             s_retry_num++;
         } 
         else if (s_retry_num >= MAX_RETRY) {
             ESP_LOGE(TAG, "达到最大重试次数，彻底放弃连接");
             xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
         }
     }
     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
         ESP_LOGI(TAG, "成功获取局域网 IP: " IPSTR, IP2STR(&event->ip_info.ip));
         s_retry_num = 0;  // 成功连通，重置失败计数器
         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);  
     }
     else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
         smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
 
         // 仅在全局保存 SSID 供外部界面调用
         memset(s_sc_ssid, 0, sizeof(s_sc_ssid));
         memcpy(s_sc_ssid, evt->ssid, sizeof(evt->ssid));
         ESP_LOGI(TAG, "SmartConfig 解析到目标 SSID: %s", s_sc_ssid);
 
         esp_smartconfig_stop();
 
         // 局部变量组装 WiFi 参数，用完即焚
         wifi_config_t wifi_config = {0};
         memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(evt->ssid));
         memcpy(wifi_config.sta.password, evt->password, sizeof(evt->password));
 
         if (evt->type == SC_TYPE_ESPTOUCH_V2) {
             wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
         }
 
         ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
         esp_wifi_connect();
 
         // 【优化】敏感数据防泄漏：
         // 密码已被交付给底层协议栈，立即在应用层擦除带有密码明文的局部结构体
         memset(&wifi_config, 0, sizeof(wifi_config));
 
         xEventGroupSetBits(s_wifi_event_group, SMARTCONFIG_DONE);
     }
 }
 
 /**
  * @brief 初始化 WiFi 连接调度器
  */
 void wifi_connector_init(void)
 {
     if (s_wifi_inited) return;
 
     esp_err_t ret = nvs_flash_init();
     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
         ESP_ERROR_CHECK(nvs_flash_erase());
         ret = nvs_flash_init();
     }
     ESP_ERROR_CHECK(ret);
 
     // 【优化】安全系统初始化（处理重复调用的非法状态）
     ret = esp_netif_init();
     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(ret);
     
     ret = esp_event_loop_create_default();
     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(ret);
 
     esp_netif_create_default_wifi_sta();
 
     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
 
     esp_event_handler_instance_t instance_any_id;
     esp_event_handler_instance_t instance_got_ip;
     esp_event_handler_instance_t instance_sc;
 
     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
     ESP_ERROR_CHECK(esp_event_handler_instance_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_sc));
 
     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
     ESP_ERROR_CHECK(esp_wifi_start());
 
     s_wifi_event_group = xEventGroupCreate();
     s_wifi_inited = true;
     ESP_LOGI(TAG, "WiFi 调度核心已就绪");
 }
 
/**
 * @brief 优化建议：在 main.c 中调用此函数时，请放入独立 Task
 */
 esp_err_t wifi_connect_sta(const char *ssid, const char *password, int timeout_ms)
 {
     if (!s_wifi_inited) wifi_connector_init();
 
     // 清除之前的标志位
     xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_CANCEL_BIT);
     s_retry_num = 1;
 
     esp_wifi_disconnect();
     vTaskDelay(pdMS_TO_TICKS(100)); // 给底层协议栈喘息时间
 
     wifi_config_t wifi_config = {0};
     strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
     strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
 
     ESP_LOGI(TAG, "连接中: %s...", ssid);
     esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
     esp_wifi_connect();
 
     // 这里的阻塞是发生在任务内部的，只要不是在 LVGL 任务里运行就没问题
     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_CANCEL_BIT,
                         pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
 
     // 安全擦除内存中的密码明文
     memset(&wifi_config, 0, sizeof(wifi_config));
 
     if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
     return ESP_FAIL;
 }
 
 /**
  * @brief 启动 SmartConfig 监听与一键配网（阻塞执行）
  */
 esp_err_t wifi_smartconfig_start(int timeout_ms)
 {
     if (!s_wifi_inited) wifi_connector_init();
 
     xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | SMARTCONFIG_DONE | WIFI_CANCEL_BIT);
     s_retry_num = 1;
 
     esp_wifi_disconnect();
     vTaskDelay(pdMS_TO_TICKS(100));
 
     ESP_LOGI(TAG, "启动 SmartConfig (ESPTouch V2) 监听...");
 
     smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
     ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2));
     ESP_ERROR_CHECK(esp_smartconfig_start(&sc_cfg));
 
     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                         SMARTCONFIG_DONE | WIFI_CANCEL_BIT,
                         pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
 
     if (!(bits & SMARTCONFIG_DONE)) {
         esp_smartconfig_stop();
         ESP_LOGE(TAG, "SmartConfig 监听超时结束");
         return ESP_FAIL;
     }
 
     // 拿到密码后，继续等待连接结果
     bits = xEventGroupWaitBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_CANCEL_BIT,
                         pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
 
     xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | SMARTCONFIG_DONE | WIFI_CANCEL_BIT);
 
     if (bits & WIFI_CONNECTED_BIT) {
         ESP_LOGI(TAG, "SmartConfig 配网并连接成功: %s", s_sc_ssid);
         return ESP_OK;
     }
     if (bits & WIFI_CANCEL_BIT) {
         ESP_LOGW(TAG, "SmartConfig 进程被强行中止");
         return ESP_FAIL;
     }
 
     ESP_LOGE(TAG, "SmartConfig 后续握手连接失败");
     return ESP_FAIL;
 }
 
 /**
  * @brief 读取配网成功的 SSID 供外部 UI 调用
  */
 const char *wifi_get_connected_ssid(void)
 {
     return s_sc_ssid;
 }
 
 /**
  * @brief 强制中断网络任务并释放堵塞
  */
 void wifi_cancel(void)
 {
     if (!s_wifi_inited) return;
     
     // 设置中断标志，立刻唤醒正在 WaitBits 死等的任务
     xEventGroupSetBits(s_wifi_event_group, WIFI_CANCEL_BIT);
     
     // 强制切断正在进行的底层硬件动作
     esp_wifi_disconnect();
     esp_smartconfig_stop();
 }