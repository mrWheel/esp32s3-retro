#include "cpm86Core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

//-- Independent 8086 conformance layer for the tiny8086 core. Every expected value below is
//-- derived from the Intel 8086 Family User's Manual rules, not from the core's behaviour.
//-- Table-driven vectors load code at 0x1000, expect HLT at the end and compare result, flags,
//-- registers stored through memory and (optionally) guest memory regions.

enum
{
  codeAddress = 0x1000,
  interruptHandlerAddress = 0x1100,
  interruptReturnHandlerAddress = 0x1110,
  dataAddress = 0x2000,
  flagsAddress = 0x2FFE,
  interruptMarkerAddress = 0x2FFF,
  stackGuardAddress = 0xFF00,
  guestRamSize = 640 * 1024,
  instructionBudget = 65536
};

enum
{
  flagCf = 0x0001,
  flagPf = 0x0004,
  flagAf = 0x0010,
  flagZf = 0x0040,
  flagSf = 0x0080,
  flagOf = 0x0800,
  allStatusFlags = 0x08D5
};

typedef struct
{
  uint32_t address;
  const uint8_t* bytes;
  size_t length;
} memoryPreset;

typedef struct
{
  const char* name;
  const uint8_t* code;
  size_t codeLength;
  cpm86CoreResult expected;
  memoryPreset presets[3];
  bool checkFlags;
  uint16_t flagsMask;
  uint16_t flagsValue;
  memoryPreset expectedMemory[2];
} cpuVector;

static int failures;

//-- INT 0/4 handler: marks 0x2FFF with 5Ah and halts; INT 3/5 handler: a bare IRET.
static const uint8_t interruptMarkerHandler[] = {0xB0, 0x5A, 0xA2, 0xFF, 0x2F, 0xF4};
static const uint8_t interruptReturnHandler[] = {0xCF};
//-- INT 2 handler: copies the pushed FLAGS word (SS:SP+2) to 0x2FFE and halts.
static const uint8_t interruptFlagsHandler[] = {0x58, 0x58, 0x58, 0xA3, 0xFE, 0x2F, 0xF4};
static const uint8_t flagsEpilogue[] = {0x9C, 0x58, 0xA3, 0xFE, 0x2F, 0xF4};

static void prepareCore(cpm86Core* core, const cpuVector* vector)
{
  uint8_t zeros[512];
  memset(zeros, 0, sizeof(zeros));
  assert(cpm86CoreReset(core) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, 0, zeros, sizeof(zeros)) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, dataAddress, zeros, sizeof(zeros)) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, dataAddress + sizeof(zeros), zeros, sizeof(zeros)) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, 0x2F00, zeros, sizeof(zeros)) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, stackGuardAddress, zeros, sizeof(zeros)) == cpm86CoreOk);
  uint8_t invalidFill[64];
  memset(invalidFill, 0x0F, sizeof(invalidFill));
  assert(cpm86CoreWrite(core, codeAddress, invalidFill, sizeof(invalidFill)) == cpm86CoreOk);
  assert(cpm86CoreLoad(core, interruptHandlerAddress, interruptMarkerHandler,
                       sizeof(interruptMarkerHandler)) == cpm86CoreOk);
  assert(cpm86CoreLoad(core, interruptReturnHandlerAddress, interruptReturnHandler,
                       sizeof(interruptReturnHandler)) == cpm86CoreOk);
  const uint8_t markerVector[4] = {0x00, 0x11, 0x00, 0x00};
  const uint8_t returnVector[4] = {0x10, 0x11, 0x00, 0x00};
  assert(cpm86CoreWrite(core, 0 * 4, markerVector, 4) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, 3 * 4, returnVector, 4) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, 4 * 4, markerVector, 4) == cpm86CoreOk);
  assert(cpm86CoreWrite(core, 5 * 4, returnVector, 4) == cpm86CoreOk);
  const uint8_t flagsVector[4] = {0x20, 0x11, 0x00, 0x00};
  assert(cpm86CoreLoad(core, 0x1120, interruptFlagsHandler, sizeof(interruptFlagsHandler)) ==
         cpm86CoreOk);
  assert(cpm86CoreWrite(core, 2 * 4, flagsVector, 4) == cpm86CoreOk);
  for (size_t index = 0; index < 3; ++index)
  {
    if (vector->presets[index].bytes != NULL)
    {
      assert(cpm86CoreLoad(core, vector->presets[index].address, vector->presets[index].bytes,
                           vector->presets[index].length) == cpm86CoreOk);
    }
  }
  assert(cpm86CoreLoad(core, codeAddress, vector->code, vector->codeLength) == cpm86CoreOk);
  if (vector->checkFlags)
  {
    assert(cpm86CoreLoad(core, (uint32_t)(codeAddress + vector->codeLength), flagsEpilogue,
                         sizeof(flagsEpilogue)) == cpm86CoreOk);
  }
  assert(cpm86CoreSetEntry(core, 0, codeAddress) == cpm86CoreOk);
}

static void reportFailure(const cpuVector* vector, cpm86Core* core, cpm86CoreResult result,
                          size_t executed)
{
  uint16_t segment = 0;
  uint16_t offset = 0;
  cpm86CoreGetProgramCounter(core, &segment, &offset);
  fprintf(stderr, "FAIL %s: result=%d expected=%d executed=%zu CS:IP=%04X:%04X\n", vector->name,
          result, vector->expected, executed, segment, offset);
  ++failures;
}

