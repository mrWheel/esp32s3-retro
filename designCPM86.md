# CP/M-86 Machine Design

## 1. Purpose and scope

This document is the complete technical design of the RETRO86_V1 CP/M-86 guest machine
hosted by the ESP32-S3 Retro Computer. It defines the architecture, the project-owned
8086 CPU core, the guest-visible hardware, the boot system and exact load addresses, the
guest BIOS, the virtual I/O port contract, the disk formats and drive configuration, the
console contract, and the testing required to prove a correct implementation.

The implementation in `components/cpm86Core/` (including `tiny8086/` and
`bios/retro86bios.a86`), `main/cpm86Machine.c`, `main/cpm86BiosOverlay.c`,
`main/cpm86DriveConfig.c` and their headers is the normative reference. Where this
document and the code appear to differ, the code is leading and this document must be
corrected.

A senior programmer who reads this document plus [projectPrompt.md](./projectPrompt.md)
(common host services: menu, console, LittleFS, SD, image I/O, disk-activity LED) must be
able to implement an equivalent emulator without further interview.

### Requirement levels

- **MUST** — mandatory; an implementation that violates it is non-conforming.
- **SHOULD** — mandatory unless a documented, explicit deviation is accepted.
- **MAY** — optional.

## 2. Design goals

1. Execute genuine Digital Research CP/M-86 1.1 CCP/BDOS binaries unmodified on a
   project-owned 8086 core inside a 640 KiB populated real-mode address space.
2. Provide the BIOS as guest-resident 8086 code loaded from a separate Intel-Hex overlay,
   plus a narrow set of virtual byte-I/O ports; the host never substitutes its own BDOS
   and never presents the host FAT filesystem as a guest filesystem.
3. Support up to six CP/M drives (A:–F:) with independent image file, profile and access
   mode; A: boots from LittleFS, B:–F: from the SD card.
4. Keep the CPU core and core adapter free of ESP-IDF dependencies so they compile and
   run natively for host-side testing; all filesystem, console and indicator ownership
   stays in the machine adapter.
5. Cooperate with FreeRTOS: bounded instruction batches and time-sliced cooperative
   yields, so the task scheduler and watchdog are serviced during guest execution.
6. Reproducible media: every image the firmware accepts is produced by the checked-in
   host tools with byte-deterministic output.

Non-goals: IBM PC/XT or OEM hardware compatibility, graphics adapters, DOS, protected
mode, 8087 emulation, cycle-accurate timing, interrupt-driven peripherals, and terminal
emulation inside the host console.

## 3. Responsibility boundary

| Layer | Owner | Provides |
| --- | --- | --- |
| Menu, machine registry, probe states | common host | Selection, lifecycle scheduling |
| USB Serial/JTAG byte-stream console | common host | `hostConsoleCharAvailable/PeekChar/GetChar/PutChar` |
| LittleFS mount, SD/FatFS mount, path resolver | common host | VFS paths, `storageResolveRetroPath`, `storageReady` |
| Virtual image random access | common host | `imageOpen/imageReadAt/imageWriteAt/imageFlush/imageClose/imageSize`, `resourceSize` |
| Disk-activity RGB indicator (GPIO 38) | common host | `diskActivityRead()` green, `diskActivityWrite()` red, off 120 ms after last activity |
| 8086 core, adapter, guest BIOS, drives | CP/M-86 machine | Everything below this line |

The CP/M-86 machine MUST NOT start Wi-Fi or any HTTP server, MUST NOT open the console or
mount filesystems itself, and MUST NOT be active at the same time as the browser File
Transfer service (enforced by the common host menu).

## 4. Architecture and module boundaries

```text
machine registry (main/machineRegistry.c)
  -> cpm86MachineProbe / cpm86MachineInitialize / cpm86MachineRun   main/cpm86Machine.c
       -> cpm86DriveConfigLoad / profile resolution                 main/cpm86DriveConfig.c
       -> cpm86BiosLoadOverlay (Intel-Hex H86 loader)               main/cpm86BiosOverlay.c
       -> imageFile random-access I/O + diskActivity                (common host)
       -> hostConsole                                               (common host)
       -> cpm86Core adapter                                         components/cpm86Core/cpm86Core.c
            -> tiny8086 interpreter                                 components/cpm86Core/tiny8086/
            -> RETRO86_V1 guest BIOS (H86 overlay in guest RAM)     components/cpm86Core/bios/retro86bios.a86
```

| Module | Responsibility |
| --- | --- |
| `main/cpm86Machine.c` | Registry callbacks; open/close images; port handlers (console, disk, PC BIOS stub installation); guest RAM allocation size; run batches; fault logging. |
| `main/cpm86DriveConfig.c/.h` | Parse and validate `drives.cfg`; A: profile resolution from image size; path confinement; the `cpm86DriveRecordValid` geometry gate. |
| `main/cpm86BiosOverlay.c/.h` | Load the H86 BIOS overlay into guest RAM with record/checksum validation. |
| `components/cpm86Core/cpm86Core.c` | Own the guest RAM allocation; wire tiny8086 hooks; serve the `RETRO86_TIMER_B1` timer ports; enforce the single-active-core rule; map core results. |
| `components/cpm86Core/tiny8086/` | Project-owned 8086 interpreter; flat RAM array; 20-bit physical wrap; byte port hooks; fault context capture. |
| `components/cpm86Core/bios/retro86bios.a86` | ASM-86 source of the RETRO86_V1 guest BIOS (assembled to `littlefs/cpm86/retro86bios.h86`). |

The core and adapter MUST NOT include ESP-IDF headers except through the conditional
allocation block described in §7, and MUST NOT call filesystem or console APIs directly —
only through the `cpm86CoreConfig` callbacks.

## 5. Guest resources and provenance

| Resource | Provenance | Use |
| --- | --- | --- |
| `tiny8086` CPU core | Project-owned (`components/cpm86Core/tiny8086/`), 8086tiny-style switch interpreter; no third-party license applies. | Instruction interpreter. |
| `littlefs/cpm86/cpm.sys` | CMD-format CP/M-86 1.1 system file (10,240 bytes). CCP/BDOS from `tsupplis/cpm86-kernel` at commit `00927e17f43ea4241cc0809531cf68ce835e9c16`; its bundled IBM PC BIOS is NOT linked in. Rights per that repository's `LICENSE.md` and the user's non-commercial-use confirmation. | Genuine guest operating system. |
| `littlefs/cpm86/retro86bios.h86` | Project-authored; assembled with ASM-86 1.1N from `components/cpm86Core/bios/retro86bios.a86`. | RETRO86_V1 BIOS overlay. |
| `littlefs/cpm86/system.dsk` | Built by `tools/diskManager.py --type SYSTEMDSK --os cpm86` from `bootDisks/cpm86/systemDsk` (native `.CMD` utilities: `ASM86`, `ED`, `GENCMD`, `PIP`, `STAT`, `HELP`, `BENCH86`, `TIMER86`, `WBOOT86`, `BAD86`, …). | A: system disk. |
| Reference manuals | CP/M-86 System Guide June 1981 (BIOS entry table Table 5-1 p.57, DPB Tables 6-2/6-4/6-5 pp.67-71, memory region table and segment-aware DMA Table 5-4 pp.64-65, BDOS argument rule p.23), Programmer's Guide June 1981, CP/M-86 1.1 release notes 1982. | ABI authority; page audit recorded in `docs/cpm86/abiAudit.md`. |

