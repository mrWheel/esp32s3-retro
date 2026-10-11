#include <apple2.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define zeroPageSaveStart 0x80
#define zeroPageSaveSize 0x30

static void __fastcall__ callOutputVector(unsigned char character)
{
  (void)character;
  __asm__("jmp ($0036)");
}

static char hostGetKey(void)
{
  unsigned char key;

  while ((*(volatile unsigned char *)0xC000 & 0x80) == 0)
  {
  }
  key = *(volatile unsigned char *)0xC000 & 0x7F;
  __asm__("bit $C010");
  return (char)key;
}

static void hostPutchar(char character)
{
  unsigned char index;
  static unsigned char savedZeroPage[zeroPageSaveSize];

  if (character == '\n')
  {
    character = '\r';
  }
  //-- The output vector (the 80-column card firmware) may use the zero page cc65 keeps its registers in.
  for (index = 0; index < zeroPageSaveSize; ++index)
  {
    savedZeroPage[index] = *(volatile unsigned char *)(zeroPageSaveStart + index);
  }
  callOutputVector((unsigned char)character | 0x80);
  for (index = 0; index < zeroPageSaveSize; ++index)
  {
    *(volatile unsigned char *)(zeroPageSaveStart + index) = savedZeroPage[index];
  }
}

static void hostWrite(const char *text)
{
  while (*text != '\0')
  {
    hostPutchar(*text++);
  }
}

static void hostPuts(const char *text)
{
  hostWrite(text);
  hostPutchar('\n');
}

static void hostPrintf(const char *format, ...)
{
  char buffer[96];
  va_list arguments;

  va_start(arguments, format);
  vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  hostWrite(buffer);
}

#define printf hostPrintf
#define puts hostPuts
#define putchar hostPutchar

enum
{
  exchangeDataRegisterAddress = 0xC0A0,
  exchangeAbortRegisterAddress = 0xC0A1,
  exchangeCommandQuery = 0,
  exchangeCommandDirectory = 1,
  exchangeCommandGet = 2,
  exchangeCommandPut = 3,
  exchangeCommandPutOverwrite = 4,
  exchangeStatusOk = 0,
  exchangeStatusInvalid = 1,
  exchangeStatusUnavailable = 2,
  exchangeStatusExists = 3,
  exchangeStatusIoError = 4,
  exchangeStatusBusy = 5,
  exchangeCapabilities = 0x07,
  maximumFilenameLength = 15,
  maximumMatches = 128,
  transferBufferSize = 256,
  progressInterval = 2048
};

#define exchangeData (*(volatile unsigned char *)exchangeDataRegisterAddress)
#define exchangeAbort (*(volatile unsigned char *)exchangeAbortRegisterAddress)

static uint8_t transferBuffer[transferBufferSize];
static char localFilenameList[maximumMatches][maximumFilenameLength + 1];

static uint32_t updateCrc32(uint32_t crc, uint8_t value);
static uint8_t readExchange(void);
static void writeExchange(uint8_t value);
static void abortExchange(bool *directoryActive);
static uint32_t readLittleEndian32(void);
static void writeLittleEndian32(uint32_t value);
static bool queryExchange(void);
static const char *statusText(uint8_t status);
static bool normalizeName(const char *source, char destination[maximumFilenameLength + 1], bool allowWildcards);
static bool hasWildcard(const char *pattern);
static bool wildcardMatch(const char *pattern, const char *filename);
static bool isReservedName(const char *filename);
static bool fileExists(const char *filename, bool *exists);
static bool readDirectoryName(char filename[maximumFilenameLength + 1]);
static bool beginDirectory(void);
static bool finishDirectory(void);
static int listExchangeFiles(void);
static bool collectLocalMatches(const char *pattern, char filenames[maximumMatches][maximumFilenameLength + 1],
                               size_t *count);
static void printProgress(uint32_t *nextProgress, uint32_t transferred, uint32_t total);
static bool equalsIgnoreCase(const char *left, const char *right);
static bool setDownloadedFileType(const char *filename);
static bool publishDownloadedFile(const char *filename, bool overwrite);
static bool getOneFile(const char *filename, bool overwrite, bool wildcardTransfer, bool *directoryActive);
static bool getFiles(const char *pattern, bool overwrite);
static bool putOneFile(const char *filename, bool overwrite);
static bool putFiles(const char *pattern, bool overwrite);
static int runCommand(int argc, char **argv);