static void runVector(cpm86Core* core, const cpuVector* vector)
{
  prepareCore(core, vector);
  size_t executed = 0;
  cpm86CoreResult result = cpm86CoreRun(core, instructionBudget, &executed);
  if (result != vector->expected)
  {
    reportFailure(vector, core, result, executed);
    return;
  }
  if (vector->expected == cpm86CoreInvalidInstruction)
  {
    uint16_t segment;
    uint16_t offset;
    assert(cpm86CoreGetProgramCounter(core, &segment, &offset) == cpm86CoreOk);
    if (segment != 0 || offset != codeAddress)
    {
      fprintf(stderr, "FAIL %s: invalid opcode did not restore PC to %04X:%04X but %04X:%04X\n",
              vector->name, 0, codeAddress, segment, offset);
      ++failures;
      return;
    }
    return;
  }
  if (vector->checkFlags)
  {
    uint16_t observed = 0xFFFF;
    uint8_t flagsBytes[2];
    assert(cpm86CoreRead(core, flagsAddress, flagsBytes, sizeof(flagsBytes)) == cpm86CoreOk);
    observed = (uint16_t)(flagsBytes[0] | (flagsBytes[1] << 8));
    if ((observed & vector->flagsMask) != vector->flagsValue)
    {
      fprintf(stderr, "FAIL %s: flags=%04X mask=%04X expected=%04X\n", vector->name, observed,
              vector->flagsMask, vector->flagsValue);
      ++failures;
      return;
    }
  }
  for (size_t index = 0; index < 2; ++index)
  {
    const memoryPreset* expected = &vector->expectedMemory[index];
    if (expected->bytes == NULL)
    {
      continue;
    }
    uint8_t observed[16];
    if (expected->length > sizeof(observed))
    {
      fprintf(stderr, "FAIL %s: expected memory region too long\n", vector->name);
      ++failures;
      return;
    }
    memset(observed, 0, sizeof(observed));
    assert(cpm86CoreRead(core, expected->address, observed, expected->length) == cpm86CoreOk);
    if (memcmp(observed, expected->bytes, expected->length) != 0)
    {
      fprintf(stderr, "FAIL %s: memory at %05X:", vector->name, (unsigned)expected->address);
      for (size_t byte = 0; byte < expected->length; ++byte)
      {
        fprintf(stderr, " %02X", observed[byte]);
      }
      fprintf(stderr, " expected:");
      for (size_t byte = 0; byte < expected->length; ++byte)
      {
        fprintf(stderr, " %02X", expected->bytes[byte]);
      }
      fputc('\n', stderr);
      ++failures;
      return;
    }
  }
}

//-- ---------------------------------------------------------------------------
//-- Vectors. Expected values come from the manual's rules for each instruction.
//-- ---------------------------------------------------------------------------

static const uint8_t codeAddByteOverflow[] = {0xB0, 0x7F, 0x04, 0x01};
static const uint8_t codeAddByteCarry[] = {0xB0, 0xFF, 0x04, 0x01};
static const uint8_t codeAddWordOverflow[] = {0xB8, 0xFF, 0x7F, 0x05, 0x01, 0x00};
static const uint8_t codeSubByteBorrow[] = {0xB3, 0x01, 0xB0, 0x00, 0x2A, 0xC3};
static const uint8_t codeSbbNeg128Minus1[] = {0xF8, 0xB0, 0x80, 0xB3, 0x01, 0x1A, 0xC3};
static const uint8_t codeCmpEqual[] = {0xB0, 0x05, 0xB3, 0x05, 0x38, 0xD8};
static const uint8_t codeAdcWithCarry[] = {0xF9, 0xB0, 0xFF, 0x14, 0x01};
static const uint8_t codeAndClearsFlags[] = {0xB0, 0x7F, 0x04, 0x01, 0xB0,
                                             0xFF, 0x04, 0x01, 0x24, 0xF0};
static const uint8_t codeOrSetsFlags[] = {0xB0, 0x0F, 0x0C, 0xF0};
static const uint8_t codeXorSelf[] = {0xB0, 0x5A, 0x34, 0x5A};

static const uint8_t codeIncPreservesCarry[] = {0xF9, 0xB0, 0x7F, 0xFE, 0xC0};
static const uint8_t codeDecZeroOf[] = {0xF8, 0xB0, 0x00, 0xFE, 0xC8};
static const uint8_t codeDecPreservesCarry[] = {0xF9, 0xB0, 0x00, 0xFE, 0xC8};

static const uint8_t codeMulByteCarry[] = {0xB0, 0x10, 0xB3, 0x10, 0xF6, 0xE3, 0xA3, 0x00, 0x20};
static const uint8_t codeImulByteNegative[] = {0xB0, 0x80, 0xB3, 0xFF, 0xF6,
                                               0xEB, 0xA3, 0x00, 0x20};
static const uint8_t codeImulByteSmall[] = {0xB0, 0x02, 0xB3, 0x03, 0xF6, 0xEB, 0xA3, 0x00, 0x20};
static const uint8_t codeMulWordCarry[] = {0xB8, 0xFF, 0xFF, 0xB9, 0x02, 0x00, 0xF7, 0xE1,
                                           0xA3, 0x00, 0x20, 0x89, 0x16, 0x02, 0x20};

