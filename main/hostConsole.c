#include "hostConsole.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

static int pendingCharacter = -1;

esp_err_t hostConsoleInit(void)
{
  usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 1024, .rx_buffer_size = 256};
  setvbuf(stdout, NULL, _IONBF, 0);
  esp_err_t result = usb_serial_jtag_driver_install(&config);
  if (result == ESP_OK)
  {
    usb_serial_jtag_vfs_use_driver();
  }
  return result;
}

bool hostConsoleCharAvailable(void)
{
  if (pendingCharacter >= 0)
  {
    return true;
  }
  unsigned char value;
  if (usb_serial_jtag_read_bytes(&value, 1, 0) == 1)
  {
    pendingCharacter = value;
  }
  return pendingCharacter >= 0;
}

int hostConsoleGetChar(void)
{
  if (pendingCharacter >= 0)
  {
    int value = pendingCharacter;
    pendingCharacter = -1;
    return value;
  }
  unsigned char value;
  if (usb_serial_jtag_read_bytes(&value, 1, pdMS_TO_TICKS(20)) == 1)
  {
    return value;
  }
  return -1;
}

int hostConsolePeekChar(void)
{
  if (!hostConsoleCharAvailable())
  {
    return -1;
  }
  return pendingCharacter;
}

void hostConsolePutChar(char value)
{
  //— Write directly to the driver so echo does not depend on stdio buffering
  usb_serial_jtag_write_bytes(&value, 1, pdMS_TO_TICKS(100));
}

void hostConsoleWrite(const char *text)
{
  usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(100));
}
