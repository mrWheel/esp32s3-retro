#include "network.h"
#include "wifi_provisioner.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>

static atomic_bool hasAddress;
static atomic_bool startFinished;
static atomic_int startResult;
static bool startRequested;
static bool eventsRegistered;

static void networkEvent(void *argument, esp_event_base_t base, int32_t eventId, void *eventData)
{
  (void)argument;
  (void)eventData;
  if (base == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP)
  {
    atomic_store(&hasAddress, true);
  }
  else
  {
    atomic_store(&hasAddress, false);
  }
}

static void startNetworkTask(void *argument)
{
  (void)argument;
  wifi_prov_config_t config = WIFI_PROV_DEFAULT_CONFIG();
  config.ap_ssid = "Retro-Setup";
  config.max_retries = 2;
  config.http_port = 80;
  esp_err_t result = wifi_prov_start(&config);
  atomic_store(&startResult, result);
  atomic_store(&startFinished, true);
  vTaskDelete(NULL);
}

esp_err_t networkStart(void)
{
  if (startRequested)
  {
    return ESP_OK;
  }
  esp_err_t result = wifi_prov_init();
  if (result != ESP_OK)
  {
    return result;
  }
  if (!eventsRegistered)
  {
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, networkEvent, NULL);
    if (result != ESP_OK)
    {
      return result;
    }
    result = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, networkEvent, NULL);
    if (result != ESP_OK)
    {
      esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, networkEvent);
      return result;
    }
    eventsRegistered = true;
  }
  if (xTaskCreate(startNetworkTask, "retroWifiStart", 6144, NULL, 4, NULL) != pdPASS)
  {
    return ESP_ERR_NO_MEM;
  }
  startRequested = true;
  return ESP_OK;
}

bool networkReady(char *address, size_t capacity)
{
  if (!atomic_load(&startFinished) || atomic_load(&startResult) != ESP_OK || !atomic_load(&hasAddress) ||
      !wifi_prov_is_connected())
  {
    return false;
  }
  esp_netif_ip_info_t info;
  if (wifi_prov_get_ip_info(&info) != ESP_OK || info.ip.addr == 0)
  {
    return false;
  }
  snprintf(address, capacity, IPSTR, IP2STR(&info.ip));
  return true;
}

const char *networkStatus(void)
{
  if (!atomic_load(&startFinished))
  {
    return "Starting WiFi; ENTER remains available";
  }
  if (atomic_load(&startResult) != ESP_OK)
  {
    return "WiFi startup failed; reset to retry provisioning";
  }
  return "Waiting for WiFi. For setup join Retro-Setup and open http://192.168.4.1/";
}