static const uint32_t crcNibbleTable[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL, 0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL, 0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL};

//-- Two table lookups per byte; the bitwise loop was too slow on a 1 MHz 6502.
static uint32_t updateCrc32(uint32_t crc, uint8_t value)
{
  crc ^= value;
  crc = (crc >> 4) ^ crcNibbleTable[(uint8_t)crc & 0x0F];
  crc = (crc >> 4) ^ crcNibbleTable[(uint8_t)crc & 0x0F];
  return crc;
}

static uint8_t readExchange(void)
{
  return exchangeData;
}

static void writeExchange(uint8_t value)
{
  exchangeData = value;
}

static void abortExchange(bool *directoryActive)
{
  exchangeAbort = 0;
  if (directoryActive != NULL)
  {
    *directoryActive = false;
  }
}

static uint32_t readLittleEndian32(void)
{
  uint32_t value = 0;
  uint8_t index;
  for (index = 0; index < 4; ++index)
  {
    value |= (uint32_t)readExchange() << (index * 8);
  }
  return value;
}

static void writeLittleEndian32(uint32_t value)
{
  uint8_t index;
  for (index = 0; index < 4; ++index)
  {
    writeExchange((uint8_t)(value >> (index * 8)));
  }
}

static bool queryExchange(void)
{
  uint8_t status;
  uint8_t version;
  uint8_t capabilities;
  writeExchange(exchangeCommandQuery);
  status = readExchange();
  version = readExchange();
  capabilities = readExchange();
  if (status != exchangeStatusOk)
  {
    printf("HOST exchange unavailable: %s.\n", statusText(status));
    return false;
  }
  if (version != 1 || (capabilities & exchangeCapabilities) != exchangeCapabilities)
  {
    printf("Unsupported HOST exchange protocol %u (capabilities $%02X).\n", version, capabilities);
    return false;
  }
  return true;
}

static const char *statusText(uint8_t status)
{
  switch (status)
  {
  case exchangeStatusOk:
    return "success";
  case exchangeStatusInvalid:
    return "invalid name or request";
  case exchangeStatusUnavailable:
    return "file or exchange directory unavailable";
  case exchangeStatusExists:
    return "destination already exists";
  case exchangeStatusIoError:
    return "I/O or checksum error";
  case exchangeStatusBusy:
    return "exchange service busy";
  default:
    return "unknown exchange error";
  }
}

