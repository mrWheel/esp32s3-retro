#include "tiny8086.h"
#include <string.h>

//-- Small helpers must be inlined: every out-of-line call costs a windowed-ABI entry/retw on
//Xtensa.
#define TINY8086_INLINE static inline __attribute__((always_inline))

//-- Layout assumption: little-endian host (Xtensa LX7) so that the byte registers
//-- AL/CL/DL/BL and AH/CH/DH/BH are reached with a plain byte pointer into reg[].

#define CPU_FAULT(cpu, code) ((cpu)->fault = (cpu)->fault == tiny8086Ok ? (code) : (cpu)->fault)

typedef struct
{
  uint8_t mod;
  uint8_t regField;
  uint8_t rm;
  bool isReg;
  uint16_t seg;
  uint16_t off;
} modRmInfo;

//-- ---------------------------------------------------------------------------
//-- Guest memory: direct (segment << 4) + offset onto the PSRAM array.
//-- ---------------------------------------------------------------------------

static inline uint32_t physicalAddress(uint16_t segment, uint16_t offset)
{
  return (((uint32_t)segment << 4) + offset) & tiny8086AddressMask;
}

static inline uint8_t readByte(tiny8086* cpu, uint32_t address)
{
  if (__builtin_expect(address >= cpu->memorySize, 0))
  {
    CPU_FAULT(cpu, tiny8086MemoryFault);
    return 0xFF;
  }
  return cpu->memory[address];
}

static inline void writeByte(tiny8086* cpu, uint32_t address, uint8_t value)
{
  if (__builtin_expect(address >= cpu->memorySize, 0))
  {
    CPU_FAULT(cpu, tiny8086MemoryFault);
    return;
  }
  cpu->memory[address] = value;
}

//-- The offset wraps inside the 64 KiB segment, the physical address wraps at 20 bits.
TINY8086_INLINE uint16_t readWord(tiny8086* cpu, uint16_t segment, uint16_t offset)
{
  uint32_t address = physicalAddress(segment, offset);
  //-- Fast path: no 64 KiB offset wrap, no 20-bit wrap, fully inside populated RAM.
  if (__builtin_expect(offset != 0xFFFF && address + 1 < cpu->memorySize, 1))
  {
    const uint8_t* pointer = cpu->memory + address;
    return (uint16_t)(pointer[0] | (pointer[1] << 8));
  }
  uint8_t low = readByte(cpu, address);
  uint8_t high = readByte(cpu, physicalAddress(segment, (uint16_t)(offset + 1)));
  return (uint16_t)(low | (high << 8));
}

TINY8086_INLINE void writeWord(tiny8086* cpu, uint16_t segment, uint16_t offset, uint16_t value)
{
  uint32_t address = physicalAddress(segment, offset);
  if (__builtin_expect(offset != 0xFFFF && address + 1 < cpu->memorySize, 1))
  {
    uint8_t* pointer = cpu->memory + address;
    pointer[0] = (uint8_t)value;
    pointer[1] = (uint8_t)(value >> 8);
    return;
  }
  writeByte(cpu, address, (uint8_t)value);
  writeByte(cpu, physicalAddress(segment, (uint16_t)(offset + 1)), (uint8_t)(value >> 8));
}

TINY8086_INLINE uint8_t fetchByte(tiny8086* cpu)
{
  return readByte(cpu, physicalAddress(cpu->seg[tiny8086SegCs], cpu->ip++));
}

TINY8086_INLINE uint16_t fetchWord(tiny8086* cpu)
{
  uint32_t address = physicalAddress(cpu->seg[tiny8086SegCs], cpu->ip);
  if (__builtin_expect(cpu->ip < 0xFFFE && address + 1 < cpu->memorySize, 1))
  {
    const uint8_t* pointer = cpu->memory + address;
    cpu->ip += 2;
    return (uint16_t)(pointer[0] | (pointer[1] << 8));
  }
  uint16_t low = fetchByte(cpu);
  uint16_t high = fetchByte(cpu);
  return (uint16_t)(low | (high << 8));
}

TINY8086_INLINE void push16(tiny8086* cpu, uint16_t value)
{
  cpu->reg[tiny8086RegSp] -= 2;
  writeWord(cpu, cpu->seg[tiny8086SegSs], cpu->reg[tiny8086RegSp], value);
}

TINY8086_INLINE uint16_t pop16(tiny8086* cpu)
{
  uint16_t value = readWord(cpu, cpu->seg[tiny8086SegSs], cpu->reg[tiny8086RegSp]);
  cpu->reg[tiny8086RegSp] += 2;
  return value;
}

//-- ---------------------------------------------------------------------------
//-- Registers and flags
//-- ---------------------------------------------------------------------------

static inline uint8_t* reg8Pointer(tiny8086* cpu, uint8_t index)
{
  return ((uint8_t*)&cpu->reg[index & 3]) + (index >> 2);
}

static inline void setFlag(tiny8086* cpu, uint16_t mask, bool condition)
{
  if (condition)
  {
    cpu->flags |= mask;
  }
  else
  {
    cpu->flags &= (uint16_t)~mask;
  }
}

//-- PF for every byte value (even parity -> 0x04), kept in internal RAM by the noflash mapping.
static const uint8_t parityFlagTable[256] = {
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04};

static inline void setResultFlags(tiny8086* cpu, uint32_t result, bool wide)
{
  //-- Branch-free ZF/SF/PF; result must already be masked to the operand width.
  uint32_t zero = result == 0 ? tiny8086FlagZf : 0u;
  uint32_t sign = (result >> (wide ? 8 : 0)) & tiny8086FlagSf;
  uint32_t flags = parityFlagTable[result & 0xFF] | zero | sign;
  cpu->flags =
      (uint16_t)((cpu->flags & ~(tiny8086FlagZf | tiny8086FlagSf | tiny8086FlagPf)) | flags);
}

