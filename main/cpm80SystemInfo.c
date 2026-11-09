/*
 * Copyright (C) 2026 Willem Aandewiel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "cpm80SystemInfo.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void append(cpm80InfoStream* stream, const char* format, ...)
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

// Host paths/version text never inject terminal controls; wrap long image paths.
static void safeText(cpm80InfoStream* stream, const char* text, size_t column)
{
  if (text == NULL)
    text = "(unknown)";
  for (size_t i = 0; text[i] != '\0' && i < 512; ++i)
  {
    if (column >= 74)
    {
      append(stream, "\r\n    ");
      column = 4;
    }
    unsigned char ch = (unsigned char)text[i];
    append(stream, "%c", ch >= 32 && ch <= 126 ? ch : '?');
    ++column;
  }
  append(stream, "\r\n");
}

void cpm80InfoBuild(cpm80InfoStream* stream, const cpm80InfoValues* values)
{
  memset(stream, 0, sizeof(*stream));
  append(stream, "RETRO80-SYSINFO/1\r\nFirmware:           ");
  safeText(stream, values->version, 20);
  append(stream, "CPU core:           z80 (Z80 instruction emulator, CP/M-80 guest)\r\n");
  append(stream, "Guest RAM:          %lu KiB (%lu bytes)\r\n",
         (unsigned long)(values->guestRamBytes / 1024), (unsigned long)values->guestRamBytes);
  append(stream, "Host PSRAM:         %lu KiB (installed; not free guest RAM)\r\n",
         (unsigned long)(values->psramBytes / 1024));
  append(stream, "Host uptime:        %llu seconds since boot\r\n",
         (unsigned long long)(values->uptimeMs / 1000));
  append(stream, "Guest Z80 T-states: %llu (since the last cold boot)\r\n",
         (unsigned long long)values->tstates);
  append(stream, "Console:            RETRO80 console; ANSI terminal\r\n");
  append(stream, "LIST / PUNCH:       Discarded by the virtual BIOS (no physical device)\r\n");
  append(stream, "READER:             Reports end-of-file (no physical reader)\r\n\r\n");
  append(stream, "Mounted virtual disks (not physical floppy drives):\r\n");
  unsigned mounted = 0;
  for (unsigned i = 0; i < cpm80InfoDriveCount; ++i)
  {
    const cpm80InfoDrive* drive = &values->drives[i];
    if (!drive->mounted)
    {
      append(stream, "  %c: not mounted\r\n", 'A' + i);
      continue;
    }
    ++mounted;
    append(stream, "  %c: %llu bytes (%llu KiB), %s\r\n", 'A' + i, (unsigned long long)drive->bytes,
           (unsigned long long)(drive->bytes / 1024), drive->readOnly ? "read-only" : "read/write");
    append(stream, "    ");
    safeText(stream, drive->path, 4);
  }
  append(stream, "\r\nMounted: %u of %u virtual slots.\r\n", mounted, cpm80InfoDriveCount);
}

uint8_t cpm80InfoRead(cpm80InfoStream* stream)
{
  if (stream->position >= stream->length)
    return 0;
  return (uint8_t)stream->text[stream->position++];
}