static bool normalizeName(const char *source, char destination[maximumFilenameLength + 1], bool allowWildcards)
{
  size_t length = strlen(source);
  size_t index;
  unsigned char character;
  bool valid;
  if (length == 0 || length > maximumFilenameLength)
  {
    return false;
  }
  for (index = 0; index < length; ++index)
  {
    character = (unsigned char)source[index];
    if (character >= 'a' && character <= 'z')
    {
      character = (unsigned char)(character - ('a' - 'A'));
    }
    valid = (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
            character == '.' || (allowWildcards && (character == '*' || character == '?'));
    if (!valid)
    {
      return false;
    }
    destination[index] = (char)character;
  }
  destination[length] = '\0';
  if (!((destination[0] >= 'A' && destination[0] <= 'Z') || (allowWildcards && destination[0] == '*') ||
        (allowWildcards && destination[0] == '?')))
  {
    return false;
  }
  return true;
}

static bool hasWildcard(const char *pattern)
{
  return strchr(pattern, '*') != NULL || strchr(pattern, '?') != NULL;
}

static bool equalsIgnoreCase(const char *left, const char *right)
{
  while (*left != '\0' && *right != '\0')
  {
    char leftCharacter = *left >= 'a' && *left <= 'z' ? (char)(*left - ('a' - 'A')) : *left;
    char rightCharacter = *right >= 'a' && *right <= 'z' ? (char)(*right - ('a' - 'A')) : *right;
    if (leftCharacter != rightCharacter)
    {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

static bool wildcardMatch(const char *pattern, const char *filename)
{
  const char *starPattern = NULL;
  const char *starFilename = NULL;
  if (strcmp(pattern, "*.*") == 0)
  {
    return true;
  }
  while (*filename != '\0')
  {
    if (*pattern == '?' || *pattern == *filename)
    {
      ++pattern;
      ++filename;
    }
    else if (*pattern == '*')
    {
      starPattern = ++pattern;
      starFilename = filename;
    }
    else if (starPattern != NULL)
    {
      pattern = starPattern;
      filename = ++starFilename;
    }
    else
    {
      return false;
    }
  }
  while (*pattern == '*')
  {
    ++pattern;
  }
  return *pattern == '\0';
}

static bool isReservedName(const char *filename)
{
  const char *extension = strrchr(filename, '.');
  return (extension != NULL && strcmp(extension, ".HST") == 0) || strcmp(filename, "HOST.TMP") == 0 ||
         strcmp(filename, "HOST.BAK") == 0;
}

static bool fileExists(const char *filename, bool *exists)
{
  DIR *directory;
  struct dirent *entry;
  directory = opendir(".");
  if (directory == NULL)
  {
    return false;
  }
  *exists = false;
  while ((entry = readdir(directory)) != NULL)
  {
    if (equalsIgnoreCase(entry->d_name, filename))
    {
      *exists = true;
      break;
    }
  }
  return closedir(directory) == 0;
}

static bool readDirectoryName(char filename[maximumFilenameLength + 1])
{
  size_t length = 0;
  bool overflow = false;
  for (;;)
  {
    uint8_t character = readExchange();
    if (character == 0)
    {
      filename[length] = '\0';
      return !overflow;
    }
    if (length < maximumFilenameLength)
    {
      filename[length++] = (char)character;
    }
    else
    {
      overflow = true;
    }
  }
}

static bool beginDirectory(void)
{
  uint8_t status;
  writeExchange(exchangeCommandDirectory);
  status = readExchange();
  if (status != exchangeStatusOk)
  {
    printf("HOST DIR: %s.\n", statusText(status));
    return false;
  }
  return true;
}

static bool finishDirectory(void)
{
  uint8_t status;
  status = readExchange();
  if (status != exchangeStatusOk)
  {
    printf("HOST DIR: %s.\n", statusText(status));
    return false;
  }
  return true;
}

static int listExchangeFiles(void)
{
  char filename[maximumFilenameLength + 1];
  bool valid = true;
  size_t count = 0;
  if (!beginDirectory())
  {
    return 1;
  }
  while (readDirectoryName(filename) && filename[0] != '\0')
  {
    puts(filename);
    ++count;
  }
  valid = finishDirectory();
  if (count == 0 && valid)
  {
    puts("No files.");
  }
  return valid ? 0 : 1;
}

static bool collectLocalMatches(const char *pattern, char filenames[maximumMatches][maximumFilenameLength + 1],
                                size_t *count)
{
  DIR *directory;
  struct dirent *entry;
  bool valid = true;
  char normalizedName[maximumFilenameLength + 1];
  directory = opendir(".");
  if (directory == NULL)
  {
    printf("HOST PUT: cannot read the current ProDOS directory.\n");
    return false;
  }
  *count = 0;
  while ((entry = readdir(directory)) != NULL)
  {
    if (_DE_ISDIR(entry->d_type) || isReservedName(entry->d_name))
    {
      continue;
    }
    if (!normalizeName(entry->d_name, normalizedName, false) || !wildcardMatch(pattern, normalizedName))
    {
      continue;
    }
    if (*count == maximumMatches)
    {
      printf("HOST PUT: wildcard match list is full; no files were transferred.\n");
      valid = false;
      break;
    }
    strcpy(filenames[*count], normalizedName);
    ++*count;
  }
  if (closedir(directory) != 0)
  {
    printf("HOST PUT: closing the current ProDOS directory failed.\n");
    valid = false;
  }
  return valid;
}

static void printProgress(uint32_t *nextProgress, uint32_t transferred, uint32_t total)
{
  if (transferred >= *nextProgress && transferred < total)
  {
    putchar('.');
    *nextProgress += progressInterval;
  }
}

typedef struct
{
  const char *extension;
  unsigned char fileType;
  unsigned int auxType;
} fileTypeRule;

//-- The exchange protocol carries no ProDOS metadata, so the type follows the filename extension.
static const fileTypeRule fileTypeRules[] = {
    {".TXT", PRODOS_T_TXT, 0},  {".MD", PRODOS_T_TXT, 0},  {".C", PRODOS_T_TXT, 0},   {".H", PRODOS_T_TXT, 0},
    {".S", PRODOS_T_TXT, 0},    {".ASM", PRODOS_T_TXT, 0}, {".INC", PRODOS_T_TXT, 0}, {".CFG", PRODOS_T_TXT, 0},
    {".LOG", PRODOS_T_TXT, 0},  {".CSV", PRODOS_T_TXT, 0}, {".PY", PRODOS_T_TXT, 0},  {".SH", PRODOS_T_TXT, 0},
    {".JSON", PRODOS_T_TXT, 0}, {".BAS", PRODOS_T_BAS, PRODOS_AUX_T_BAS_PGM_LOAD_ADDR},
    {".INT", PRODOS_T_INT, 0},  {".SYS", PRODOS_T_SYS, 0x2000}, {".REL", PRODOS_T_REL, 0},
    {".BIN", PRODOS_T_BIN, 0x0800}};

static bool setDownloadedFileType(const char *filename)
{
  const char *extension = strrchr(filename, '.');
  unsigned char index;
  _filetype = PRODOS_T_BIN;
  _auxtype = 0;
  if (extension == NULL)
  {
    return true;
  }
  for (index = 0; index < sizeof(fileTypeRules) / sizeof(fileTypeRules[0]); ++index)
  {
    if (strcmp(extension, fileTypeRules[index].extension) == 0)
    {
      _filetype = fileTypeRules[index].fileType;
      _auxtype = fileTypeRules[index].auxType;
      break;
    }
  }
  return true;
}

static bool publishDownloadedFile(const char *filename, bool overwrite)
{
  bool exists;
  bool backupExists;
  if (!fileExists(filename, &exists))
  {
    printf("GET %s: cannot inspect the existing destination.\n", filename);
    return false;
  }
  if (!exists)
  {
    if (rename("HOST.TMP", filename) != 0)
    {
      printf("GET %s: cannot install the verified file.\n", filename);
      return false;
    }
    return true;
  }
  if (!overwrite)
  {
    printf("GET %s: destination already exists.\n", filename);
    return false;
  }
  if (!fileExists("HOST.BAK", &backupExists) || backupExists)
  {
    printf("GET %s: HOST.BAK exists or cannot be checked; existing file preserved.\n", filename);
    return false;
  }
  if (rename(filename, "HOST.BAK") != 0)
  {
    printf("GET %s: cannot preserve the existing destination.\n", filename);
    return false;
  }
  if (rename("HOST.TMP", filename) != 0)
  {
    if (rename("HOST.BAK", filename) != 0)
    {
      printf("GET %s: replacement failed and HOST.BAK could not be restored.\n", filename);
      return false;
    }
    printf("GET %s: replacement failed; existing file restored.\n", filename);
    return false;
  }
  if (remove("HOST.BAK") != 0)
  {
    printf("GET %s: complete, but HOST.BAK could not be removed.\n", filename);
  }
  return true;
}

static bool getOneFile(const char *filename, bool overwrite, bool wildcardTransfer, bool *directoryActive)
{
  static bool exists;
  static size_t index;
  static uint8_t status;
  static uint32_t length;
  static FILE *file;
  static uint32_t crc;
  static uint32_t transferred;
  static uint32_t nextProgress;
  static bool localWriteOk;
  static uint32_t expectedCrc;
  static uint8_t finalStatus;
  if (!fileExists(filename, &exists))
  {
    printf("GET %s: cannot inspect the local destination.\n", filename);
    return false;
  }
  if (exists && !overwrite)
  {
    printf("GET %s: destination already exists.\n", filename);
    return false;
  }
  if (!fileExists("HOST.TMP", &exists) || exists)
  {
    printf("GET %s: HOST.TMP exists or cannot be checked.\n", filename);
    return false;
  }

  printf("GET %s: ", filename);
  writeExchange(exchangeCommandGet);
  for (index = 0; filename[index] != '\0'; ++index)
  {
    writeExchange((uint8_t)filename[index]);
  }
  writeExchange(0);
  status = readExchange();
  if (status != exchangeStatusOk)
  {
    printf("%s.\n", statusText(status));
    return false;
  }
  length = readLittleEndian32();
  if (!setDownloadedFileType(filename))
  {
    abortExchange(directoryActive);
    printf("unsupported ProDOS file type.\n");
    return false;
  }
  file = fopen("HOST.TMP", "wb");
  if (file == NULL)
  {
    abortExchange(directoryActive);
    printf("cannot create HOST.TMP (ProDOS error $%02X).\n", _oserror);
    return false;
  }
  crc = 0xFFFFFFFFUL;
  transferred = 0;
  nextProgress = progressInterval;
  localWriteOk = true;
  while (transferred < length)
  {
    size_t amount = length - transferred;
    if (amount > sizeof(transferBuffer))
    {
      amount = sizeof(transferBuffer);
    }
    for (index = 0; index < amount; ++index)
    {
      transferBuffer[index] = readExchange();
      crc = updateCrc32(crc, transferBuffer[index]);
    }
    if (fwrite(transferBuffer, 1, amount, file) != amount)
    {
      localWriteOk = false;
      break;
    }
    transferred += amount;
    printProgress(&nextProgress, transferred, length);
  }
  if (!localWriteOk)
  {
    fclose(file);
    abortExchange(directoryActive);
    remove("HOST.TMP");
    printf("local ProDOS write failed; temporary file removed.\n");
    return false;
  }
  expectedCrc = readLittleEndian32();
  finalStatus = readExchange();
  if (fclose(file) != 0)
  {
    abortExchange(directoryActive);
    remove("HOST.TMP");
    printf("local ProDOS close failed; temporary file removed.\n");
    return false;
  }
  if ((crc ^ 0xFFFFFFFFUL) != expectedCrc || finalStatus != exchangeStatusOk)
  {
    abortExchange(directoryActive);
    remove("HOST.TMP");
    if (finalStatus == exchangeStatusOk)
    {
      printf("checksum mismatch; temporary file removed.\n");
    }
    else
    {
      printf("%s; temporary file removed.\n", statusText(finalStatus));
    }
    return false;
  }
  if (!publishDownloadedFile(filename, overwrite))
  {
    remove("HOST.TMP");
    if (!wildcardTransfer)
    {
      printf("downloaded data was verified but could not be installed.\n");
    }
    return false;
  }
  puts("GET complete.");
  return true;
}

static bool getFiles(const char *pattern, bool overwrite)
{
  char filename[maximumFilenameLength + 1];
  size_t matches = 0;
  bool success = true;
  if (!hasWildcard(pattern))
  {
    return getOneFile(pattern, overwrite, false, NULL);
  }
  if (!beginDirectory())
  {
    return false;
  }
  {
    bool directoryActive = true;
    while (directoryActive && readDirectoryName(filename) && filename[0] != '\0')
    {
      if (!isReservedName(filename) && wildcardMatch(pattern, filename))
      {
        ++matches;
        if (!getOneFile(filename, overwrite, true, &directoryActive))
        {
          success = false;
        }
      }
    }
    if (directoryActive && !finishDirectory())
    {
      success = false;
    }
  }
  if (matches == 0 && success)
  {
    puts("No matching files.");
  }
  return success;
}

static bool putOneFile(const char *filename, bool overwrite)
{
  static size_t index;
  static size_t amount;
  static uint32_t length;
  static FILE *file;
  static uint32_t crc;
  static uint8_t status;
  static uint32_t transferred;
  static uint32_t nextProgress;
  file = fopen(filename, "rb");
  if (file == NULL)
  {
    printf("PUT %s: local file could not be opened.\n", filename);
    return false;
  }
  length = 0;
  crc = 0xFFFFFFFFUL;
  printf("PUT %s: ", filename);
  while ((amount = fread(transferBuffer, 1, sizeof(transferBuffer), file)) != 0)
  {
    if (length + amount < length)
    {
      fclose(file);
      printf("file is too large for the exchange protocol.\n");
      return false;
    }
    for (index = 0; index < amount; ++index)
    {
      crc = updateCrc32(crc, transferBuffer[index]);
    }
    length += amount;
    if ((length & 0x7FF) == 0)
    {
      putchar('.');
    }
  }
  if (ferror(file))
  {
    fclose(file);
    printf("local file read failed.\n");
    return false;
  }
  if (fseek(file, 0, SEEK_SET) != 0)
  {
    fclose(file);
    printf("local file rewind failed.\n");
    return false;
  }

  writeExchange(overwrite ? exchangeCommandPutOverwrite : exchangeCommandPut);
  for (index = 0; filename[index] != '\0'; ++index)
  {
    writeExchange((uint8_t)filename[index]);
  }
  writeExchange(0);
  writeLittleEndian32(length);
  writeLittleEndian32(crc ^ 0xFFFFFFFFUL);
  status = readExchange();
  if (status != exchangeStatusOk)
  {
    fclose(file);
    printf("%s.\n", statusText(status));
    return false;
  }
  transferred = 0;
  nextProgress = progressInterval;
  while (transferred < length)
  {
    amount = length - transferred;
    if (amount > sizeof(transferBuffer))
    {
      amount = sizeof(transferBuffer);
    }
    if (fread(transferBuffer, 1, amount, file) != amount)
    {
      fclose(file);
      abortExchange(NULL);
      printf("local ProDOS read failed; transfer aborted.\n");
      return false;
    }
    for (index = 0; index < amount; ++index)
    {
      writeExchange(transferBuffer[index]);
    }
    transferred += amount;
    printProgress(&nextProgress, transferred, length);
  }
  if (fclose(file) != 0)
  {
    abortExchange(NULL);
    printf("local ProDOS close failed; transfer aborted.\n");
    return false;
  }
  status = readExchange();
  if (status != exchangeStatusOk)
  {
    printf("%s.\n", statusText(status));
    return false;
  }
  puts("PUT complete.");
  return true;
}

static bool putFiles(const char *pattern, bool overwrite)
{
  size_t count = 0;
  size_t index;
  bool success = true;
  if (!collectLocalMatches(pattern, localFilenameList, &count))
  {
    return false;
  }
  if (count == 0)
  {
    puts("No matching files.");
    return true;
  }
  for (index = 0; index < count; ++index)
  {
    if (!putOneFile(localFilenameList[index], overwrite))
    {
      success = false;
    }
  }
  return success;
}

static int runCommand(int argc, char **argv)
{
  bool overwrite;
  char pattern[maximumFilenameLength + 1];
  if (argc == 2 && equalsIgnoreCase(argv[1], "DIR"))
  {
    return listExchangeFiles();
  }
  if ((argc == 3 || argc == 4) && (equalsIgnoreCase(argv[1], "GET") || equalsIgnoreCase(argv[1], "PUT")))
  {
    overwrite = argc == 4 && equalsIgnoreCase(argv[3], "O");
    if (argc == 4 && !overwrite)
    {
      puts("Usage: DIR | GET name [O] | PUT name [O] | QUIT");
      return 1;
    }
    if (!normalizeName(argv[2], pattern, true) || isReservedName(pattern))
    {
      puts("Invalid or reserved ProDOS filename.");
      return 1;
    }
    if (equalsIgnoreCase(argv[1], "GET"))
    {
      return getFiles(pattern, overwrite) ? 0 : 1;
    }
    if (equalsIgnoreCase(argv[1], "PUT"))
    {
      return putFiles(pattern, overwrite) ? 0 : 1;
    }
  }
  puts("Usage: DIR | GET name [O] | PUT name [O] | QUIT");
  return 1;
}

static int readCommandLine(char **argv)
{
  static char line[48];
  unsigned char length = 0;
  int argc = 1;
  char key;
  char *cursor;

  argv[0] = "HOST";
  hostWrite("HOST> ");
  for (;;)
  {
    key = hostGetKey();
    if (key == '\r' || key == '\n')
    {
      break;
    }
    if ((key == 0x08 || key == 0x7F) && length > 0)
    {
      length--;
      hostWrite("\b \b");
    }
    else if (key >= ' ' && key < 0x7F && length < sizeof(line) - 1)
    {
      line[length++] = key;
      hostPutchar(key);
    }
  }
  line[length] = '\0';
  hostPutchar('\n');
  cursor = line;
  while (argc < 4)
  {
    while (*cursor == ' ')
    {
      cursor++;
    }
    if (*cursor == '\0')
    {
      break;
    }
    argv[argc++] = cursor;
    while (*cursor != ' ' && *cursor != '\0')
    {
      cursor++;
    }
    if (*cursor == ' ')
    {
      *cursor++ = '\0';
    }
  }
  return argc;
}

extern void hostReturnToBasic(void);

static void waitForKey(void)
{
  hostWrite("Press a key.");
  hostGetKey();
  hostPutchar('\n');
}

int main(int argc, char **argv)
{
  static char *promptArgv[4];
  int result;

  if (argc >= 2)
  {
    if (!queryExchange())
    {
      return 1;
    }
    return runCommand(argc, argv);
  }
  if (!queryExchange())
  {
    waitForKey();
    return 1;
  }
  //-- Returning to ProDOS clears the screen, so stay at the prompt until an empty line or QUIT.
  for (;;)
  {
    argc = readCommandLine(promptArgv);
    if (argc < 2 || equalsIgnoreCase(promptArgv[1], "QUIT") || equalsIgnoreCase(promptArgv[1], "Q"))
    {
      break;
    }
    result = runCommand(argc, promptArgv);
    (void)result;
  }
  hostReturnToBasic();
  return 0;
}
