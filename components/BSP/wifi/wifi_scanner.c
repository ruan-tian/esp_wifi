#include "wifi_scanner.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_SCAN";  // ESP-IDF日志标签，用于调试输出

// ======================= 全局变量定义 =======================
wifi_ap_record_t *g_ap_records = NULL;  // 动态分配的AP记录数组指针
                                        // 存储扫描到的所有WiFi热点信息
                                        
uint16_t g_ap_count = 0;                // 实际扫描到的AP数量
                                        // 该变量对外公开，供其他模块读取扫描结果

static bool s_scanner_inited = false;   // 模块初始化状态标志
                                        // 防止重复初始化

#define MAX_SCAN_AP 64                  // 最大扫描AP数量限制
                                        // 根据内存和实际需求调整
                                        // 每个wifi_ap_record_t约占用~100字节
                                        // 64个AP约需6.4KB RAM

// ======================= 排序函数 =======================
/**
 * @brief RSSI信号强度比较函数（降序排列）
 * @param a 第一个AP记录指针
 * @param b 第二个AP记录指针
 * @return 正数表示b的信号更强，负数表示a的信号更强
 * 
 * @details 用于qsort()排序，将信号强的WiFi排在前面
 *          RSSI值越大表示信号越强（例如：-50dBm > -80dBm）
 * 
 * @note qsort要求：
 *       - 返回值 > 0: b排在a前面
 *       - 返回值 < 0: a排在b前面
 *       - 返回值 = 0: 两者相等
 */
static int sort_by_rssi(const void *a, const void *b) {
    // 降序排列：信号强的在前
    // 强制类型转换为wifi_ap_record_t指针，然后访问rssi字段
    return ((wifi_ap_record_t *)b)->rssi - ((wifi_ap_record_t *)a)->rssi;
}

// ======================= 模块初始化 =======================
/**
 * @brief WiFi扫描模块初始化函数
 * @details 完成以下工作：
 *          1. 初始化ESP-NETIF网络接口层
 *          2. 创建默认事件循环
 *          3. 创建STA模式的网络接口
 *          4. 初始化WiFi驱动并设置为STA模式
 *          5. 启动WiFi子系统
 * 
 * @note 幂等性设计：多次调用只会执行一次初始化
 *       使用s_scanner_inited标志防止重复初始化
 * 
 * @warning 此函数会阻塞直到WiFi子系统完全启动
 *          建议在应用启动早期调用
 */
void wifi_scanner_init(void) {
    // 检查是否已经初始化，避免重复初始化
    if (s_scanner_inited) return;

    // ---- 第1步：初始化ESP-NETIF网络接口层 ----
    // esp_netif是ESP-IDF的网络抽象层，提供统一的网络接口
    esp_err_t ret = esp_netif_init();
    
    // 处理可能的错误：
    // - ESP_OK: 初始化成功
    // - ESP_ERR_INVALID_STATE: 已经初始化过（可忽略）
    // - 其他错误：严重错误，需要检查
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);  // 非预期错误，触发断言并重启
    }
    
    // ---- 第2步：创建默认事件循环 ----
    // 事件循环用于处理WiFi事件（如连接、断开、扫描完成等）
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    // ---- 第3步：创建默认的STA网络接口 ----
    // STA（Station）模式：设备作为客户端连接到WiFi路由器
    // 此函数会创建一个虚拟网络接口，用于后续WiFi通信
    esp_netif_create_default_wifi_sta();

    // ---- 第4步：初始化WiFi驱动 ----
    // 使用默认配置初始化WiFi子系统
    // 默认配置包括：内存分配、任务优先级、缓冲区大小等
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));  // 应用配置并初始化
    
    // ---- 第5步：设置WiFi工作模式 ----
    // WIFI_MODE_STA: 仅作为站点（客户端）模式
    // 其他可选模式：
    // - WIFI_MODE_AP: 仅作为接入点（热点）
    // - WIFI_MODE_APSTA: 同时作为AP和STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // ---- 第6步：启动WiFi子系统 ----
    // 此时WiFi硬件开始工作，可以进行扫描、连接等操作
    ESP_ERROR_CHECK(esp_wifi_start());

    // 标记初始化完成
    s_scanner_inited = true;
    
    // 输出初始化成功日志
    ESP_LOGI(TAG, "WiFi 扫描模块已就绪 (无 UI 依赖)");
}

