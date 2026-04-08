 D:\espidfv5.4.1\export.ps1 //激活 ESP-IDF 环境

idf.py --version  //确认 idf.py 可用

idf.py add-dependency "espressif/button^4.1.6"  
//添加 Button 组件依赖


idf.py reconfigure //同步组件（下载代码）
