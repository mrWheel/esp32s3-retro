#include "cpm80Guest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef BENCH80_COM_PATH
#error BENCH80_COM_PATH is required
#endif

typedef struct
{
  uint32_t value;
  unsigned calls;
} clockFixture;

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
  return drive == 0;
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
static void yield(void* context)
{
  (void)context;
}

static uint32_t readClock(void* context)
{
  clockFixture* fixture = context;
  ++fixture->calls;
  return fixture->value;
}

static void initialize(cpm80Guest* guest, clockFixture* fixture, bool withClock)
{
  static uint8_t ccp[cpm80CcpSize];
  static uint8_t bdos[cpm80BdosSize];
  cpm80HostOps host = {.consoleAvailable = consoleAvailable,
                       .consoleRead = consoleRead,
                       .consoleWrite = consoleWrite,
                       .diskDriveAvailable = driveAvailable,
                       .diskDriveProfile = driveProfile,
                       .diskReadRecord = diskRead,
                       .diskWriteRecord = diskWrite,
                       .yield = yield,
                       .readMilliseconds = withClock ? readClock : NULL,
                       .clockContext = fixture};
  memset(guest, 0, sizeof(*guest));
  assert(cpm80GuestInitialize(guest, &host, ccp, bdos));
}

// Executes IN A,(port) at 0x0100 and returns A.
static uint8_t inPort(cpm80Guest* guest, uint8_t port)
{
  assert(cpm80CpuWriteMemory(&guest->cpu, 0x0100, 0xDB));
  assert(cpm80CpuWriteMemory(&guest->cpu, 0x0101, port));
  guest->cpu.processor.pc = 0x0100;
  assert(cpm80GuestStep(guest));
  return guest->cpu.processor.a;
}

static uint32_t readAll(cpm80Guest* guest)
{
  uint32_t value = inPort(guest, cpm80TimerLowPort);
  value |= (uint32_t)inPort(guest, 0xF2) << 8;
  value |= (uint32_t)inPort(guest, 0xF3) << 16;
  value |= (uint32_t)inPort(guest, cpm80TimerHighPort) << 24;
  return value;
}

typedef struct
{
  cpm80Guest* guest;
  unsigned long divisor;
  unsigned calls;
} cycleClock;

//-- Milliseconds derived from the emulated Z80 T-states: divisor 1000 is an ideal 1 MHz machine.
static uint32_t readCycleClock(void* context)
{
  cycleClock* clock = context;
  ++clock->calls;
  return (uint32_t)(clock->guest->cpu.processor.cyc / clock->divisor);
}

// Runs BENCH80.COM with minimal BDOS function 2 and 9 emulation at address 5.
static void runBench80(bool withClock, const char* commandTail, unsigned long divisor, char* output,
                       size_t outputSize)
{
  static uint8_t program[8192];
  static uint8_t ccp[cpm80CcpSize];
  static uint8_t bdos[cpm80BdosSize];
  cycleClock clock = {.divisor = divisor};
  cpm80HostOps host = {.consoleAvailable = consoleAvailable,
                       .consoleRead = consoleRead,
                       .consoleWrite = consoleWrite,
                       .diskDriveAvailable = driveAvailable,
                       .diskDriveProfile = driveProfile,
                       .diskReadRecord = diskRead,
                       .diskWriteRecord = diskWrite,
                       .yield = yield,
                       .readMilliseconds = withClock ? readCycleClock : NULL,
                       .clockContext = &clock};
  cpm80Guest guest;
  memset(&guest, 0, sizeof(guest));
  assert(cpm80GuestInitialize(&guest, &host, ccp, bdos));
  clock.guest = &guest;
  FILE* file = fopen(BENCH80_COM_PATH, "rb");
  assert(file != NULL);
  size_t length = fread(program, 1, sizeof(program), file);
  assert(length > 0 && length < sizeof(program) && feof(file));
  fclose(file);
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, program, length));
  size_t tailLength = strlen(commandTail);
  assert(cpm80CpuWriteMemory(&guest.cpu, 0x0080, (uint8_t)tailLength));
  for (size_t index = 0; index < tailLength; ++index)
  {
    assert(
        cpm80CpuWriteMemory(&guest.cpu, (uint16_t)(0x0081 + index), (uint8_t)commandTail[index]));
  }
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0xC000;
  output[0] = '\0';
  for (unsigned long steps = 0; steps < 2000000000UL && guest.cpu.processor.pc != 0; ++steps)
  {
    if (guest.cpu.processor.pc == 5)
    {
      uint16_t address = (uint16_t)((guest.cpu.processor.d << 8) | guest.cpu.processor.e);
      size_t used = strlen(output);
      if (guest.cpu.processor.c == 2)
      {
        assert(used + 1 < outputSize);
        output[used] = (char)guest.cpu.processor.e;
        output[used + 1] = '\0';
      }
      else
      {
        assert(guest.cpu.processor.c == 9);
        uint8_t character;
        while (cpm80CpuReadMemory(&guest.cpu, address++, &character) && character != '$')
        {
          used = strlen(output);
          assert(used + 1 < outputSize);
          output[used] = (char)character;
          output[used + 1] = '\0';
        }
      }
      uint8_t low, high;
      assert(cpm80CpuReadMemory(&guest.cpu, guest.cpu.processor.sp, &low));
      assert(cpm80CpuReadMemory(&guest.cpu, guest.cpu.processor.sp + 1, &high));
      guest.cpu.processor.pc = (uint16_t)((high << 8) | low);
      guest.cpu.processor.sp += 2;
      continue;
    }
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.cpu.processor.pc == 0);
  cpm80GuestDestroy(&guest);
}

