#include "../components/apple2Core/apple2Core.c"
#include <stdio.h>
#include <stdlib.h>

//-- Differential test: the instruction-stepped fast 6502 core must be indistinguishable from the
//-- cycle-stepped chips/m6502.h core. Both run the same random programs; after every random-sized
//-- chunk of cycles the CPU state, cycle counter, memory and soft-switch state must be identical.

static uint64_t rngState;

static uint32_t rnd(void)
{
  rngState ^= rngState << 13;
  rngState ^= rngState >> 7;
  rngState ^= rngState << 17;
  return (uint32_t)(rngState >> 11);
}

static bool isJam(uint8_t opcode)
{
  return (opcode & 0x0F) == 0x02 && opcode <= 0xF2 && opcode != 0xA2 && opcode != 0x82 &&
         opcode != 0xC2 && opcode != 0xE2;
}

static uint8_t randomByte(void)
{
  uint8_t value;
  do
  {
    value = (uint8_t)rnd();
  } while (isJam(value));
  return value;
}

static void compareCores(const apple2Core* a, const apple2Core* b, unsigned trial, unsigned chunk)
{
#define SAME(field)                                                                                \
  do                                                                                               \
  {                                                                                                \
    if (a->field != b->field)                                                                      \
    {                                                                                              \
      fprintf(stderr, "trial %u chunk %u: %s differs (%llu vs %llu)\n", trial, chunk, #field,      \
              (unsigned long long)a->field, (unsigned long long)b->field);                         \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)
  SAME(cpu.A);
  SAME(cpu.X);
  SAME(cpu.Y);
  SAME(cpu.S);
  SAME(cpu.P);
  SAME(cpu.PC);
  SAME(cpu.AD);
  SAME(pins);
  SAME(cycles);
  SAME(busValue);
  SAME(keyboardData);
  SAME(keyboardStrobe);
  SAME(lowercaseEchoPending);
  SAME(lowercaseEchoCycles);
  SAME(setvidActive);
  SAME(pascalCardActive);
  SAME(videxOutputSelected);
  SAME(videxWorkspaceActive);
  SAME(languageCardBank2);
  SAME(languageCardReadEnabled);
  SAME(languageCardWriteEnabled);
  SAME(languageCardPrewrite);
  SAME(ramRead);
  SAME(ramWrite);
  SAME(videxRamBank);
  SAME(videxRegisterAddress);
#undef SAME
  if (memcmp(a->ram, b->ram, apple2RamSize) != 0 ||
      memcmp(a->languageCardRam, b->languageCardRam, apple2LanguageCardRamSize) != 0 ||
      memcmp(a->videxRam, b->videxRam, sizeof(a->videxRam)) != 0 ||
      memcmp(a->videxRegisters, b->videxRegisters, sizeof(a->videxRegisters)) != 0 ||
      memcmp(a->videxWorkspace, b->videxWorkspace, sizeof(a->videxWorkspace)) != 0 ||
      memcmp(a->videxSavedWorkspace, b->videxSavedWorkspace, sizeof(a->videxSavedWorkspace)) != 0)
  {
    fprintf(stderr, "trial %u chunk %u: memory differs\n", trial, chunk);
    abort();
  }
}

static void runPair(apple2Core* reference, apple2Core* fast, size_t cycles)
{
  apple2CoreDisableFast6502 = 1;
  assert(apple2CoreRunCycles(reference, cycles) == apple2CoreOk);
  apple2CoreDisableFast6502 = 0;
  assert(apple2CoreRunCycles(fast, cycles) == apple2CoreOk);
}

static void runTrial(unsigned trial, uint64_t seed, bool iie)
{
  rngState = seed;
  apple2Core* cores[2];
  uint8_t rom[apple2IIeRomSize];
  size_t romSize = iie ? apple2IIeRomSize : apple2RomSize;
  for (size_t index = 0; index < romSize; ++index)
  {
    rom[index] = randomByte();
  }
  //-- Reset vector: into the upper ROM, away from the card ROM at $C000-$CFFF.
  uint16_t reset = (uint16_t)(0xD000 + rnd() % 0x2F00);
  rom[romSize - 4] = (uint8_t)reset;
  rom[romSize - 3] = (uint8_t)(reset >> 8);
  uint8_t ram[apple2RamSize];
  for (size_t index = 0; index < sizeof(ram); ++index)
  {
    ram[index] = randomByte();
  }
  bool boot80 = !iie && (rnd() & 1);
  bool lowercase = (rnd() & 1);
  for (int side = 0; side < 2; ++side)
  {
    assert(apple2CoreCreate(&cores[side]) == apple2CoreOk);
    if (iie)
    {
      assert(apple2CoreEnableIIe(cores[side]) == apple2CoreOk);
    }
    assert(apple2CoreSetCharacterOptions(cores[side], lowercase, lowercase) == apple2CoreOk);
    assert(apple2CoreSetBootIn80Columns(cores[side], boot80) == apple2CoreOk);
    assert(apple2CoreLoadRom(cores[side], rom, romSize) == apple2CoreOk);
    memcpy(cores[side]->ram, ram, sizeof(ram));
  }
  for (unsigned chunk = 0; chunk < 300; ++chunk)
  {
    size_t cycles;
    switch (rnd() % 4)
    {
    case 0:
      cycles = 1 + rnd() % 12;
      break;
    case 1:
      cycles = 1 + rnd() % 200;
      break;
    default:
      cycles = 1 + rnd() % 3000;
      break;
    }
    if ((rnd() % 8) == 0)
    {
      uint8_t key = (uint8_t)('a' + rnd() % 26);
      apple2CorePressKey(cores[0], key);
      apple2CorePressKey(cores[1], key);
    }
    runPair(cores[0], cores[1], cycles);
    compareCores(cores[0], cores[1], trial, chunk);
  }
  apple2CoreDestroy(cores[0]);
  apple2CoreDestroy(cores[1]);
}

int main(void)
{
  unsigned trials = 400;
  for (unsigned trial = 0; trial < trials; ++trial)
  {
    runTrial(trial, 0x9E3779B97F4A7C15ULL * (trial + 1), (trial % 3) == 2);
  }
  printf("apple2Fast6502 differential test: %u trials, %llu cycles on the fast core\n", trials,
         (unsigned long long)apple2CoreFast6502Cycles);
  if (apple2CoreFast6502Cycles < 1000000)
  {
    fprintf(stderr, "fast core was barely exercised\n");
    return 1;
  }
  return 0;
}
