idf_component_register(
  SRCS "cpmDriveConfig.c" "main.c" "systemMenu.c" "hostConsole.c" "imageFile.c" "storage.c" "machineRegistry.c" "network.c"
    "fileTransfer.c" "cpmMachine.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES hostCore cpmCore esp_driver_usb_serial_jtag esp_driver_spi esp_driver_sdspi
    sdmmc fatfs esp_http_server esp_netif esp_event esp_wifi esp_timer esp_system mbedtls
    log
  EMBED_TXTFILES "web.html"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