static void testBench80(void)
{
  char output[2048];
  //-- A 1 MHz clock must reproduce the instruction-table model, so the index is about 100.
  runBench80(true, " 1", 1000, output, sizeof(output));
  printf("%s", output);
  assert(strstr(output, "BENCH80 v2 SCALE=1\r\n@BEGIN INTEGER\r\n") != NULL);
  assert(strstr(output, "@END INTEGER PASS AX=4ED8 BX=D678\r\n  Elapsed: ") != NULL);
  assert(strstr(output, "@END SIEVE PASS PRIMES=1028\r\n") != NULL);
  assert(strstr(output,
                "  Z80 MODEL ONLY (not measured): 1MHz=15000 ms, 2MHz=7500 ms, 4MHz=3750 ms") !=
         NULL);
  assert(strstr(output,
                "  Z80 MODEL ONLY (not measured): 1MHz=12222 ms, 2MHz=6111 ms, 4MHz=3056 ms") !=
         NULL);
  assert(strstr(output, "Index vs 1MHz MODEL: 99 ") != NULL);
  assert(strstr(output, "@DONE PASS\r\n") != NULL);
  //-- Dots appear at 5 s boundaries (checked once per block) on a line of their own.
  assert(strstr(output, "@BEGIN INTEGER\r\n..\r\n@END INTEGER") != NULL);
  //-- A 4x faster clock gives an index of about 400 and, at scale 2, doubles the model.
  runBench80(true, "2", 4000, output, sizeof(output));
  assert(strstr(output, "SCALE=2") != NULL);
  assert(strstr(output, "1MHz=30000 ms, 2MHz=15000 ms, 4MHz=7500 ms") != NULL);
  assert(strstr(output, "Index vs 1MHz MODEL: 399 ") != NULL);
  runBench80(true, "0", 1000, output, sizeof(output));
  assert(strstr(output, "Usage: BENCH80 [1-9]") != NULL);
  runBench80(false, "", 1000, output, sizeof(output));
  assert(strstr(output, "Timer unavailable") != NULL);
}

int main(void)
{
  cpm80Guest guest;
  clockFixture clock = {.value = 0x12345678u};

  initialize(&guest, &clock, true);
  assert(inPort(&guest, cpm80TimerCapabilityPort) == 0xB1);
  assert(clock.calls == 0);
  assert(inPort(&guest, 0xF2) == 0 && inPort(&guest, 0xF4) == 0);
  assert(clock.calls == 0);
  assert(inPort(&guest, 0xF5) == 0xFF);

  assert(readAll(&guest) == 0x12345678u);
  assert(clock.calls == 1);
  clock.value = 0xA1B2C3D4u;
  assert(inPort(&guest, 0xF2) == 0x56 && inPort(&guest, 0xF3) == 0x34 &&
         inPort(&guest, 0xF4) == 0x12);
  assert(inPort(&guest, cpm80TimerCapabilityPort) == 0xB1);
  assert(clock.calls == 1);
  assert(inPort(&guest, cpm80TimerLowPort) == 0xD4 && clock.calls == 2);
  assert(inPort(&guest, 0xF2) == 0xC3 && inPort(&guest, 0xF3) == 0xB2 &&
         inPort(&guest, 0xF4) == 0xA1);

  clock.value = 0xFFFFFFFEu;
  uint32_t start = readAll(&guest);
  clock.value = 3;
  assert((uint32_t)(readAll(&guest) - start) == 5);

  assert(cpm80GuestColdBoot(&guest));
  assert(inPort(&guest, 0xF2) == 0 && inPort(&guest, 0xF3) == 0 && inPort(&guest, 0xF4) == 0);
  cpm80GuestDestroy(&guest);

  clock.calls = 0;
  initialize(&guest, &clock, false);
  for (uint8_t port = 0xF0; port <= 0xF4; ++port)
  {
    assert(inPort(&guest, port) == 0);
  }
  assert(clock.calls == 0);
  cpm80GuestDestroy(&guest);
  testBench80();
  puts("PASS: CP/M-80 RETRO80_TIMER_B1");
  return 0;
}
