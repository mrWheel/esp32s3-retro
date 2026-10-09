#include "hostConsole.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

static const char *tag = "hostConsole";
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

bool hostConsoleWrite(const char *text)
{
  size_t totalSize = strlen(text);
  size_t remaining = totalSize;
  size_t writtenTotal = 0;
  unsigned stalledWrites = 0;
  while (remaining > 0)
  {
    size_t chunkSize = remaining > 128 ? 128 : remaining;
    int written = usb_serial_jtag_write_bytes(text, chunkSize, pdMS_TO_TICKS(100));
    if (written < 0 || (size_t)written > chunkSize)
    {
      ESP_LOGE(tag, "USB console write returned invalid count %d for %u requested bytes",
               written, (unsigned)chunkSize);
      return false;
    }
    if (written == 0)
    {
      if (++stalledWrites > 3 || usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000)) != ESP_OK)
      {
        ESP_LOGE(tag, "USB console write stalled after %u of %u bytes", (unsigned)writtenTotal,
                 (unsigned)totalSize);
        return false;
      }
      continue;
    }
    stalledWrites = 0;
    text += (size_t)written;
    remaining -= (size_t)written;
    writtenTotal += (size_t)written;
  }
  return true;
}
