idf_component_register(
  SRCS "cpm86BiosOverlay.c" "cpm80DriveConfig.c" "cpm86DriveConfig.c" "storagePath.c" "diskActivity.c" "main.c" "systemMenu.c" "hostConsole.c" "imageFile.c" "storage.c" "machineRegistry.c" "network.c"
    "fileTransfer.c" "cpm80Machine.c" "cpm86Machine.c" "apple2Machine.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES hostCore cpm80Core cpm86Core apple2Core esp_driver_usb_serial_jtag esp_driver_spi esp_driver_sdspi esp_driver_rmt
    sdmmc fatfs esp_http_server esp_netif esp_event esp_wifi esp_timer esp_system mbedtls
    log
  EMBED_TXTFILES "web.html"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
