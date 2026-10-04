#include "fileTransfer.h"
#include "hostCore.h"
#include "storage.h"
#include "esp_http_server.h"
#include "esp_random.h"
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
static char sessionToken[33];
static const char *temporaryPath = "/sdcard/retro/exchange/.upload-part";

static esp_err_t respond(httpd_req_t *request, const char *status, const char *message)
{
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, message);
}

static bool tokenEquals(const char *candidate)
{
  if (strlen(candidate) != 32)
  {
    return false;
  }
  unsigned difference = 0;
  for (size_t index = 0; index < 32; ++index)
  {
    difference |= (unsigned char)candidate[index] ^ (unsigned char)sessionToken[index];
  }
  return difference == 0;
}

static bool headerAuthorized(httpd_req_t *request)
{
  char token[40];
  return httpd_req_get_hdr_value_str(request, "X-Retro-Token", token, sizeof(token)) == ESP_OK && tokenEquals(token);
}

static bool cookieAuthorized(httpd_req_t *request)
{
  char cookie[256];
  if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
  {
    return false;
  }
  char *context = NULL;
  char *item = strtok_r(cookie, ";", &context);
  while (item != NULL)
  {
    while (*item == ' ')
    {
      ++item;
    }
    if (strncmp(item, "retroSession=", 13) == 0 && tokenEquals(item + 13))
    {
      return true;
    }
    item = strtok_r(NULL, ";", &context);
  }
  return false;
}

static bool authorize(httpd_req_t *request, bool mutation)
{
  if (atomic_load(&stopping))
  {
    respond(request, "503 Service Unavailable", "File Transfer is stopping");
    return false;
  }
  if (!cookieAuthorized(request) || (mutation && !headerAuthorized(request)))
  {
    respond(request, "403 Forbidden", "Session expired or access token missing");
    return false;
  }
  return true;
}

static bool resolveRequest(httpd_req_t *request, char *absolute, size_t capacity)
{
  char query[800];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK || strncmp(query, "path=", 5) != 0 ||
      strchr(query + 5, '&') != NULL || !pathResolve(query + 5, absolute, capacity))
  {
    respond(request, "400 Bad Request", "Invalid exchange path");
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

static esp_err_t sessionHandler(httpd_req_t *request)
{
  if (atomic_load(&stopping) || !headerAuthorized(request))
  {
    return respond(request, "403 Forbidden", "Invalid session token");
  }
  char cookie[128];
  snprintf(cookie, sizeof(cookie), "retroSession=%s; HttpOnly; SameSite=Strict; Path=/", sessionToken);
  httpd_resp_set_hdr(request, "Set-Cookie", cookie);
  return respond(request, "200 OK", "Session opened");
}

static esp_err_t listHandler(httpd_req_t *request)
{
  char path[absolutePathCapacity];
  if (!authorize(request, false) || !resolveRequest(request, path, sizeof(path)))
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
    if (length < 0 || (size_t)length >= sizeof(child) ||
        strlen(child + strlen("/sdcard/retro/exchange/")) >= relativePathCapacity)
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
  if (!authorize(request, false) || !resolveRequest(request, path, sizeof(path)))
  {
    return ESP_OK;
  }
  struct stat info;
  if (stat(path, &info) != 0 || !S_ISREG(info.st_mode))
  {
    return respond(request, "404 Not Found", "File unavailable");
  }
  if (info.st_size < 0 || (uint64_t)info.st_size > 2147483647U)
  {
    return respond(request, "413 Content Too Large", "HOST-M1 transfer limit is 2 GiB minus 1 byte");
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
  if (!authorize(request, true) || !resolveRequest(request, path, sizeof(path)))
  {
    return ESP_FAIL;
  }
  if (request->content_len > 2147483647U)
  {
    respond(request, "413 Content Too Large", "HOST-M1 transfer limit is 2 GiB minus 1 byte");
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
  if (!authorize(request, true) || !resolveRequest(request, path, sizeof(path)))
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
  if (unlink(temporaryPath) != 0 && errno != ENOENT)
  {
    return ESP_FAIL;
  }
  unsigned char randomBytes[16];
  esp_fill_random(randomBytes, sizeof(randomBytes));
  for (size_t index = 0; index < sizeof(randomBytes); ++index)
  {
    snprintf(sessionToken + index * 2, 3, "%02x", randomBytes[index]);
  }
  atomic_store(&stopping, false);
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.ctrl_port = 32769;
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
                                  {.uri = "/api/session", .method = HTTP_POST, .handler = sessionHandler},
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
  memset(sessionToken, 0, sizeof(sessionToken));
}

bool fileTransferActive(void)
{
  return server != NULL;
}

const char *fileTransferToken(void)
{
  return sessionToken;
}
