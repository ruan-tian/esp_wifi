#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化主 UI 及状态机
 */
void ui_init(void);

/**
 * @brief 弹出 Web 消息提示框
 * @param msg_text 收到的文本消息
 */
void ui_show_web_message(const char *msg_text);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */