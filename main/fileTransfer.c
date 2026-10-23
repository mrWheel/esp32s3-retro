#include "fileTransfer.h"
#include "hostCore.h"
#include "storage.h"
#include "guestFs.h"
#include "guestFsImage.h"
#include "esp_log.h"
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
#include <ctype.h>
#include <strings.h>

extern const unsigned char webStart[] asm("_binary_web_html_start");
extern const unsigned char webEnd[] asm("_binary_web_html_end");

static httpd_handle_t server;
static atomic_bool stopping;
static atomic_flag imageOperationActive = ATOMIC_FLAG_INIT;
static const char* fileTransferTag = "fileTransfer";
static const char* imageTemporaryPath = "/microSD/retro/images/.upload-part";
static const char* guestTemporaryPath = "/microSD/retro/images/.guest-part";
static const char* imageMachineIds[] = {"cpm80", "cpm86", "ucsd", "apple2", "swtpc"};

typedef enum
{
  imageArea
} fileArea;

static const char* areaRoot(fileArea area)
{
  (void)area;
  return "/microSD/retro/images";
}

static const char* areaTemporaryPath(fileArea area)
{
  (void)area;
  return imageTemporaryPath;
}

static char* trim(char* text)
{
  while (*text == ' ' || *text == '\t')
  {
    ++text;
  }
  size_t length = strlen(text);
  while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                        text[length - 1] == '\r' || text[length - 1] == '\n'))
  {
    text[--length] = '\0';
  }
  return text;
}

static bool imageIsMapped(const char* retroPath, bool* mapped)
{
  *mapped = false;
  for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
  {
    char configPath[absolutePathCapacity];
    int pathLength =
        snprintf(configPath, sizeof(configPath), "/littlefs/%s/drives.cfg", imageMachineIds[index]);
    if (pathLength < 0 || (size_t)pathLength >= sizeof(configPath))
    {
      return false;
    }
    FILE* config = fopen(configPath, "rb");
    if (config == NULL)
    {
      if (errno == ENOENT)
      {
        continue;
      }
      return false;
    }
    char line[256];
    while (fgets(line, sizeof(line), config) != NULL)
    {
      size_t lineLength = strlen(line);
      if (lineLength == sizeof(line) - 1 && line[lineLength - 1] != '\n' && !feof(config))
      {
        fclose(config);
        return false;
      }
      char* content = trim(line);
      if (*content == '\0' || *content == '#')
      {
        continue;
      }
      char* equals = strchr(content, '=');
      char* comma = equals == NULL ? NULL : strchr(equals + 1, ',');
      if (comma == NULL)
      {
        continue;
      }
      *comma = '\0';
      if (strcasecmp(trim(equals + 1), retroPath) == 0)
      {
        *mapped = true;
        break;
      }
    }
    bool readError = ferror(config) != 0;
    fclose(config);
    if (readError)
    {
      return false;
    }
    if (*mapped)
    {
      return true;
    }
  }
  return true;
}

static bool isDiskImagePath(const char* path)
{
  const char* extension = strrchr(path, '.');
  if (extension == NULL)
  {
    return false;
  }
  char normalized[8];
  size_t length = strlen(extension);
  if (length >= sizeof(normalized))
  {
    return false;
  }
  for (size_t index = 0; index < length; ++index)
  {
    normalized[index] = (char)tolower((unsigned char)extension[index]);
  }
  normalized[length] = '\0';
  return strcmp(normalized, ".dsk") == 0 || strcmp(normalized, ".img") == 0 ||
         strcmp(normalized, ".po") == 0 || strcmp(normalized, ".woz") == 0 ||
         strcmp(normalized, ".2mg") == 0;
}

static bool isDriveConfiguration(const char* path)
{
  const char* name = strrchr(path, '/');
  name = name == NULL ? path : name + 1;
  return strcasecmp(name, "drives.cfg") == 0 || strcasecmp(name, "config.cfg") == 0;
}

static bool isProtectedImageMetadata(const char* path)
{
  const char* name = strrchr(path, '/');
  name = name == NULL ? path : name + 1;
  return isDriveConfiguration(path) || strcasecmp(name, "manager-copy-part.tmp") == 0 ||
         strcasecmp(name, "manager-replace-backup.tmp") == 0 ||
         strcasecmp(name, "manager-guest-journal.tmp") == 0;
}

//-- The emulator drive-configuration loaders read lines with a 256-byte buffer.
static const size_t driveConfigurationMaximumBytes = 16384;
static const size_t driveConfigurationMaximumLineBytes = 254;

static bool isManagedDriveConfiguration(const char* retroPath)
{
  static const char prefix[] = "/retro/images/";
  if (strncmp(retroPath, prefix, sizeof(prefix) - 1) != 0)
  {
    return false;
  }
  const char* machine = retroPath + sizeof(prefix) - 1;
  const char* separator = strchr(machine, '/');
  if (separator == NULL || strcmp(separator + 1, "drives.cfg") != 0)
  {
    return false;
  }
  size_t machineLength = (size_t)(separator - machine);
  for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
  {
    if (strlen(imageMachineIds[index]) == machineLength &&
        strncmp(imageMachineIds[index], machine, machineLength) == 0)
    {
      return true;
    }
  }
  return false;
}

static bool driveConfigurationChunkIsValid(const char* data, size_t length, size_t* lineLength)
{
  for (size_t index = 0; index < length; ++index)
  {
    if (data[index] == '\0')
    {
      return false;
    }
    if (data[index] == '\n')
    {
      *lineLength = 0;
      continue;
    }
    if (++(*lineLength) > driveConfigurationMaximumLineBytes)
    {
      return false;
    }
  }
  return true;
}

static bool buildSiblingPath(const char* path, const char* name, char* sibling,
                             size_t siblingCapacity)
{
  char directory[absolutePathCapacity];
  int length = snprintf(directory, sizeof(directory), "%s", path);
  if (length < 0 || (size_t)length >= sizeof(directory))
  {
    return false;
  }
  char* separator = strrchr(directory, '/');
  if (separator == NULL || separator == directory)
  {
    return false;
  }
  *separator = '\0';
  length = snprintf(sibling, siblingCapacity, "%s/%s", directory, name);
  return length >= 0 && (size_t)length < siblingCapacity;
}

static bool isFlatImagePath(const char* relative)
{
  const char* separator = strchr(relative, '/');
  return separator != NULL && strchr(separator + 1, '/') == NULL && separator[1] != '\0';
}

static bool resolveImageOperationPaths(httpd_req_t* request, char* source, size_t sourceCapacity,
                                       char* destination, size_t destinationCapacity)
{
  char query[800];
  char encodedSource[relativePathCapacity];
  char encodedDestination[relativePathCapacity];
  char relativeSource[relativePathCapacity];
  char relativeDestination[relativePathCapacity];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "source", encodedSource, sizeof(encodedSource)) != ESP_OK ||
      httpd_query_key_value(query, "destination", encodedDestination, sizeof(encodedDestination)) !=
          ESP_OK ||
      !pathDecode(encodedSource, relativeSource, sizeof(relativeSource)) ||
      !pathDecode(encodedDestination, relativeDestination, sizeof(relativeDestination)) ||
      relativeSource[0] == '\0' || relativeDestination[0] == '\0' ||
      !isDiskImagePath(relativeSource) || !isDiskImagePath(relativeDestination) ||
      !isFlatImagePath(relativeSource) || !isFlatImagePath(relativeDestination))
  {
    return false;
  }
  int sourceLength = snprintf(source, sourceCapacity, "/microSD/retro/images/%s", relativeSource);
  int destinationLength =
      snprintf(destination, destinationCapacity, "/microSD/retro/images/%s", relativeDestination);
  return sourceLength >= 0 && (size_t)sourceLength < sourceCapacity && destinationLength >= 0 &&
         (size_t)destinationLength < destinationCapacity;
}

static esp_err_t respond(httpd_req_t* request, const char* status, const char* message)
{
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, message);
}

static bool acceptingRequests(httpd_req_t* request)
{
  if (atomic_load(&stopping))
  {
    respond(request, "503 Service Unavailable", "File Transfer is stopping");
    return false;
  }
  return true;
}

//-- drives.cfg lives in LittleFS next to the system disk (/littlefs/<OS>/drives.cfg), but the
// manager
//-- still addresses it as <OS>/drives.cfg in the images area.
static const char* littlefsRoot = "/littlefs";

static bool driveConfigurationLittlefsPath(const char* relative, char* absolute, size_t capacity)
{
  const char* separator = strchr(relative, '/');
  if (separator == NULL || strcmp(separator + 1, "drives.cfg") != 0)
  {
    return false;
  }
  size_t machineLength = (size_t)(separator - relative);
  for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
  {
    if (strlen(imageMachineIds[index]) == machineLength &&
        strncmp(imageMachineIds[index], relative, machineLength) == 0)
    {
      int length = snprintf(absolute, capacity, "%s/%s", littlefsRoot, relative);
      return length >= 0 && (size_t)length < capacity;
    }
  }
  return false;
}

static bool littlefsImagePath(const char* relative, char* absolute, size_t capacity)
{
  for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
  {
    size_t machineLength = strlen(imageMachineIds[index]);
    if (strncmp(relative, imageMachineIds[index], machineLength) != 0 ||
        relative[machineLength] != '/')
    {
      continue;
    }
    const char* imageName = relative + machineLength + 1;
    if (imageName[0] == '\0' || strchr(imageName, '/') != NULL || !pathIsSafe(imageName) ||
        !isDiskImagePath(imageName))
    {
      return false;
    }
    int pathLength =
        snprintf(absolute, capacity, "%s/%s/%s", littlefsRoot, imageMachineIds[index], imageName);
    return pathLength >= 0 && (size_t)pathLength < capacity;
  }
  return false;
}

static bool isLittlefsPath(const char* path)
{
  return strncmp(path, "/littlefs/", strlen("/littlefs/")) == 0;
}

//-- Maps a resolved request path back to its /retro/images/... name.
static bool retroPathForRequestPath(const char* path, char* retroPath, size_t capacity)
{
  const char* relative =
      isLittlefsPath(path) ? path + strlen("/littlefs/") : path + strlen(areaRoot(imageArea)) + 1;
  int length = snprintf(retroPath, capacity, "/retro/images/%s", relative);
  return length >= 0 && (size_t)length < capacity;
}

static bool resolveRequest(httpd_req_t* request, char* absolute, size_t capacity, fileArea* area)
{
  char query[800];
  const char* encodedPath;
  char relative[relativePathCapacity];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK)
  {
    respond(request, "400 Bad Request", "Invalid SD-card path");
    return false;
  }
  if (strncmp(query, "area=images&path=", sizeof("area=images&path=") - 1) == 0)
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
  size_t separators = 0;
  for (const char* character = relative; *character != '\0'; ++character)
  {
    separators += *character == '/';
  }
  if (separators > 1)
  {
    respond(request, "400 Bad Request",
            "Images live directly in /retro/images/<OS>/; sub-directories are not supported");
    return false;
  }
  int length =
      snprintf(absolute, capacity, "%s%s%s", areaRoot(*area), relative[0] ? "/" : "", relative);
  if (length < 0 || (size_t)length >= capacity)
  {
    respond(request, "400 Bad Request", "SD-card path is too long");
    return false;
  }
  char littlefsPath[absolutePathCapacity];
  if (driveConfigurationLittlefsPath(relative, littlefsPath, sizeof(littlefsPath)))
  {
    snprintf(absolute, capacity, "%s", littlefsPath);
  }
  return true;
}

static bool uploadLengthIsSupported(httpd_req_t* request)
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

static esp_err_t pageHandler(httpd_req_t* request)
{
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Content-Security-Policy",
                     "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                     "connect-src 'self'; "
                     "form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
  return httpd_resp_send(request, (const char*)webStart, webEnd - webStart - 1);
}

//-- Help texts are plain HTML fragments in /littlefs/help/<id>.html; the id is restricted to
//[a-z0-9-].
static esp_err_t helpHandler(httpd_req_t* request)
{
  char query[64];
  char identifier[40];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "id", identifier, sizeof(identifier)) != ESP_OK ||
      identifier[0] == '\0')
  {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing help id");
  }
  for (const char* character = identifier; *character != '\0'; ++character)
  {
    if (!islower((unsigned char)*character) && !isdigit((unsigned char)*character) &&
        *character != '-')
    {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid help id");
    }
  }
  char path[96];
  snprintf(path, sizeof(path), "/littlefs/help/%s.html", identifier);
  FILE* file = fopen(path, "rb");
  if (file == NULL)
  {
    return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "help text not found");
  }
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  char buffer[512];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0)
  {
    if (httpd_resp_send_chunk(request, buffer, (ssize_t)count) != ESP_OK)
    {
      fclose(file);
      return ESP_FAIL;
    }
  }
  fclose(file);
  return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t listHandler(httpd_req_t* request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || !resolveRequest(request, path, sizeof(path), &area))
  {
    return ESP_OK;
  }
  DIR* directory = opendir(path);
  if (directory == NULL)
  {
    return respond(request, "404 Not Found", "Directory unavailable");
  }
  const char* listRelative = path + strlen(areaRoot(area));
  DIR* littlefsDirectory = NULL;
  if (area == imageArea && listRelative[0] == '/')
  {
    const char* osId = listRelative + 1;
    for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
    {
      if (strcmp(osId, imageMachineIds[index]) == 0)
      {
        char littlefsDirectoryPath[absolutePathCapacity];
        int pathLength = snprintf(littlefsDirectoryPath, sizeof(littlefsDirectoryPath), "%s/%s",
                                  littlefsRoot, imageMachineIds[index]);
        if (pathLength < 0 || (size_t)pathLength >= sizeof(littlefsDirectoryPath))
        {
          closedir(directory);
          return respond(request, "500 Internal Server Error",
                         "Cannot resolve the LittleFS OS directory");
        }
        littlefsDirectory = opendir(littlefsDirectoryPath);
        if (littlefsDirectory == NULL)
        {
          closedir(directory);
          return respond(request, "500 Internal Server Error",
                         "Cannot open the LittleFS OS directory");
        }
        break;
      }
    }
  }
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  esp_err_t result = httpd_resp_sendstr_chunk(request, "[");
  struct dirent* entry;
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
    if (length < 0 || (size_t)length >= sizeof(child) ||
        strlen(child + rootLength + 1) >= relativePathCapacity)
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
    bool mapped = false;
    if (area == imageArea && S_ISREG(info.st_mode))
    {
      char retroPath[relativePathCapacity];
      int retroPathLength =
          snprintf(retroPath, sizeof(retroPath), "/retro/images/%s", child + rootLength + 1);
      if (retroPathLength < 0 || (size_t)retroPathLength >= sizeof(retroPath) ||
          !imageIsMapped(retroPath, &mapped))
      {
        result = ESP_FAIL;
        break;
      }
    }
    char row[224];
    snprintf(row, sizeof(row),
             "%s{\"name\":\"%.64s\",\"directory\":%s,\"size\":%llu,\"mapped\":%s}",
             first ? "" : ",", entry->d_name, S_ISDIR(info.st_mode) ? "true" : "false",
             (unsigned long long)info.st_size, mapped ? "true" : "false");
    result = httpd_resp_sendstr_chunk(request, row);
    first = false;
    vTaskDelay(1);
  }
  closedir(directory);
  //-- drives.cfg is stored in LittleFS; list it with the images of its OS.
  char littlefsConfigPath[absolutePathCapacity];
  char configRelative[relativePathCapacity];
  struct stat configInfo;
  if (result == ESP_OK && listRelative[0] == '/' &&
      snprintf(configRelative, sizeof(configRelative), "%s/drives.cfg", listRelative + 1) > 0 &&
      driveConfigurationLittlefsPath(configRelative, littlefsConfigPath,
                                     sizeof(littlefsConfigPath)) &&
      stat(littlefsConfigPath, &configInfo) == 0 && S_ISREG(configInfo.st_mode))
  {
    char row[96];
    snprintf(row, sizeof(row),
             "%s{\"name\":\"drives.cfg\",\"directory\":false,\"size\":%llu,\"mapped\":false}",
             first ? "" : ",", (unsigned long long)configInfo.st_size);
    result = httpd_resp_sendstr_chunk(request, row);
    first = false;
  }
  if (result == ESP_OK && area == imageArea && listRelative[0] == '/')
  {
    struct dirent* littlefsEntry;
    while (result == ESP_OK && !atomic_load(&stopping) && littlefsDirectory != NULL &&
           (littlefsEntry = readdir(littlefsDirectory)) != NULL)
    {
      if (!pathIsSafe(littlefsEntry->d_name) || strchr(littlefsEntry->d_name, '/') != NULL ||
          !isDiskImagePath(littlefsEntry->d_name))
      {
        continue;
      }
      char littlefsImage[absolutePathCapacity];
      int imageLength = snprintf(littlefsImage, sizeof(littlefsImage), "%s%s/%s", littlefsRoot,
                                 listRelative, littlefsEntry->d_name);
      struct stat imageInfo;
      if (imageLength < 0 || (size_t)imageLength >= sizeof(littlefsImage) ||
          stat(littlefsImage, &imageInfo) != 0 || !S_ISREG(imageInfo.st_mode))
      {
        result = ESP_FAIL;
        break;
      }
      char row[224];
      snprintf(row, sizeof(row),
               "%s{\"name\":\"%.64s\",\"directory\":false,\"size\":%llu,"
               "\"mapped\":false,\"littlefs\":true}",
               first ? "" : ",", littlefsEntry->d_name, (unsigned long long)imageInfo.st_size);
      result = httpd_resp_sendstr_chunk(request, row);
      first = false;
      vTaskDelay(1);
    }
  }
  if (littlefsDirectory != NULL)
  {
    closedir(littlefsDirectory);
  }
  if (result != ESP_OK || atomic_load(&stopping))
  {
    return ESP_FAIL;
  }
  result = httpd_resp_sendstr_chunk(request, "]");
  return result == ESP_OK ? httpd_resp_send_chunk(request, NULL, 0) : result;
}

static bool resolveLittlefsImageDownload(httpd_req_t* request, char* path, size_t capacity)
{
  static const char queryPrefix[] = "area=littlefs&path=";
  char query[800];
  char relative[relativePathCapacity];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      strncmp(query, queryPrefix, sizeof(queryPrefix) - 1) != 0)
  {
    return false;
  }
  const char* encodedPath = query + sizeof(queryPrefix) - 1;
  return strchr(encodedPath, '&') == NULL && pathDecode(encodedPath, relative, sizeof(relative)) &&
         littlefsImagePath(relative, path, capacity);
}

static esp_err_t downloadHandler(httpd_req_t* request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || (!resolveLittlefsImageDownload(request, path, sizeof(path)) &&
                                      !resolveRequest(request, path, sizeof(path), &area)))
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
    return respond(request, "413 Content Too Large",
                   "Maximum transfer size is 4 GiB minus 2 bytes");
  }
  FILE* file = fopen(path, "rb");
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

static esp_err_t uploadHandler(httpd_req_t* request)
{
  char path[absolutePathCapacity];
  fileArea area;
  if (!acceptingRequests(request) || !resolveRequest(request, path, sizeof(path), &area))
  {
    return ESP_OK;
  }
  if (!uploadLengthIsSupported(request))
  {
    return ESP_OK;
  }
  char overwriteValue[4] = "";
  bool overwriteAllowed = httpd_req_get_hdr_value_str(request, "X-Overwrite", overwriteValue,
                                                      sizeof(overwriteValue)) == ESP_OK &&
                          strcmp(overwriteValue, "1") == 0;
  char imageBackup[absolutePathCapacity] = "";
  bool driveConfigurationUpload = false;
  if (area == imageArea)
  {
    char retroPath[relativePathCapacity];
    if (!retroPathForRequestPath(path, retroPath, sizeof(retroPath)))
    {
      return respond(request, "400 Bad Request", "Image path is too long");
    }
    //-- Only <OS>/drives.cfg (stored in LittleFS) may be replaced; manager files stay protected.
    driveConfigurationUpload = isManagedDriveConfiguration(retroPath);
    if (!driveConfigurationUpload && isProtectedImageMetadata(retroPath))
    {
      return respond(request, "409 Conflict",
                     "Drive configuration and manager operation files cannot be changed");
    }
    if (driveConfigurationUpload &&
        (request->content_len == 0 || request->content_len > driveConfigurationMaximumBytes))
    {
      return respond(request, "413 Content Too Large",
                     "drives.cfg must be between 1 and 16384 bytes");
    }
  }
  bool driveConfigurationInvalid = false;
  size_t driveConfigurationLineLength = 0;
  struct stat info;
  bool destinationExists = false;
  if (stat(path, &info) == 0)
  {
    if (!overwriteAllowed)
    {
      return respond(request, "409 Conflict",
                     "Destination exists; enable OVERWRITE or delete explicitly before uploading");
    }
    if (!S_ISREG(info.st_mode))
    {
      return respond(request, "409 Conflict", "Destination is not a regular file");
    }
    if (area == imageArea)
    {
      struct stat backupInfo;
      if (!buildSiblingPath(path, "manager-replace-backup.tmp", imageBackup, sizeof(imageBackup)))
      {
        return respond(request, "400 Bad Request",
                       "Cannot prepare image replacement recovery path");
      }
      if (stat(imageBackup, &backupInfo) == 0 || errno != ENOENT)
      {
        return respond(
            request, "507 Insufficient Storage",
            "A preserved replacement backup exists; restore it before replacing another image");
      }
    }
    destinationExists = true;
  }
  else if (errno != ENOENT)
  {
    return respond(request, "500 Internal Server Error", "Cannot inspect destination");
  }
  char parent[absolutePathCapacity];
  snprintf(parent, sizeof(parent), "%s", path);
  char* separator = strrchr(parent, '/');
  if (separator == NULL)
  {
    return respond(request, "400 Bad Request", "Invalid destination");
  }
  *separator = '\0';
  if (!storageDirectoryExists(parent))
  {
    return respond(request, "404 Not Found", "Destination directory missing");
  }
  //-- A rename cannot cross file systems: the LittleFS drives.cfg is staged next to its
  // destination.
  char littlefsTemporaryPath[absolutePathCapacity] = "";
  if (isLittlefsPath(path) &&
      !buildSiblingPath(path, "drives-upload-part.tmp", littlefsTemporaryPath,
                        sizeof(littlefsTemporaryPath)))
  {
    return respond(request, "400 Bad Request", "Cannot prepare drives.cfg staging path");
  }
  const char* temporaryPath =
      isLittlefsPath(path) ? littlefsTemporaryPath : areaTemporaryPath(area);
  if (isLittlefsPath(path))
  {
    unlink(temporaryPath);
  }
  int descriptor = open(temporaryPath, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (descriptor < 0)
  {
    return respond(request, "507 Insufficient Storage",
                   "Cannot create temporary file; check storage and free space");
  }
  size_t remaining = request->content_len;
  char buffer[4096];
  bool success = true;
  while (remaining > 0 && !atomic_load(&stopping))
  {
    size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    int received = httpd_req_recv(request, buffer, requested);
    if (received == HTTPD_SOCK_ERR_TIMEOUT)
    {
      //-- SD writes can stall the receive loop; retry instead of aborting.
      continue;
    }
    if (received <= 0)
    {
      success = false;
      break;
    }
    if (driveConfigurationUpload &&
        !driveConfigurationChunkIsValid(buffer, (size_t)received, &driveConfigurationLineLength))
    {
      driveConfigurationInvalid = true;
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
  bool imageLockHeld = false;
  if (success && area == imageArea)
  {
    if (atomic_flag_test_and_set(&imageOperationActive))
    {
      unlink(temporaryPath);
      return respond(request, "409 Conflict", "Another whole-image operation is in progress");
    }
    imageLockHeld = true;
  }
  bool backedUp = false;
  if (success && destinationExists && area == imageArea)
  {
    if (rename(path, imageBackup) != 0)
    {
      success = false;
    }
    else
    {
      backedUp = true;
    }
  }
  else if (success && destinationExists && unlink(path) != 0 && errno != ENOENT)
  {
    success = false;
  }
  if (success && !atomic_load(&stopping) && rename(temporaryPath, path) == 0)
  {
    if (backedUp && unlink(imageBackup) != 0)
    {
      if (imageLockHeld)
      {
        atomic_flag_clear(&imageOperationActive);
      }
      return respond(request, "500 Internal Server Error",
                     "Upload committed, but the previous-image backup could not be removed");
    }
    if (imageLockHeld)
    {
      atomic_flag_clear(&imageOperationActive);
    }
    return respond(request, "201 Created", "Upload complete");
  }
  if (backedUp && rename(imageBackup, path) != 0)
  {
    unlink(temporaryPath);
    if (imageLockHeld)
    {
      atomic_flag_clear(&imageOperationActive);
    }
    return respond(request, "500 Internal Server Error",
                   "Upload failed; the previous image remains in manager-replace-backup.tmp beside "
                   "the destination");
  }
  if (imageLockHeld)
  {
    atomic_flag_clear(&imageOperationActive);
  }
  unlink(temporaryPath);
  if (driveConfigurationInvalid)
  {
    return respond(request, "422 Unprocessable Content",
                   "drives.cfg must be a text file with lines of at most 254 characters; "
                   "the existing file was not changed");
  }
  return respond(request, "500 Internal Server Error",
                 "Upload aborted or write failed; no final file created");
}

static esp_err_t deleteHandler(httpd_req_t* request)
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
    return respond(request, "404 Not Found",
                   "Regular file unavailable; directories cannot be deleted");
  }
  if (area == imageArea)
  {
    char retroPath[relativePathCapacity];
    if (!retroPathForRequestPath(path, retroPath, sizeof(retroPath)))
    {
      return respond(request, "400 Bad Request", "Image path is too long");
    }
    if (isDriveConfiguration(retroPath))
    {
      return respond(request, "409 Conflict", "Drive configuration files cannot be deleted");
    }
  }
  if (area == imageArea && atomic_flag_test_and_set(&imageOperationActive))
  {
    return respond(request, "409 Conflict", "Another whole-image operation is in progress");
  }
  if (unlink(path) != 0)
  {
    if (area == imageArea)
    {
      atomic_flag_clear(&imageOperationActive);
    }
    return respond(request, "500 Internal Server Error", "Delete failed");
  }
  if (area == imageArea)
  {
    atomic_flag_clear(&imageOperationActive);
  }
  return respond(request, "200 OK", "File deleted");
}

static esp_err_t imageCopyHandler(httpd_req_t* request)
{
  char source[absolutePathCapacity];
  char destination[absolutePathCapacity];
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  if (!resolveImageOperationPaths(request, source, sizeof(source), destination,
                                  sizeof(destination)))
  {
    return respond(request, "400 Bad Request",
                   "Valid source and destination image paths are required");
  }
  if (strcasecmp(source, destination) == 0 || isProtectedImageMetadata(destination))
  {
    return respond(request, "400 Bad Request",
                   "Source and destination must be distinct disk-image files");
  }
  if (atomic_flag_test_and_set(&imageOperationActive))
  {
    return respond(request, "409 Conflict", "Another whole-image operation is in progress");
  }
  esp_err_t response = ESP_OK;
  struct stat sourceInfo;
  struct stat destinationInfo;
  char copyTemporaryPath[absolutePathCapacity];
  char backupPath[absolutePathCapacity];
  if (stat(source, &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode) || sourceInfo.st_size < 0)
  {
    response = respond(request, "404 Not Found", "Source disk image is unavailable");
    goto done;
  }
  if ((uint64_t)sourceInfo.st_size > UINT32_MAX - 1U)
  {
    response = respond(request, "413 Content Too Large",
                       "Maximum whole-image size is 4 GiB minus 2 bytes");
    goto done;
  }
  bool overwriteAllowed = false;
  char overwriteValue[4] = "";
  overwriteAllowed = httpd_req_get_hdr_value_str(request, "X-Overwrite", overwriteValue,
                                                 sizeof(overwriteValue)) == ESP_OK &&
                     strcmp(overwriteValue, "1") == 0;
  bool destinationExists = stat(destination, &destinationInfo) == 0;
  if (destinationExists && !overwriteAllowed)
  {
    response =
        respond(request, "409 Conflict", "Destination exists; enable OVERWRITE to replace it");
    goto done;
  }
  if (destinationExists && !S_ISREG(destinationInfo.st_mode))
  {
    response = respond(request, "409 Conflict", "Destination is not a regular file");
    goto done;
  }
  if (destinationExists && sourceInfo.st_dev == destinationInfo.st_dev &&
      sourceInfo.st_ino == destinationInfo.st_ino)
  {
    response =
        respond(request, "400 Bad Request", "Source and destination refer to the same disk image");
    goto done;
  }
  if (!destinationExists && errno != ENOENT)
  {
    response = respond(request, "500 Internal Server Error", "Cannot inspect destination");
    goto done;
  }
  {
    char parent[absolutePathCapacity];
    snprintf(parent, sizeof(parent), "%s", destination);
    char* separator = strrchr(parent, '/');
    if (separator == NULL)
    {
      response = respond(request, "400 Bad Request", "Invalid destination directory");
      goto done;
    }
    *separator = '\0';
    if (!storageDirectoryExists(parent))
    {
      response = respond(request, "404 Not Found", "Destination directory is unavailable");
      goto done;
    }
  }
  if (!buildSiblingPath(destination, "manager-copy-part.tmp", copyTemporaryPath,
                        sizeof(copyTemporaryPath)) ||
      !buildSiblingPath(destination, "manager-replace-backup.tmp", backupPath, sizeof(backupPath)))
  {
    response =
        respond(request, "400 Bad Request", "Cannot prepare image operation temporary paths");
    goto done;
  }
  if (stat(copyTemporaryPath, &destinationInfo) == 0 || errno != ENOENT ||
      stat(backupPath, &destinationInfo) == 0 || errno != ENOENT)
  {
    response = respond(request, "507 Insufficient Storage",
                       "An image-operation recovery file exists; inspect it before continuing");
    goto done;
  }
  {
    FILE* input = fopen(source, "rb");
    int outputDescriptor = open(copyTemporaryPath, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (input == NULL || outputDescriptor < 0)
    {
      if (input != NULL)
      {
        fclose(input);
      }
      if (outputDescriptor >= 0)
      {
        close(outputDescriptor);
        unlink(copyTemporaryPath);
      }
      response = respond(request, "507 Insufficient Storage",
                         "Cannot open source or temporary image file");
      goto done;
    }
    char buffer[4096];
    bool success = true;
    size_t count;
    while (!atomic_load(&stopping) && (count = fread(buffer, 1, sizeof(buffer), input)) > 0)
    {
      size_t written = 0;
      while (written < count)
      {
        ssize_t result = write(outputDescriptor, buffer + written, count - written);
        if (result <= 0)
        {
          success = false;
          break;
        }
        written += (size_t)result;
      }
      if (!success)
      {
        break;
      }
      vTaskDelay(1);
    }
    success = success && !ferror(input) && !atomic_load(&stopping) && fsync(outputDescriptor) == 0;
    if (fclose(input) != 0)
    {
      success = false;
    }
    if (close(outputDescriptor) != 0)
    {
      success = false;
    }
    if (success)
    {
      FILE* original = fopen(source, "rb");
      FILE* copy = fopen(copyTemporaryPath, "rb");
      if (original == NULL || copy == NULL)
      {
        success = false;
        if (original != NULL)
        {
          fclose(original);
        }
        if (copy != NULL)
        {
          fclose(copy);
        }
      }
      else
      {
        char originalBuffer[1024];
        char copyBuffer[1024];
        size_t originalCount;
        size_t copyCount;
        do
        {
          originalCount = fread(originalBuffer, 1, sizeof(originalBuffer), original);
          copyCount = fread(copyBuffer, 1, sizeof(copyBuffer), copy);
          if (originalCount != copyCount || memcmp(originalBuffer, copyBuffer, originalCount) != 0)
          {
            success = false;
            break;
          }
          if (atomic_load(&stopping))
          {
            success = false;
            break;
          }
          vTaskDelay(1);
        } while (originalCount > 0);
        if (ferror(original) || ferror(copy))
        {
          success = false;
        }
        if (fclose(original) != 0)
        {
          success = false;
        }
        if (fclose(copy) != 0)
        {
          success = false;
        }
      }
    }
    if (!success)
    {
      unlink(copyTemporaryPath);
      response = respond(request, "500 Internal Server Error",
                         "Image copy failed verification; source is unchanged");
      goto done;
    }
  }
  if (atomic_load(&stopping))
  {
    unlink(copyTemporaryPath);
    response = respond(request, "503 Service Unavailable", "Image copy cancelled before commit");
    goto done;
  }
  {
    bool backedUp = false;
    if (destinationExists)
    {
      if (rename(destination, backupPath) != 0)
      {
        unlink(copyTemporaryPath);
        response = respond(request, "500 Internal Server Error",
                           "Cannot preserve the previous destination image");
        goto done;
      }
      backedUp = true;
    }
    if (rename(copyTemporaryPath, destination) != 0)
    {
      if (backedUp && rename(backupPath, destination) != 0)
      {
        response = respond(request, "500 Internal Server Error",
                           "Copy failed; the previous image remains in manager-replace-backup.tmp "
                           "beside the destination");
      }
      else
      {
        unlink(copyTemporaryPath);
        response =
            respond(request, "500 Internal Server Error", "Cannot commit verified image copy");
      }
      goto done;
    }
    if (backedUp && unlink(backupPath) != 0)
    {
      response = respond(request, "500 Internal Server Error",
                         "Copy committed, but the previous-image backup could not be removed");
      goto done;
    }
  }
  response = respond(request, "201 Created", "Whole-image copy complete and byte-verified");

done:
  atomic_flag_clear(&imageOperationActive);
  return response;
}

static esp_err_t imageRenameHandler(httpd_req_t* request)
{
  char source[absolutePathCapacity];
  char destination[absolutePathCapacity];
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  if (!resolveImageOperationPaths(request, source, sizeof(source), destination,
                                  sizeof(destination)))
  {
    return respond(request, "400 Bad Request",
                   "Valid source and destination image paths are required");
  }
  if (strcasecmp(source, destination) == 0 || isProtectedImageMetadata(destination))
  {
    return respond(request, "400 Bad Request",
                   "Source and destination must be distinct disk-image files");
  }
  if (atomic_flag_test_and_set(&imageOperationActive))
  {
    return respond(request, "409 Conflict", "Another whole-image operation is in progress");
  }
  esp_err_t response = ESP_OK;
  struct stat info;
  if (stat(source, &info) != 0 || !S_ISREG(info.st_mode))
  {
    response = respond(request, "404 Not Found", "Source disk image is unavailable");
    goto done;
  }
  if (stat(destination, &info) == 0)
  {
    response = respond(request, "409 Conflict", "Destination already exists");
    goto done;
  }
  if (errno != ENOENT)
  {
    response = respond(request, "500 Internal Server Error", "Cannot inspect destination");
    goto done;
  }
  char parent[absolutePathCapacity];
  snprintf(parent, sizeof(parent), "%s", destination);
  char* separator = strrchr(parent, '/');
  if (separator == NULL)
  {
    response = respond(request, "400 Bad Request", "Invalid destination directory");
    goto done;
  }
  *separator = '\0';
  if (!storageDirectoryExists(parent))
  {
    response = respond(request, "404 Not Found", "Destination directory is unavailable");
    goto done;
  }
  response = rename(source, destination) == 0
                 ? respond(request, "200 OK", "Whole-image rename complete")
                 : respond(request, "500 Internal Server Error", "Whole-image rename failed");

done:
  atomic_flag_clear(&imageOperationActive);
  return response;
}

static esp_err_t imageRecoverHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  if (atomic_flag_test_and_set(&imageOperationActive))
  {
    return respond(request, "409 Conflict", "Another whole-image operation is in progress");
  }
  esp_err_t response = ESP_OK;
  char query[800];
  char encodedDestination[relativePathCapacity];
  char relativeDestination[relativePathCapacity];
  char destination[absolutePathCapacity];
  char backupPath[absolutePathCapacity];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "destination", encodedDestination, sizeof(encodedDestination)) !=
          ESP_OK ||
      !pathDecode(encodedDestination, relativeDestination, sizeof(relativeDestination)) ||
      relativeDestination[0] == '\0' || !isDiskImagePath(relativeDestination))
  {
    response =
        respond(request, "400 Bad Request", "A valid destination disk-image path is required");
    goto done;
  }
  int destinationLength =
      snprintf(destination, sizeof(destination), "/microSD/retro/images/%s", relativeDestination);
  if (destinationLength < 0 || (size_t)destinationLength >= sizeof(destination))
  {
    response = respond(request, "400 Bad Request", "Destination image path is too long");
    goto done;
  }
  if (!buildSiblingPath(destination, "manager-replace-backup.tmp", backupPath, sizeof(backupPath)))
  {
    response = respond(request, "400 Bad Request", "Cannot resolve the preserved backup path");
    goto done;
  }
  struct stat info;
  if (stat(backupPath, &info) != 0 || !S_ISREG(info.st_mode))
  {
    response = respond(request, "404 Not Found",
                       "No preserved image backup exists beside that destination");
    goto done;
  }
  if (stat(destination, &info) == 0)
  {
    response =
        respond(request, "409 Conflict",
                "Destination already exists; move it aside before restoring the preserved backup");
    goto done;
  }
  if (errno != ENOENT)
  {
    response = respond(request, "500 Internal Server Error", "Cannot inspect recovery destination");
    goto done;
  }
  char parent[absolutePathCapacity];
  snprintf(parent, sizeof(parent), "%s", destination);
  char* separator = strrchr(parent, '/');
  if (separator == NULL)
  {
    response = respond(request, "400 Bad Request", "Invalid recovery directory");
    goto done;
  }
  *separator = '\0';
  if (!storageDirectoryExists(parent))
  {
    response = respond(request, "404 Not Found", "Recovery destination directory is unavailable");
    goto done;
  }
  response = rename(backupPath, destination) == 0
                 ? respond(request, "200 OK", "Preserved image backup restored")
                 : respond(request, "500 Internal Server Error",
                           "Could not restore preserved image backup");

done:
  atomic_flag_clear(&imageOperationActive);
  return response;
}

//-- ---------------------------------------------------------------------------
//-- Guest file access inside disk images (/api/guest/*). The logic lives in the
//-- host-tested guestFs component; these handlers only translate HTTP.
//-- ---------------------------------------------------------------------------

enum
{
  guestOsIdCapacity = 16,
  guestQueryCapacity = 1100,
  guestIdCapacity = 128,
  guestTransferChunk = 4096
};

typedef struct
{
  imageFile image;
  guestFsIo io;
  guestFsVolume* volume;
  bool littlefs;
  char osId[guestOsIdCapacity];
  char relative[relativePathCapacity];
  char absolute[absolutePathCapacity];
  char detail[guestFsDetailCapacity];
} guestSession;

static bool guestOsIsKnown(const char* osId)
{
  for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
  {
    if (strcmp(osId, imageMachineIds[index]) == 0)
    {
      return true;
    }
  }
  return false;
}

//-- Disk images in LittleFS (the system/boot disks) are addressed as "littlefs.<OS>/<image>"; the
//-- journal of such an image is kept in LittleFS next to it.
static const char guestLittlefsPrefix[] = "littlefs.";

static bool guestJournalPath(const char* osId, bool littlefs, char* path, size_t capacity)
{
  int length = littlefs ? snprintf(path, capacity, "/littlefs/%s/manager-guest-journal.tmp", osId)
                        : snprintf(path, capacity,
                                   "/microSD/retro/images/%s/manager-guest-journal.tmp", osId);
  return length >= 0 && (size_t)length < capacity;
}

static bool guestImageAbsolutePath(const char* osId, bool littlefs, const char* relative,
                                   char* path, size_t capacity)
{
  const char* name = strchr(relative, '/');
  if (name == NULL)
  {
    return false;
  }
  int length = littlefs ? snprintf(path, capacity, "/littlefs/%s%s", osId, name)
                        : snprintf(path, capacity, "/microSD/retro/images/%s", relative);
  return length >= 0 && (size_t)length < capacity;
}

static const char* guestStatusToHttp(guestFsStatus status)
{
  switch (status)
  {
  case guestFsOk:
    return "200 OK";
  case guestFsErrInvalidArgument:
  case guestFsErrBadName:
  case guestFsErrEmptyFile:
    return "400 Bad Request";
  case guestFsErrNotFound:
    return "404 Not Found";
  case guestFsErrExists:
  case guestFsErrReadOnly:
    return "409 Conflict";
  case guestFsErrUnsupported:
  case guestFsErrCorrupt:
  case guestFsErrDamagedFile:
    return "422 Unprocessable Content";
  case guestFsErrNoSpace:
  case guestFsErrDirectoryFull:
    return "507 Insufficient Storage";
  case guestFsErrAborted:
    return "503 Service Unavailable";
  case guestFsErrIo:
  default:
    return "500 Internal Server Error";
  }
}

static esp_err_t respondGuestStatus(httpd_req_t* request, guestFsStatus status, const char* detail)
{
  char message[guestFsDetailCapacity + 96];
  snprintf(message, sizeof(message), "%s%s%s", guestFsStatusText(status),
           detail != NULL && detail[0] != '\0' ? ": " : "", detail != NULL ? detail : "");
  return respond(request, guestStatusToHttp(status), message);
}

static bool guestPoll(void* context)
{
  (void)context;
  vTaskDelay(1);
  return atomic_load(&stopping);
}

static int guestHexValue(char character)
{
  if (character >= '0' && character <= '9')
  {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f')
  {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F')
  {
    return character - 'A' + 10;
  }
  return -1;
}

static bool guestQueryPresent(const char* query, const char* key)
{
  char probe[1];
  esp_err_t result = httpd_query_key_value(query, key, probe, sizeof(probe));
  return result == ESP_OK || result == ESP_ERR_HTTPD_RESULT_TRUNC;
}

//-- Decode a guest file name: any printable text except path separators; the file system validates
// it.
static bool guestQueryText(const char* query, const char* key, char* decoded, size_t capacity)
{
  char encoded[relativePathCapacity];
  if (httpd_query_key_value(query, key, encoded, sizeof(encoded)) != ESP_OK)
  {
    return false;
  }
  size_t used = 0;
  for (size_t index = 0; encoded[index] != '\0'; ++index)
  {
    unsigned char character = (unsigned char)encoded[index];
    if (character == '%')
    {
      if (encoded[index + 1] == '\0' || encoded[index + 2] == '\0')
      {
        return false;
      }
      int high = guestHexValue(encoded[index + 1]);
      int low = guestHexValue(encoded[index + 2]);
      if (high < 0 || low < 0)
      {
        return false;
      }
      character = (unsigned char)((high << 4) | low);
      index += 2;
    }
    if (character < 0x20 || character == 0x7f || character == '/' || character == '\\' ||
        used + 1 >= capacity)
    {
      return false;
    }
    decoded[used++] = (char)character;
  }
  decoded[used] = '\0';
  return used > 0;
}

static bool guestQueryUser(const char* query, const char* key, uint8_t* user)
{
  char text[8];
  *user = 0;
  if (!guestQueryPresent(query, key))
  {
    return true;
  }
  if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK || text[0] == '\0')
  {
    return false;
  }
  unsigned value = 0;
  for (const char* digit = text; *digit != '\0'; ++digit)
  {
    if (*digit < '0' || *digit > '9')
    {
      return false;
    }
    value = value * 10 + (unsigned)(*digit - '0');
    if (value > guestFsMaxUser)
    {
      return false;
    }
  }
  *user = (uint8_t)value;
  return true;
}

static bool guestQueryFlag(const char* query, const char* key)
{
  char text[4];
  return httpd_query_key_value(query, key, text, sizeof(text)) == ESP_OK && strcmp(text, "1") == 0;
}

//-- Optional numeric query value (hexadecimal or decimal). Returns false when present but
// malformed.
static bool guestQueryNumber(const char* query, const char* key, unsigned base, unsigned maximum,
                             unsigned* value, bool* present)
{
  char text[8];
  *present = false;
  if (!guestQueryPresent(query, key))
  {
    return true;
  }
  if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK || text[0] == '\0')
  {
    return false;
  }
  unsigned number = 0;
  for (const char* digit = text; *digit != '\0'; ++digit)
  {
    int digitValue = guestHexValue(*digit);
    if (digitValue < 0 || (unsigned)digitValue >= base)
    {
      return false;
    }
    number = number * base + (unsigned)digitValue;
    if (number > maximum)
    {
      return false;
    }
  }
  *value = number;
  *present = true;
  return true;
}

//-- Optional ptype (hex), aux (hex) and kind (decimal) query values become interchange attributes.
static bool guestQueryAttributes(const char* query, guestFsAttributes* attributes, bool* present)
{
  unsigned typeValue = 0;
  unsigned auxValue = 0;
  unsigned kindValue = 0;
  bool typePresent = false;
  bool auxPresent = false;
  bool kindPresent = false;
  *present = false;
  if (!guestQueryNumber(query, "ptype", 16, 0xff, &typeValue, &typePresent) ||
      !guestQueryNumber(query, "aux", 16, 0xffff, &auxValue, &auxPresent) ||
      !guestQueryNumber(query, "kind", 10, 15, &kindValue, &kindPresent))
  {
    return false;
  }
  memset(attributes, 0, sizeof(*attributes));
  attributes->prodosType = (uint16_t)typeValue;
  attributes->auxType = (uint16_t)auxValue;
  attributes->pascalKind = (uint8_t)kindValue;
  *present = typePresent || auxPresent || kindPresent;
  return true;
}

//-- Resolve a pending undo journal of an OS directory before any other guest operation.
static bool guestRecoverPending(const char* osId, bool littlefs, char* message,
                                size_t messageCapacity)
{
  char journalPath[absolutePathCapacity];
  char imageId[guestIdCapacity];
  if (!guestJournalPath(osId, littlefs, journalPath, sizeof(journalPath)))
  {
    snprintf(message, messageCapacity, "Journal path is too long");
    return false;
  }
  struct stat info;
  if (stat(journalPath, &info) != 0)
  {
    return true;
  }
  guestFsStatus status = guestFsJournalPeek(journalPath, imageId, sizeof(imageId));
  if (status == guestFsErrCorrupt || status == guestFsErrInvalidArgument)
  {
    status = guestFsJournalRecover(journalPath, NULL);
  }
  else if (status == guestFsOk)
  {
    char absolute[absolutePathCapacity];
    imageFile image = {.file = NULL, .size = 0, .readOnly = false};
    guestFsIo io;
    char expectedPrefix[guestOsIdCapacity + sizeof(guestLittlefsPrefix) + 1];
    int prefixLength = snprintf(expectedPrefix, sizeof(expectedPrefix), "%s%s/",
                                littlefs ? guestLittlefsPrefix : "", osId);
    if (prefixLength > 0 && strncmp(imageId, expectedPrefix, (size_t)prefixLength) == 0 &&
        isFlatImagePath(imageId) && isDiskImagePath(imageId) &&
        guestImageAbsolutePath(osId, littlefs,
                               imageId + (littlefs ? strlen(guestLittlefsPrefix) : 0), absolute,
                               sizeof(absolute)) &&
        imageOpen(&image, absolute, false))
    {
      guestFsImageBind(&io, &image);
      status = guestFsJournalRecover(journalPath, &io);
      imageClose(&image);
    }
    else
    {
      //-- The image is gone or the id is unusable: only an unsealed journal can be discarded.
      status = guestFsJournalRecover(journalPath, NULL);
    }
  }
  if (status != guestFsOk)
  {
    snprintf(message, messageCapacity,
             "Pending guest journal could not be resolved (%s); image left untouched",
             guestFsStatusText(status));
    return false;
  }
  ESP_LOGW(fileTransferTag, "Resolved a pending guest-file journal in %s", osId);
  return true;
}

static void guestSessionClose(guestSession* session)
{
  if (session->volume != NULL)
  {
    guestFsClose(session->volume);
    session->volume = NULL;
  }
  if (session->image.file != NULL)
  {
    imageClose(&session->image);
  }
}

//-- Returns NULL on success, otherwise an HTTP status string with the reason in message.
static const char* guestSessionOpen(guestSession* session, const char* query, const char* key,
                                    bool writable, char* message, size_t messageCapacity)
{
  char encoded[relativePathCapacity];
  memset(session, 0, sizeof(*session));
  if (httpd_query_key_value(query, key, encoded, sizeof(encoded)) != ESP_OK ||
      !pathDecode(encoded, session->relative, sizeof(session->relative)) ||
      !isDiskImagePath(session->relative) || !isFlatImagePath(session->relative))
  {
    snprintf(message, messageCapacity, "A valid <OS>/<image> path is required");
    return "400 Bad Request";
  }
  const char* osStart = session->relative;
  if (strncmp(osStart, guestLittlefsPrefix, strlen(guestLittlefsPrefix)) == 0)
  {
    session->littlefs = true;
    osStart += strlen(guestLittlefsPrefix);
  }
  size_t osLength = (size_t)(strchr(osStart, '/') - osStart);
  if (osLength == 0 || osLength >= sizeof(session->osId))
  {
    snprintf(message, messageCapacity, "Invalid OS directory");
    return "400 Bad Request";
  }
  memcpy(session->osId, osStart, osLength);
  session->osId[osLength] = '\0';
  if (!guestOsIsKnown(session->osId))
  {
    snprintf(message, messageCapacity, "Unknown OS directory");
    return "400 Bad Request";
  }
  if (!guestFsSupportsOs(session->osId))
  {
    snprintf(message, messageCapacity, "File access inside %s images is not supported yet",
             session->osId);
    return "501 Not Implemented";
  }
  struct stat info;
  if (!guestImageAbsolutePath(session->osId, session->littlefs, osStart, session->absolute,
                              sizeof(session->absolute)) ||
      stat(session->absolute, &info) != 0 || !S_ISREG(info.st_mode))
  {
    snprintf(message, messageCapacity, "Disk image unavailable");
    return "404 Not Found";
  }
  if (!guestRecoverPending(session->osId, session->littlefs, message, messageCapacity))
  {
    return "500 Internal Server Error";
  }
  if (!imageOpen(&session->image, session->absolute, !writable))
  {
    snprintf(message, messageCapacity, "Cannot open the disk image");
    return "500 Internal Server Error";
  }
  guestFsImageBind(&session->io, &session->image);
  char journalPath[absolutePathCapacity];
  if (!guestJournalPath(session->osId, session->littlefs, journalPath, sizeof(journalPath)))
  {
    guestSessionClose(session);
    snprintf(message, messageCapacity, "Journal path is too long");
    return "500 Internal Server Error";
  }
  guestFsStatus status = guestFsOpen(&session->io, session->osId, journalPath, session->relative,
                                     &session->volume, session->detail, sizeof(session->detail));
  if (status != guestFsOk)
  {
    snprintf(message, messageCapacity, "%s%s%s", guestFsStatusText(status),
             session->detail[0] != '\0' ? ": " : "", session->detail);
    guestSessionClose(session);
    return guestStatusToHttp(status);
  }
  return NULL;
}

static bool guestLockAcquire(httpd_req_t* request)
{
  if (atomic_flag_test_and_set(&imageOperationActive))
  {
    respond(request, "409 Conflict", "Another whole-image operation is in progress");
    return false;
  }
  return true;
}

static void jsonEscapeInto(char* target, size_t capacity, const char* text)
{
  size_t used = 0;
  for (; *text != '\0' && used + 7 < capacity; ++text)
  {
    unsigned char character = (unsigned char)*text;
    if (character == '"' || character == '\\')
    {
      target[used++] = '\\';
      target[used++] = (char)character;
    }
    else if (character < 0x20 || character >= 0x7f)
    {
      used += (size_t)snprintf(target + used, capacity - used, "\\u%04x", character);
    }
    else
    {
      target[used++] = (char)character;
    }
  }
  target[used] = '\0';
}

typedef struct
{
  httpd_req_t* request;
  esp_err_t result;
  bool first;
} guestListState;

static bool guestListEntry(void* context, const guestFsEntry* entry)
{
  guestListState* state = context;
  char escaped[guestFsNameCapacity * 6 + 1];
  char row[448];
  jsonEscapeInto(escaped, sizeof(escaped), entry->name);
  int used = snprintf(
      row, sizeof(row),
      "%s{\"user\":%u,\"name\":\"%s\",\"size\":%lu,\"allocated\":%lu,\"readOnly\":%s,\"system\":%s,"
      "\"damaged\":%s,\"directory\":%s,\"type\":\"%s\"",
      state->first ? "" : ",", (unsigned)entry->user, escaped, (unsigned long)entry->size,
      (unsigned long)entry->allocatedBytes, entry->readOnlyAttribute ? "true" : "false",
      entry->systemAttribute ? "true" : "false", entry->damaged ? "true" : "false",
      entry->directory ? "true" : "false", entry->typeLabel);
  if (entry->hasAttributes && used > 0 && (size_t)used < sizeof(row))
  {
    snprintf(row + used, sizeof(row) - (size_t)used,
             ",\"prodosType\":%u,\"auxType\":%u,\"pascalKind\":%u}",
             (unsigned)entry->attributes.prodosType, (unsigned)entry->attributes.auxType,
             (unsigned)entry->attributes.pascalKind);
  }
  else if (used > 0 && (size_t)used < sizeof(row))
  {
    snprintf(row + used, sizeof(row) - (size_t)used, "}");
  }
  state->first = false;
  state->result = httpd_resp_sendstr_chunk(state->request, row);
  vTaskDelay(1);
  return state->result == ESP_OK && !atomic_load(&stopping);
}

static esp_err_t guestFilesHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[guestQueryCapacity];
  char message[guestFsDetailCapacity + 96];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK)
  {
    return respond(request, "400 Bad Request", "Missing image parameter");
  }
  if (!guestLockAcquire(request))
  {
    return ESP_OK;
  }
  guestSession session;
  const char* failure = guestSessionOpen(&session, query, "image", false, message, sizeof(message));
  if (failure != NULL)
  {
    atomic_flag_clear(&imageOperationActive);
    return respond(request, failure, message);
  }
  guestFsInfo info;
  guestFsInfoGet(session.volume, &info);
  char header[512];
  snprintf(
      header, sizeof(header),
      "{\"image\":\"%s\",\"fs\":\"%s\",\"profile\":\"%s\",\"blockSize\":%lu,\"totalBlocks\":%lu,"
      "\"freeBlocks\":%lu,\"freeBytes\":%llu,\"directoryEntries\":%lu,\"freeDirectoryEntries\":%lu,"
      "\"maxUser\":%u,\"nameRule\":\"%s\",\"files\":[",
      session.relative, info.fsName, info.profileName, (unsigned long)info.blockSize,
      (unsigned long)info.totalBlocks, (unsigned long)info.freeBlocks,
      (unsigned long long)info.freeBlocks * info.blockSize, (unsigned long)info.directoryEntries,
      (unsigned long)info.freeDirectoryEntries, (unsigned)info.maxUser, info.nameRule);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  guestListState state = {request, httpd_resp_sendstr_chunk(request, header), true};
  guestFsStatus status = guestFsOk;
  if (state.result == ESP_OK)
  {
    status = guestFsList(session.volume, guestListEntry, &state);
  }
  guestSessionClose(&session);
  atomic_flag_clear(&imageOperationActive);
  if (state.result != ESP_OK || status != guestFsOk || atomic_load(&stopping))
  {
    return ESP_FAIL;
  }
  state.result = httpd_resp_sendstr_chunk(request, "]}");
  return state.result == ESP_OK ? httpd_resp_send_chunk(request, NULL, 0) : state.result;
}

static esp_err_t guestFileHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[guestQueryCapacity];
  char message[guestFsDetailCapacity + 96];
  char name[guestFsNameCapacity];
  uint8_t user;
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !guestQueryText(query, "name", name, sizeof(name)) || !guestQueryUser(query, "user", &user))
  {
    return respond(request, "400 Bad Request", "image, user and name parameters are required");
  }
  if (!guestLockAcquire(request))
  {
    return ESP_OK;
  }
  guestSession session;
  const char* failure = guestSessionOpen(&session, query, "image", false, message, sizeof(message));
  if (failure != NULL)
  {
    atomic_flag_clear(&imageOperationActive);
    return respond(request, failure, message);
  }
  esp_err_t response = ESP_OK;
  uint8_t* buffer = NULL;
  guestFsEntry entry;
  guestFsStatus status = guestFsStat(session.volume, user, name, &entry);
  if (status == guestFsOk && entry.damaged)
  {
    status = guestFsErrDamagedFile;
  }
  if (status != guestFsOk)
  {
    response = respondGuestStatus(request, status, "");
    goto done;
  }
  buffer = malloc(guestTransferChunk);
  if (buffer == NULL)
  {
    response = respond(request, "500 Internal Server Error", "Out of memory");
    goto done;
  }
  char safeName[guestFsNameCapacity];
  size_t safeLength = 0;
  for (const char* character = entry.name; *character != '\0' && safeLength + 1 < sizeof(safeName);
       ++character)
  {
    safeName[safeLength++] = isalnum((unsigned char)*character) || *character == '.' ||
                                     *character == '-' || *character == '_'
                                 ? *character
                                 : '_';
  }
  safeName[safeLength] = '\0';
  char disposition[96];
  char sizeText[16];
  snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", safeName);
  snprintf(sizeText, sizeof(sizeText), "%lu", (unsigned long)entry.size);
  httpd_resp_set_type(request, "application/octet-stream");
  httpd_resp_set_hdr(request, "Content-Disposition", disposition);
  httpd_resp_set_hdr(request, "X-Guest-Size", sizeText);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  uint32_t offset = 0;
  while (offset < entry.size && !atomic_load(&stopping))
  {
    size_t wanted =
        entry.size - offset > guestTransferChunk ? guestTransferChunk : entry.size - offset;
    size_t got = 0;
    status = guestFsReadAt(session.volume, user, name, offset, buffer, wanted, &got);
    if (status != guestFsOk || got == 0 ||
        httpd_resp_send_chunk(request, (const char*)buffer, got) != ESP_OK)
    {
      response = ESP_FAIL;
      break;
    }
    offset += (uint32_t)got;
    vTaskDelay(1);
  }
  if (response == ESP_OK && (offset < entry.size || atomic_load(&stopping)))
  {
    response = ESP_FAIL;
  }
  if (response == ESP_OK)
  {
    response = httpd_resp_send_chunk(request, NULL, 0);
  }

done:
  free(buffer);
  guestSessionClose(&session);
  atomic_flag_clear(&imageOperationActive);
  return response;
}

