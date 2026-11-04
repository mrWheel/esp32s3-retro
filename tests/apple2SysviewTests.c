//-- Host test for the Apple II / UCSD Pascal SYSVIEW host interface: builds the
//-- RETROAPPLE-SYSINFO/1 stream with fixture values, drives the slot-1 registers through the real
//-- apple2Core bus (probe, arm, drain, re-arm) on the plain and IIe address paths, and executes a
//-- small 6502 stub that arms the snapshot and drains it into page $20 memory.
#include "../components/apple2Core/apple2Core.c"
#include "apple2SystemInfo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static apple2InfoStream stream;
static char collected[4200];

//-- A fixed snapshot so the expected stream is fully known; the collector is the guest-visible
//-- path taken when the guest writes 1 to the status register.
static void fixtureCollect(void* context, apple2InfoValues* values)
{
  unsigned* calls = (unsigned*)context;
  ++*calls;
  memset(values, 0, sizeof(*values));
  values->version = "v1.2.3";
  values->profile = "UCSD Pascal profile (menu 3), Apple IIe model";
  values->ramText = "128 KiB (64 KiB main + 64 KiB auxiliary)";
  values->consoleText = "IIe 80-column text page; rendered to the ANSI/VT100 host console";
  values->psramBytes = 8U * 1024U * 1024U;
  values->uptimeMs = 65432ULL;
  values->cycles = 123456789ULL;
  values->volumes[0].mounted = true;
  values->volumes[0].readOnly = false;
  values->volumes[0].smartPort = true;
  values->volumes[0].slot = 5;
  values->volumes[0].unit = 1;
  values->volumes[0].bytes = 655360;
  values->volumes[0].path = "/retro/images/ucsd/pascalWork1.po";
  values->volumes[0].volumeName = "WORK1";
  values->volumes[1].mounted = true;
  values->volumes[1].readOnly = true;
  values->volumes[1].smartPort = false;
  values->volumes[1].slot = 6;
  values->volumes[1].unit = 1;
  values->volumes[1].bytes = 819200;
  values->volumes[1].path = "/littlefs/ucsd/system.po";
  values->volumes[1].volumeName = "PASCAL";
}

static void expectContains(const char* label, const char* needle)
{
  if (strstr(stream.text, needle) == NULL)
  {
    fprintf(stderr, "FAIL %s: missing \"%s\"\n%s\n", label, needle, stream.text);
    abort();
  }
}

static void testBuilder(void)
{
  apple2InfoValues values;
  memset(&values, 0, sizeof(values));
  values.version = "v1.2.3";
  values.profile = "Apple II profile (menu 4), Apple II Plus model";
  values.ramText = "64 KiB (48 KiB motherboard + 16 KiB Language Card)";
  values.consoleText = "40x24 motherboard text plus slot-3 80-column card";
  values.psramBytes = 8U * 1024U * 1024U;
  values.uptimeMs = 5000ULL;
  values.cycles = 42ULL;
  values.volumes[0].mounted = true;
  values.volumes[0].smartPort = true;
  values.volumes[0].slot = 4;
  values.volumes[0].unit = 2;
  values.volumes[0].bytes = 655360;
  values.volumes[0].path = "/retro/images/ucsd/bad\007name.po";
  apple2InfoBuild(&stream, &values);
  expectContains("builder", "RETROAPPLE-SYSINFO/1\r\n");
  expectContains("builder", "Firmware:           v1.2.3");
  expectContains("builder", "Machine profile:    Apple II profile");
  expectContains("builder", "CPU core:           NMOS 6502");
  expectContains("builder", "Guest RAM:          64 KiB");
  expectContains("builder", "Host PSRAM:         8192 KiB");
  expectContains("builder", "Host uptime:        5 seconds");
  expectContains("builder", "Guest 6502 cycles:  42");
  expectContains("builder", "  SP4.2: 655360 bytes (640 KiB), read/write");
  //-- Control characters in host paths are shown as '?' instead of reaching the terminal.
  expectContains("builder", "/retro/images/ucsd/bad?name.po");
  if (strstr(stream.text, "\r\n  SD") != NULL || strstr(stream.text, "SP4.1:") != NULL)
  {
    fprintf(stderr, "FAIL builder: unmounted volume was listed\n");
    abort();
  }
  expectContains("builder", "Mounted: 1 of 16 virtual volume slots.");
  //-- The drain position advances only through apple2InfoRead and zero-pads at the end.
  size_t consumed = 0;
  while (stream.text[consumed] != '\0')
  {
    uint8_t byte = apple2InfoRead(&stream);
    if (byte != (uint8_t)stream.text[consumed])
    {
      fprintf(stderr, "FAIL builder: stream byte %zu mismatch\n", consumed);
      abort();
    }
    ++consumed;
  }
  for (int i = 0; i < 4; ++i)
  {
    if (apple2InfoRead(&stream) != 0)
    {
      fprintf(stderr, "FAIL builder: exhausted stream must read zero\n");
      abort();
    }
  }
  printf("PASS apple2 sysinfo stream builder\n");
}

