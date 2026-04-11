//激活 ESP-IDF 环境
D:\.espressif\v6.0\esp-idf\export.ps1 
idf.py --version  //确认 idf.py 可用

idf.py add-dependency "espressif/button^4.1.6"  
//添加 Button 组件依赖


idf.py reconfigure //同步组件（下载代码）


idf.py add-dependency "lvgl/lvgl^8.3.11"
idf.py menuconfig


target_add_binary_data(${COMPONENT_LIB} "font_picture/HZK16C" TEXT)//连接字库
# Cursor 常用快捷键（Windows 版）
## 一、基础编辑
- `Ctrl + C`：复制
- `Ctrl + V`：粘贴
- `Ctrl + X`：剪切
- `Ctrl + Z`：撤销
- `Ctrl + Shift + Z`：重做
- `Ctrl + S`：保存
- `Ctrl + /`：单行注释
- `Shift + Alt + A`：块注释
- `Alt + ↑ / ↓`：整行上下移动
- `Shift + Alt + ↑ / ↓`：复制当前行

## 二、光标与选择
- `Ctrl + D`：选中下一个相同文本（多光标）
- `Alt + 鼠标点击`：手动添加多光标
- `Ctrl + L`：选中整行
- `Home`：行首
- `End`：行尾
- `Ctrl + Home`：文件开头
- `Ctrl + End`：文件结尾

## 三、搜索与跳转
- `Ctrl + F`：查找
- `Ctrl + H`：替换
- `Ctrl + P`：快速搜索文件
- `Ctrl + G`：跳转到指定行
- `Ctrl + Shift + P`：打开命令面板

## 四、面板与窗口
- `Ctrl + ``：打开/关闭终端
- `Ctrl + B`：显示/隐藏侧边栏
- `Ctrl + \`：编辑器分屏
- `Ctrl + W`：关闭标签页
- `Ctrl + Shift + T`：重新打开关闭的标签页

## 五、代码格式化
- `Alt + Shift + F`：格式化文档
- `Ctrl + K + Ctrl + F`：格式化选中代码

## 六、Cursor AI 核心快捷键
- `Ctrl + I`：唤起 AI 对话/让 AI 编辑选中代码
- `Tab`：接受 AI 代码补全
- `Esc`：关闭 AI 提示框

## 七、调试运行
- `F5`：开始调试
- `Ctrl + F5`：运行（不调试）
- `F9`：切换断点
- `F10`：单步跳过
- `F11`：单步进入