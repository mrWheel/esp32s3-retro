#include "apple2SystemInfo.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void append(apple2InfoStream* stream, const char* format, ...)
{
  if (stream->length >= sizeof(stream->text) - 1)
    return;
  va_list args;
  va_start(args, format);
  size_t remaining = sizeof(stream->text) - stream->length;
  int count = vsnprintf(stream->text + stream->length, remaining, format, args);
  va_end(args);
  if (count > 0)
    stream->length += (size_t)count < remaining ? (size_t)count : remaining - 1;
}

//-- Host paths/version text never inject terminal controls. Long values wrap on word boundaries
//-- so the 80 column guest screen never splits a word and never reaches the right screen margin.
static void safeText(apple2InfoStream* stream, const char* text, size_t column)
{
  if (text == NULL)
    text = "(unknown)";
  size_t indent = column;
  size_t i = 0;
  while (text[i] != '\0' && i < 512)
  {
    if (text[i] == ' ')
    {
      ++i;
      if (column < 74)
      {
        append(stream, " ");
        ++column;
      }
      continue;
    }
    size_t wordEnd = i;
    while (wordEnd < 512 && text[wordEnd] != '\0' && text[wordEnd] != ' ')
      ++wordEnd;
    //-- Wrapping continues the value on its own row, aligned under the first value column,
    //-- so a long value reads as two deliberate rows instead of a terminal soft wrap.
    if (column + (wordEnd - i) > 74 && column > indent)
    {
      append(stream, "\r\n%*s", (int)indent, "");
      column = indent;
    }
    for (; i < wordEnd; ++i)
    {
      unsigned char ch = (unsigned char)text[i];
      append(stream, "%c", ch >= 32 && ch <= 126 ? ch : '?');
      ++column;
    }
  }
  append(stream, "\r\n");
}

void apple2InfoBuild(apple2InfoStream* stream, const apple2InfoValues* values)
{
  memset(stream, 0, sizeof(*stream));
  append(stream, "RETROAPPLE-SYSINFO/1\r\nFirmware:           ");
  safeText(stream, values->version, 20);
  append(stream, "Machine profile:    ");
  safeText(stream, values->profile, 20);
  append(stream, "CPU core:           NMOS 6502 instruction emulator (shared Apple II core)\r\n");
  append(stream, "Guest RAM:          ");
  safeText(stream, values->ramText, 20);
  append(stream, "Host PSRAM:         %lu KiB (installed; not free guest RAM)\r\n",
         (unsigned long)(values->psramBytes / 1024));
  append(stream, "Host uptime:        %llu seconds since boot\r\n",
         (unsigned long long)(values->uptimeMs / 1000));
  append(stream, "Guest 6502 cycles:  %llu (emulated since machine start)\r\n",
         (unsigned long long)values->cycles);
  append(stream, "Console:            ");
  safeText(stream, values->consoleText, 20);
  append(stream, "Keyboard:           USB console key strokes (no physical keyboard matrix)\r\n");
  append(stream, "Disk activity:      RGB LED on the ESP32-S3 board\r\n\r\n");
  append(stream, "Mounted virtual disks (not physical floppy drives):\r\n");
  unsigned mounted = 0;
  for (unsigned i = 0; i < apple2InfoVolumeCapacity; ++i)
  {
    const apple2InfoVolume* volume = &values->volumes[i];
    if (!volume->mounted)
      continue;
    ++mounted;
    append(stream, "  %s%u.%u:", volume->smartPort ? "SP" : "SD", (unsigned)volume->slot,
           (unsigned)volume->unit);
    if (volume->volumeName != NULL && volume->volumeName[0] != '\0')
      append(stream, " volume %s:", volume->volumeName);
    append(stream, " %llu bytes (%llu KiB), %s\r\n", (unsigned long long)volume->bytes,
           (unsigned long long)(volume->bytes / 1024),
           volume->readOnly ? "read-only" : "read/write");
    append(stream, "    ");
    safeText(stream, volume->path, 4);
  }
  append(stream, "\r\nMounted: %u of %u virtual volume slots.\r\n", mounted,
         (unsigned)apple2InfoVolumeCapacity);
}

uint8_t apple2InfoRead(apple2InfoStream* stream)
{
  if (stream->position >= stream->length)
    return 0;
  return (uint8_t)stream->text[stream->position++];
}

//-- One Apple II machine instance exists at a time, so a single file-scope device context is
//-- sufficient; the attach call refreshes all three pointers before the core runs.
static apple2InfoStream* attachedStream;
static apple2InfoCollect attachedCollect;
static void* attachedContext;

static uint8_t infoReadRegister(void* context, uint8_t registerIndex)
{
  (void)context;
  //-- The status register always answers with the magic so the guest can probe support before
  //-- writing anything; the data register drains the last built snapshot.
  if (registerIndex == apple2InfoStatusRegister)
  {
    return apple2InfoMagic;
  }
  if (registerIndex == apple2InfoDataRegister)
  {
    return apple2InfoRead(attachedStream);
  }
  return 0x00;
}

static void infoWriteRegister(void* context, uint8_t registerIndex, uint8_t value)
{
  (void)context;
  //-- Writing the snapshot value to the status register rebuilds the stream drained by reads of
  //-- the data register; other writes (and values) are ignored.
  if (registerIndex == apple2InfoStatusRegister && value == apple2InfoSnapshot)
  {
    apple2InfoValues values;
    memset(&values, 0, sizeof(values));
    if (attachedCollect != NULL)
    {
      attachedCollect(attachedContext, &values);
    }
    apple2InfoBuild(attachedStream, &values);
  }
}

apple2CoreResult apple2InfoAttach(apple2Core* core, apple2InfoStream* stream,
                                  apple2InfoCollect collect, void* context)
{
  apple2CoreResult result =
      apple2CoreAttachSlotIoDevice(core, apple2InfoSlot, infoReadRegister, infoWriteRegister, NULL);
  if (result == apple2CoreOk)
  {
    memset(stream, 0, sizeof(*stream));
    attachedStream = stream;
    attachedCollect = collect;
    attachedContext = context;
  }
  return result;
}