typedef struct
{
  httpd_req_t* request;
  size_t remaining;
} guestUploadState;

static bool guestUploadSource(void* context, void* buffer, size_t length)
{
  guestUploadState* state = context;
  char* target = buffer;
  while (length > 0)
  {
    if (atomic_load(&stopping) || length > state->remaining)
    {
      return false;
    }
    int received = httpd_req_recv(state->request, target, length);
    if (received == HTTPD_SOCK_ERR_TIMEOUT)
    {
      //-- SD writes can stall the receive loop; retry instead of aborting.
      continue;
    }
    if (received <= 0)
    {
      return false;
    }
    target += received;
    length -= (size_t)received;
    state->remaining -= (size_t)received;
  }
  return true;
}

static esp_err_t guestUploadHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[guestQueryCapacity];
  char message[guestFsDetailCapacity + 96];
  char name[guestFsNameCapacity];
  uint8_t user;
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !guestQueryText(query, "name", name, sizeof(name)) || !guestQueryUser(query, "user", &user))
  {
    httpd_resp_set_hdr(request, "Connection", "close");
    return respond(request, "400 Bad Request", "image, user and name parameters are required");
  }
  if (!uploadLengthIsSupported(request))
  {
    return ESP_OK;
  }
  bool overwrite = guestQueryFlag(query, "overwrite");
  guestFsAttributes attributes;
  bool attributesPresent = false;
  if (!guestQueryAttributes(query, &attributes, &attributesPresent))
  {
    httpd_resp_set_hdr(request, "Connection", "close");
    return respond(request, "400 Bad Request", "Invalid ptype, aux or kind parameter");
  }
  if (!guestLockAcquire(request))
  {
    return ESP_OK;
  }
  guestSession session;
  const char* failure = guestSessionOpen(&session, query, "image", true, message, sizeof(message));
  if (failure != NULL)
  {
    atomic_flag_clear(&imageOperationActive);
    httpd_resp_set_hdr(request, "Connection", "close");
    return respond(request, failure, message);
  }
  guestUploadState state = {request, request->content_len};
  guestFsStatus status =
      guestFsWriteEx(session.volume, user, name, (uint32_t)request->content_len, guestUploadSource,
                     &state, overwrite, attributesPresent ? &attributes : NULL);
  guestSessionClose(&session);
  atomic_flag_clear(&imageOperationActive);
  if (status != guestFsOk)
  {
    if (state.remaining > 0)
    {
      httpd_resp_set_hdr(request, "Connection", "close");
    }
    return respondGuestStatus(request, status, session.detail);
  }
  return respond(request, "201 Created", "File stored in the disk image");
}

static esp_err_t guestDeleteHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[guestQueryCapacity];
  char message[guestFsDetailCapacity + 96];
  char name[guestFsNameCapacity];
  uint8_t user;
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !guestQueryText(query, "name", name, sizeof(name)) || !guestQueryUser(query, "user", &user))
  {
    return respond(request, "400 Bad Request", "image, user and name parameters are required");
  }
  if (!guestLockAcquire(request))
  {
    return ESP_OK;
  }
  guestSession session;
  const char* failure = guestSessionOpen(&session, query, "image", true, message, sizeof(message));
  if (failure != NULL)
  {
    atomic_flag_clear(&imageOperationActive);
    return respond(request, failure, message);
  }
  guestFsStatus status = guestFsRemove(session.volume, user, name);
  guestSessionClose(&session);
  atomic_flag_clear(&imageOperationActive);
  return status == guestFsOk ? respond(request, "200 OK", "File deleted from the disk image")
                             : respondGuestStatus(request, status, "");
}

static esp_err_t guestRenameHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[guestQueryCapacity];
  char message[guestFsDetailCapacity + 96];
  char name[guestFsNameCapacity];
  char newName[guestFsNameCapacity];
  uint8_t user;
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !guestQueryText(query, "name", name, sizeof(name)) ||
      !guestQueryText(query, "to", newName, sizeof(newName)) ||
      !guestQueryUser(query, "user", &user))
  {
    return respond(request, "400 Bad Request", "image, user, name and to parameters are required");
  }
  if (!guestLockAcquire(request))
  {
    return ESP_OK;
  }
  guestSession session;
  const char* failure = guestSessionOpen(&session, query, "image", true, message, sizeof(message));
  if (failure != NULL)
  {
    atomic_flag_clear(&imageOperationActive);
    return respond(request, failure, message);
  }
  guestFsStatus status = guestFsRename(session.volume, user, name, newName);
  guestSessionClose(&session);
  atomic_flag_clear(&imageOperationActive);
  return status == guestFsOk ? respond(request, "200 OK", "File renamed")
                             : respondGuestStatus(request, status, "");
}

//-- An ordinary SD file lives directly in /retro/images/<OS>/ and is never a disk image or manager
// metadata.
static bool guestResolveSdFile(const char* query, const char* key, char* relative,
                               size_t relativeCapacity, char* absolute, size_t absoluteCapacity)
{
  char encoded[relativePathCapacity];
  if (httpd_query_key_value(query, key, encoded, sizeof(encoded)) != ESP_OK ||
      !pathDecode(encoded, relative, relativeCapacity) || !isFlatImagePath(relative) ||
      isDiskImagePath(relative) || isProtectedImageMetadata(relative))
  {
    return false;
  }
  char osId[guestOsIdCapacity];
  size_t osLength = (size_t)(strchr(relative, '/') - relative);
  if (osLength == 0 || osLength >= sizeof(osId))
  {
    return false;
  }
  memcpy(osId, relative, osLength);
  osId[osLength] = '\0';
  int length = snprintf(absolute, absoluteCapacity, "/microSD/retro/images/%s", relative);
  return guestOsIsKnown(osId) && length >= 0 && (size_t)length < absoluteCapacity;
}

//-- Commit the temporary file over the destination, keeping the old file until the new one is in
// place.
static bool guestCommitSdFile(const char* destination, bool destinationExists)
{
  char backup[absolutePathCapacity];
  bool backedUp = false;
  if (destinationExists)
  {
    if (!buildSiblingPath(destination, "manager-replace-backup.tmp", backup, sizeof(backup)) ||
        rename(destination, backup) != 0)
    {
      return false;
    }
    backedUp = true;
  }
  if (rename(guestTemporaryPath, destination) != 0)
  {
    if (backedUp)
    {
      rename(backup, destination);
    }
    return false;
  }
  if (backedUp)
  {
    unlink(backup);
  }
  return true;
}

//-- Returns NULL when the SD destination may be written, otherwise an HTTP status and reason.
static const char* guestCheckSdDestination(const char* destination, bool overwrite, bool* exists,
                                           char* message, size_t messageCapacity)
{
  struct stat info;
  char parent[absolutePathCapacity];
  char backup[absolutePathCapacity];
  snprintf(parent, sizeof(parent), "%s", destination);
  char* separator = strrchr(parent, '/');
  if (separator == NULL)
  {
    snprintf(message, messageCapacity, "Invalid destination");
    return "400 Bad Request";
  }
  *separator = '\0';
  if (!storageDirectoryExists(parent))
  {
    snprintf(message, messageCapacity, "Destination directory missing");
    return "404 Not Found";
  }
  *exists = false;
  if (stat(destination, &info) == 0)
  {
    if (!overwrite || !S_ISREG(info.st_mode))
    {
      snprintf(message, messageCapacity, "Destination exists; enable OVERWRITE to replace it");
      return "409 Conflict";
    }
    if (!buildSiblingPath(destination, "manager-replace-backup.tmp", backup, sizeof(backup)) ||
        stat(backup, &info) == 0 || errno != ENOENT)
    {
      snprintf(message, messageCapacity, "A preserved replacement backup exists; restore it first");
      return "507 Insufficient Storage";
    }
    *exists = true;
  }
  else if (errno != ENOENT)
  {
    snprintf(message, messageCapacity, "Cannot inspect destination");
    return "500 Internal Server Error";
  }
  return NULL;
}

static const char* guestLeafName(const char* relative)
{
  const char* separator = strrchr(relative, '/');
  return separator == NULL ? relative : separator + 1;
}

