#include "diskActivity.h"
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>

#define DISK_ACTIVITY_GPIO GPIO_NUM_38
#define DISK_ACTIVITY_HOLD_MS 120
#define DISK_ACTIVITY_BRIGHTNESS 32

typedef enum
{
  diskActivityOff,
  diskActivityGreen,
  diskActivityRed
} diskActivityColor;

static const char *tag = "diskActivity";
static rmt_channel_handle_t txChannel;
static rmt_encoder_handle_t bytesEncoder;
static QueueHandle_t activityQueue;

#if DISK_ACTIVITY_DEBUG_LOG
static diskActivityColor lastRequestedColor = diskActivityOff;

static const char *colorName(diskActivityColor color)
{
  return color == diskActivityGreen ? "GREEN (read)" : (color == diskActivityRed ? "RED (write)" : "OFF");
}
#endif

static esp_err_t setColor(diskActivityColor color)
{
  //-- The LED on this board shows the first byte as red (RGB order, measured with the start-up self-test).
  uint8_t rgb[3] = {0, 0, 0};
  if (color == diskActivityGreen)
  {
    rgb[1] = DISK_ACTIVITY_BRIGHTNESS;
  }
  else if (color == diskActivityRed)
  {
    rgb[0] = DISK_ACTIVITY_BRIGHTNESS;
  }

#if DISK_ACTIVITY_DEBUG_LOG
  ESP_LOGW(tag, "LED transmit %s: bytes R=%u G=%u B=%u", colorName(color), rgb[0], rgb[1], rgb[2]);
#endif
  const rmt_transmit_config_t transmitConfig = {.loop_count = 0};
  esp_err_t result = rmt_transmit(txChannel, bytesEncoder, rgb, sizeof(rgb), &transmitConfig);
  if (result != ESP_OK)
  {
    return result;
  }
  result = rmt_tx_wait_all_done(txChannel, 100);
  if (result == ESP_OK)
  {
    esp_rom_delay_us(300);
  }
  return result;
}