//-- ALU operation order matches opcode bits 5..3 and group-1 /r:
//-- 0 ADD, 1 OR, 2 ADC, 3 SBB, 4 AND, 5 SUB, 6 XOR, 7 CMP.
//-- All six status flags are computed together and written with a single store.
static uint16_t aluOperation(tiny8086* cpu, uint8_t operation, uint16_t a, uint16_t b, bool wide)
{
  const uint32_t mask = wide ? 0xFFFFu : 0xFFu;
  const uint32_t carryShift = wide ? 16 : 8;
  const uint32_t signShift = wide ? 15 : 7;
  const uint32_t carryIn = cpu->flags & tiny8086FlagCf;
  uint32_t result;
  uint32_t flags;
  const uint32_t statusMask = tiny8086FlagCf | tiny8086FlagPf | tiny8086FlagAf | tiny8086FlagZf |
                              tiny8086FlagSf | tiny8086FlagOf;

  switch (operation)
  {
  case 0:
  case 2:
    result = (uint32_t)a + b + (operation == 2 ? carryIn : 0u);
    flags = ((result >> carryShift) & 1u) | (((a ^ b ^ result) & 0x10u)) |
            ((((~(a ^ b) & (a ^ result)) >> signShift) & 1u) << 11);
    break;
  case 3:
  case 5:
  case 7:
    result = (uint32_t)a - b - (operation == 3 ? carryIn : 0u);
    flags = ((result >> carryShift) & 1u) | (((a ^ b ^ result) & 0x10u)) |
            ((((a ^ b) & (a ^ result)) >> signShift) & 1u) << 11;
    break;
  case 1:
    result = a | b;
    flags = 0;
    break;
  case 4:
    result = a & b;
    flags = 0;
    break;
  default:
    result = a ^ b;
    flags = 0;
    break;
  }
  result &= mask;
  flags |= parityFlagTable[result & 0xFF] | (result == 0 ? tiny8086FlagZf : 0u) |
           ((result >> (wide ? 8 : 0)) & tiny8086FlagSf);
  cpu->flags = (uint16_t)((cpu->flags & ~statusMask) | flags);
  return (uint16_t)result;
}

static bool conditionTrue(const tiny8086* cpu, uint8_t condition)
{
  uint16_t f = cpu->flags;
  bool cf = (f & tiny8086FlagCf) != 0;
  bool zf = (f & tiny8086FlagZf) != 0;
  bool sf = (f & tiny8086FlagSf) != 0;
  bool of = (f & tiny8086FlagOf) != 0;
  bool pf = (f & tiny8086FlagPf) != 0;
  bool result;

  switch (condition >> 1)
  {
  case 0:
    result = of;
    break;
  case 1:
    result = cf;
    break;
  case 2:
    result = zf;
    break;
  case 3:
    result = cf || zf;
    break;
  case 4:
    result = sf;
    break;
  case 5:
    result = pf;
    break;
  case 6:
    result = sf != of;
    break;
  default:
    result = zf || (sf != of);
    break;
  }
  return (condition & 1) ? !result : result;
}

//-- ---------------------------------------------------------------------------
//-- ModRM decoding and operand access
//-- ---------------------------------------------------------------------------

TINY8086_INLINE void decodeModRm(tiny8086* cpu, modRmInfo* info, int segmentOverride)
{
  uint8_t modRm = fetchByte(cpu);
  info->mod = modRm >> 6;
  info->regField = (modRm >> 3) & 7;
  info->rm = modRm & 7;
  info->isReg = info->mod == 3;
  if (info->isReg)
  {
    return;
  }

  uint16_t offset;
  uint16_t defaultSegment = cpu->seg[tiny8086SegDs];
  if (info->mod == 0 && info->rm == 6)
  {
    offset = fetchWord(cpu);
  }
  else
  {
    switch (info->rm)
    {
    case 0:
      offset = cpu->reg[tiny8086RegBx] + cpu->reg[tiny8086RegSi];
      break;
    case 1:
      offset = cpu->reg[tiny8086RegBx] + cpu->reg[tiny8086RegDi];
      break;
    case 2:
      offset = cpu->reg[tiny8086RegBp] + cpu->reg[tiny8086RegSi];
      defaultSegment = cpu->seg[tiny8086SegSs];
      break;
    case 3:
      offset = cpu->reg[tiny8086RegBp] + cpu->reg[tiny8086RegDi];
      defaultSegment = cpu->seg[tiny8086SegSs];
      break;
    case 4:
      offset = cpu->reg[tiny8086RegSi];
      break;
    case 5:
      offset = cpu->reg[tiny8086RegDi];
      break;
    case 6:
      offset = cpu->reg[tiny8086RegBp];
      defaultSegment = cpu->seg[tiny8086SegSs];
      break;
    default:
      offset = cpu->reg[tiny8086RegBx];
      break;
    }
    if (info->mod == 1)
    {
      offset += (uint16_t)(int16_t)(int8_t)fetchByte(cpu);
    }
    else if (info->mod == 2)
    {
      offset += fetchWord(cpu);
    }
  }
  info->off = offset;
  info->seg = segmentOverride >= 0 ? cpu->seg[segmentOverride] : defaultSegment;
}

TINY8086_INLINE uint16_t readRm(tiny8086* cpu, const modRmInfo* info, bool wide)
{
  if (info->isReg)
  {
    return wide ? cpu->reg[info->rm] : *reg8Pointer(cpu, info->rm);
  }
  return wide ? readWord(cpu, info->seg, info->off)
              : readByte(cpu, physicalAddress(info->seg, info->off));
}

TINY8086_INLINE void writeRm(tiny8086* cpu, const modRmInfo* info, uint16_t value, bool wide)
{
  if (info->isReg)
  {
    if (wide)
    {
      cpu->reg[info->rm] = value;
    }
    else
    {
      *reg8Pointer(cpu, info->rm) = (uint8_t)value;
    }
    return;
  }
  if (wide)
  {
    writeWord(cpu, info->seg, info->off, value);
  }
  else
  {
    writeByte(cpu, physicalAddress(info->seg, info->off), (uint8_t)value);
  }
}

TINY8086_INLINE uint16_t readRegField(tiny8086* cpu, uint8_t index, bool wide)
{
  return wide ? cpu->reg[index] : *reg8Pointer(cpu, index);
}

TINY8086_INLINE void writeRegField(tiny8086* cpu, uint8_t index, uint16_t value, bool wide)
{
  if (wide)
  {
    cpu->reg[index] = value;
  }
  else
  {
    *reg8Pointer(cpu, index) = (uint8_t)value;
  }
}

//-- ---------------------------------------------------------------------------
//-- Interrupts and I/O (the places where the host is injected)
//-- ---------------------------------------------------------------------------