static esp_err_t guestCopyHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[guestQueryCapacity];
  char message[guestFsDetailCapacity + 96];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK)
  {
    return respond(request, "400 Bad Request", "Missing copy parameters");
  }
  bool fromImage = guestQueryPresent(query, "fromImage");
  bool fromFile = guestQueryPresent(query, "fromFile");
  bool toImage = guestQueryPresent(query, "toImage");
  bool toFile = guestQueryPresent(query, "toFile");
  uint8_t fromUser;
  uint8_t toUser;
  char fromName[guestFsNameCapacity] = "";
  char toName[guestFsNameCapacity] = "";
  if (fromImage == fromFile || toImage == toFile || (fromFile && toFile) ||
      !guestQueryUser(query, "fromUser", &fromUser) || !guestQueryUser(query, "toUser", &toUser) ||
      (fromImage && !guestQueryText(query, "fromName", fromName, sizeof(fromName))) ||
      (guestQueryPresent(query, "toName") &&
       !guestQueryText(query, "toName", toName, sizeof(toName))))
  {
    return respond(
        request, "400 Bad Request",
        "Exactly one source (fromImage or fromFile) and one destination (toImage or toFile) are "
        "required; an SD file cannot be copied to an SD file here");
  }
  bool overwrite = guestQueryFlag(query, "overwrite");
  char sdRelative[relativePathCapacity];
  char sdAbsolute[absolutePathCapacity];
  if ((fromFile && !guestResolveSdFile(query, "fromFile", sdRelative, sizeof(sdRelative),
                                       sdAbsolute, sizeof(sdAbsolute))) ||
      (toFile && !guestResolveSdFile(query, "toFile", sdRelative, sizeof(sdRelative), sdAbsolute,
                                     sizeof(sdAbsolute))))
  {
    return respond(
        request, "400 Bad Request",
        "SD file must be <OS>/<file> directly in an OS directory and must not be a disk image");
  }
  if (toImage && toName[0] == '\0')
  {
    const char* defaultName = fromImage ? fromName : guestLeafName(sdRelative);
    if (strlen(defaultName) >= sizeof(toName))
    {
      return respond(request, "400 Bad Request",
                     "File name is too long for the guest file system; give a toName");
    }
    memcpy(toName, defaultName, strlen(defaultName) + 1);
  }
  if (!guestLockAcquire(request))
  {
    return ESP_OK;
  }
  esp_err_t response = ESP_OK;
  guestSession source;
  guestSession destination;
  bool sourceOpen = false;
  bool destinationOpen = false;
  FILE* file = NULL;
  bool temporaryCreated = false;
  const char* failure = NULL;
  guestFsStatus status = guestFsOk;
  if (fromImage && toImage)
  {
    char sourceEncoded[relativePathCapacity];
    char destinationEncoded[relativePathCapacity];
    char sourceRelative[relativePathCapacity];
    char destinationRelative[relativePathCapacity];
    bool same =
        httpd_query_key_value(query, "fromImage", sourceEncoded, sizeof(sourceEncoded)) == ESP_OK &&
        httpd_query_key_value(query, "toImage", destinationEncoded, sizeof(destinationEncoded)) ==
            ESP_OK &&
        pathDecode(sourceEncoded, sourceRelative, sizeof(sourceRelative)) &&
        pathDecode(destinationEncoded, destinationRelative, sizeof(destinationRelative)) &&
        strcasecmp(sourceRelative, destinationRelative) == 0;
    failure = guestSessionOpen(&destination, query, "toImage", true, message, sizeof(message));
    if (failure != NULL)
    {
      response = respond(request, failure, message);
      goto done;
    }
    destinationOpen = true;
    if (same)
    {
      status = guestFsCopy(destination.volume, fromUser, fromName, destination.volume, toUser,
                           toName, overwrite, guestPoll, NULL);
    }
    else
    {
      failure = guestSessionOpen(&source, query, "fromImage", false, message, sizeof(message));
      if (failure != NULL)
      {
        response = respond(request, failure, message);
        goto done;
      }
      sourceOpen = true;
      status = guestFsCopy(source.volume, fromUser, fromName, destination.volume, toUser, toName,
                           overwrite, guestPoll, NULL);
    }
    response = status == guestFsOk ? respond(request, "201 Created", "File copied")
                                   : respondGuestStatus(request, status, destination.detail);
    goto done;
  }
  if (fromImage && toFile)
  {
    bool exists = false;
    failure = guestCheckSdDestination(sdAbsolute, overwrite, &exists, message, sizeof(message));
    if (failure == NULL)
    {
      failure = guestSessionOpen(&source, query, "fromImage", false, message, sizeof(message));
      sourceOpen = failure == NULL;
    }
    if (failure != NULL)
    {
      response = respond(request, failure, message);
      goto done;
    }
    file = fopen(guestTemporaryPath, "wb");
    if (file == NULL)
    {
      response = respond(request, "507 Insufficient Storage",
                         "Cannot create temporary file; check SD and free space");
      goto done;
    }
    temporaryCreated = true;
    status = guestFsExportToFile(source.volume, fromUser, fromName, file, guestPoll, NULL);
    bool flushed = fflush(file) == 0 && fsync(fileno(file)) == 0;
    bool closed = fclose(file) == 0;
    file = NULL;
    if (status == guestFsOk && (!flushed || !closed))
    {
      status = guestFsErrIo;
    }
    if (status != guestFsOk)
    {
      response = respondGuestStatus(request, status, "");
      goto done;
    }
    if (!guestCommitSdFile(sdAbsolute, exists))
    {
      response = respond(request, "500 Internal Server Error", "Could not commit the SD file");
      goto done;
    }
    temporaryCreated = false;
    response = respond(request, "201 Created", "File copied to the SD card");
    goto done;
  }
  //-- fromFile -> toImage
  struct stat info;
  if (stat(sdAbsolute, &info) != 0 || !S_ISREG(info.st_mode))
  {
    response = respond(request, "404 Not Found", "SD file unavailable");
    goto done;
  }
  if (info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX - 1U)
  {
    response = respond(request, "413 Content Too Large", "SD file is too large");
    goto done;
  }
  failure = guestSessionOpen(&destination, query, "toImage", true, message, sizeof(message));
  if (failure != NULL)
  {
    response = respond(request, failure, message);
    goto done;
  }
  destinationOpen = true;
  file = fopen(sdAbsolute, "rb");
  if (file == NULL)
  {
    response = respond(request, "500 Internal Server Error", "Cannot open the SD file");
    goto done;
  }
  status = guestFsImportFromFile(destination.volume, toUser, toName, file, (uint32_t)info.st_size,
                                 overwrite, guestPoll, NULL);
  response = status == guestFsOk
                 ? respond(request, "201 Created", "File copied into the disk image")
                 : respondGuestStatus(request, status, destination.detail);

done:
  if (file != NULL)
  {
    fclose(file);
  }
  if (temporaryCreated)
  {
    unlink(guestTemporaryPath);
  }
  if (sourceOpen)
  {
    guestSessionClose(&source);
  }
  if (destinationOpen)
  {
    guestSessionClose(&destination);
  }
  atomic_flag_clear(&imageOperationActive);
  return response;
}

//-- Lists the disk images in /littlefs/<OS>/ (system/boot disks) as guest-file targets.
static esp_err_t guestLittlefsImagesHandler(httpd_req_t* request)
{
  if (!acceptingRequests(request))
  {
    return ESP_OK;
  }
  char query[64];
  char osId[guestOsIdCapacity];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "os", osId, sizeof(osId)) != ESP_OK || !guestOsIsKnown(osId))
  {
    return respond(request, "400 Bad Request", "A valid os is required");
  }
  char directoryPath[absolutePathCapacity];
  snprintf(directoryPath, sizeof(directoryPath), "/littlefs/%s", osId);
  DIR* directory = opendir(directoryPath);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  esp_err_t result = httpd_resp_sendstr_chunk(request, "[");
  bool first = true;
  struct dirent* entry;
  while (directory != NULL && result == ESP_OK && (entry = readdir(directory)) != NULL)
  {
    char child[absolutePathCapacity];
    struct stat info;
    int length = snprintf(child, sizeof(child), "%s/%s", directoryPath, entry->d_name);
    if (!pathIsSafe(entry->d_name) || !isDiskImagePath(entry->d_name) || length < 0 ||
        (size_t)length >= sizeof(child) || stat(child, &info) != 0 || !S_ISREG(info.st_mode))
    {
      continue;
    }
    char row[160];
    snprintf(row, sizeof(row), "%s{\"name\":\"%.64s\",\"size\":%llu}", first ? "" : ",",
             entry->d_name, (unsigned long long)info.st_size);
    result = httpd_resp_sendstr_chunk(request, row);
    first = false;
  }
  if (directory != NULL)
  {
    closedir(directory);
  }
  if (result != ESP_OK)
  {
    return ESP_FAIL;
  }
  result = httpd_resp_sendstr_chunk(request, "]");
  return result == ESP_OK ? httpd_resp_send_chunk(request, NULL, 0) : result;
}

static void guestRecoverAllPending(void)
{
  char message[guestFsDetailCapacity + 96];
  for (size_t index = 0; index < sizeof(imageMachineIds) / sizeof(imageMachineIds[0]); ++index)
  {
    for (int littlefs = 0; littlefs < 2; ++littlefs)
    {
      if (!guestRecoverPending(imageMachineIds[index], littlefs != 0, message, sizeof(message)))
      {
        ESP_LOGE(fileTransferTag, "%s: %s", imageMachineIds[index], message);
      }
    }
  }
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
  if (unlink(imageTemporaryPath) != 0 && errno != ENOENT)
  {
    return ESP_FAIL;
  }
  if (unlink(guestTemporaryPath) != 0 && errno != ENOENT)
  {
    return ESP_FAIL;
  }
  guestRecoverAllPending();
  atomic_store(&stopping, false);
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 12288;
  config.max_uri_handlers = 20;
  config.max_open_sockets = 7;
  //-- Never purge the socket of an active large transfer; allow slow SD/WiFi stalls.
  config.lru_purge_enable = false;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 15;
  esp_err_t result = httpd_start(&server, &config);
  if (result != ESP_OK)
  {
    server = NULL;
    return result;
  }
  const httpd_uri_t handlers[] = {
      {.uri = "/", .method = HTTP_GET, .handler = pageHandler},
      {.uri = "/api/help", .method = HTTP_GET, .handler = helpHandler},
      {.uri = "/api/list", .method = HTTP_GET, .handler = listHandler},
      {.uri = "/api/file", .method = HTTP_GET, .handler = downloadHandler},
      {.uri = "/api/file", .method = HTTP_PUT, .handler = uploadHandler},
      {.uri = "/api/file", .method = HTTP_DELETE, .handler = deleteHandler},
      {.uri = "/api/images/copy", .method = HTTP_POST, .handler = imageCopyHandler},
      {.uri = "/api/images/rename", .method = HTTP_POST, .handler = imageRenameHandler},
      {.uri = "/api/images/recover", .method = HTTP_POST, .handler = imageRecoverHandler},
      {.uri = "/api/guest/images", .method = HTTP_GET, .handler = guestLittlefsImagesHandler},
      {.uri = "/api/guest/files", .method = HTTP_GET, .handler = guestFilesHandler},
      {.uri = "/api/guest/file", .method = HTTP_GET, .handler = guestFileHandler},
      {.uri = "/api/guest/upload", .method = HTTP_POST, .handler = guestUploadHandler},
      {.uri = "/api/guest/delete", .method = HTTP_POST, .handler = guestDeleteHandler},
      {.uri = "/api/guest/rename", .method = HTTP_POST, .handler = guestRenameHandler},
      {.uri = "/api/guest/copy", .method = HTTP_POST, .handler = guestCopyHandler}};
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