`GENCMD` and `ASM86` binary provenance is recorded as UNKNOWN in `docs/cpm86/abiAudit.md`;
review rights before redistribution. Preserve upstream provenance statements in-tree.

## 6. CPU core (`tiny8086`)

Exact interface (see `components/cpm86Core/tiny8086/tiny8086.h`):

```c
enum { tiny8086AddressMask = 0x000FFFFF };

typedef enum
{
  tiny8086Ok, tiny8086Halted, tiny8086InvalidInstruction,
  tiny8086MemoryFault, tiny8086IoFault, tiny8086InvalidArgument
} tiny8086Result;

typedef struct
{
  uint8_t* memory;
  uint32_t memorySize;
  tiny8086PortReadHook portRead;
  tiny8086PortWriteHook portWrite;
  tiny8086IntHook intHook;
  void* hookContext;
} tiny8086Config;

tiny8086Result tiny8086Init(tiny8086* cpu, const tiny8086Config* config);
void tiny8086Reset(tiny8086* cpu);
tiny8086Result tiny8086Step(tiny8086* cpu);
tiny8086Result tiny8086Run(tiny8086* cpu, size_t instructionBudget, size_t* instructionsExecuted);
bool tiny8086ReadMemory(const tiny8086* cpu, uint32_t address, void* data, size_t length);
bool tiny8086WriteMemory(tiny8086* cpu, uint32_t address, const void* data, size_t length);
```

Requirements:

1. Guest memory is one flat byte array of `memorySize` bytes (1 … 1 MiB);
   `tiny8086Init` rejects `NULL` cpu/config/memory, zero size, or a size above
   `tiny8086AddressMask + 1`. Hooks return `false` to signal an unsupported port
   (→ `tiny8086IoFault`).
2. Real-mode addressing: `physicalAddress = ((segment << 4) + offset) & 0xFFFFF`, computed
   in a 32-bit host integer. Word accesses at offset `0xFFFF` wrap the offset inside the
   segment and take the high byte at offset 0 (and correspondingly at the 20-bit
   boundary); the two bytes are read/written independently.
3. Data accesses above the populated RAM behave like an empty bus: reads return `$FF`,
   writes are dropped. Only an **instruction fetch** from above populated RAM is a
   `tiny8086MemoryFault` (the fault records the physical address and `faultMemWrite =
   false`).
4. Byte I/O only: `E4/E6` (immediate port) and `EC/EE` (DX port) call the port hooks.
   `E5/E7/ED/EF` (word I/O) fault deterministically with `tiny8086IoFault` and record the
   DX port, direction and AL value for diagnostics.
5. Software interrupts dispatch through the guest IVT at physical `vector*4`
   (push flags with bit 1 forced to 1, push CS, push IP, clear IF and TF, load SS:SP
   untouched, load new CS:IP). `intHook` is `NULL` in the production core; the hook
   exists so a host could take over an INT and continue after it.
6. Reset (`tiny8086Reset`): all GPRs and segment registers 0, `SP = $FFFE`, `IP = 0`,
   `flags = $0200` (IF only), `instructionCount = 0`, `halted = false`, fault state
   cleared.
7. `tiny8086Run` executes at most `instructionBudget` instructions and stops early on
   halt or fault. A HLT executed as the batch's last instruction counts as executed and
   returns `tiny8086Halted`. `tiny8086Step` is `tiny8086Run(cpu, 1, …)`.
8. Fault context is captured on the first fault of an instruction: `faultStartIp` (the
   offset of the faulting instruction in CS), `faultMemAddr`/`faultMemWrite`, and
   `faultIoPort`/`faultIoValue`/`faultIoRead`. On an invalid opcode the core restores
   `ip = startIp` and returns `tiny8086InvalidInstruction` without recording a fault.
9. The instruction set is full 8086 plus the 80186 opcodes exercised by the test vectors
   (`PUSHA/POPA`, `INS/OUTS`, `PUSH SP` = FF /6, `INTO`, `BOUND`, `SAHF/LAHF`, `XLAT`,
   `DAA`, `AAM`-family and shift/rotate groups). Divide errors raise `INT 0`; `INT3`
   raises `INT 3`; `INTO` raises `INT 4`; `INT n` reads the vector byte from the code
   stream.
10. Optimisation requirements for the ESP32-S3 build (functional on other hosts):
    `tiny8086.c` compiles with `-O2 -fjump-tables -ftree-switch-conversion
    -fno-bit-tests --param=jump-table-max-growth-ratio-for-speed=2000` (ESP-IDF defaults
    to `-fno-jump-tables`; the 256-way opcode switch needs its jump table), and the
    `CPM86_TINY8086_IRAM` Kconfig option (default `y`) maps the whole `tiny8086` object
    `noflash` into internal RAM via the component linker fragment. The interpreter
    assumes a little-endian host (byte registers alias `reg[]`).

### Declared RETRO86_V1 CPU profile rules

These behaviours are profile rules of the core, not open bugs. Guests MUST NOT depend on
behaviours marked "not modelled"; conformance claims MUST NOT assume full-chip semantics
beyond this list.

1. Trap flag: TF is storable (PUSHF/POPF/IRET and INT frames preserve it) but single-step
   is not implemented; setting TF has no architectural effect.
2. External interrupts: INTR/NMI are not modelled and no interrupt source exists (the
   production core has no `intHook`); IF is storable but inert. HLT is sticky: only a
   core reset/relaunch clears it; there is no wake-up path.
3. `LOCK` (F0h) is accepted and ignored (single core, no shared bus). REP/REPZ/REPNZ and
   segment prefixes are honoured; repeated prefixes of the same class keep the last one.
4. ESC (D8h–DFh) decodes the ModRM operand (including displacement) and ignores the
   operation; no 8087 state exists.
5. `PUSHA/POPA` and `PUSH SP` (FF /6) use the pre-decrement original SP value
   (80186-like), matching the guest binaries' expectations.
6. Shift/rotate counts come from the full CL (no 5-bit masking, 8086 rule); a count of
   zero executes no rotate and leaves all flags unchanged. `D0/D1/C0/C1` counts behave
   the same way.
7. Reserved flags: bit 1 always reads as 1 through PUSHF/POPF/IRET/INT frames
   (`flags | 0x0002` on push, `popped | 0x0002` on pop). The 8086's always-1 bits
   12..15 are not modelled (they read and write as 0); guest code MUST NOT test them.
   Bit 5 is stored like an ordinary undefined bit.
8. REP runs to completion inside one core call; because no asynchronous interrupt source
   exists this cannot observe or delay an interrupt. If external interrupts or HLT
   wake-up are ever added (rule 2), REP chunking MUST be revisited.
9. `INC/DEC` preserve CF; the core saves CF around the shared ALU path and restores it.

## 7. Core adapter (`cpm86Core`)

Exact interface (see `components/cpm86Core/include/cpm86Core.h`):

```c
enum { cpm86AddressSpaceSize = 0x100000 };

typedef struct
{
  size_t ramSize;
  cpm86PortRead portRead;
  cpm86PortWrite portWrite;
  void* portContext;
  hostReadMilliseconds readMilliseconds;
  void* clockContext;
} cpm86CoreConfig;

typedef enum
{
  cpm86CoreOk, cpm86CoreInvalidArgument, cpm86CoreBusy, cpm86CoreNoMemory,
  cpm86CoreInvalidState, cpm86CoreMemoryRange, cpm86CoreMemoryFault,
  cpm86CoreInvalidInstruction, cpm86CoreIoError, cpm86CoreHalted
} cpm86CoreResult;

cpm86CoreResult cpm86CoreCreate(cpm86Core** core, const cpm86CoreConfig* config);
void cpm86CoreDestroy(cpm86Core* core);
cpm86CoreResult cpm86CoreReset(cpm86Core* core);
cpm86CoreResult cpm86CoreSetEntry(cpm86Core* core, uint16_t segment, uint16_t offset);
cpm86CoreResult cpm86CoreLoad(cpm86Core* core, uint32_t address, const void* data, size_t length);
cpm86CoreResult cpm86CoreRead(cpm86Core* core, uint32_t address, void* data, size_t length);
cpm86CoreResult cpm86CoreWrite(cpm86Core* core, uint32_t address, const void* data, size_t length);
cpm86CoreResult cpm86CoreStep(cpm86Core* core);
cpm86CoreResult cpm86CoreRun(cpm86Core* core, size_t instructionBudget, size_t* instructionsExecuted);
cpm86CoreResult cpm86CoreGetProgramCounter(const cpm86Core* core, uint16_t* segment, uint16_t* offset);
uint64_t cpm86CoreInstructionCount(const cpm86Core* core);
cpm86CoreResult cpm86CoreGetFaultInfo(const cpm86Core* core, cpm86CoreFaultInfo* info);
```

Requirements:

1. `cpm86CoreCreate` rejects `NULL` core pointer (`cpm86CoreInvalidArgument`), `NULL`
   config, `ramSize == 0` or `ramSize > 0x100000` (`cpm86CoreInvalidArgument`), and a
   second live core (`cpm86CoreBusy` — a file-static `activeCore` enforces the
   one-active-machine contract; `cpm86CoreDestroy` releases it). The core struct is
   `calloc`ed; guest RAM is allocated on ESP-IDF first from PSRAM
   (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`), falling back to internal RAM
   (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`), and with `malloc` on the native host —
   RAM is NOT zero-initialised, so a boot sequence MUST load all code, vectors and data
   it reads; allocation failure frees all resources and returns `cpm86CoreNoMemory`.
   `cpm86CoreWrite` is an alias of `cpm86CoreLoad`.
2. `cpm86CoreLoad/Read/Write` reject `NULL` data with non-zero length and any range that
   would leave the populated RAM (`address > ramSize || length > ramSize - address` →
   `cpm86CoreMemoryRange`). Guest addresses are validated, never cast to host pointers.
3. `cpm86CoreReset` resets the CPU and zeroes the millisecond snapshot.
   `cpm86CoreSetEntry` sets `CS` and `IP` (refused on a `NULL` core or a halted core →
   `cpm86CoreInvalidState`). `cpm86CoreStep`/`cpm86CoreRun` refuse a halted core
   (`cpm86CoreHalted`); `cpm86CoreRun` also rejects a zero budget.
4. Result mapping from tiny8086: `Ok→Ok`, `Halted→Halted`, `MemoryFault→MemoryFault`,
   `IoFault→IoError`, `InvalidArgument→InvalidArgument`, everything else
   (including `InvalidInstruction`) → `cpm86CoreInvalidInstruction`.
5. `cpm86CoreGetFaultInfo` copies the CPU's fault context (see §6 rule 8) plus all
   general/segment registers, flags, post-fault `ip`, and `startIp`.

### Millisecond timer ports (`RETRO86_TIMER_B1`)

The adapter intercepts byte ports `F0h–F4h` **before** the caller's port callbacks. This
is an emulator extension outside CP/M; guest programs that do not use it are unaffected.

| IN port | Behavior |
| --- | --- |
| `$F0` | Capability: `$B1` when a `readMilliseconds` callback is configured, else `$00`. Reading it MUST NOT sample the callback. |
| `$F1` | Latch a fresh snapshot from the callback (no callback → 0) and return bits 0–7. |
| `$F2`–`$F4` | Return bits 8–15 / 16–23 / 24–31 of the last latch, WITHOUT calling the callback. |

OUT to any of `F0h–F4h` returns `false` from the write hook → guest I/O fault. The
snapshot is core state: `cpm86CoreReset` zeroes it. The portable core depends only on the
`hostReadMilliseconds` function pointer, so host tests inject deterministic values
(`tests/hostTests.c` `testCpm86TimerPorts`).

## 8. Execution model

- The machine run loop executes batches of 4096 instructions via `cpm86CoreRun`. After
  each successful batch it compares wall time (ESP timer µs) against a 200 ms slice;
  while inside the slice it continues without yielding, otherwise it calls
  `vTaskDelay(1)` and restarts the slice. A 100 Hz tick with a per-batch `vTaskDelay(1)`
  would leave the CPU idle most of the time, so the slice is required for usable
  throughput while still servicing IDLE and the watchdog.
- Before each batch the loop peeks the console (`hostConsolePeekChar`); a `Ctrl+]`
  (0x1D) byte is consumed and stops the run. The port E1h handler also sets the stop
  flag when the guest consumes a 0x1D byte (§11.1). Either way the guest stops at a
  batch/instruction boundary, media is flushed/closed, and control returns to the
  common host. Ordinary Enter and other guest keys are passed through.
- The run loop exits when the core reports `cpm86CoreHalted` or any fault result.
- There is no cycle-accurate timing model; the core maintains only an instruction count.
  The guest BIOS busy-polls port E0h while waiting for console input, so the host
  regains control between batches without a special wait mechanism.

## 9. Guest memory map at boot

Guest RAM is 640 KiB (`640 * 1024 = $A0000` bytes) allocated by the machine adapter.
Addresses ≥ `$A0000` follow the core's empty-bus rules (§6 rule 3). The frozen boot map
(physical addresses):

| Physical range | Contents | Installed by |
| --- | --- | --- |
| `$00000–$003FF` | Interrupt vector table. Used vectors: INT 16h at `$00058` → `0000:$042D`, INT 1Ah at `$00068` → `0000:$0400`, INT E0h (BDOS) at `$00380` → `$0B06:$0051` (IP word `$0B06`, CS word `$0051`). | host loader |
| `$00400–$00450` | PC BIOS compatibility stubs, 81 bytes (see §9.2). | host loader |
| `$00510–$02C8F` | `cpm.sys` payload: 10,240-byte CMD file minus its 128-byte header (10,112 bytes). Contains CCP, BDOS and BIOS support code of segment base `$0051`. | `loadSystemFile` |
| `$029C7–$02F19` | RETRO86_V1 BIOS overlay: H86 records for segment offsets `$24B7…$2A09` loaded at physical base `$00510` (the record window end is `$2A0A`, exclusive). Overwrites the tail region of the payload area (linker-map padding) — the overlay is loaded after `cpm.sys`. | `cpm86BiosLoadOverlay` |
| `$05180–$09AE7` | Guest-allocatable memory per the BIOS memory region table (§10.4): base paragraph `$0518`, top paragraph `$09AE8`. | declared in BIOS |

Boot entry: `cpm86CoreSetEntry(core, 0x0051, 0x2500)` — CS `$0051`, IP `$2500`, which is
the INIT jump of the BIOS entry table (§10.1). All other registers are the reset values
of §6 rule 6.

### 9.1 `cpm.sys` validation

Before loading, the file MUST pass: exact size 10,240 bytes; byte 0 = `$01` (CMD
relocatable flag); bytes 3–4 = little-endian `$0051` (declared load segment). The
128-byte CMD header is skipped; the payload is streamed in 512-byte chunks and loaded at
physical `$00510 + offset`. The BDOS vector `{06 0B 51 00}` is written to `$00380`
(INT E0h). These four constants (size, header checks, physical base, vector) MUST stay
consistent with the GENCMD parameters used when `cpm.sys` is rebuilt.

### 9.2 PC BIOS compatibility stubs

Third-party CP/M-86 tools occasionally call PC BIOS services. The host installs 81 bytes
of guest code at physical `$0400` plus the two IVT entries above:

- INT 1Ah AH=0: samples the `RETRO86_TIMER_B1` ports F1h–F4h and divides the two halves
  of the millisecond count independently by 55 (`MOV SI,37h; DIV SI` pattern) to produce
  an ~18.2 Hz tick count in CX:DX, AL=0 (midnight flag); `IRET`.
- INT 16h AH=0: busy-polls E0h until a byte is ready, reads E1h and returns AX with the
  character in AL and AH=0; `IRET`. AH=1 (and any other AH): non-blocking status —
  AX=0001 when E0h reports a byte, else AX=0000; `RETF 2`.

The byte sequence is the checked-in `pcBiosStubs[]` in `main/cpm86Machine.c`; it MUST be
kept byte-identical or replaced by an equivalent audited version.

## 10. Guest BIOS specification

The BIOS is guest 8086 code assembled from `components/cpm86Core/bios/retro86bios.a86`
with ASM-86 1.1N into `littlefs/cpm86/retro86bios.h86`. It is NOT linked into
`cpm.sys`; it lives in segment `$0051` at offsets `$24B7` (data, `BDOSCURDRV`) and
`$2500` (entry table). Its entry order and data structures match the CP/M-86 1.1 System
Guide (June 1981): 21-entry jump table per Table 5-1 (p.57), DPB layout per Tables
6-2/6-4/6-5 (pp.67–71), memory region table and segment-aware DMA per Table 5-4
(pp.64–65).

### 10.1 H86 overlay loading

`cpm86BiosLoadOverlay` reads the file (1–4096 bytes, RO) through `imageOpen` and parses
Intel-Hex records line by line:

- Every record line starts with `:`; byte count, 2-digit hex data, and trailing
  two's-complement checksum must make the full record sum `$00`; the line length must be
  exactly `1 + 2·(count+5)`.
- Record type `$81` (extended data, the ASM-86 custom type): address `$24B7…$2A0A`
  inclusive; data may not overlap previously written bytes; each written byte is later
  loaded to physical `$00510 + offset`.
- Record type `$03` (start segment/offset, 4 data bytes) and type `$01` (EOF, empty)
  are accepted once each and carry no data.
- A CP/M `$1A` EOF byte terminates parsing (valid only if the EOF record was seen).
- The file MUST contain at least one data record and both terminator records, and no
  record may follow the EOF record. Any violation → the whole load fails and the guest
  is not started.
- After validation, each contiguous run of written bytes is loaded with one
  `cpm86CoreLoad`.

### 10.2 Entry table at `$0051:$2500`

21 three-byte `JMP` entries at `$2500 + 3·n`, in CP/M-86 order:

| n | Entry | n | Entry | n | Entry |
| --- | --- | --- | --- | --- | --- |
| 0 | INIT | 7 | READER | 14 | WRITE |
| 1 | WBOOT | 8 | HOME | 15 | LISTST |
| 2 | CONST | 9 | SELDSK | 16 | SECTRAN |
| 3 | CONIN | 10 | SETTRK | 17 | SETDMAB |
| 4 | CONOUT | 11 | SETSEC | 18 | GETSEGB |
| 5 | LISTOUT | 12 | SETDMA | 19 | GETIOB |
| 6 | PUNCH | 13 | READ | 20 | SETIOB |

After the table: 15 reserved bytes, then the vectors `dw offset PFKTABLE, DISK, SER1,
SYS_CONI, FIDDSMEM, CRTMOD`, then the identification string `'RETR086 BIOS'`.

### 10.3 Entry semantics

| Entry | Contract |
| --- | --- |
| INIT | `SS = DS = ES = CS`, `SP = offset LCLSTK+128` (128-byte local stack), `BDOSCURDRV = 0`, `CURRENTDRIVE = 0`, jump to CCP (`$0051:$0000`). Console initialisation and the BDOS vector are the host's duty (§9). |
| WBOOT | `CURRENTDRIVE = 0`, jump to `CCP+6`. Must not return to the host menu. |
| CONST | `IN AL,0E0h`; returns `$FF` when a character is available, else `$00`. |
| CONIN | Poll E0h until non-zero, then `IN AL,0E1h`. (Blocking in the guest; the host yields between batches.) |
| CONOUT | `AL = CL`, `OUT 0E2h,AL`. |
| LISTOUT / PUNCH | Jump to CONOUT (no printer/punch). |
| READER | Jump to CONIN (never returns EOF `$1A` — documented deviation). |
| HOME | `TRACK = 0` (sector unchanged). |
| SELDSK | If `CL > 5` → `BX = 0` (bad drive). Otherwise `OUT 0E8h,CL`, `IN AL,0EFh`; AL=0 → `BX = 0` and the current drive is unchanged. Else `CURRENTDRIVE = CL`; `DX` is chosen from the EFh answer (`offset DPB1` when AL=1, `offset DPB2` when AL=2, else `offset DPB0`); `BX = CL*16 + offset DPH0`; store DX at `[BX+10]` (the DPH's DPB pointer). |
| SETTRK / SETSEC / SETDMA / SETDMAB | `CX → TRACK / SECTOR / DMAOFFSET / DMASEGMENT`. |
| GETSEGB | `BX = offset MRT` (memory region table, §10.4). |
| GETIOB / SETIOB | AL ← / CL → `IOBYTE`. |
| READ | Select drive (E8h = `CURRENTDRIVE`), set track (E9h low, EAh high), sector (EBh low, ECh high), `OUT 0EDh,0`, `IN AL,0EDh`; non-zero → `AL = 1` return. Otherwise read 128 bytes from EEh (`STOSB`) to `ES:DI` where `ES = DMASEGMENT`, `DI = DMAOFFSET`; `AL = 0`. |
| WRITE | Same positioning, `OUT 0EDh,1`; send 128 bytes from `DS:SI` (`DS = DMASEGMENT`, `SI = DMAOFFSET`) to EEh; then `IN AL,0EDh`; non-zero → `AL = 1`, else `AL = 0`. |
| LISTST | `AL = $FF` (printer always ready). |
| SECTRAN | `BX = CX` (identity sector translation). |

Register discipline: BDOS/BIOS callers use the CP/M-86 convention (`CL`/`CX` arguments,
`BX` DPH result). Invalid drive selection never aliases A: and never replaces the last
valid selected drive.

### 10.4 BIOS data structures (segment `$0051`, all `offset` values are segment offsets)

```text
BDOSCURDRV   db        at offset 24B7h (start of the overlay data region)
IOBYTE       db 0      CURRENTDRIVE db 0
TRACK        dw 0      SECTOR       dw 0
DMAOFFSET    dw 0080h  DMASEGMENT   dw 0
PFKTABLE     db 0      DISK         db 0      SER1 db 0
SYS_CONI     dw 1      SYS_CONO     dw 2      SYS_AUXI dw 1
SYS_AUXO     dw 2      SYS_LST      dw 2      FIDDSMEM dw 0   CRTMOD db 0
DPH0…DPH5    8 words each (stride 16 bytes):
               +0 +2 +4 +6 : 0, 0, 0, 0
               +8  : offset DIRBUF   (one shared 128-byte directory buffer)
               +A  : offset DPB0     (SELDSK rewrites this word)
               +C  : offset CSVn     (n = 0..5, 32 bytes each)
               +E  : offset ALVn     (n = 0..5, 64 bytes each)
DIRBUF       rb 128
CSV0…CSV5    rb 32 each     ALV0…ALV5 rb 64 each
MRT:         db 1           ; one memory region
             dw 0518h       ; base paragraph
             dw 09AE8h      ; top paragraph
LCLSTK       rb 128         ; INIT stack
```

The three DPBs (word/byte fields in System Guide order SPT, BSH, BLM, EXM, DSM, DRM,
AL0, AL1, CKS, OFF):

| DPB | SPT | BSH | BLM | EXM | DSM | DRM | AL0,AL1 | CKS | OFF |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DPB0 | 32 | 3 | $07 | 0 | 155 ($9B) | 63 ($3F) | $C0, $00 | 16 | 1 |
| DPB1 | 32 | 4 | $0F | 0 | 255 ($FF) | 127 ($7F) | $C0, $00 | 32 | 1 |
| DPB2 | 32 | 7 | $7F | 7 | 511 ($1FF) | 511 ($1FF) | $80, $00 | 0 | 1 |

Block sizes: DPB0 1 KiB (8 records), DPB1 2 KiB (16 records), DPB2 16 KiB (128 records).
DPB1 MUST keep `EXM = 0` (one 16 KiB extent per directory entry, matching the checked-in
tool layout); a two-extent encoding breaks BDOS record access for files > 16 KiB.

## 11. Virtual I/O port contract

The port numbers are project-defined virtual devices, not physical hardware. All accesses
are 8-bit `IN`/`OUT` (word I/O faults, §6 rule 4). Disk operations complete
synchronously; there is no asynchronous completion. The full normative text lives in
[docs/cpm86/virtualIo.md](./docs/cpm86/virtualIo.md); the table below is the machine
adapter's implementation (`main/cpm86Machine.c`).

### 11.1 Console

| Port | Direction | Contract |
| --- | --- | --- |
| E0h | IN | `$FF` when a console byte is available, else `$00`. Does not consume. |
| E1h | IN | Consumes one byte. Negative host result → guest I/O fault. A `$1D` (Ctrl+]) byte sets the host stop flag and returns fault — it never reaches the guest. |
| E2h | OUT | Writes the low byte to the shared host console. |

Guest bytes are forwarded verbatim; the machine performs no CR/LF or 7-bit conversion
(the CP/M-86 CCP/CCP-side line editor owns line discipline, including Ctrl-H/0x7F
erase). The intended terminal personality is 80×24 VT100/ANSI via the common host byte
stream; the host MUST NOT interpret guest escape sequences.

### 11.2 Disk drives

Per-guest state: `drive`, `track` (16-bit), `sector` (16-bit), a 128-byte record buffer,
`transferIndex`, `transferStatus`, and `transferMode ∈ {idle, read, write}`. All zero at
machine initialization.

| Port | Direction | Contract |
| --- | --- | --- |
| E8h | OUT | Select drive number (0 = A: … 5 = F:); stored unvalidated, checked at transfer. |
| E9h / EAh | OUT | Track number low / high byte. |
| EBh / ECh | OUT | 128-byte record number within the track, low / high byte; valid 0–31. |
| EDh | OUT | Start transfer: `$00` = read one record (performed synchronously), `$01` = arm a write transfer. Any other value fails with status `$01`. |
| EDh | IN | Transfer status: `$00` success, `$01` failure. |
| EEh | IN | Next byte of an active read transfer; outside an active read → guest I/O fault. |
| EEh | OUT | Next byte of an armed write transfer; the 128th byte commits the record (then mode → idle). Outside an active write → guest I/O fault. |
| EFh | IN | Geometry code of the selected drive: `$00` when unselected/out of range/not configured, else the drive's code `$FF` (CPM86_160K DPB0), `$01` (CPM86_516K DPB1) or `$02` (CPM86_8196K DPB2). |