static void raiseInterrupt(tiny8086* cpu, uint8_t vector)
{
  //-- HOOK (software interrupt): host-side BIOS/BDOS services. When the hook
  //-- handles the call the guest simply continues after the INT instruction.
  if (cpu->intHook != NULL && cpu->intHook(cpu->hookContext, cpu, vector))
  {
    return;
  }
  push16(cpu, cpu->flags);
  push16(cpu, cpu->seg[tiny8086SegCs]);
  push16(cpu, cpu->ip);
  setFlag(cpu, tiny8086FlagIf | tiny8086FlagTf, false);
  uint32_t vectorAddress = (uint32_t)vector * 4;
  cpu->ip = (uint16_t)(readByte(cpu, vectorAddress) | (readByte(cpu, vectorAddress + 1) << 8));
  cpu->seg[tiny8086SegCs] =
      (uint16_t)(readByte(cpu, vectorAddress + 2) | (readByte(cpu, vectorAddress + 3) << 8));
}

static uint8_t portInByte(tiny8086* cpu, uint16_t port)
{
  //-- HOOK (port read): console status/input, disk transfer data, timer.
  uint8_t value = 0xFF;
  if (cpu->portRead == NULL || !cpu->portRead(cpu->hookContext, port, &value))
  {
    CPU_FAULT(cpu, tiny8086IoFault);
    return 0xFF;
  }
  return value;
}

static void portOutByte(tiny8086* cpu, uint16_t port, uint8_t value)
{
  //-- HOOK (port write): console output, disk track/sector/command registers.
  if (cpu->portWrite == NULL || !cpu->portWrite(cpu->hookContext, port, value))
  {
    CPU_FAULT(cpu, tiny8086IoFault);
  }
}

//-- ---------------------------------------------------------------------------
//-- Public API
//-- ---------------------------------------------------------------------------

tiny8086Result tiny8086Init(tiny8086* cpu, const tiny8086Config* config)
{
  if (cpu == NULL || config == NULL || config->memory == NULL || config->memorySize == 0 ||
      config->memorySize > tiny8086AddressMask + 1u)
  {
    return tiny8086InvalidArgument;
  }
  memset(cpu, 0, sizeof(*cpu));
  cpu->memory = config->memory;
  cpu->memorySize = config->memorySize;
  cpu->portRead = config->portRead;
  cpu->portWrite = config->portWrite;
  cpu->intHook = config->intHook;
  cpu->hookContext = config->hookContext;
  tiny8086Reset(cpu);
  return tiny8086Ok;
}

void tiny8086Reset(tiny8086* cpu)
{
  memset(cpu->reg, 0, sizeof(cpu->reg));
  memset(cpu->seg, 0, sizeof(cpu->seg));
  cpu->reg[tiny8086RegSp] = 0xFFFE;
  cpu->ip = 0;
  cpu->flags = tiny8086FlagIf;
  cpu->instructionCount = 0;
  cpu->fault = tiny8086Ok;
  cpu->halted = false;
}

bool tiny8086ReadMemory(const tiny8086* cpu, uint32_t address, void* data, size_t length)
{
  if (address > cpu->memorySize || length > cpu->memorySize - address)
  {
    return false;
  }
  memcpy(data, cpu->memory + address, length);
  return true;
}

bool tiny8086WriteMemory(tiny8086* cpu, uint32_t address, const void* data, size_t length)
{
  if (address > cpu->memorySize || length > cpu->memorySize - address)
  {
    return false;
  }
  memcpy(cpu->memory + address, data, length);
  return true;
}

//-- Shift/rotate, operation = ModRM reg field (0 ROL, 1 ROR, 2 RCL, 3 RCR, 4/6 SHL, 5 SHR, 7 SAR).
static uint16_t shiftOperation(tiny8086* cpu, uint8_t operation, uint16_t value, uint8_t count,
                               bool wide)
{
  uint32_t signBit = wide ? 0x8000u : 0x80u;
  uint32_t mask = wide ? 0xFFFFu : 0xFFu;
  uint32_t v = value;
  bool carry = (cpu->flags & tiny8086FlagCf) != 0;
  bool overflow = false;

  for (uint8_t index = 0; index < count; ++index)
  {
    uint32_t before = v;
    switch (operation)
    {
    case 0:
      carry = (v & signBit) != 0;
      v = ((v << 1) | (carry ? 1u : 0u)) & mask;
      overflow = ((v & signBit) != 0) != carry;
      break;
    case 1:
      carry = (v & 1) != 0;
      v = (v >> 1) | (carry ? signBit : 0u);
      overflow = ((v ^ (v << 1)) & signBit) != 0;
      break;
    case 2:
    {
      bool newCarry = (v & signBit) != 0;
      v = ((v << 1) | (carry ? 1u : 0u)) & mask;
      carry = newCarry;
      overflow = ((v & signBit) != 0) != carry;
      break;
    }
    case 3:
    {
      bool newCarry = (v & 1) != 0;
      v = (v >> 1) | (carry ? signBit : 0u);
      carry = newCarry;
      overflow = ((v ^ (v << 1)) & signBit) != 0;
      break;
    }
    case 5:
      carry = (v & 1) != 0;
      v >>= 1;
      overflow = (before & signBit) != 0;
      break;
    case 7:
      carry = (v & 1) != 0;
      v = (v >> 1) | (v & signBit);
      overflow = false;
      break;
    default:
      carry = (v & signBit) != 0;
      v = (v << 1) & mask;
      overflow = ((v & signBit) != 0) != carry;
      break;
    }
  }
  setFlag(cpu, tiny8086FlagCf, carry);
  setFlag(cpu, tiny8086FlagOf, overflow);
  if (operation >= 4)
  {
    setResultFlags(cpu, v, wide);
  }
  return (uint16_t)v;
}