static const uint8_t codeDivByteNormal[] = {0xB8, 0xFA, 0x00, 0xB3, 0x03, 0xF6,
                                            0xF3, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeDivByteOverflow[] = {0xB8, 0x00, 0x01, 0xB3, 0x01, 0xF6, 0xF3, 0xF4};
static const uint8_t codeDivZeroByte[] = {0xB8, 0x64, 0x00, 0xB3, 0x00, 0xF6, 0xF3, 0xF4};
static const uint8_t codeIdivWordMinus32768[] = {0xBA, 0xFF, 0xFF, 0xB8, 0x00, 0x00, 0xBB,
                                                 0x02, 0x00, 0xF7, 0xFB, 0xA3, 0x00, 0x20,
                                                 0x89, 0x16, 0x02, 0x20, 0xF4};
static const uint8_t codeIdivByteMinus128[] = {0xB8, 0x00, 0xFF, 0xB3, 0x02, 0xF6,
                                               0xFB, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeIdivByteOverflow128[] = {0xB8, 0x00, 0xFF, 0xB3, 0xFE, 0xF6, 0xFB, 0xF4};
static const uint8_t codeIdivWordOverflow32768[] = {0xBA, 0xFF, 0xFF, 0xB8, 0x00, 0x00,
                                                    0xBB, 0xFE, 0xFF, 0xF7, 0xFB, 0xF4};
static const uint8_t codeIdivWordMinDivMinusOne[] = {0xBA, 0x00, 0x80, 0xB8, 0x00, 0x00,
                                                     0xBB, 0xFF, 0xFF, 0xF7, 0xFB, 0xF4};
static const uint8_t codeIdivByteRemainderSign[] = {0xB8, 0xF9, 0xFF, 0xB3, 0x02, 0xF6,
                                                    0xFB, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeIdivByteMinDivMinusOne[] = {0xB8, 0x00, 0x80, 0xB3,
                                                     0xFF, 0xF6, 0xFB, 0xF4};

static const uint8_t codeShlByte[] = {0xB0, 0x81, 0xD0, 0xE0};
static const uint8_t codeShrByteZero[] = {0xF9, 0xB0, 0x01, 0xD0, 0xE8};
static const uint8_t codeSarByte[] = {0xB0, 0x80, 0xD0, 0xF8};
static const uint8_t codeRolByte[] = {0xB0, 0x81, 0xD0, 0xC0};
static const uint8_t codeRcrByteWithCarry[] = {0xF9, 0xB0, 0x01, 0xD0, 0xD8};
static const uint8_t codeShlClZeroKeepsFlags[] = {0xF9, 0xB0, 0x00, 0xB1, 0x00, 0xD2, 0xE0};
static const uint8_t codeShlWordCl3[] = {0xB8, 0x80, 0x01, 0xB1, 0x03, 0xD3, 0xE0};

static const uint8_t codeCallRetImmediate[] = {0xE8, 0x07, 0x00, 0x89, 0x26, 0x00, 0x20,
                                               0xF4, 0x00, 0x00, 0xC2, 0x04, 0x00};
static const uint8_t codeCallFarReturnFar[] = {0x9A, 0x0A, 0x10, 0x00, 0x30, 0xF4};
static const uint8_t codeJmpFar[] = {0xEA, 0x0A, 0x10, 0x00, 0x20};
static const uint8_t codeJcxzTaken[] = {0xB1, 0x00, 0xE3, 0x05, 0xB0, 0x41, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeJcxzNotTaken[] = {0xB1, 0x01, 0xE3, 0x05, 0xB0,
                                           0x41, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeLoopCounts[] = {0xB1, 0x03, 0xE2, 0xFE, 0xB0,
                                         0x42, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeLoopzCounts[] = {0xB0, 0x00, 0x3C, 0x00, 0xB1, 0x02, 0xE1,
                                          0xFE, 0xB0, 0x43, 0xA2, 0x00, 0x20, 0xF4};

static const uint8_t codePushWrapStackTop[] = {0xB8, 0xEF, 0xBE, 0xBC, 0x01, 0x00, 0x50, 0xF4};
static const uint8_t codePhysicalWrap1Mib[] = {0xB8, 0xFF, 0xFF, 0x8E, 0xD8, 0xA1, 0x2F, 0x00, 0xBB,
                                               0x00, 0x00, 0x8E, 0xD9, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeOffsetWrapSegment[] = {0xB8, 0x00, 0x10, 0x8E, 0xD8, 0xA1,
                                                0xFF, 0xFF, 0xBB, 0x00, 0x00, 0x8E,
                                                0xD9, 0xA3, 0x00, 0x20, 0xF4};

static const uint8_t codeOpcode0f[] = {0x0F};
static const uint8_t codeOpcodeD6[] = {0xD6};
static const uint8_t codeOpcodeF1[] = {0xF1};
static const uint8_t codePopCs[] = {0x0F};
static const uint8_t codeGroup5PushRm[] = {0xFF, 0xF8};
static const uint8_t codeGroup4FeSlash2[] = {0xFE, 0xD0};
static const uint8_t codeGroup4FeSlash5[] = {0xFE, 0xE8};
static const uint8_t codeMovSregReg5[] = {0x8E, 0xEC};
static const uint8_t codeMovRmSregReg5[] = {0x8C, 0xEC};
static const uint8_t codeLesMod3[] = {0xC4, 0xC0};
static const uint8_t codeLdsMod3[] = {0xC5, 0xC0};
static const uint8_t codeLeaMod3[] = {0x8D, 0xC0};

//-- SS=0x0100; the override read reaches SS:3000h (physical 4000h), the plain read DS:3000h
//-- (physical 3000h with DS=0); both results are stored with DS=0.
static const uint8_t codeMoffsOverride[] = {0xB8, 0x00, 0x01, 0x8E, 0xD0, 0x36, 0xA1,
                                            0x00, 0x30, 0xA3, 0x00, 0x20, 0xA1, 0x00,
                                            0x30, 0xA3, 0x02, 0x20, 0xF4};
static const uint8_t codePushPopSegment[] = {0xB8, 0x00, 0x10, 0x50, 0x07, 0x06,
                                             0x58, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codePushImmSignExtend[] = {0x6A, 0xFF, 0x58, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codePopSpValue[] = {0xB8, 0x34, 0x12, 0x50, 0x8F, 0xC4,
                                         0x89, 0x26, 0x00, 0x20, 0xF4};
static const uint8_t codeXchgAxReg[] = {0xB8, 0x22, 0x11, 0xB9, 0x33, 0x44, 0x91, 0xA3,
                                        0x00, 0x20, 0x89, 0x0E, 0x02, 0x20, 0xF4};
static const uint8_t codeXchgMemoryByte[] = {0xC6, 0x06, 0x00, 0x20, 0x05, 0xB0, 0x09, 0x86,
                                             0x06, 0x00, 0x20, 0xA2, 0x02, 0x20, 0xF4};
static const uint8_t codeNotPreservesFlags[] = {0xF9, 0xF7, 0xD0, 0xA3, 0x00, 0x20};
static const uint8_t codeNegByteMin[] = {0xB0, 0x80, 0xF6, 0xD8, 0xA2, 0x00, 0x20};
static const uint8_t codeNegByteNormal[] = {0xB0, 0x01, 0xF6, 0xD8, 0xA2, 0x00, 0x20};
static const uint8_t codeTestByte[] = {0xB0, 0xF0, 0x84, 0xC3};
static const uint8_t codeTestImmediate[] = {0xB8, 0x00, 0xF0, 0xA9, 0xF0, 0x00};
static const uint8_t codeEscRegisterSkip[] = {0xDE, 0xC0, 0xB0, 0x21, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeEscMemorySkip[] = {0xDE, 0x06, 0x00, 0x20, 0xB0,
                                            0x22, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeNopAndLock[] = {0x90, 0xF0, 0x04, 0x01, 0x04,
                                         0x01, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeLeaBasic[] = {0xBB, 0x00, 0x20, 0x8D, 0x47, 0x10, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeLesPointer[] = {0xBB, 0x00, 0x21, 0xC4, 0x07, 0xA3, 0x00,
                                         0x20, 0x8C, 0xC0, 0xA3, 0x02, 0x20, 0xF4};
static const uint8_t codeLdsPointer[] = {0xBB, 0x00, 0x21, 0xC5, 0x07, 0xA3, 0x00,
                                         0x20, 0x8C, 0xD8, 0xA3, 0x02, 0x20, 0xF4};
static const uint8_t codeEnterLeave[] = {0xB8, 0x34, 0x12, 0x89, 0xC5, 0xC8, 0x00, 0x00,
                                         0x00, 0xC9, 0x89, 0x2E, 0x00, 0x20, 0xF4};
static const uint8_t codeSahf[] = {0xB4, 0x5A, 0x9E};
static const uint8_t codeLahf[] = {0x9F, 0x88, 0x26, 0x00, 0x20, 0xF4};
static const uint8_t codeCarryOperations[] = {0xF9, 0xF8, 0xF5};

static const uint8_t codeDaaAfterAdd[] = {0xB0, 0x09, 0x04, 0x09, 0x27, 0xA2, 0x00, 0x20};
static const uint8_t codeDasUnderflow[] = {0xB0, 0x00, 0xB3, 0x01, 0x2A,
                                           0xC3, 0x2F, 0xA2, 0x00, 0x20};
static const uint8_t codeAaaAdjust[] = {0xB0, 0x35, 0xB3, 0x38, 0x02, 0xC3, 0x37, 0xA3, 0x00, 0x20};
static const uint8_t codeAasAdjust[] = {0xB0, 0x00, 0xB3, 0x09, 0x2A, 0xC3, 0x3F, 0xA3, 0x00, 0x20};
static const uint8_t codeAam[] = {0xB0, 0x5A, 0xD4, 0x0A, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeAad[] = {0xB8, 0x00, 0x09, 0xD5, 0x0A, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codeAamBaseZero[] = {0xB0, 0x05, 0xD4, 0x00, 0xF4};
static const uint8_t codeXlat[] = {0xBB, 0x00, 0x21, 0xB0, 0x05, 0xD7, 0xA2, 0x00, 0x20, 0xF4};

static const uint8_t codeInt3Return[] = {0xCC, 0xB0, 0x42, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeIntnReturn[] = {0xCD, 0x05, 0xB0, 0x66, 0xA2, 0x00, 0x20, 0xF4};
static const uint8_t codeIntoWithOverflow[] = {0xB0, 0x7F, 0x04, 0x01, 0xCE, 0xF4};
static const uint8_t codeIntoWithoutOverflow[] = {0xB0, 0x01, 0x04, 0x01, 0xCE, 0xB0,
                                                  0x42, 0xA2, 0x00, 0x20, 0xF4};

static const uint8_t codeRepMovsb[] = {0xB1, 0x04, 0xBE, 0x00, 0x21, 0xBF,
                                       0x00, 0x22, 0xFC, 0xF3, 0xA4, 0xF4};
static const uint8_t codeRepMovsbBackward[] = {0xB1, 0x04, 0xBE, 0x03, 0x21, 0xBF,
                                               0x03, 0x22, 0xFD, 0xF3, 0xA4, 0xF4};
static const uint8_t codeRepStosb[] = {0xB0, 0x5A, 0xB1, 0x08, 0xBF, 0x00,
                                       0x20, 0xFC, 0xF3, 0xAA, 0xF4};
static const uint8_t codeRepneScasbStops[] = {0xB0, 0x2A, 0xB1, 0x05, 0xBF, 0x00, 0x20, 0xFC,
                                              0xF2, 0xAE, 0x89, 0x3E, 0xF0, 0x2F, 0xF4};
static const uint8_t codeRepzCmpsbAllMatch[] = {0xB1, 0x04, 0xBE, 0x00, 0x21, 0xBF, 0x00, 0x20,
                                                0xFC, 0xF3, 0xA6, 0x89, 0x0E, 0xF0, 0x2F, 0xF4};
static const uint8_t codeRepMovsbCxZero[] = {0xF9, 0xB1, 0x00, 0xBE, 0x00, 0x21,
                                             0xBF, 0x00, 0x22, 0xFC, 0xF3, 0xA4};
static const uint8_t codeCmpsbSourceOverride[] = {0xB8, 0x00, 0x01, 0x8E, 0xD0, 0xB1, 0x04, 0xBE,
                                                  0x00, 0x01, 0xBF, 0x00, 0x20, 0xFC, 0x36, 0xF3,
                                                  0xA6, 0x89, 0x0E, 0xF0, 0x2F, 0xF4};

static const uint8_t codeIntPushesReservedBit[] = {0xCD, 0x02, 0xF4};
//-- 82h is the BYTE-immediate form (alias of 80h), not an alias of 83h: AX=00FFh + imm8 01
//-- must leave 0000h (only AL added). The 16-bit decoding would give 0100h and made the
//-- genuine CP/M-86 STAT.CMD hang (CPM86-VERIFY-049).
static const uint8_t codeAddImm8Alias82[] = {0xB8, 0xFF, 0x00, 0x82, 0xC0,
                                             0x01, 0xA3, 0x00, 0x20, 0xF4};
static const uint8_t codePushfReservedBit[] = {0x9C, 0x58, 0xA3, 0xFE, 0x2F, 0xF4};
static const uint8_t codePopfForcesBit[] = {0x68, 0x00, 0x00, 0x9D, 0x9C,
                                            0x58, 0xA3, 0xFE, 0x2F, 0xF4};

//-- Shared presets reused by several vectors.
static const uint8_t presetAbcd[] = {'A', 'B', 'C', 'D'};
static const uint8_t presetScan[] = {0x41, 0x41, 0x2A};
static const uint8_t presetPointer[] = {0x80, 0x00, 0x20, 0x00};
static const uint8_t presetWrapBytes[] = {0xCD, 0xAB};
static const uint8_t presetWrapLow[] = {0xAA};
static const uint8_t presetWrapHigh[] = {0xBB};
static const uint8_t presetMoffsOverrideValue[] = {0xCD, 0xAB};
static const uint8_t presetMoffsPlainValue[] = {0x11, 0x22};
static const uint8_t presetXlatTable[] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                          0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
static const uint8_t presetFarCall[] = {0xB0, 0x5A, 0xA2, 0x00, 0x20, 0xCB};
static const uint8_t presetFarJmp[] = {0xB0, 0x77, 0xA2, 0x00, 0x20, 0xF4};

static const memoryPreset noPreset = {0, NULL, 0};

static const cpuVector vectors[] = {
    //-- ADD/OR/ADC/SBB/AND/SUB/XOR/CMP per manual flag rules.
    {"add byte 7F+1 sets OF/SF/AF",
     codeAddByteOverflow,
     sizeof(codeAddByteOverflow),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagOf | flagSf | flagAf,
     {{0, NULL, 0}}},
    {"add byte FF+1 sets CF/AF/ZF/PF",
     codeAddByteCarry,
     sizeof(codeAddByteCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagAf | flagZf | flagPf,
     {{0, NULL, 0}}},
    {"add word 7FFF+1 sets OF/SF/AF/PF",
     codeAddWordOverflow,
     sizeof(codeAddWordOverflow),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagOf | flagSf | flagAf | flagPf,
     {{0, NULL, 0}}},
    {"sub byte 00-01 sets CF/SF/PF/AF",
     codeSubByteBorrow,
     sizeof(codeSubByteBorrow),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagSf | flagPf | flagAf,
     {{0, NULL, 0}}},
    {"sbb byte 80-01 sets OF/AF",
     codeSbbNeg128Minus1,
     sizeof(codeSbbNeg128Minus1),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagOf | flagAf,
     {{0, NULL, 0}}},
    {"cmp equal sets ZF/PF",
     codeCmpEqual,
     sizeof(codeCmpEqual),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagZf | flagPf,
     {{0, NULL, 0}}},
    {"adc FF+1+1 sets CF/AF",
     codeAdcWithCarry,
     sizeof(codeAdcWithCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagAf,
     {{0, NULL, 0}}},
    {"and clears CF/OF sets ZF/PF",
     codeAndClearsFlags,
     sizeof(codeAndClearsFlags),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagZf | flagPf,
     {{0, NULL, 0}}},
    {"or sets SF/PF clears CF/OF",
     codeOrSetsFlags,
     sizeof(codeOrSetsFlags),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagSf | flagPf,
     {{0, NULL, 0}}},
    {"xor self sets ZF/PF",
     codeXorSelf,
     sizeof(codeXorSelf),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagZf | flagPf,
     {{0, NULL, 0}}},

    //-- INC/DEC preserve CF.
    {"inc 7F sets OF/SF/AF keeps CF",
     codeIncPreservesCarry,
     sizeof(codeIncPreservesCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagSf | flagOf | flagAf,
     {{0, NULL, 0}}},
    {"dec 0 sets SF/PF/AF clears CF",
     codeDecZeroOf,
     sizeof(codeDecZeroOf),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagSf | flagPf | flagAf,
     {{0, NULL, 0}}},
    {"dec 0 keeps CF",
     codeDecPreservesCarry,
     sizeof(codeDecPreservesCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagSf | flagPf | flagAf,
     {{0, NULL, 0}}},

    //-- MUL/IMUL flag rules.
    {"mul byte 10h*10h sets CF/OF",
     codeMulByteCarry,
     sizeof(codeMulByteCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     flagCf | flagOf,
     flagCf | flagOf,
     {{dataAddress, (const uint8_t[]){0x00, 0x01}, 2}}},
    {"imul byte -128*-1 sets CF/OF",
     codeImulByteNegative,
     sizeof(codeImulByteNegative),
     cpm86CoreHalted,
     {noPreset},
     true,
     flagCf | flagOf,
     flagCf | flagOf,
     {{dataAddress, (const uint8_t[]){0x80, 0x00}, 2}}},
    {"imul byte 2*3 clears CF/OF",
     codeImulByteSmall,
     sizeof(codeImulByteSmall),
     cpm86CoreHalted,
     {noPreset},
     true,
     flagCf | flagOf,
     0,
     {{dataAddress, (const uint8_t[]){0x06, 0x00}, 2}}},
    {"mul word FFFF*2 sets CF/OF DX=1",
     codeMulWordCarry,
     sizeof(codeMulWordCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     flagCf | flagOf,
     flagCf | flagOf,
     {{dataAddress, (const uint8_t[]){0xFE, 0xFF, 0x01, 0x00}, 4}}},

    //-- DIV/IDIV, including the signed -128/-32768 boundary regression.
    {"div byte 250/3 gives 83 rem 1",
     codeDivByteNormal,
     sizeof(codeDivByteNormal),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x53, 0x01}, 2}}},
    {"div byte overflow raises INT 0",
     codeDivByteOverflow,
     sizeof(codeDivByteOverflow),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"div by zero raises INT 0",
     codeDivZeroByte,
     sizeof(codeDivZeroByte),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"idiv word -65536/2 gives -32768 without INT 0",
     codeIdivWordMinus32768,
     sizeof(codeIdivWordMinus32768),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x00, 0x80, 0x00, 0x00}, 4},
      {interruptMarkerAddress, (const uint8_t[]){0x00}, 1}}},
    {"idiv byte -256/2 gives -128 without INT 0",
     codeIdivByteMinus128,
     sizeof(codeIdivByteMinus128),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x80, 0x00}, 2},
      {interruptMarkerAddress, (const uint8_t[]){0x00}, 1}}},
    {"idiv byte -256/-2 overflow raises INT 0",
     codeIdivByteOverflow128,
     sizeof(codeIdivByteOverflow128),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"idiv word -65536/-2 overflow raises INT 0",
     codeIdivWordOverflow32768,
     sizeof(codeIdivWordOverflow32768),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"idiv word 8000:0000/-1 overflow raises INT 0",
     codeIdivWordMinDivMinusOne,
     sizeof(codeIdivWordMinDivMinusOne),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"idiv byte 8000h/FFh overflow raises INT 0",
     codeIdivByteMinDivMinusOne,
     sizeof(codeIdivByteMinDivMinusOne),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"idiv byte -7/2 gives -3 rem -1",
     codeIdivByteRemainderSign,
     sizeof(codeIdivByteRemainderSign),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0xFD, 0xFF}, 2}}},

    //-- Shifts and rotates.
    {"shl byte 81 sets CF/OF",
     codeShlByte,
     sizeof(codeShlByte),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagOf,
     {{0, NULL, 0}}},
    {"shr byte 01 to zero sets CF/ZF/PF",
     codeShrByteZero,
     sizeof(codeShrByteZero),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagZf | flagPf,
     {{0, NULL, 0}}},
    {"sar byte 80 gives C0 sets SF/PF",
     codeSarByte,
     sizeof(codeSarByte),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagSf | flagPf,
     {{0, NULL, 0}}},
    {"rol byte 81 sets CF/OF",
     codeRolByte,
     sizeof(codeRolByte),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagOf,
     {{0, NULL, 0}}},
    {"rcr byte 01 with CF gives 81 sets CF clears OF",
     codeRcrByteWithCarry,
     sizeof(codeRcrByteWithCarry),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf,
     {{0, NULL, 0}}},
    {"shift by CL=0 leaves flags",
     codeShlClZeroKeepsFlags,
     sizeof(codeShlClZeroKeepsFlags),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf,
     {{0, NULL, 0}}},
    {"shl word by CL=3 gives 0C00 sets PF",
     codeShlWordCl3,
     sizeof(codeShlWordCl3),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagPf,
     {{0, NULL, 0}}},

    //-- Control flow and stack.
    {"call near + ret 4 adjusts SP",
     codeCallRetImmediate,
     sizeof(codeCallRetImmediate),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x02, 0x00}, 2}}},
    {"call far + retf runs remote code",
     codeCallFarReturnFar,
     sizeof(codeCallFarReturnFar),
     cpm86CoreHalted,
     {{0x3100A, presetFarCall, sizeof(presetFarCall)}},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x5A}, 1}}},
    {"jmp far executes at CS:IP",
     codeJmpFar,
     sizeof(codeJmpFar),
     cpm86CoreHalted,
     {{0x2100A, presetFarJmp, sizeof(presetFarJmp)}},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x77}, 1}}},
    {"jcxz CX=0 skips store",
     codeJcxzTaken,
     sizeof(codeJcxzTaken),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x00}, 1}}},
    {"jcxz CX!=0 runs store",
     codeJcxzNotTaken,
     sizeof(codeJcxzNotTaken),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x41}, 1}}},
    {"loop decrements CX to zero",
     codeLoopCounts,
     sizeof(codeLoopCounts),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x42}, 1}}},
    {"loopz iterates while ZF",
     codeLoopzCounts,
     sizeof(codeLoopzCounts),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x43}, 1}}},

    //-- Stack and physical-address wrapping.
    {"push at SP=1 wraps to FFFF:0000",
     codePushWrapStackTop,
     sizeof(codePushWrapStackTop),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{0xFFFF, (const uint8_t[]){0xEF}, 1}, {0x0000, (const uint8_t[]){0xBE}, 1}}},
    {"read wraps through the 1 MiB boundary",
     codePhysicalWrap1Mib,
     sizeof(codePhysicalWrap1Mib),
     cpm86CoreHalted,
     {{0x001F, presetWrapBytes, sizeof(presetWrapBytes)}},
     false,
     0,
     0,
     {{dataAddress, presetWrapBytes, sizeof(presetWrapBytes)}}},
    {"word read wraps inside the segment",
     codeOffsetWrapSegment,
     sizeof(codeOffsetWrapSegment),
     cpm86CoreHalted,
     {{0x1FFFF, presetWrapLow, sizeof(presetWrapLow)},
      {0x10000, presetWrapHigh, sizeof(presetWrapHigh)}},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0xAA, 0xBB}, 2}}},

    //-- Undefined encodings must fail with the PC on the instruction start.
    {"opcode 0Fh invalid",
     codeOpcode0f,
     sizeof(codeOpcode0f),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"opcode D6h invalid",
     codeOpcodeD6,
     sizeof(codeOpcodeD6),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"opcode F1h invalid",
     codeOpcodeF1,
     sizeof(codeOpcodeF1),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"POP CS invalid",
     codePopCs,
     sizeof(codePopCs),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"FF /7 PUSH r/m invalid",
     codeGroup5PushRm,
     sizeof(codeGroup5PushRm),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"FE /2 invalid without disturbing the stack",
     codeGroup4FeSlash2,
     sizeof(codeGroup4FeSlash2),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"FE /5 invalid without disturbing the stack",
     codeGroup4FeSlash5,
     sizeof(codeGroup4FeSlash5),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"MOV sreg with reg field 5 invalid",
     codeMovSregReg5,
     sizeof(codeMovSregReg5),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"MOV r/m,sreg with reg field 5 invalid",
     codeMovRmSregReg5,
     sizeof(codeMovRmSregReg5),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"LES with mod=3 invalid",
     codeLesMod3,
     sizeof(codeLesMod3),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"LDS with mod=3 invalid",
     codeLdsMod3,
     sizeof(codeLdsMod3),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},
    {"LEA with mod=3 invalid",
     codeLeaMod3,
     sizeof(codeLeaMod3),
     cpm86CoreInvalidInstruction,
     {noPreset},
     false,
     0,
     0,
     {{0, NULL, 0}}},

    //-- MOV forms, exchange, group 3, control helpers.
    {"moffs16 honours segment override",
     codeMoffsOverride,
     sizeof(codeMoffsOverride),
     cpm86CoreHalted,
     {{0x4000, presetMoffsOverrideValue, sizeof(presetMoffsOverrideValue)},
      {0x3000, presetMoffsPlainValue, sizeof(presetMoffsPlainValue)}},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0xCD, 0xAB, 0x11, 0x22}, 4}}},
    {"push imm16 sign-extends byte form",
     codePushImmSignExtend,
     sizeof(codePushImmSignExtend),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0xFF, 0xFF}, 2}}},
    {"push/pop segment round trip",
     codePushPopSegment,
     sizeof(codePushPopSegment),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x00, 0x10}, 2}}},
    {"pop SP restores popped value",
     codePopSpValue,
     sizeof(codePopSpValue),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x34, 0x12}, 2}}},
    {"xchg AX,CX swaps",
     codeXchgAxReg,
     sizeof(codeXchgAxReg),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x33, 0x44, 0x22, 0x11}, 4}}},
    {"xchg AL with memory byte",
     codeXchgMemoryByte,
     sizeof(codeXchgMemoryByte),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x09}, 1}, {0x2002, (const uint8_t[]){0x05}, 1}}},
    {"not does not affect flags",
     codeNotPreservesFlags,
     sizeof(codeNotPreservesFlags),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf,
     {{dataAddress, (const uint8_t[]){0xFF, 0xFF}, 2}}},
    {"neg 80h keeps value sets CF/SF/OF",
     codeNegByteMin,
     sizeof(codeNegByteMin),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagSf | flagOf,
     {{dataAddress, (const uint8_t[]){0x80}, 1}}},
    {"neg 01h gives FFh sets CF/SF/PF/AF",
     codeNegByteNormal,
     sizeof(codeNegByteNormal),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagSf | flagPf | flagAf,
     {{dataAddress, (const uint8_t[]){0xFF}, 1}}},
    {"test byte zero sets ZF/PF",
     codeTestByte,
     sizeof(codeTestByte),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagZf | flagPf,
     {{0, NULL, 0}}},
    {"test immediate zero sets ZF/PF",
     codeTestImmediate,
     sizeof(codeTestImmediate),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagZf | flagPf,
     {{0, NULL, 0}}},
    {"ESC register form decodes and ignores",
     codeEscRegisterSkip,
     sizeof(codeEscRegisterSkip),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x21}, 1}}},
    {"ESC memory form decodes displacement",
     codeEscMemorySkip,
     sizeof(codeEscMemorySkip),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x22}, 1}}},
    {"NOP and LOCK prefix execute",
     codeNopAndLock,
     sizeof(codeNopAndLock),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x02}, 1}}},
    {"lea computes offset",
     codeLeaBasic,
     sizeof(codeLeaBasic),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x10, 0x20}, 2}}},
    {"les loads pointer",
     codeLesPointer,
     sizeof(codeLesPointer),
     cpm86CoreHalted,
     {{0x2100, presetPointer, sizeof(presetPointer)}},
     false,
     0,
     0,
     {{dataAddress, presetPointer, sizeof(presetPointer)}}},
    {"lds loads pointer and DS",
     codeLdsPointer,
     sizeof(codeLdsPointer),
     cpm86CoreHalted,
     {{0x2100, presetPointer, sizeof(presetPointer)}},
     false,
     0,
     0,
     {{0x2200, presetPointer, sizeof(presetPointer)}}},
    {"enter then leave restores BP",
     codeEnterLeave,
     sizeof(codeEnterLeave),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x34, 0x12}, 2}}},
    {"sahf loads low flags",
     codeSahf,
     sizeof(codeSahf),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagZf | flagAf,
     {{0, NULL, 0}}},
    {"lahf copies status bits to AH",
     codeLahf,
     sizeof(codeLahf),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x02}, 1}}},
    {"stc clc cmc leave CF set",
     codeCarryOperations,
     sizeof(codeCarryOperations),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf,
     {{0, NULL, 0}}},

    //-- BCD operations per the manual's adjustment rules.
    {"daa after 09+09 gives 18h sets AF/PF",
     codeDaaAfterAdd,
     sizeof(codeDaaAfterAdd),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagAf | flagPf,
     {{dataAddress, (const uint8_t[]){0x18}, 1}}},
    {"das underflow gives 99h sets CF/AF/SF/PF",
     codeDasUnderflow,
     sizeof(codeDasUnderflow),
     cpm86CoreHalted,
     {noPreset},
     true,
     allStatusFlags,
     flagCf | flagAf | flagSf | flagPf,
     {{dataAddress, (const uint8_t[]){0x99}, 1}}},
    {"aaa adjusts to 01:03 sets CF/AF",
     codeAaaAdjust,
     sizeof(codeAaaAdjust),
     cpm86CoreHalted,
     {noPreset},
     true,
     flagCf | flagAf,
     flagCf | flagAf,
     {{dataAddress, (const uint8_t[]){0x03, 0x01}, 2}}},
    {"aas adjusts to FF:01 sets CF/AF",
     codeAasAdjust,
     sizeof(codeAasAdjust),
     cpm86CoreHalted,
     {noPreset},
     true,
     flagCf | flagAf,
     flagCf | flagAf,
     {{dataAddress, (const uint8_t[]){0x01, 0xFF}, 2}}},
    {"aam base 10 splits 90",
     codeAam,
     sizeof(codeAam),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x00, 0x09}, 2}}},
    {"aad base 10 combines 09",
     codeAad,
     sizeof(codeAad),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x5A, 0x00}, 2}}},
    {"aam base 0 raises INT 0",
     codeAamBaseZero,
     sizeof(codeAamBaseZero),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"xlat indexes DS:BX+AL",
     codeXlat,
     sizeof(codeXlat),
     cpm86CoreHalted,
     {{0x2100, presetXlatTable, sizeof(presetXlatTable)}},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x35}, 1}}},

    //-- Software interrupts dispatch through the guest IVT.
    {"int 3 returns via guest IRET",
     codeInt3Return,
     sizeof(codeInt3Return),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x42}, 1}}},
    {"int n returns via guest IRET",
     codeIntnReturn,
     sizeof(codeIntnReturn),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x66}, 1}}},
    {"into with OF raises INT 4",
     codeIntoWithOverflow,
     sizeof(codeIntoWithOverflow),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{interruptMarkerAddress, (const uint8_t[]){0x5A}, 1}}},
    {"into without OF continues",
     codeIntoWithoutOverflow,
     sizeof(codeIntoWithoutOverflow),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x42}, 1},
      {interruptMarkerAddress, (const uint8_t[]){0x00}, 1}}},

    //-- String operations with REP, direction flag and source override.
    {"rep movsb copies forward",
     codeRepMovsb,
     sizeof(codeRepMovsb),
     cpm86CoreHalted,
     {{0x2100, presetAbcd, sizeof(presetAbcd)}},
     false,
     0,
     0,
     {{0x2200, presetAbcd, sizeof(presetAbcd)}}},
    {"rep movsb copies backward with DF",
     codeRepMovsbBackward,
     sizeof(codeRepMovsbBackward),
     cpm86CoreHalted,
     {{0x2100, presetAbcd, sizeof(presetAbcd)}},
     false,
     0,
     0,
     {{0x2200, presetAbcd, sizeof(presetAbcd)}}},
    {"rep stosb fills eight bytes",
     codeRepStosb,
     sizeof(codeRepStosb),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A}, 8}}},
    {"repne scasb stops after the matched byte",
     codeRepneScasbStops,
     sizeof(codeRepneScasbStops),
     cpm86CoreHalted,
     {{0x2000, presetScan, sizeof(presetScan)}},
     false,
     0,
     0,
     {{0x2FF0, (const uint8_t[]){0x03, 0x20}, 2}}},
    {"repz cmpsb consumes all matches",
     codeRepzCmpsbAllMatch,
     sizeof(codeRepzCmpsbAllMatch),
     cpm86CoreHalted,
     {{0x2000, presetAbcd, sizeof(presetAbcd)}, {0x2100, presetAbcd, sizeof(presetAbcd)}},
     false,
     0,
     0,
     {{0x2FF0, (const uint8_t[]){0x00, 0x00}, 2}}},
    {"rep with CX=0 touches nothing",
     codeRepMovsbCxZero,
     sizeof(codeRepMovsbCxZero),
     cpm86CoreHalted,
     {{0x2100, presetAbcd, sizeof(presetAbcd)}},
     true,
     allStatusFlags,
     flagCf,
     {{0x2200, (const uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4}}},
    {"cmpsb honours source segment override",
     codeCmpsbSourceOverride,
     sizeof(codeCmpsbSourceOverride),
     cpm86CoreHalted,
     {{0x2000, presetAbcd, sizeof(presetAbcd)}, {0x1100, presetAbcd, sizeof(presetAbcd)}},
     false,
     0,
     0,
     {{0x2FF0, (const uint8_t[]){0x00, 0x00}, 2}}},

    //-- Reserved flag bit 1 is always set (PUSHF/POPF/IRET/INT contract).
    {"int pushes flags with reserved bit 1",
     codeIntPushesReservedBit,
     sizeof(codeIntPushesReservedBit),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{flagsAddress, (const uint8_t[]){0x02, 0x02}, 2}}},
    {"82h aliases 80h (byte r/m, imm8) not 83h",
     codeAddImm8Alias82,
     sizeof(codeAddImm8Alias82),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{dataAddress, (const uint8_t[]){0x00, 0x00}, 2}}},
    {"pushf reports reserved bit 1",
     codePushfReservedBit,
     sizeof(codePushfReservedBit),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{flagsAddress, (const uint8_t[]){0x02, 0x02}, 2}}},
    {"popf forces reserved bit 1",
     codePopfForcesBit,
     sizeof(codePopfForcesBit),
     cpm86CoreHalted,
     {noPreset},
     false,
     0,
     0,
     {{flagsAddress, (const uint8_t[]){0x02, 0x00}, 2}}},
};