// ======================= WiFi扫描核心功能 =======================
/**
 * @brief 执行WiFi扫描并更新全局AP列表
 * @details 工作流程：
 *          1. 分配或复用AP记录缓冲区
 *          2. 启动阻塞式扫描（等待扫描完成）
 *          3. 获取扫描到的AP数量
 *          4. 获取AP详细信息
 *          5. 按信号强度降序排序
 * 
 * @note 阻塞式扫描：
 *       - esp_wifi_scan_start(&scan_cfg, true) 的第二个参数为true
 *       - 函数会一直阻塞直到扫描完成
 *       - 适合在后台任务中调用，不适合在主线程中使用
 * 
 * @warning 内存管理：
 *          - g_ap_records只在首次调用时分配内存
 *          - 后续调用会复用同一块内存
 *          - 程序退出前应手动free(g_ap_records)
 * 
 * @usage 典型调用场景：
 *        ```c
 *        wifi_scanner_init();           // 先初始化
 *        wifi_scan_and_update_list();   // 执行扫描
 *        
 *        // 访问扫描结果
 *        for(int i=0; i<g_ap_count; i++) {
 *            printf("SSID: %s, RSSI: %d\n", 
 *                   g_ap_records[i].ssid, 
 *                   g_ap_records[i].rssi);
 *        }
 *        ```
 */
void wifi_scan_and_update_list(void) {
    // ---- 第1步：确保AP记录缓冲区已分配 ----
    // 采用懒加载策略：首次调用时才分配内存
    if (g_ap_records == NULL) {
        // 动态分配数组，可容纳MAX_SCAN_AP个AP记录
        // sizeof(wifi_ap_record_t) 通常为96-100字节
        g_ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * MAX_SCAN_AP);
        
        // 注意：生产环境应检查malloc返回值是否为NULL
        // 如果分配失败，应进行错误处理
    }

    // ---- 第2步：配置扫描参数 ----
    wifi_scan_config_t scan_cfg = { 
        .show_hidden = true  // 显示隐藏SSID的网络
                             // true: 扫描隐藏网络（会增加扫描时间）
                             // false: 只扫描广播SSID的网络
    };
    
    // ---- 第3步：启动阻塞式扫描 ----
    // 参数说明：
    // - &scan_cfg: 扫描配置结构体指针
    // - true: 阻塞模式，函数会等待扫描完成后才返回
    //         false: 非阻塞模式，立即返回，需要通过事件通知扫描完成
    esp_wifi_scan_start(&scan_cfg, true);  // 同步阻塞，直到扫描结束

    // ---- 第4步：获取扫描到的AP总数 ----
    uint16_t ap_num = 0;  // 实际扫描到的AP数量
    esp_wifi_scan_get_ap_num(&ap_num);  // 查询扫描结果中的AP总数
    
    // 限制AP数量不超过缓冲区容量
    // 如果扫描到的AP超过MAX_SCAN_AP，只保留前MAX_SCAN_AP个
    g_ap_count = (ap_num > MAX_SCAN_AP) ? MAX_SCAN_AP : ap_num;

    // ---- 第5步：获取AP详细信息 ----
    // 将扫描结果复制到g_ap_records数组中
    // 参数说明：
    // - &g_ap_count: 输入/输出参数
    //   输入：期望获取的最大AP数量
    //   输出：实际获取的AP数量
    // - g_ap_records: 存储AP信息的数组指针
    esp_wifi_scan_get_ap_records(&g_ap_count, g_ap_records);
    
    // 此时g_ap_count可能被修改为实际获取的数量
    // （可能小于之前设置的值）

    // ---- 第6步：按信号强度排序 ----
    // 使用标准库的快速排序算法
    // 参数说明：
    // - g_ap_records: 待排序数组
    // - g_ap_count: 数组元素个数
    // - sizeof(wifi_ap_record_t): 每个元素的大小
    // - sort_by_rssi: 比较函数指针
    qsort(g_ap_records, g_ap_count, sizeof(wifi_ap_record_t), sort_by_rssi);
    
    // 排序后，信号最强的AP在数组开头（索引0）
    // 信号最弱的AP在数组末尾（索引g_ap_count-1）
    
    // ---- 第7步：输出扫描结果日志 ----
    ESP_LOGI(TAG, "扫描完成，找到 %d 个网络", g_ap_count);
    
    // 可选：打印详细扫描结果（调试用）
    // for(int i = 0; i < g_ap_count; i++) {
    //     ESP_LOGI(TAG, "[%d] SSID: %-32s | RSSI: %4d dBm | BSSID: %02x:%02x:%02x:%02x:%02x:%02x | Channel: %d",
    //              i,
    //              g_ap_records[i].ssid,
    //              g_ap_records[i].rssi,
    //              g_ap_records[i].bssid[0], g_ap_records[i].bssid[1], g_ap_records[i].bssid[2],
    //              g_ap_records[i].bssid[3], g_ap_records[i].bssid[4], g_ap_records[i].bssid[5],
    //              g_ap_records[i].primary);
    // }
}