#include "fileTransfer.h"
#include "hostCore.h"
#include "storage.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

extern const unsigned char webStart[] asm("_binary_web_html_start");
extern const unsigned char webEnd[] asm("_binary_web_html_end");

static httpd_handle_t server;
static atomic_bool stopping;
static const char *exchangeTemporaryPath = "/sdcard/retro/exchange/.upload-part";
static const char *imageTemporaryPath = "/sdcard/retro/images/.upload-part";

typedef enum
{
  exchangeArea,
  imageArea
} fileArea;

static const char *areaRoot(fileArea area)
{
  return area == imageArea ? "/sdcard/retro/images" : "/sdcard/retro/exchange";
}

static const char *areaTemporaryPath(fileArea area)
{
  return area == imageArea ? imageTemporaryPath : exchangeTemporaryPath;
}

static esp_err_t respond(httpd_req_t *request, const char *status, const char *message)
{
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, message);
}

static bool acceptingRequests(httpd_req_t *request)
{
  if (atomic_load(&stopping))
  {
    respond(request, "503 Service Unavailable", "File Transfer is stopping");
    return false;
  }
  return true;
}

static bool resolveRequest(httpd_req_t *request, char *absolute, size_t capacity, fileArea *area)
{
  char query[800];
  const char *encodedPath;
  char relative[relativePathCapacity];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK)
  {
    respond(request, "400 Bad Request", "Invalid SD-card path");
    return false;
  }
  if (strncmp(query, "area=exchange&path=", sizeof("area=exchange&path=") - 1) == 0)
  {
    *area = exchangeArea;
    encodedPath = query + sizeof("area=exchange&path=") - 1;
  }
  else if (strncmp(query, "area=images&path=", sizeof("area=images&path=") - 1) == 0)
  {
    *area = imageArea;
    encodedPath = query + sizeof("area=images&path=") - 1;
  }
  else
  {
    respond(request, "400 Bad Request", "Invalid SD-card area");
    return false;
  }
  if (strchr(encodedPath, '&') != NULL || !pathDecode(encodedPath, relative, sizeof(relative)))
  {
    respond(request, "400 Bad Request", "Invalid SD-card path");
    return false;
  }
  int length = snprintf(absolute, capacity, "%s%s%s", areaRoot(*area), relative[0] ? "/" : "", relative);
  if (length < 0 || (size_t)length >= capacity)
  {
    respond(request, "400 Bad Request", "SD-card path is too long");
    return false;
  }
  return true;
}

static bool uploadLengthIsSupported(httpd_req_t *request)
{
  size_t headerLength = httpd_req_get_hdr_value_len(request, "Content-Length");
  if (headerLength == 0 || headerLength >= 11)
  {
    respond(request, "400 Bad Request", "A valid Content-Length header is required");
    return false;
  }
  char header[11];
  if (httpd_req_get_hdr_value_str(request, "Content-Length", header, sizeof(header)) != ESP_OK)
  {
    respond(request, "400 Bad Request", "Cannot read Content-Length header");
    return false;
  }
  uint64_t contentLength = 0;
  for (size_t index = 0; index < headerLength; ++index)
  {
    if (header[index] < '0' || header[index] > '9')
    {
      respond(request, "400 Bad Request", "Invalid Content-Length header");
      return false;
    }
    contentLength = contentLength * 10 + (uint64_t)(header[index] - '0');
  }
  if (contentLength > UINT32_MAX - 1U)
  {
    respond(request, "413 Content Too Large", "Maximum upload size is 4 GiB minus 2 bytes");
    return false;
  }
  if (contentLength != request->content_len)
  {
    respond(request, "400 Bad Request", "HTTP body length does not match Content-Length");
    return false;
  }
  return true;
}

static esp_err_t pageHandler(httpd_req_t *request)
{
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy",
                     "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; "
                     "form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
  return httpd_resp_send(request, (const char *)webStart, webEnd - webStart - 1);
}

static esp_err_t listHandler(httpd_req_t *request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || !resolveRequest(request, path, sizeof(path), &area))
  {
    return ESP_OK;
  }
  DIR *directory = opendir(path);
  if (directory == NULL)
  {
    return respond(request, "404 Not Found", "Directory unavailable");
  }
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  esp_err_t result = httpd_resp_sendstr_chunk(request, "[");
  struct dirent *entry;
  bool first = true;
  while (result == ESP_OK && !atomic_load(&stopping) && (entry = readdir(directory)) != NULL)
  {
    if (!pathIsSafe(entry->d_name) || strchr(entry->d_name, '/') != NULL)
    {
      continue;
    }
    char child[absolutePathCapacity];
    int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    size_t rootLength = strlen(areaRoot(area));
    if (length < 0 || (size_t)length >= sizeof(child) || strlen(child + rootLength + 1) >= relativePathCapacity)
    {
      continue;
    }
    struct stat info;
    if (stat(child, &info) != 0)
    {
      result = ESP_FAIL;
      break;
    }
    if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode))
    {
      continue;
    }
    char row[192];
    snprintf(row, sizeof(row), "%s{\"name\":\"%.64s\",\"directory\":%s,\"size\":%llu}", first ? "" : ",", entry->d_name,
             S_ISDIR(info.st_mode) ? "true" : "false", (unsigned long long)info.st_size);
    result = httpd_resp_sendstr_chunk(request, row);
    first = false;
    vTaskDelay(1);
  }
  closedir(directory);
  if (result != ESP_OK || atomic_load(&stopping))
  {
    return ESP_FAIL;
  }
  result = httpd_resp_sendstr_chunk(request, "]");
  return result == ESP_OK ? httpd_resp_send_chunk(request, NULL, 0) : result;
}