//-- Group 3 /4../7: MUL, IMUL, DIV, IDIV on AL/AX. Divide errors raise INT 0.
static void multiplyDivide(tiny8086* cpu, uint8_t operation, uint16_t operand, bool wide)
{
  if (operation == 4 || operation == 5)
  {
    bool isSigned = operation == 5;
    if (wide)
    {
      uint32_t product =
          isSigned
              ? (uint32_t)((int32_t)(int16_t)cpu->reg[tiny8086RegAx] * (int32_t)(int16_t)operand)
              : (uint32_t)cpu->reg[tiny8086RegAx] * operand;
      cpu->reg[tiny8086RegAx] = (uint16_t)product;
      cpu->reg[tiny8086RegDx] = (uint16_t)(product >> 16);
      bool significant =
          isSigned ? (int32_t)product != (int32_t)(int16_t)product : cpu->reg[tiny8086RegDx] != 0;
      setFlag(cpu, tiny8086FlagCf | tiny8086FlagOf, significant);
    }
    else
    {
      uint8_t al = (uint8_t)cpu->reg[tiny8086RegAx];
      uint16_t product = isSigned ? (uint16_t)((int16_t)(int8_t)al * (int16_t)(int8_t)operand)
                                  : (uint16_t)(al * (uint8_t)operand);
      cpu->reg[tiny8086RegAx] = product;
      bool significant =
          isSigned ? (int16_t)product != (int16_t)(int8_t)product : (product >> 8) != 0;
      setFlag(cpu, tiny8086FlagCf | tiny8086FlagOf, significant);
    }
    return;
  }

  bool isSigned = operation == 7;
  if (operand == 0 || (!wide && isSigned && (int8_t)operand == 0))
  {
    raiseInterrupt(cpu, 0);
    return;
  }
  if (wide)
  {
    uint32_t dividend = ((uint32_t)cpu->reg[tiny8086RegDx] << 16) | cpu->reg[tiny8086RegAx];
    if (isSigned)
    {
      int32_t signedDividend = (int32_t)dividend;
      int32_t divisor = (int16_t)operand;
      int32_t quotient = signedDividend / divisor;
      int32_t remainder = signedDividend % divisor;
      if (signedDividend == INT32_MIN && divisor == -1)
      {
        raiseInterrupt(cpu, 0);
        return;
      }
      if (quotient > 0x7FFF || quotient < -0x7FFF)
      {
        raiseInterrupt(cpu, 0);
        return;
      }
      cpu->reg[tiny8086RegAx] = (uint16_t)quotient;
      cpu->reg[tiny8086RegDx] = (uint16_t)remainder;
    }
    else
    {
      uint32_t quotient = dividend / operand;
      if (quotient > 0xFFFF)
      {
        raiseInterrupt(cpu, 0);
        return;
      }
      cpu->reg[tiny8086RegAx] = (uint16_t)quotient;
      cpu->reg[tiny8086RegDx] = (uint16_t)(dividend % operand);
    }
  }
  else
  {
    uint16_t dividend = cpu->reg[tiny8086RegAx];
    if (isSigned)
    {
      int16_t divisor = (int8_t)operand;
      int16_t quotient = (int16_t)dividend / divisor;
      int16_t remainder = (int16_t)dividend % divisor;
      if (quotient > 0x7F || quotient < -0x7F)
      {
        raiseInterrupt(cpu, 0);
        return;
      }
      cpu->reg[tiny8086RegAx] = (uint16_t)(((uint8_t)remainder << 8) | (uint8_t)quotient);
    }
    else
    {
      uint16_t divisor = (uint8_t)operand;
      uint16_t quotient = dividend / divisor;
      if (quotient > 0xFF)
      {
        raiseInterrupt(cpu, 0);
        return;
      }
      cpu->reg[tiny8086RegAx] = (uint16_t)(((dividend % divisor) << 8) | quotient);
    }
  }
}

//-- MOVS, CMPS, STOS, LODS, SCAS with optional REP/REPZ/REPNZ. A REP runs inside
//-- this call (no external interrupt sources exist), which avoids re-decoding per iteration.
static void stringOperation(tiny8086* cpu, uint8_t opcode, int segmentOverride, int repeatMode)
{
  bool wide = (opcode & 1) != 0;
  uint8_t kind = opcode & 0xFE;
  uint16_t sourceSegment = cpu->seg[segmentOverride >= 0 ? segmentOverride : tiny8086SegDs];
  uint16_t extraSegment = cpu->seg[tiny8086SegEs];
  uint16_t delta = (cpu->flags & tiny8086FlagDf) ? (wide ? 0xFFFE : 0xFFFF) : (wide ? 2 : 1);

  for (;;)
  {
    if (repeatMode != 0 && cpu->reg[tiny8086RegCx] == 0)
    {
      break;
    }
    uint16_t si = cpu->reg[tiny8086RegSi];
    uint16_t di = cpu->reg[tiny8086RegDi];
    switch (kind)
    {
    case 0xA4:
      if (wide)
      {
        writeWord(cpu, extraSegment, di, readWord(cpu, sourceSegment, si));
      }
      else
      {
        writeByte(cpu, physicalAddress(extraSegment, di),
                  readByte(cpu, physicalAddress(sourceSegment, si)));
      }
      cpu->reg[tiny8086RegSi] = (uint16_t)(si + delta);
      cpu->reg[tiny8086RegDi] = (uint16_t)(di + delta);
      break;
    case 0xA6:
    {
      uint16_t a = wide ? readWord(cpu, sourceSegment, si)
                        : readByte(cpu, physicalAddress(sourceSegment, si));
      uint16_t b =
          wide ? readWord(cpu, extraSegment, di) : readByte(cpu, physicalAddress(extraSegment, di));
      (void)aluOperation(cpu, 7, a, b, wide);
      cpu->reg[tiny8086RegSi] = (uint16_t)(si + delta);
      cpu->reg[tiny8086RegDi] = (uint16_t)(di + delta);
      break;
    }
    case 0xAA:
      if (wide)
      {
        writeWord(cpu, extraSegment, di, cpu->reg[tiny8086RegAx]);
      }
      else
      {
        writeByte(cpu, physicalAddress(extraSegment, di), (uint8_t)cpu->reg[tiny8086RegAx]);
      }
      cpu->reg[tiny8086RegDi] = (uint16_t)(di + delta);
      break;
    case 0xAC:
      if (wide)
      {
        cpu->reg[tiny8086RegAx] = readWord(cpu, sourceSegment, si);
      }
      else
      {
        *reg8Pointer(cpu, 0) = readByte(cpu, physicalAddress(sourceSegment, si));
      }
      cpu->reg[tiny8086RegSi] = (uint16_t)(si + delta);
      break;
    default:
    {
      uint16_t memoryValue =
          wide ? readWord(cpu, extraSegment, di) : readByte(cpu, physicalAddress(extraSegment, di));
      uint16_t accumulator = wide ? cpu->reg[tiny8086RegAx] : (uint8_t)cpu->reg[tiny8086RegAx];
      (void)aluOperation(cpu, 7, accumulator, memoryValue, wide);
      cpu->reg[tiny8086RegDi] = (uint16_t)(di + delta);
      break;
    }
    }
    if (repeatMode == 0 || cpu->fault != tiny8086Ok)
    {
      break;
    }
    --cpu->reg[tiny8086RegCx];
    if (kind == 0xA6 || kind == 0xAE)
    {
      bool zero = (cpu->flags & tiny8086FlagZf) != 0;
      if ((repeatMode == 0xF3 && !zero) || (repeatMode == 0xF2 && zero))
      {
        break;
      }
    }
  }
}