static uint8_t readRegister(apple2Core* core, uint16_t address)
{
  uint8_t value = 0xFF;
  assert(apple2CoreReadMemory(core, address, &value) == apple2CoreOk);
  return value;
}

static void armSnapshot(apple2Core* core)
{
  assert(apple2CoreWriteMemory(core, 0xC090, apple2InfoSnapshot) == apple2CoreOk);
}

//-- The protocol as the SYSVIEW Pascal program uses it: probe the status register, ignore foreign
//-- writes, arm a snapshot, drain to the terminator, drain zeros, and arm a fresh snapshot.
static void testProtocol(apple2Core* core, const char* label)
{
  unsigned calls = 0;
  assert(apple2InfoAttach(core, &stream, fixtureCollect, &calls) == apple2CoreOk);
  if (readRegister(core, 0xC090) != apple2InfoMagic)
  {
    fprintf(stderr, "FAIL %s: status register does not answer with the magic\n", label);
    abort();
  }
  if (readRegister(core, 0xC091) != 0 || readRegister(core, 0xC092) != 0)
  {
    fprintf(stderr, "FAIL %s: data register must read zero before the first snapshot\n", label);
    abort();
  }
  assert(apple2CoreWriteMemory(core, 0xC090, 0x00) == apple2CoreOk);
  if (calls != 0 || readRegister(core, 0xC091) != 0)
  {
    fprintf(stderr, "FAIL %s: writing 0 must not build a snapshot\n", label);
    abort();
  }
  armSnapshot(core);
  if (calls != 1)
  {
    fprintf(stderr, "FAIL %s: arming must build exactly one snapshot\n", label);
    abort();
  }
  size_t used = 0;
  memset(collected, 0, sizeof(collected));
  for (;;)
  {
    uint8_t byte = readRegister(core, 0xC091);
    if (byte == 0)
    {
      break;
    }
    if (used == sizeof(collected) - 1)
    {
      fprintf(stderr, "FAIL %s: stream longer than the fixture buffer\n", label);
      abort();
    }
    collected[used++] = (char)byte;
  }
  collected[used] = '\0';
  if (strstr(collected, "RETROAPPLE-SYSINFO/1\r\n") != collected)
  {
    fprintf(stderr, "FAIL %s: stream must start with the signature: %s\n", label, collected);
    abort();
  }
  if (strstr(collected, "Mounted: 2 of 16 virtual volume slots.") == NULL ||
      strstr(collected, "SP5.1: volume WORK1:") == NULL ||
      strstr(collected, "SD6.1: volume PASCAL:") == NULL ||
      strstr(collected, "Firmware:           v1.2.3") == NULL)
  {
    fprintf(stderr, "FAIL %s: snapshot text incomplete:\n%s\n", label, collected);
    abort();
  }
  if (readRegister(core, 0xC091) != 0)
  {
    fprintf(stderr, "FAIL %s: stream must keep returning zero after the terminator\n", label);
    abort();
  }
  armSnapshot(core);
  if (calls != 2 || readRegister(core, 0xC091) != 'R')
  {
    fprintf(stderr, "FAIL %s: re-arming must rebuild the snapshot from the start\n", label);
    abort();
  }
  printf("PASS apple2 sysinfo slot-1 protocol (%s)\n", label);
}