static esp_err_t downloadHandler(httpd_req_t *request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || !resolveRequest(request, path, sizeof(path), &area))
  {
    return ESP_OK;
  }
  struct stat info;
  if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
  {
    return respond(request, "404 Not Found", "File unavailable");
  }
  if (info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX - 1U)
  {
    return respond(request, "413 Content Too Large", "Maximum transfer size is 4 GiB minus 2 bytes");
  }
  FILE *file = fopen(path, "rb");
  if (file == NULL)
  {
    return respond(request, "500 Internal Server Error", "Cannot open file");
  }
  char disposition[128];
  snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", strrchr(path, '/') + 1);
  httpd_resp_set_type(request, "application/octet-stream");
  httpd_resp_set_hdr(request, "Content-Disposition", disposition);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  char buffer[4096];
  size_t length;
  esp_err_t result = ESP_OK;
  while (!atomic_load(&stopping) && (length = fread(buffer, 1, sizeof(buffer), file)) > 0)
  {
    result = httpd_resp_send_chunk(request, buffer, length);
    if (result != ESP_OK)
    {
      break;
    }
    vTaskDelay(1);
  }
  bool failed = ferror(file) || atomic_load(&stopping);
  fclose(file);
  if (failed || result != ESP_OK)
  {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t uploadHandler(httpd_req_t *request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || !resolveRequest(request, path, sizeof(path), &area))
  {
    return ESP_FAIL;
  }
  if (!uploadLengthIsSupported(request))
  {
    return ESP_FAIL;
  }
  struct stat info;
  if (stat(path, &info) == 0)
  {
    respond(request, "409 Conflict", "Destination exists; delete explicitly before uploading");
    return ESP_FAIL;
  }
  if (errno != ENOENT)
  {
    respond(request, "500 Internal Server Error", "Cannot inspect destination");
    return ESP_FAIL;
  }
  char parent[absolutePathCapacity];
  snprintf(parent, sizeof(parent), "%s", path);
  char *separator = strrchr(parent, '/');
  if (separator == NULL)
  {
    return respond(request, "400 Bad Request", "Invalid destination");
  }
  *separator = '\0';
  if (!storageDirectoryExists(parent))
  {
    respond(request, "404 Not Found", "Destination directory missing");
    return ESP_FAIL;
  }
  const char *temporaryPath = areaTemporaryPath(area);
  int descriptor = open(temporaryPath, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (descriptor < 0)
  {
    respond(request, "507 Insufficient Storage", "Cannot create temporary file; check SD and free space");
    return ESP_FAIL;
  }
  size_t remaining = request->content_len;
  char buffer[4096];
  bool success = true;
  while (remaining > 0 && !atomic_load(&stopping))
  {
    size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    int received = httpd_req_recv(request, buffer, requested);
    if (received <= 0)
    {
      success = false;
      break;
    }
    size_t written = 0;
    while (written < (size_t)received)
    {
      ssize_t count = write(descriptor, buffer + written, (size_t)received - written);
      if (count <= 0)
      {
        success = false;
        break;
      }
      written += (size_t)count;
    }
    if (!success)
    {
      break;
    }
    remaining -= (size_t)received;
    vTaskDelay(1);
  }
  success = success && remaining == 0 && !atomic_load(&stopping);
  if (fsync(descriptor) != 0)
  {
    success = false;
  }
  if (close(descriptor) != 0)
  {
    success = false;
  }
  if (success && !atomic_load(&stopping) && rename(temporaryPath, path) == 0)
  {
    return respond(request, "201 Created", "Upload complete");
  }
  unlink(temporaryPath);
  respond(request, "500 Internal Server Error", "Upload aborted or write failed; no final file created");
  return ESP_FAIL;
}

static esp_err_t deleteHandler(httpd_req_t *request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || !resolveRequest(request, path, sizeof(path), &area))
  {
    return ESP_OK;
  }
  struct stat info;
  if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
  {
    return respond(request, "404 Not Found", "Regular file unavailable; directories cannot be deleted");
  }
  if (unlink(path) != 0)
  {
    return respond(request, "500 Internal Server Error", "Delete failed");
  }
  return respond(request, "200 OK", "File deleted");
}

esp_err_t fileTransferStart(void)
{
  if (server != NULL)
  {
    return ESP_ERR_INVALID_STATE;
  }
  if (!storageReady())
  {
    return ESP_ERR_INVALID_STATE;
  }
  if ((unlink(exchangeTemporaryPath) != 0 && errno != ENOENT) ||
      (unlink(imageTemporaryPath) != 0 && errno != ENOENT))
  {
    return ESP_FAIL;
  }
  atomic_store(&stopping, false);
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 12288;
  config.max_open_sockets = 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  esp_err_t result = httpd_start(&server, &config);
  if (result != ESP_OK)
  {
    server = NULL;
    return result;
  }
  const httpd_uri_t handlers[] = {{.uri = "/", .method = HTTP_GET, .handler = pageHandler},
                                  {.uri = "/api/list", .method = HTTP_GET, .handler = listHandler},
                                  {.uri = "/api/file", .method = HTTP_GET, .handler = downloadHandler},
                                  {.uri = "/api/file", .method = HTTP_PUT, .handler = uploadHandler},
                                  {.uri = "/api/file", .method = HTTP_DELETE, .handler = deleteHandler}};
  for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index)
  {
    result = httpd_register_uri_handler(server, &handlers[index]);
    if (result != ESP_OK)
    {
      fileTransferStop();
      return result;
    }
  }
  return ESP_OK;
}

void fileTransferStop(void)
{
  atomic_store(&stopping, true);
  if (server != NULL)
  {
    httpd_stop(server);
    server = NULL;
  }
}

bool fileTransferActive(void)
{
  return server != NULL;
}
