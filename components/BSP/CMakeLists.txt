set(src_dirs
            spi
            lcd
            font_picture
            wifi
            menu)
set(include_dirs
            spi
            lcd
            font_picture
            wifi
            menu)
set(requires
            driver
            esp_event
            nvs_flash
            esp_wifi
            esp_netif
            button
            esp_timer
            )
idf_component_register(SRC_DIRS "${src_dirs}"
                            INCLUDE_DIRS "${include_dirs}"
                            REQUIRES "${requires}")
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)