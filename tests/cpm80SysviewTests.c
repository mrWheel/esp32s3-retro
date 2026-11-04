//-- Host test for the CP/M-80 SYSVIEW: runs the built SYSVIEW.COM in the real cpm80Guest
//-- core with a scripted console (BDOS at address 5), simulated RETRO80-SYSINFO/1 ports
//-- (F8h/F9h) and scripted keys, plus direct checks of the snapshot stream builder.
#include "cpm80Guest.h"
#include "cpm80SystemInfo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  cpm80InfoStream stream;
  cpm80InfoValues values;
  unsigned snapshots, keys;
  bool supported, badHeader;
  char output[40000];
  size_t used;
  const char* input;
} fixture;

static bool consoleAvailable(void* context)
{
  (void)context;
  return false;
}

static int consoleRead(void* context)
{
  (void)context;
  return -1;
}

static void consoleWrite(void* context, uint8_t character)
{
  (void)context;
  (void)character;
}

static bool driveAvailable(void* context, uint8_t drive)
{
  (void)context;
  return drive < cpm80DiskDriveCount;
}

static bool driveProfile(void* context, uint8_t drive, cpm80DiskProfile* profile)
{
  (void)context;
  (void)drive;
  *profile = cpm80DiskProfileSystem;
  return true;
}

static bool diskRead(void* context, uint8_t drive, uint16_t track, uint16_t sector,
                     uint8_t record[cpm80DiskSectorSize])
{
  (void)context;
  (void)drive;
  (void)track;
  (void)sector;
  (void)record;
  return false;
}

static bool diskWrite(void* context, uint8_t drive, uint16_t track, uint16_t sector,
                      const uint8_t record[cpm80DiskSectorSize])
{
  (void)context;
  (void)drive;
  (void)track;
  (void)sector;
  (void)record;
  return false;
}

static void guestYield(void* context)
{
  (void)context;
}

//-- RETRO80-SYSINFO/1 status and data ports; unknown ports must never be touched.
static uint8_t portInput(void* context, uint8_t port)
{
  fixture* f = context;
  if (port == cpm80InfoStatusPort)
  {
    return f->supported ? cpm80InfoMagic : 0xFF;
  }
  if (port == cpm80InfoDataPort)
  {
    return cpm80InfoRead(&f->stream);
  }
  assert(!"unexpected port read");
  return 0xFF;
}

static void portOutput(void* context, uint8_t port, uint8_t value)
{
  fixture* f = context;
  assert(port == cpm80InfoStatusPort && value == cpm80InfoSnapshot);
  ++f->snapshots;
  cpm80InfoBuild(&f->stream, &f->values);
  if (f->badHeader)
  {
    f->stream.text[0] = 'X';
  }
}

//-- Minimal CP/M 2.2 BDOS at address 5: function 6 (output E / status with E=FF feeds the
//-- scripted keys), function 12 (version in HL), function 25 (current drive in A) and
//-- function 32 (user area in A).
static void serviceBdos(cpm80Guest* guest, fixture* f, uint8_t currentDrive)
{
  z80* cpu = &guest->cpu.processor;
  switch (cpu->c)
  {
  case 6:
    if (cpu->e == 0xFF)
    {
      assert(f->keys < strlen(f->input));
      cpu->a = (uint8_t)f->input[f->keys++];
    }
    else
    {
      assert(f->used < sizeof(f->output) - 1);
      f->output[f->used++] = (char)cpu->e;
      f->output[f->used] = '\0';
    }
    break;
  case 12:
    cpu->h = 0x11;
    cpu->l = 0x22;
    break;
  case 25:
    cpu->a = currentDrive;
    break;
  case 32:
    assert(cpu->e == 0xFF);
    cpu->a = 3;
    break;
  default:
    assert(!"unexpected BDOS call");
  }
  uint8_t low, high;
  assert(cpm80CpuReadMemory(&guest->cpu, cpu->sp, &low));
  assert(cpm80CpuReadMemory(&guest->cpu, (uint16_t)(cpu->sp + 1), &high));
  cpu->pc = (uint16_t)((high << 8) | low);
  cpu->sp += 2;
}