tiny8086Result tiny8086Step(tiny8086* cpu)
{
  if (cpu == NULL)
  {
    return tiny8086InvalidArgument;
  }
  if (cpu->halted)
  {
    return tiny8086Halted;
  }

  cpu->fault = tiny8086Ok;
  uint16_t startIp = cpu->ip;
  int segmentOverride = -1;
  int repeatMode = 0;
  uint8_t opcode;

  //-- Prefix loop: segment overrides, LOCK, REPNZ/REPZ.
  for (;;)
  {
    opcode = fetchByte(cpu);
    if ((opcode & 0xE7) == 0x26)
    {
      segmentOverride = (opcode >> 3) & 3;
    }
    else if (opcode == 0xF2 || opcode == 0xF3)
    {
      repeatMode = opcode;
    }
    else if (opcode != 0xF0)
    {
      break;
    }
  }

  bool wide = (opcode & 1) != 0;
  modRmInfo modRm = {0};
  if (cpu->fault != tiny8086Ok)
  {
    return cpu->fault;
  }

  switch (opcode)
  {
  //-- ALU: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP in the six classic forms.
  case 0x00 ... 0x05:
  case 0x08 ... 0x0D:
  case 0x10 ... 0x15:
  case 0x18 ... 0x1D:
  case 0x20 ... 0x25:
  case 0x28 ... 0x2D:
  case 0x30 ... 0x35:
  case 0x38 ... 0x3D:
  {
    uint8_t operation = (opcode >> 3) & 7;
    uint8_t form = opcode & 7;
    if (form < 4)
    {
      decodeModRm(cpu, &modRm, segmentOverride);
      uint16_t rmValue = readRm(cpu, &modRm, wide);
      uint16_t regValue = readRegField(cpu, modRm.regField, wide);
      if (form < 2)
      {
        uint16_t result = aluOperation(cpu, operation, rmValue, regValue, wide);
        if (operation != 7)
        {
          writeRm(cpu, &modRm, result, wide);
        }
      }
      else
      {
        uint16_t result = aluOperation(cpu, operation, regValue, rmValue, wide);
        if (operation != 7)
        {
          writeRegField(cpu, modRm.regField, result, wide);
        }
      }
    }
    else
    {
      uint16_t immediate = wide ? fetchWord(cpu) : fetchByte(cpu);
      uint16_t accumulator = wide ? cpu->reg[tiny8086RegAx] : *reg8Pointer(cpu, 0);
      uint16_t result = aluOperation(cpu, operation, accumulator, immediate, wide);
      if (operation != 7)
      {
        writeRegField(cpu, 0, result, wide);
      }
    }
    break;
  }

  //-- PUSH/POP segment register (POP CS does not exist on the 8086).
  case 0x06:
  case 0x0E:
  case 0x16:
  case 0x1E:
    push16(cpu, cpu->seg[(opcode >> 3) & 3]);
    break;
  case 0x07:
  case 0x17:
  case 0x1F:
    cpu->seg[(opcode >> 3) & 3] = pop16(cpu);
    break;

  //-- INC/DEC r16 (CF is preserved).
  case 0x40 ... 0x4F:
  {
    uint8_t index = opcode & 7;
    bool decrement = opcode >= 0x48;
    bool savedCarry = (cpu->flags & tiny8086FlagCf) != 0;
    cpu->reg[index] = aluOperation(cpu, decrement ? 5 : 0, cpu->reg[index], 1, true);
    setFlag(cpu, tiny8086FlagCf, savedCarry);
    break;
  }

  case 0x50 ... 0x57:
    push16(cpu, cpu->reg[opcode & 7]);
    break;
  case 0x58 ... 0x5F:
    cpu->reg[opcode & 7] = pop16(cpu);
    break;

  //-- Jcc rel8
  case 0x70 ... 0x7F:
  {
    int8_t displacement = (int8_t)fetchByte(cpu);
    if (conditionTrue(cpu, opcode & 0x0F))
    {
      cpu->ip += (uint16_t)(int16_t)displacement;
    }
    break;
  }

  //-- Group 1: ALU r/m, immediate.
  case 0x80:
  case 0x81:
  case 0x82:
  case 0x83:
  {
    bool wideOperand = (opcode & 1) != 0;
    decodeModRm(cpu, &modRm, segmentOverride);
    uint16_t immediate;
    if (opcode == 0x81)
    {
      immediate = fetchWord(cpu);
    }
    else if (opcode == 0x83)
    {
      immediate = (uint16_t)(int16_t)(int8_t)fetchByte(cpu);
    }
    else
    {
      immediate = fetchByte(cpu);
    }
    uint16_t result =
        aluOperation(cpu, modRm.regField, readRm(cpu, &modRm, wideOperand), immediate, wideOperand);
    if (modRm.regField != 7)
    {
      writeRm(cpu, &modRm, result, wideOperand);
    }
    break;
  }

  //-- TEST r/m, r
  case 0x84:
  case 0x85:
    decodeModRm(cpu, &modRm, segmentOverride);
    (void)aluOperation(cpu, 4, readRm(cpu, &modRm, wide), readRegField(cpu, modRm.regField, wide),
                       wide);
    break;

  //-- XCHG r/m, r
  case 0x86:
  case 0x87:
  {
    decodeModRm(cpu, &modRm, segmentOverride);
    uint16_t rmValue = readRm(cpu, &modRm, wide);
    uint16_t regValue = readRegField(cpu, modRm.regField, wide);
    writeRm(cpu, &modRm, regValue, wide);
    writeRegField(cpu, modRm.regField, rmValue, wide);
    break;
  }

  //-- MOV r/m <-> r, MOV r/m <-> sreg, LEA
  case 0x88:
  case 0x89:
    decodeModRm(cpu, &modRm, segmentOverride);
    writeRm(cpu, &modRm, readRegField(cpu, modRm.regField, wide), wide);
    break;
  case 0x8A:
  case 0x8B:
    decodeModRm(cpu, &modRm, segmentOverride);
    writeRegField(cpu, modRm.regField, readRm(cpu, &modRm, wide), wide);
    break;
  case 0x8C:
    decodeModRm(cpu, &modRm, segmentOverride);
    writeRm(cpu, &modRm, cpu->seg[modRm.regField & 3], true);
    break;
  case 0x8E:
    decodeModRm(cpu, &modRm, segmentOverride);
    cpu->seg[modRm.regField & 3] = readRm(cpu, &modRm, true);
    break;
  case 0x8D:
    decodeModRm(cpu, &modRm, segmentOverride);
    if (modRm.isReg)
    {
      cpu->ip = startIp;
      return tiny8086InvalidInstruction;
    }
    cpu->reg[modRm.regField] = modRm.off;
    break;

  //-- NOP and XCHG AX, r16
  case 0x90:
    break;
  case 0x91 ... 0x97:
  {
    uint16_t temporary = cpu->reg[tiny8086RegAx];
    cpu->reg[tiny8086RegAx] = cpu->reg[opcode & 7];
    cpu->reg[opcode & 7] = temporary;
    break;
  }

  case 0x98:
    cpu->reg[tiny8086RegAx] = (uint16_t)(int16_t)(int8_t)cpu->reg[tiny8086RegAx];
    break;
  case 0x99:
    cpu->reg[tiny8086RegDx] = (cpu->reg[tiny8086RegAx] & 0x8000) ? 0xFFFF : 0;
    break;

  case 0x9C:
    push16(cpu, cpu->flags);
    break;
  case 0x9D:
    cpu->flags = (uint16_t)(pop16(cpu) | 0x0002);
    break;

  //-- MOV AL/AX <-> [moffs16]
  case 0xA0:
  case 0xA1:
  {
    uint16_t offset = fetchWord(cpu);
    uint16_t segment = cpu->seg[segmentOverride >= 0 ? segmentOverride : tiny8086SegDs];
    cpu->reg[tiny8086RegAx] = wide ? readWord(cpu, segment, offset)
                                   : (uint16_t)((cpu->reg[tiny8086RegAx] & 0xFF00) |
                                                readByte(cpu, physicalAddress(segment, offset)));
    break;
  }
  case 0xA2:
  case 0xA3:
  {
    uint16_t offset = fetchWord(cpu);
    uint16_t segment = cpu->seg[segmentOverride >= 0 ? segmentOverride : tiny8086SegDs];
    if (wide)
    {
      writeWord(cpu, segment, offset, cpu->reg[tiny8086RegAx]);
    }
    else
    {
      writeByte(cpu, physicalAddress(segment, offset), (uint8_t)cpu->reg[tiny8086RegAx]);
    }
    break;
  }

  //-- MOVSB/MOVSW with optional REP.
  case 0xA4:
  case 0xA5:
    stringOperation(cpu, opcode, segmentOverride, repeatMode);
    break;

  //-- TEST AL/AX, imm
  case 0xA8:
  case 0xA9:
  {
    uint16_t immediate = wide ? fetchWord(cpu) : fetchByte(cpu);
    (void)aluOperation(cpu, 4, wide ? cpu->reg[tiny8086RegAx] : *reg8Pointer(cpu, 0), immediate,
                       wide);
    break;
  }

  //-- MOV r, imm
  case 0xB0 ... 0xB7:
    *reg8Pointer(cpu, opcode & 7) = fetchByte(cpu);
    break;
  case 0xB8 ... 0xBF:
    cpu->reg[opcode & 7] = fetchWord(cpu);
    break;

  //-- Near/far RET
  case 0xC2:
  {
    uint16_t release = fetchWord(cpu);
    cpu->ip = pop16(cpu);
    cpu->reg[tiny8086RegSp] += release;
    break;
  }
  case 0xC3:
    cpu->ip = pop16(cpu);
    break;
  case 0xCA:
  {
    uint16_t release = fetchWord(cpu);
    cpu->ip = pop16(cpu);
    cpu->seg[tiny8086SegCs] = pop16(cpu);
    cpu->reg[tiny8086RegSp] += release;
    break;
  }
  case 0xCB:
    cpu->ip = pop16(cpu);
    cpu->seg[tiny8086SegCs] = pop16(cpu);
    break;

  //-- MOV r/m, imm
  case 0xC6:
  case 0xC7:
    decodeModRm(cpu, &modRm, segmentOverride);
    writeRm(cpu, &modRm, wide ? fetchWord(cpu) : fetchByte(cpu), wide);
    break;

  //-- INT3 / INT n / IRET
  case 0xCC:
    raiseInterrupt(cpu, 3);
    break;
  case 0xCD:
  {
    uint8_t vector = fetchByte(cpu);
    raiseInterrupt(cpu, vector);
    break;
  }
  case 0xCF:
    cpu->ip = pop16(cpu);
    cpu->seg[tiny8086SegCs] = pop16(cpu);
    cpu->flags = (uint16_t)(pop16(cpu) | 0x0002);
    break;

  //-- LOOP / LOOPZ / LOOPNZ / JCXZ
  case 0xE0 ... 0xE3:
  {
    int8_t displacement = (int8_t)fetchByte(cpu);
    bool take;
    if (opcode == 0xE3)
    {
      take = cpu->reg[tiny8086RegCx] == 0;
    }
    else
    {
      take = --cpu->reg[tiny8086RegCx] != 0;
      if (opcode == 0xE1)
      {
        take = take && (cpu->flags & tiny8086FlagZf) != 0;
      }
      else if (opcode == 0xE0)
      {
        take = take && (cpu->flags & tiny8086FlagZf) == 0;
      }
    }
    if (take)
    {
      cpu->ip += (uint16_t)(int16_t)displacement;
    }
    break;
  }

  //-- IN/OUT. Only byte ports are supported; word access is a deterministic fault.
  case 0xE4:
    *reg8Pointer(cpu, 0) = portInByte(cpu, fetchByte(cpu));
    break;
  case 0xE6:
    portOutByte(cpu, fetchByte(cpu), (uint8_t)cpu->reg[tiny8086RegAx]);
    break;
  case 0xEC:
    *reg8Pointer(cpu, 0) = portInByte(cpu, cpu->reg[tiny8086RegDx]);
    break;
  case 0xEE:
    portOutByte(cpu, cpu->reg[tiny8086RegDx], (uint8_t)cpu->reg[tiny8086RegAx]);
    break;
  case 0xE5:
  case 0xE7:
  case 0xED:
  case 0xEF:
    CPU_FAULT(cpu, tiny8086IoFault);
    break;

  //-- CALL / JMP
  case 0xE8:
  {
    uint16_t displacement = fetchWord(cpu);
    push16(cpu, cpu->ip);
    cpu->ip += displacement;
    break;
  }
  case 0xE9:
  {
    uint16_t displacement = fetchWord(cpu);
    cpu->ip += displacement;
    break;
  }
  case 0xEA:
  {
    uint16_t newIp = fetchWord(cpu);
    cpu->seg[tiny8086SegCs] = fetchWord(cpu);
    cpu->ip = newIp;
    break;
  }
  case 0xEB:
  {
    int8_t displacement = (int8_t)fetchByte(cpu);
    cpu->ip += (uint16_t)(int16_t)displacement;
    break;
  }

  //-- HLT and flag instructions
  case 0xF4:
    cpu->halted = true;
    break;
  case 0xF5:
    cpu->flags ^= tiny8086FlagCf;
    break;
  case 0xF8:
    setFlag(cpu, tiny8086FlagCf, false);
    break;
  case 0xF9:
    setFlag(cpu, tiny8086FlagCf, true);
    break;
  case 0xFA:
    setFlag(cpu, tiny8086FlagIf, false);
    break;
  case 0xFB:
    setFlag(cpu, tiny8086FlagIf, true);
    break;
  case 0xFC:
    setFlag(cpu, tiny8086FlagDf, false);
    break;
  case 0xFD:
    setFlag(cpu, tiny8086FlagDf, true);
    break;

  //-- 80186 additions the previous core accepted (PUSHA/POPA, PUSH imm, IMUL imm,
  //-- shift by imm8, ENTER/LEAVE); kept so existing guest code and tests keep working.
  case 0x60:
  {
    uint16_t originalSp = cpu->reg[tiny8086RegSp];
    for (uint8_t index = 0; index < 8; ++index)
    {
      push16(cpu, index == tiny8086RegSp ? originalSp : cpu->reg[index]);
    }
    break;
  }
  case 0x61:
    for (int index = 7; index >= 0; --index)
    {
      uint16_t value = pop16(cpu);
      if (index != tiny8086RegSp)
      {
        cpu->reg[index] = value;
      }
    }
    break;
  case 0x68:
    push16(cpu, fetchWord(cpu));
    break;
  case 0x6A:
    push16(cpu, (uint16_t)(int16_t)(int8_t)fetchByte(cpu));
    break;
  case 0x69:
  case 0x6B:
  {
    decodeModRm(cpu, &modRm, segmentOverride);
    int32_t multiplicand = (int16_t)readRm(cpu, &modRm, true);
    int32_t multiplier = opcode == 0x69 ? (int16_t)fetchWord(cpu) : (int8_t)fetchByte(cpu);
    int32_t product = multiplicand * multiplier;
    cpu->reg[modRm.regField] = (uint16_t)product;
    setFlag(cpu, tiny8086FlagCf | tiny8086FlagOf, product != (int16_t)product);
    break;
  }
  case 0xC0:
  case 0xC1:
  {
    decodeModRm(cpu, &modRm, segmentOverride);
    uint8_t count = fetchByte(cpu);
    if (count != 0)
    {
      writeRm(cpu, &modRm,
              shiftOperation(cpu, modRm.regField, readRm(cpu, &modRm, wide), count, wide), wide);
    }
    break;
  }
  case 0xC9:
    cpu->reg[tiny8086RegSp] = cpu->reg[tiny8086RegBp];
    cpu->reg[tiny8086RegBp] = pop16(cpu);
    break;
  case 0xC8:
  {
    uint16_t frameSize = fetchWord(cpu);
    uint8_t level = fetchByte(cpu) & 0x1F;
    push16(cpu, cpu->reg[tiny8086RegBp]);
    uint16_t framePointer = cpu->reg[tiny8086RegSp];
    for (uint8_t index = 1; index < level; ++index)
    {
      cpu->reg[tiny8086RegBp] -= 2;
      push16(cpu, readWord(cpu, cpu->seg[tiny8086SegSs], cpu->reg[tiny8086RegBp]));
    }
    if (level > 0)
    {
      push16(cpu, framePointer);
    }
    cpu->reg[tiny8086RegBp] = framePointer;
    cpu->reg[tiny8086RegSp] -= frameSize;
    break;
  }

  //-- BCD adjust
  case 0x27:
  case 0x2F:
  {
    bool subtract = opcode == 0x2F;
    uint8_t al = (uint8_t)cpu->reg[tiny8086RegAx];
    uint8_t oldAl = al;
    bool oldCarry = (cpu->flags & tiny8086FlagCf) != 0;
    bool carry = false;
    if ((al & 0x0F) > 9 || (cpu->flags & tiny8086FlagAf))
    {
      uint16_t adjusted = subtract ? (uint16_t)(al - 6) : (uint16_t)(al + 6);
      carry = oldCarry || (adjusted & 0x100) != 0;
      al = (uint8_t)adjusted;
      setFlag(cpu, tiny8086FlagAf, true);
    }
    else
    {
      setFlag(cpu, tiny8086FlagAf, false);
    }
    if (oldAl > 0x99 || oldCarry)
    {
      al = subtract ? (uint8_t)(al - 0x60) : (uint8_t)(al + 0x60);
      carry = true;
    }
    setFlag(cpu, tiny8086FlagCf, carry);
    setResultFlags(cpu, al, false);
    *reg8Pointer(cpu, 0) = al;
    break;
  }
  case 0x37:
  case 0x3F:
  {
    bool subtract = opcode == 0x3F;
    uint8_t al = (uint8_t)cpu->reg[tiny8086RegAx];
    uint8_t* ah = reg8Pointer(cpu, 4);
    bool adjust = (al & 0x0F) > 9 || (cpu->flags & tiny8086FlagAf);
    if (adjust)
    {
      al = subtract ? (uint8_t)(al - 6) : (uint8_t)(al + 6);
      *ah = subtract ? (uint8_t)(*ah - 1) : (uint8_t)(*ah + 1);
    }
    setFlag(cpu, tiny8086FlagAf, adjust);
    setFlag(cpu, tiny8086FlagCf, adjust);
    *reg8Pointer(cpu, 0) = al & 0x0F;
    break;
  }

  //-- POP r/m
  case 0x8F:
  {
    uint16_t value = pop16(cpu);
    decodeModRm(cpu, &modRm, segmentOverride);
    writeRm(cpu, &modRm, value, true);
    break;
  }

  //-- CALL far, WAIT, SAHF, LAHF
  case 0x9A:
  {
    uint16_t newIp = fetchWord(cpu);
    uint16_t newCs = fetchWord(cpu);
    push16(cpu, cpu->seg[tiny8086SegCs]);
    push16(cpu, cpu->ip);
    cpu->seg[tiny8086SegCs] = newCs;
    cpu->ip = newIp;
    break;
  }
  case 0x9B:
    break;
  case 0x9E:
    cpu->flags = (uint16_t)((cpu->flags & 0xFF00) | (*reg8Pointer(cpu, 4) & 0xD5) | 0x02);
    break;
  case 0x9F:
    *reg8Pointer(cpu, 4) = (uint8_t)((cpu->flags & 0xD5) | 0x02);
    break;

  //-- CMPS, STOS, LODS, SCAS
  case 0xA6:
  case 0xA7:
  case 0xAA:
  case 0xAB:
  case 0xAC:
  case 0xAD:
  case 0xAE:
  case 0xAF:
    stringOperation(cpu, opcode, segmentOverride, repeatMode);
    break;

  //-- LES / LDS
  case 0xC4:
  case 0xC5:
    decodeModRm(cpu, &modRm, segmentOverride);
    if (modRm.isReg)
    {
      cpu->ip = startIp;
      return tiny8086InvalidInstruction;
    }
    cpu->reg[modRm.regField] = readWord(cpu, modRm.seg, modRm.off);
    cpu->seg[opcode == 0xC4 ? tiny8086SegEs : tiny8086SegDs] =
        readWord(cpu, modRm.seg, (uint16_t)(modRm.off + 2));
    break;

  //-- INTO
  case 0xCE:
    if (cpu->flags & tiny8086FlagOf)
    {
      raiseInterrupt(cpu, 4);
    }
    break;

  //-- Shifts and rotates by 1 or CL
  case 0xD0 ... 0xD3:
  {
    decodeModRm(cpu, &modRm, segmentOverride);
    uint8_t count = opcode >= 0xD2 ? (uint8_t)cpu->reg[tiny8086RegCx] : 1;
    uint16_t value = readRm(cpu, &modRm, wide);
    if (count != 0)
    {
      value = shiftOperation(cpu, modRm.regField, value, count, wide);
      writeRm(cpu, &modRm, value, wide);
    }
    break;
  }

  //-- AAM / AAD
  case 0xD4:
  {
    uint8_t base = fetchByte(cpu);
    if (base == 0)
    {
      raiseInterrupt(cpu, 0);
      break;
    }
    uint8_t al = (uint8_t)cpu->reg[tiny8086RegAx];
    *reg8Pointer(cpu, 4) = al / base;
    *reg8Pointer(cpu, 0) = al % base;
    setResultFlags(cpu, al % base, false);
    break;
  }
  case 0xD5:
  {
    uint8_t base = fetchByte(cpu);
    uint8_t result = (uint8_t)(*reg8Pointer(cpu, 4) * base + *reg8Pointer(cpu, 0));
    *reg8Pointer(cpu, 0) = result;
    *reg8Pointer(cpu, 4) = 0;
    setResultFlags(cpu, result, false);
    break;
  }

  //-- XLAT
  case 0xD7:
  {
    uint16_t segment = cpu->seg[segmentOverride >= 0 ? segmentOverride : tiny8086SegDs];
    uint16_t offset = (uint16_t)(cpu->reg[tiny8086RegBx] + *reg8Pointer(cpu, 0));
    *reg8Pointer(cpu, 0) = readByte(cpu, physicalAddress(segment, offset));
    break;
  }

  //-- ESC (coprocessor): operand is decoded and ignored
  case 0xD8 ... 0xDF:
    decodeModRm(cpu, &modRm, segmentOverride);
    break;

  //-- Group 3
  case 0xF6:
  case 0xF7:
  {
    decodeModRm(cpu, &modRm, segmentOverride);
    uint16_t value = readRm(cpu, &modRm, wide);
    switch (modRm.regField)
    {
    case 0:
    case 1:
      (void)aluOperation(cpu, 4, value, wide ? fetchWord(cpu) : fetchByte(cpu), wide);
      break;
    case 2:
      writeRm(cpu, &modRm, (uint16_t)~value, wide);
      break;
    case 3:
      writeRm(cpu, &modRm, aluOperation(cpu, 5, 0, value, wide), wide);
      break;
    default:
      multiplyDivide(cpu, modRm.regField, value, wide);
      break;
    }
    break;
  }

  //-- Group 4/5
  case 0xFE:
  case 0xFF:
  {
    decodeModRm(cpu, &modRm, segmentOverride);
    switch (modRm.regField)
    {
    case 0:
    case 1:
    {
      bool savedCarry = (cpu->flags & tiny8086FlagCf) != 0;
      uint16_t result =
          aluOperation(cpu, modRm.regField == 0 ? 0 : 5, readRm(cpu, &modRm, wide), 1, wide);
      setFlag(cpu, tiny8086FlagCf, savedCarry);
      writeRm(cpu, &modRm, result, wide);
      break;
    }
    case 2:
    {
      uint16_t target = readRm(cpu, &modRm, true);
      push16(cpu, cpu->ip);
      cpu->ip = target;
      break;
    }
    case 3:
    case 5:
    {
      if (modRm.isReg)
      {
        cpu->ip = startIp;
        return tiny8086InvalidInstruction;
      }
      uint16_t newIp = readWord(cpu, modRm.seg, modRm.off);
      uint16_t newCs = readWord(cpu, modRm.seg, (uint16_t)(modRm.off + 2));
      if (modRm.regField == 3)
      {
        push16(cpu, cpu->seg[tiny8086SegCs]);
        push16(cpu, cpu->ip);
      }
      cpu->seg[tiny8086SegCs] = newCs;
      cpu->ip = newIp;
      break;
    }
    case 4:
      cpu->ip = readRm(cpu, &modRm, true);
      break;
    case 6:
      push16(cpu, readRm(cpu, &modRm, true));
      break;
    default:
      cpu->ip = startIp;
      return tiny8086InvalidInstruction;
    }
    if (opcode == 0xFE && modRm.regField > 1)
    {
      cpu->ip = startIp;
      return tiny8086InvalidInstruction;
    }
    break;
  }

  default:
    cpu->ip = startIp;
    return tiny8086InvalidInstruction;
  }

  if (cpu->fault != tiny8086Ok)
  {
    return cpu->fault;
  }
  ++cpu->instructionCount;
  return cpu->halted ? tiny8086Halted : tiny8086Ok;
}

tiny8086Result tiny8086Run(tiny8086* cpu, size_t instructionBudget, size_t* instructionsExecuted)
{
  size_t executed = 0;
  tiny8086Result result = tiny8086Ok;

  while (executed < instructionBudget)
  {
    result = tiny8086Step(cpu);
    if (result == tiny8086Ok)
    {
      ++executed;
      continue;
    }
    if (result == tiny8086Halted && cpu->halted && cpu->fault == tiny8086Ok)
    {
      ++executed;
    }
    break;
  }
  if (instructionsExecuted != NULL)
  {
    *instructionsExecuted = executed;
  }
  return result;
}
