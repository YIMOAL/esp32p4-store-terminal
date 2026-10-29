
set(INCLUDE "." "./ui/generated" "./ui/custom" "./ui/generated/guider_customer_fonts")
file(GLOB_RECURSE LVGL_SOURCE "./ui/*.c")

idf_component_register(
    SRCS
	    ${LVGL_SOURCE}
        main.c
        bsp/display_init.c
        bsp/touch_init.c
        bsp/SD.c
        adapter/lvgl_adapter.c
        ui/ui_manager.c
		network/mqtt.c
		network/wifi_hosted.c
    INCLUDE_DIRS
	    ${INCLUDE}
        .
        bsp
        adapter
        ui
		network
		config
    REQUIRES esp_driver_gpio esp_driver_uart nvs_flash esp_lcd_touch mqtt esp_hosted esp_wifi esp_netif esp_event fatfs sdmmc json
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