//-- Every Jcc condition against two flag patterns; the taken/untaken answers are the manual's
//-- condition table, independent of the core's conditionTrue implementation.
static const bool jccTakenAllFlags[16] = {true, false, true, false, true,  false, true, false,
                                          true, false, true, false, false, true,  true, false};
static const bool jccTakenNoFlags[16] = {false, true, false, true, false, true, false, true,
                                         false, true, false, true, false, true, false, true};

static void runJccVectors(cpm86Core* core)
{
  for (size_t pass = 0; pass < 2; ++pass)
  {
    const uint16_t pattern =
        pass == 0 ? (uint16_t)(flagCf | flagZf | flagSf | flagOf | flagPf) : (uint16_t)0x0000;
    const bool* taken = pass == 0 ? jccTakenAllFlags : jccTakenNoFlags;
    for (uint8_t condition = 0; condition < 16; ++condition)
    {
      //-- PUSH imm16 + POPF preset the flags, then Jcc either skips or performs the marker
      // store.
      const uint8_t code[] = {0x68,
                              (uint8_t)pattern,
                              (uint8_t)(pattern >> 8),
                              0x9D,
                              (uint8_t)(0x70 | condition),
                              0x05,
                              0xB0,
                              0x01,
                              0xA2,
                              0x00,
                              0x20,
                              0xF4};
      const uint8_t* expectedByte =
          taken[condition] ? (const uint8_t[]){0x00} : (const uint8_t[]){0x01};
      char name[64];
      snprintf(name, sizeof(name), "jcc %02Xh pattern %04Xh %s", condition, pattern,
               taken[condition] ? "taken" : "not taken");
      cpuVector vector;
      memset(&vector, 0, sizeof(vector));
      vector.name = name;
      vector.code = code;
      vector.codeLength = sizeof(code);
      vector.expected = cpm86CoreHalted;
      vector.expectedMemory[0].address = dataAddress;
      vector.expectedMemory[0].bytes = expectedByte;
      vector.expectedMemory[0].length = 1;
      runVector(core, &vector);
    }
  }
}

int main(void)
{
  const cpm86CoreConfig config = {
      .ramSize = guestRamSize,
      .portRead = NULL,
      .portWrite = NULL,
      .portContext = NULL,
      .readMilliseconds = NULL,
      .clockContext = NULL,
  };
  cpm86Core* core = NULL;
  assert(cpm86CoreCreate(&core, &config) == cpm86CoreOk);
  for (size_t index = 0; index < sizeof(vectors) / sizeof(vectors[0]); ++index)
  {
    runVector(core, &vectors[index]);
  }
  runJccVectors(core);
  cpm86CoreDestroy(core);
  if (failures != 0)
  {
    printf("FAIL: %d CP/M-86 conformance vectors failed\n", failures);
    return 1;
  }
  printf("PASS: %zu CP/M-86 conformance vectors plus 32 Jcc cases\n",
         sizeof(vectors) / sizeof(vectors[0]));
  return 0;
}