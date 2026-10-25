#pragma once
#include "guestFs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//-- Conversions between host (PC) files and native guest formats:
//-- guestFsConvertPascalText: host text <-> UCSD Pascal .TEXT (1 KiB header, 1 KiB pages, CR line
//-- ends, DLE indentation, NUL padding).
//-- guestFsConvertApplesoft: host numbered listing <-> tokenized Applesoft program.
typedef enum
{
  guestFsConvertNone = 0,
  guestFsConvertPascalText,
  guestFsConvertApplesoft
} guestFsConvertKind;

enum
{
  guestFsConvertTextPageSize = 1024,
  //-- Whole-buffer conversions refuse larger sources.
  guestFsConvertMaxSource = 1024 * 1024,
  guestFsConvertMaxBasic = 65536
};

typedef bool (*guestFsSinkFn)(void* context, const void* data, size_t length);

//-- Allocation that prefers external RAM on the target; release with free().
void* guestFsConvertAlloc(size_t size);

//-- Host text to UCSD .TEXT. *out is allocated with guestFsConvertAlloc.
guestFsStatus guestFsTextEncode(const uint8_t* input, size_t length, uint8_t** out,
                                size_t* outLength, char* detail, size_t detailCapacity);

//-- Streaming UCSD .TEXT to host text (LF line ends).
typedef struct
{
  uint32_t position;
  bool dlePending;
} guestFsTextDecoder;

void guestFsTextDecoderInit(guestFsTextDecoder* decoder);
bool guestFsTextDecode(guestFsTextDecoder* decoder, const uint8_t* data, size_t length,
                       guestFsSinkFn sink, void* context);

//-- True when the bytes look like a numbered ASCII Applesoft listing.
bool guestFsBasicLooksLikeSource(const uint8_t* data, size_t length);
guestFsStatus guestFsBasicTokenize(const uint8_t* input, size_t length, uint8_t** out,
                                   size_t* outLength, char* detail, size_t detailCapacity);
guestFsStatus guestFsBasicDetokenize(const uint8_t* program, size_t length, guestFsSinkFn sink,
                                     void* context, char* detail, size_t detailCapacity);

//-- Which conversion applies to a file moving between host and guest. attributes may be NULL (the
//-- file name suffix .TEXT / .BAS then decides); volume selects the file system rule.
guestFsConvertKind guestFsConvertKindFor(guestFsVolume* volume, const char* name,
                                         const guestFsAttributes* attributes);

//-- Stream a guest file to the host through sink, converting when convert is true.
guestFsStatus guestFsExportStream(guestFsVolume* volume, uint8_t user, const char* name,
                                  bool convert, guestFsSinkFn sink, void* sinkContext,
                                  guestFsPollFn poll, void* pollContext, char* detail,
                                  size_t detailCapacity);

//-- Store length host bytes delivered by source, converting when convert is true. Converted files
//-- are buffered in memory because their stored length differs from the host length.
guestFsStatus guestFsImportStream(guestFsVolume* volume, uint8_t user, const char* name,
                                  uint32_t length, guestFsSourceFn source, void* sourceContext,
                                  bool overwrite, const guestFsAttributes* attributes, bool convert,
                                  char* detail, size_t detailCapacity);

guestFsStatus guestFsExportToFileEx(guestFsVolume* volume, uint8_t user, const char* name,
                                    FILE* output, bool convert, guestFsPollFn poll,
                                    void* pollContext, char* detail, size_t detailCapacity);
guestFsStatus guestFsImportFromFileEx(guestFsVolume* volume, uint8_t user, const char* name,
                                      FILE* input, uint32_t length, bool overwrite, bool convert,
                                      guestFsPollFn poll, void* pollContext, char* detail,
                                      size_t detailCapacity);
