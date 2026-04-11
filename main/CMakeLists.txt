# 这里的 REQUIRES 是关键，它告诉构建系统：main 组件依赖于 BSP 和 lvgl
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES BSP lvgl nvs_flash esp_wifi esp_event)