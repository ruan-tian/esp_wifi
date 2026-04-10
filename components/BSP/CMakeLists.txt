set(src_dirs
            spi
            lcd
            font_picture
            wifi
            menu
            unicodeToGB2312
            )

set(include_dirs
            spi
            lcd
            font_picture
            wifi
            menu
            unicodeToGB2312
            )

set(requires
            esp_driver_spi
            esp_driver_gpio
            esp_driver_ledc    
            esp_event
            nvs_flash
            esp_wifi
            esp_netif
            button
            esp_timer
            esp_http_server
            spiffs  
            )

idf_component_register(SRC_DIRS ${src_dirs}
                            INCLUDE_DIRS ${include_dirs}
                            REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error -Wno-missing-braces)
target_add_binary_data(${COMPONENT_LIB} "font_picture/HZK16C" TEXT)