Transfer rules:

- Every read and every committing write re-validates through `cpm86DriveRecordValid`
  (§12.2) AND bounds the computed byte range inside the image file size before touching
  storage.
- A read signals `diskActivityRead()`, then `imageReadAt`; any failure leaves status `$01`
  and idle mode. A read leaves the record buffer staged for exactly 128 IN reads.
- A write collects 128 bytes; the commit signals `diskActivityWrite()`, performs
  `imageWriteAt` followed by `imageFlush` (a write is durable only after a successful
  flush), and sets status `$01` on any failure.
- The record byte offset is `((track · 32) + record) · 128` (identity order, no skew).

### 11.3 Unknown and misused ports

- Ports inside the BIOS range E0h–EFh with no handler in the requested direction (for
  example IN E2h, OUT E0h, IN/OUT E3h–E7h) return `false` → guest I/O fault. There is no
  silent idle value inside the range.
- Ports outside E0h–EFh (except the F0h–F4h timer, handled in the core adapter): a read
  returns the idle value `$FF`, a write is ignored; each port prints the one-time console
  notice `Device 0xNNNN not available.` at most once per machine run (tracked list,
  capacity 16).

## 12. Disk subsystem

### 12.1 Profiles

Images are raw, identity-ordered sequences of 128-byte records; 32 records (8 physical
512-byte sectors) per track. Raw track 0 is reserved; DPB OFF = 1 makes the BDOS treat
raw track 1 as its track 0, so the BIOS passes the guest track to the backend unchanged —
adding another offset would skip the directory.

| Size identifier (`drives.cfg`) | Exact image size | Tracks | Block | Dir entries | DPB | EFh code | Role |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| `CPM86_160K` | 163,840 | 40 | 1 KiB | 64 | DPB0 | $FF | A: (built-in) or B:–F: |
| `CPM86_516K` | 528,384 | 129 | 2 KiB | 128 | DPB1 | $01 | A: (built-in) or B:–F: |
| `CPM86_8196K` | 8,392,704 | 2049 | 16 KiB | 512 | DPB2 | $02 | A: (built-in) or B:–F: |

The identifier carries the image size only; the drive letter decides the internal
variant: the firmware models each size twice (`cpm86DiskProfileSystem*` for the
built-in A: image, `cpm86DiskProfileData*` for SD images) and both share a geometry.

SELDSK selects DPB0/1/2 purely from the EFh answer; the firmware stores the geometry code
per mounted drive at initialize time. A: is mounted as the `System*` variant matching the
exact size of its image file; a declared identifier that contradicts the file size keeps
A: unmounted (§14).

### 12.2 Geometry gate

`cpm86DriveRecordValid(tracks, track, sector)` is the single pure predicate shared by the
production record path and the host test fixture:

```c
bool cpm86DriveRecordValid(uint32_t tracks, uint32_t track, uint32_t sector)
{
  return tracks > 1 && track != 0 && track < tracks && sector < 32;
}
```

Track 0 is never readable or writable; the valid record range is records 0–31 of tracks
1…tracks−1 (1–39 / 1–128 / 1–2048). Bounds against the real image size remain a separate
check in the caller (§11.2).

### 12.3 Drive roles

A: is the required LittleFS system image; B:–F: are optional SD images. Each configured
drive owns its image handle and read-only flag in the machine adapter; every drive gets
its own DPH/CSV/ALV in guest memory and shares the single DIRBUF. An unconfigured or
mis-sized image leaves the drive absent: SELDSK probes EFh, gets 0 and returns `BX = 0`
without aliasing or changing the selected drive. A:–F: supported here does not claim any
application supports six drives.

## 13. BDOS interface