//-- Executes a 6502 stub that mirrors the SYSVIEW guest access pattern byte by byte through the
//-- real fetch/execute path (including the fast core): arm via STA $C090 and drain via LDA $C091.
static void testExecutedGuest(void)
{
  static const uint8_t stub[] = {
      0xA9, 0x01,       //-- LDA #$01
      0x8D, 0x90, 0xC0, //-- STA $C090 (arm)
      0xA2, 0x00,       //-- LDX #$00
      0xAD, 0x91, 0xC0, //-- LDA $C091 (drain)
      0xF0, 0x09,       //-- BEQ $6015 (terminator)
      0x9D, 0x00, 0x20, //-- STA $2000,X
      0xE8,             //-- INX
      0xE0, 0xFF,       //-- CPX #$FF (stop after 255 bytes)
      0xD0, 0xF3,       //-- BNE $6007
      0xEA,             //-- NOP
      0x4C, 0x15, 0x60  //-- JMP $6015 (spin, so the monitor never runs)
  };
  FILE* romFile = fopen(APPLE2_SYSTEM_ROM_PATH, "rb");
  if (romFile == NULL)
  {
    fprintf(stderr, "FAIL apple2 sysinfo executed guest: cannot open %s\n", APPLE2_SYSTEM_ROM_PATH);
    abort();
  }
  uint8_t* rom = malloc(apple2RomSize);
  assert(rom != NULL);
  assert(fread(rom, 1, apple2RomSize, romFile) == apple2RomSize);
  assert(fclose(romFile) == 0);
  apple2Core* core = NULL;
  unsigned calls = 0;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreLoadRom(core, rom, apple2RomSize) == apple2CoreOk);
  free(rom);
  assert(apple2InfoAttach(core, &stream, fixtureCollect, &calls) == apple2CoreOk);
  for (size_t i = 0; i < sizeof(stub); ++i)
  {
    assert(apple2CoreWriteMemory(core, (uint16_t)(0x6000 + i), stub[i]) == apple2CoreOk);
  }
  //-- m6502_init started the reset sequence but has not ticked yet; redirecting the reset vectors
  //-- in the loaded ROM makes the pending vector fetches enter the stub at $6000.
  core->rom[0x2FFC] = 0x00;
  core->rom[0x2FFD] = 0x60;
  assert(apple2CoreRunCycles(core, 200000) == apple2CoreOk);
  assert(calls == 1);
  char drained[4200];
  size_t used = 0;
  for (;;)
  {
    uint8_t byte = 0;
    assert(apple2CoreReadMemory(core, (uint16_t)(0x2000 + used), &byte) == apple2CoreOk);
    if (byte == 0 || used == sizeof(drained) - 1)
    {
      break;
    }
    drained[used++] = (char)byte;
  }
  drained[used] = '\0';
  if (strstr(drained, "RETROAPPLE-SYSINFO/1") != drained ||
      strstr(drained, "Firmware:           v1.2.3") == NULL)
  {
    fprintf(stderr, "FAIL apple2 sysinfo executed guest: buffer holds:\n%s\n", drained);
    abort();
  }
  printf("PASS apple2 sysinfo executed 6502 drain (X=%u bytes captured)\n", (unsigned)core->cpu.X);
  apple2CoreDestroy(core);
}

int main(void)
{
  testBuilder();
  apple2Core* core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  testProtocol(core, "bus");
  apple2CoreDestroy(core);
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreEnableIIe(core) == apple2CoreOk);
  testProtocol(core, "IIe");
  apple2CoreDestroy(core);
  testExecutedGuest();
  printf("apple2 sysview tests passed\n");
  return 0;
}
