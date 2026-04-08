#ifndef MENU_H
#define MENU_H

/**
 * @brief 启动菜单系统（创建菜单任务）
 * 
 * 该函数会创建一个FreeRTOS任务，负责WiFi扫描、列表显示、按键处理及连接。
 */
void menu_start(void);

#endif