The BDOS is the genuine Digital Research CP/M-86 1.1 binary inside `cpm.sys`; transient
programs are native `.CMD` files. Guest BDOS calls use `INT $E0` (vector `$00380` →
`$0051:$0B06`) with the function number in CL; byte parameters go in DL, word parameters
in DX (System Guide p.23 — DX performs the role of CP/M-80's DE). The CPU MUST execute
the real interrupt vector and guest handler; the host MUST NOT turn INT E0h into a host
syscall dispatcher and adds no BDOS emulation of its own.

CP/M-80 `.COM` utilities and DOS `.COM`/`.EXE` binaries are not executable on this
machine; only native CP/M-86 `.CMD` programs qualify.

## 14. Drive configuration (`drives.cfg`)

Path: `/littlefs/cpm86/drives.cfg`. Grammar, per non-comment line:

```text
drive=/path/to/image,RO|RW,PROFILE
```

Parser requirements (see `main/cpm86DriveConfig.c`):

- Blank lines and lines whose first non-blank character is `#` are comments. Spaces and
  tabs around fields are trimmed; trailing CR/LF is trimmed. A line filling the 256-byte
  buffer without a newline is rejected as too long.
- Exactly one `=` (not at the start); the drive name is one character `A`–`F`
  (case-sensitive uppercase); duplicates are rejected. The value is three
  comma-separated fields: path, mode (`RO`/`RW` exactly), size identifier (one of
  `CPM86_160K`, `CPM86_516K`, `CPM86_8196K` exactly); a comma inside the identifier is
  rejected; empty path or identifier is rejected.
- A: MUST name the exact path `/littlefs/cpm86/system.dsk`; its RO/RW mode is honored
  (RW shows `[Writable!]` in the menu and persists guest writes in LittleFS until the
  firmware rebuilds the image). B:–F: MUST name paths in the SD namespace
  `/retro/images/cpm86/`; the stored path is resolved through `storageResolveRetroPath`,
  which prefixes `/microSD` to reach the VFS mount.
- Path safety (all drives): must start with the required prefix, be longer than it, and
  contain no trailing `/`, `//`, `/./`, `/../`, final `/.` or `/..`, backslash, comma, or
  control character (< `$20` or `$7F`). Stored paths are limited to 191 characters
  (buffer 192).
- Outcomes (`cpm86DriveConfigResult`): file missing → `cpm86DriveConfigMissing`, keep the
  built-in default table; any malformed line, over-long line or read error →
  `cpm86DriveConfigInvalid`, the whole file is discarded (partial entries are not kept)
  and the default table restored. The caller-visible error string identifies the failing
  line number.
- Built-in default table: A: = `/littlefs/cpm86/system.dsk`, RO, size from the image
  file (profileFromSize); B:–F: unconfigured.
- A: profile resolution (`cpm86DriveConfigSystemProfileFromSize` /
  `cpm86DriveConfigResolveSystemProfile`): 163,840 → System, 528,384 → SystemLarge,
  8,392,704 → SystemBig, any other size is an error and A: is not mounted (the error
  string reports the size and the three accepted sizes). An explicit size identifier
  from drives.cfg must equal the actual file size; only the built-in default
  (profileFromSize) follows the file size.
- Optional B:–F: mount only when configured, present, exactly the size named by the
  identifier, and openable with the requested access mode; otherwise the drive is
  absent. The firmware never creates blank media during boot.

## 15. Machine lifecycle integration

Registry entry (in `main/machineRegistry.c`): display name `CP/M-86`, id `cpm86`, design
document `designCPM86.md`, implemented, SD not required for availability. Resource
manifest (each must exist as a regular file with at least the listed size):
`/littlefs/cpm86/cpm.sys` 10,240; `/littlefs/cpm86/retro86bios.h86` 1;
`/littlefs/cpm86/system.dsk` 163,840.

### 15.1 Probe (`cpm86MachineProbe`)

Fast, non-booting, never leaves files open:

1. `!machine->implemented` → `machineNotImplemented`.
2. Load `drives.cfg` (result retained for the final verdict).
3. `cpm.sys` must exist with exact size 10,240 (missing → `machineMissingResource`,
   wrong size → `machineResourceInvalid`).
4. `retro86bios.h86` must exist, size 1–4096 → else `machineResourceInvalid`.
5. `system.dsk` must exist (missing → `machineMissingResource`) and its size must map to
   a system profile (else `machineResourceInvalid`).
6. Open and close the A: image with its configured mode; failure →
   `machineResourceInvalid`.
7. Return `machineWritable` when the configuration loaded cleanly and A: is RW, else
   `machineAvailable`.

### 15.2 Initialize (`cpm86MachineInitialize`)

1. Refuse (`ESP_FAIL`) when already ready or a core exists. Clear the disk controller and
   the unknown-port notice list.
2. Load `drives.cfg`; log `Invalid` (with the error and the note that the built-in A: is
   used) or `Missing` (when the SD is ready) and continue with the restored defaults.
3. For each configured drive: A: first resolves its profile from the exact image size,
   which must equal the identifier it declared (failure → log, close all images,
   `ESP_FAIL`); then each drive is size-checked against its profile and opened with its
   mode. A: failure is fatal; B:–F: failures are logged and the drive stays offline. If
   A: is not mounted → `ESP_FAIL`.
4. Create the core: `ramSize = 640 KiB`, port callbacks (§11),
   `readMilliseconds = esp_timer_get_time()/1000`. `cpm86CoreNoMemory` →
   `ESP_ERR_NO_MEM` with PSRAM/internal free-and-largest-block diagnostics; any other
   error → `ESP_FAIL`; always close images and clear the core on failure.
5. Load `cpm.sys` (header validation + payload + overlay + BDOS vector + entry point,
   §9), then install the PC BIOS stubs (§9.2). Any failure → destroy core, close images,
   `ESP_ERR_INVALID_STATE`.
6. Log PSRAM size/free/largest and the guest RAM size; report ready.

### 15.3 Run (`cpm86MachineRun`)

If not ready, print `CP/M-86 guest is not initialized.` and return. Discard stray menu
input so it cannot reach the guest console, print `Starting CP/M-86.`, clear the stop
flag, then execute the batch loop of §8. On exit (fault, halt, or stop request): print
`CP/M-86 stopped by Ctrl+].` when stopped by the stop key, destroy the core, clear the
ready flag, and close every image (logging close failures). Re-entering CP/M-86 from the
menu initializes from scratch. Physical RESET always returns to the main menu (common
host rule).

### 15.4 Fault diagnostics

On any core result other than `Ok`/`Halted` the machine logs through
`cpm86CoreGetFaultInfo`: result code; `CS:startIp` with the physical address and the six
instruction bytes at the faulting instruction; post-fault IP; ES/CS/SS/DS and all
general registers; flags; total and batch instruction counts; and the specific failing
access — `memory read/write at physical NNNNN` or `I/O IN/OUT port NNNN value NN`. A
halt logs the instruction count. Host logs MUST NOT inject bytes into the guest console
stream while the guest runs.

## 16. Host-side tools (media production requirements)

The firmware never formats media. These development-computer tools (all `python3`,
options via `--help`) MUST produce images that satisfy §12 exactly:

| Tool | Requirement |
| --- | --- |
| `tools/diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_160K\|CPM86_516K\|CPM86_1440K\|CPM86_8196K [--source-dir …]` | Deterministic system-image composer (`tools/cpm86/buildDiskImageCpm86.py` engine): builds A: from `bootDisks/cpm86/systemDsk` (only `.CMD`/`.SYS` names; CPM.SYS is refused on A:), using the geometry named by the size identifier; validates `cpm.sys` (10,240) and the overlay (1–4096) remain outside A:; replaces `littlefs/cpm86/system.dsk` only on success. |
| `tools/diskManager.py --type WORKDSK --os cpm86 [--profile …] <image>` | Create raw profile images (`CPM86_160K`, `CPM86_516K`, `CPM86_1440K`, `CPM86_8196K`); refuses an existing target without `--force`. |
| `tools/extractFiles.py --os cpm86 <image> --add/--extract/--remove` (`tools/cpm86/extractCpm86.py` adapter) | Inspect and edit raw profile images; `add` validates 8.3 names, duplicates, extents, directory and block capacity, and writes atomically. This tool is the authority for multi-extent `CPM86_516K` content. |
| `tools/cpm86/diskdefs` | cpmtools geometry definitions (`retro86-160`, `retro86-large`, `retro86-big`) for cross-checking geometry only — cpmtools cannot express EXM and its LARGE multi-extent layout is incompatible; never use `mkfs.cpm`/`cpmcp` as the authority for LARGE content. |
| `tools/prepareSd.py` | Create the `/retro/…` tree and a sample `drives.cfg` without overwriting an existing one or formatting the card. |
| `tools/cpm86/cpm86HardwareTest.py` | Drive a running device over the console for the hardware procedures of §18.2. |
| `assets/cpm86/{BENCH86,TIMER86,WBOOT86,BAD86}` | Guest test programs (NASM `.ASM` + `build.py`), packaged into A: via `bootDisks/cpm86/systemDsk`; registered in `tests/test_bootDisksBench.py`. |

## 17. Verification: host tests

Run natively with ASan/UBSan and `-Wall -Wextra -Werror -funsigned-char`:

```sh
cmake -S tests -B /tmp/retro-host-tests
cmake --build /tmp/retro-host-tests
ctest --test-dir /tmp/retro-host-tests -N
ctest --test-dir /tmp/retro-host-tests --output-on-failure -R '<name|name>'
```

ctest target names differ from file names: `hostCore` (default `hostTests` run) and
`cpm86Conformance` (`tests/cpm86ConformanceTests.c`).

A conforming implementation MUST ship host tests covering each item below; the suite is
"done" only when every listed assertion passes deterministically with zero sanitizer
reports. Required coverage (current suites):

1. **Core adapter (`testCpm86Core`):** create argument/state rejection,
   single-active-core `Busy`, arithmetic program execution to HLT, RAM bounds, reset,
   port callback wiring, word-I/O and unmapped-fetch faults.
2. **Instruction conformance (`cpm86Conformance`):** the vector table (currently 109
   vectors + 32 Jcc cases) pins flags and results for arithmetic, logic, shifts,
   multiply/divide (including `INT32_MIN/-1` and `INT16_MIN/-1` → `INT 0`), string
   operations, `8Ch/8Eh` reg-field validation, `FE /2../7` rejection, PUSHF/POPF/IRET
   reserved bit 1, and the "82h aliases 80h" byte-immediate ALU decode.
3. **Timer (`testCpm86TimerPorts`):** capability byte with/without callback, F1 latches
   (F2–F4 do not re-call the callback even when the injected value changes), writes
   fault.
4. **Boot (`testCpm86Boot`, `testCpm86LargeSystemBoot`, `testCpm86BigSystemBoot`):** real
   `cpm.sys` header checks, real guest cold boot to `A>`, `DIR` lists the system files
   (incl. `ASM86`, `PIP`, `STAT`), `TYPE` of a multi-extent text file, drive selection to
   a second configured drive and `DIR` there, STAT message-table regression witness, and
   the same flow on generated CPM86_516K and CPM86_8196K A: images. The fixture exercises the
   production geometry gate via `cpm86DriveRecordValid`.
5. **Geometry (`testCpm86DriveGeometryMatrix`):** per profile 40/129/2049 tracks — first
   and last valid track, track 0 and overflow rejected, sector 31 accepted / 32 and
   `$FFFF` rejected, degenerate track counts rejected.
6. **Storage guards (`testCpm86StorageGuards`):** through the BIOS ports E8h–EEh —
   out-of-range reads fail with status 1, last valid record reads, write/read-back of a
   128-byte record, and a read-only-opened image refuses the write with status 1 leaving
   the data untouched.
7. **Storage full (`testCpm86StorageFull`):** guest-visible on disposable CPM86_516K images —
   block-exhausted and directory-exhausted volumes refuse new files (PIP creates nothing,
   no `NO FILE`, DIR still lists the expected entries, the guest returns to a prompt) and
   an identical copy succeeds on an empty control volume.
8. **Drives.cfg (`testCpm86DriveConfig`, `testCpm86SystemProfileDetection`):** defaults
   on missing/invalid, every §14 rejection rule (duplicate, unknown drive/size identifier/
   mode, long line, traversal forms, backslash, comma in path, wrong prefixes, A: wrong
   path, unknown identifier on B:–F:), SD namespace prefixing to `/microSD/retro/...`,
   A: size-based profile detection (163,840 / 528,384 / 8,392,704 accepted; other sizes
   refused with a useful error) and the explicit A: identifier matching the image size.

## 18. Verification: hardware acceptance

Acceptance is complete when every case below is executed on the target board (ESP32-S3,
LOLIN S3 Pro configuration with 8 MiB octal PSRAM enabled, USB Serial/JTAG console,
80×24 terminal, valid Retro SD card) and each observation matches its pass criterion. A
host-test pass or a successful build is never evidence of hardware behaviour. The
scripted driver is `tools/cpm86/cpm86HardwareTest.py`.

| # | Procedure | Pass criterion |
| --- | --- | --- |
| H1 | RESET, menu shows CP/M-86; select it. | Menu reports `PSRAM: 8192 KiB, free …`; guest reaches `A>`; `DIR` lists the system files; `STAT` completes (message-table strings intact — the 82h decode witness). |
| H2 | `DIR`/`STAT` on A:, `DIR` on B:/C:/D:, select E:/F:. | Configured drives list their images; unconfigured drives report the BDOS drive error without aliasing. |
| H3 | `PIP` copy A:→D: and D:→B:, run a copied CMD from its own drive. | Copies complete; the CMD runs (`@DONE PASS` witness); the LED shows green/red per read/write. |
| H4 | Write to read-only A:; disk-full and directory-full volumes. | CP/M reports the failure; no crash; image bytes unchanged. |
| H5 | Ctrl+] stop and relaunch (≥ 3 cycles); RESET mid-copy. | Clean stop at batch boundary, data written before the stop persists byte-identically; interrupted writes leave the volume usable. |
| H6 | Native workflow: ED insert/save (verified by `DIR`+`TYPE`), `A:ASM86 C:HELLO` (`NUMBER OF ERRORS: 0`), `A:GENCMD C:HELLO`, run the produced CMD from its own drive. | Every produced file verifies by `TYPE`; run completes. |
| H7 | Warm boot (`WBOOT86` / BDOS function 0). | Returns to `A>` on the same drive without the host menu. |
| H8 | `BAD86` (invalid opcode). | Register/fault dump is logged, guest fault path returns to the menu on ENTER without a host reset. |
| H9 | `TIMER86` with a host-driven ~1000 ms wait for its key. | F0h = `$B1`; printed `DELTA=` within 900–1100 ms. |
| H10 | `BENCH86` scale 1. | INTEGER/SIEVE complete; elapsed times and index vs the 5 MHz model are logged; values track the previous measured baseline within tolerance. |
| H11 | Terminal checks: CR/LF pairs, Ctrl-H/0x7F erase, named `DIR` (reports `NO FILE` without an explicit type — CCP 1.1 behaviour), full-screen programs / escape sequences (manual). | Character erase and line delivery behave; escape-sequence behaviour recorded as a manual observation. |
| H12 | Sustained run ≥ 30 minutes with disk activity. | No watchdog reset; heap stays flat across enter/exit cycles. |

## 19. Definition of done

1. All §17 host tests pass (sanitizer-clean) and all §18 hardware cases are recorded as
   passing.
2. The guest is the genuine pinned CP/M-86 1.1 CCP/BDOS in `cpm.sys`; no BDOS logic
   exists in host code; INT E0h dispatches through the guest IVT.
3. The core and adapter (`tiny8086`/`cpm86Core`) build and test natively with no ESP-IDF
   or filesystem/console dependencies beyond the `cpm86CoreConfig` callbacks.
4. Every acceptance-gated behaviour in this document is traceable to code: boot map §9,
   BIOS §10, ports §11, disks §12, configuration §14, lifecycle §15.
5. Provenance, licenses and rights records for the kernel binaries, BIOS tools and all
   bundled applications are present in-tree.