#if DISK_ACTIVITY_DEBUG_LOG
//-- Shows RED, GREEN and BLUE for three seconds each (with the LED off in between) at start, sent as raw RGB bytes, to prove which colour the LED really shows.
static void ledSelfTest(void)
{
  static const uint8_t patterns[3][3] = {{DISK_ACTIVITY_BRIGHTNESS, 0, 0},
                                         {0, DISK_ACTIVITY_BRIGHTNESS, 0},
                                         {0, 0, DISK_ACTIVITY_BRIGHTNESS}};
  static const char *names[3] = {"RED", "GREEN", "BLUE"};
  static const uint8_t off[3] = {0, 0, 0};
  const rmt_transmit_config_t transmitConfig = {.loop_count = 0};
  for (int index = 0; index < 3; ++index)
  {
    ESP_LOGW(tag, "LED self-test %s: bytes R=%u G=%u B=%u", names[index], patterns[index][0], patterns[index][1],
             patterns[index][2]);
    if (rmt_transmit(txChannel, bytesEncoder, patterns[index], 3, &transmitConfig) == ESP_OK &&
        rmt_tx_wait_all_done(txChannel, 100) == ESP_OK)
    {
      esp_rom_delay_us(300);
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (rmt_transmit(txChannel, bytesEncoder, off, 3, &transmitConfig) == ESP_OK &&
        rmt_tx_wait_all_done(txChannel, 100) == ESP_OK)
    {
      esp_rom_delay_us(300);
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}
#endif

static void activityTask(void *context)
{
  (void)context;
  diskActivityColor color = diskActivityOff;
  diskActivityColor shownColor = diskActivityOff;
  TickType_t timeout = portMAX_DELAY;
#if DISK_ACTIVITY_DEBUG_LOG
  ledSelfTest();
#endif
  if (setColor(color) != ESP_OK)
  {
    ESP_LOGW(tag, "Could not initialize RGB LED output");
  }

  for (;;)
  {
    diskActivityColor nextColor;
    if (xQueueReceive(activityQueue, &nextColor, timeout) == pdTRUE)
    {
      color = nextColor;
      //-- A continuous stream of reads must not keep re-sending the same colour; only a change is transmitted.
      if (color != shownColor)
      {
        esp_err_t result = setColor(color);
        if (result != ESP_OK)
        {
          ESP_LOGW(tag, "Could not update RGB LED: %s", esp_err_to_name(result));
        }
        shownColor = color;
      }
      timeout = pdMS_TO_TICKS(DISK_ACTIVITY_HOLD_MS);
      if (timeout == 0)
      {
        timeout = 1;
      }
    }
    else
    {
      color = diskActivityOff;
      esp_err_t result = setColor(color);
      if (result != ESP_OK)
      {
        ESP_LOGW(tag, "Could not turn off RGB LED: %s", esp_err_to_name(result));
      }
      shownColor = color;
#if DISK_ACTIVITY_DEBUG_LOG
      lastRequestedColor = diskActivityOff;
#endif
      timeout = portMAX_DELAY;
    }
  }
}

static void signalActivity(diskActivityColor color)
{
#if DISK_ACTIVITY_DEBUG_LOG
  if (color != lastRequestedColor)
  {
    ESP_LOGW(tag, "LED request %s (was %s)", colorName(color), colorName(lastRequestedColor));
    lastRequestedColor = color;
  }
#endif
  if (activityQueue != NULL)
  {
    xQueueOverwrite(activityQueue, &color);
  }
}

esp_err_t diskActivityInit(void)
{
  const rmt_tx_channel_config_t channelConfig = {
      .gpio_num = DISK_ACTIVITY_GPIO,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10000000,
      .mem_block_symbols = 64,
      .trans_queue_depth = 1,
  };
  esp_err_t result = rmt_new_tx_channel(&channelConfig, &txChannel);
  if (result != ESP_OK)
  {
    ESP_LOGE(tag, "Could not initialize GPIO %d RMT channel: %s", DISK_ACTIVITY_GPIO, esp_err_to_name(result));
    return result;
  }

  const rmt_bytes_encoder_config_t encoderConfig = {
      .bit0 = {.duration0 = 4, .level0 = 1, .duration1 = 8, .level1 = 0},
      .bit1 = {.duration0 = 8, .level0 = 1, .duration1 = 4, .level1 = 0},
      .flags.msb_first = 1,
  };
  result = rmt_new_bytes_encoder(&encoderConfig, &bytesEncoder);
  if (result != ESP_OK)
  {
    ESP_LOGE(tag, "Could not initialize WS2812 encoder: %s", esp_err_to_name(result));
    rmt_del_channel(txChannel);
    txChannel = NULL;
    return result;
  }

  result = rmt_enable(txChannel);
  if (result != ESP_OK)
  {
    ESP_LOGE(tag, "Could not enable GPIO %d RMT channel: %s", DISK_ACTIVITY_GPIO, esp_err_to_name(result));
    rmt_del_encoder(bytesEncoder);
    rmt_del_channel(txChannel);
    bytesEncoder = NULL;
    txChannel = NULL;
    return result;
  }

  activityQueue = xQueueCreate(1, sizeof(diskActivityColor));
  if (activityQueue == NULL)
  {
    ESP_LOGE(tag, "Could not create RGB LED activity queue");
    rmt_disable(txChannel);
    rmt_del_encoder(bytesEncoder);
    rmt_del_channel(txChannel);
    bytesEncoder = NULL;
    txChannel = NULL;
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreate(activityTask, "diskActivity", 3072, NULL, 5, NULL) != pdPASS)
  {
    ESP_LOGE(tag, "Could not create RGB LED activity task");
    vQueueDelete(activityQueue);
    activityQueue = NULL;
    rmt_disable(txChannel);
    rmt_del_encoder(bytesEncoder);
    rmt_del_channel(txChannel);
    bytesEncoder = NULL;
    txChannel = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void diskActivityRead(void)
{
  signalActivity(diskActivityGreen);
}

void diskActivityWrite(void)
{
  signalActivity(diskActivityRed);
}