static void run(const char* filename, bool supported, bool badHeader, const char* keys,
                unsigned snapshots, uint8_t currentDrive)
{
  static uint8_t program[8192];
  static uint8_t ccp[cpm80CcpSize];
  static uint8_t bdos[cpm80BdosSize];
  fixture* f = calloc(1, sizeof(*f));
  assert(f != NULL);
  f->supported = supported;
  f->badHeader = badHeader;
  f->input = keys;
  f->values.version = "v-test";
  f->values.guestRamBytes = cpm80MemorySize;
  f->values.psramBytes = 8u * 1024u * 1024u;
  f->values.uptimeMs = 123456;
  f->values.tstates = 9876543210ULL;
  for (unsigned i = 0; i < cpm80InfoDriveCount; ++i)
  {
    f->values.drives[i].mounted = i != 5;
    f->values.drives[i].readOnly = i == 1;
    f->values.drives[i].bytes = i != 5 ? 8392704 : 0;
    f->values.drives[i].path =
        i != 5 ? "/sdcard/retro/images/cpm80/disk image with spaces.dsk" : NULL;
  }
  cpm80HostOps host = {.consoleAvailable = consoleAvailable,
                       .consoleRead = consoleRead,
                       .consoleWrite = consoleWrite,
                       .diskDriveAvailable = driveAvailable,
                       .diskDriveProfile = driveProfile,
                       .diskReadRecord = diskRead,
                       .diskWriteRecord = diskWrite,
                       .yield = guestYield,
                       .context = f,
                       .portInput = portInput,
                       .portOutput = portOutput};
  cpm80Guest guest;
  memset(&guest, 0, sizeof(guest));
  assert(cpm80GuestInitialize(&guest, &host, ccp, bdos) == cpm80GuestInitOk);
  FILE* file = fopen(filename, "rb");
  assert(file != NULL);
  size_t length = fread(program, 1, sizeof(program), file);
  assert(length > 0 && length < sizeof(program) && feof(file));
  fclose(file);
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, program, length));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0xC000;
  for (unsigned long steps = 0; steps < 20000000UL && guest.cpu.processor.pc != 0; ++steps)
  {
    if (guest.cpu.processor.pc == 5)
    {
      serviceBdos(&guest, f, currentDrive);
      continue;
    }
    assert(cpm80GuestStep(&guest));
  }
  //-- Transient exit is ld c,0 / jp 0; both the exit address and the function must match.
  assert(guest.cpu.processor.pc == 0);
  assert(guest.cpu.processor.c == 0);
  assert(f->snapshots == snapshots);
  assert(f->keys == strlen(f->input));
  assert(strstr(f->output, "SYSVIEW 1.0 - RETRO80 emulator information") != NULL);
  assert(strstr(f->output, "\033[?25h") != NULL);
  if (!supported)
  {
    assert(strstr(f->output, "interface is not available") != NULL);
    assert(strstr(f->output, "CP/M BDOS version") == NULL);
  }
  else if (badHeader)
  {
    assert(strstr(f->output, "Invalid or unsupported") != NULL);
    assert(strstr(f->output, "Firmware:") == NULL);
  }
  else
  {
    assert(strstr(f->output, "CP/M BDOS version:  1122h") != NULL);
    assert(strstr(f->output, "Current CP/M drive: ") != NULL);
    assert((strstr(f->output, "Current CP/M drive: ") + 20)[0] == 'A' + currentDrive);
    assert(strstr(f->output, "User area: 3") != NULL);
    assert(strstr(f->output, "v-test") != NULL);
    assert(strstr(f->output, "64 KiB (65536 bytes)") != NULL);
    assert(strstr(f->output, "8192 KiB") != NULL);
    assert(strstr(f->output, "123 seconds since boot") != NULL);
    assert(strstr(f->output, "9876543210") != NULL);
    assert(strstr(f->output, "read-only") != NULL);
    assert(strstr(f->output, "disk image with spaces.dsk") != NULL);
    assert(strstr(f->output, "  F: not mounted") != NULL);
    assert(strstr(f->output, "Mounted: 5 of 6") != NULL);
    assert(strstr(f->output, "Type any key to continue") != NULL);
    assert(strstr(f->output, "\033[2K") != NULL);
    assert(strstr(f->output, "\033[A") != NULL);
  }
  printf("PASS: supported=%u badHeader=%u snapshots=%u keys=\"%s\", %zu output bytes\n", supported,
         badHeader, snapshots, keys, f->used);
  cpm80GuestDestroy(&guest);
  free(f);
}

int main(int argc, char** argv)
{
  assert(argc == 2);
  //-- First draw pauses at the continue prompt (any key), r rebuilds (second snapshot),
  //-- q exits from the second continue prompt, so two snapshots and three keys are used.
  run(argv[1], true, false, " rq", 2, 1);
  run(argv[1], false, false, "q", 0, 0);
  run(argv[1], true, true, "q", 1, 0);

  cpm80InfoStream stream;
  cpm80InfoValues values;
  memset(&values, 0, sizeof(values));
  values.version = "bad\033[2J\nname";
  cpm80InfoBuild(&stream, &values);
  assert(strchr(stream.text, 27) == NULL);
  for (size_t i = 0; i < stream.length; ++i)
  {
    assert(cpm80InfoRead(&stream) != 0);
  }
  assert(cpm80InfoRead(&stream) == 0 && cpm80InfoRead(&stream) == 0);
  puts("PASS: snapshot sanitization and end-of-stream");
  return 0;
}
