# CP/M-80 Machine Design

## 1. Purpose and scope

This document is the complete technical design of the CP/M-80 guest machine hosted by the
ESP32-S3 Retro Computer. It defines the architecture, guest-visible hardware, memory map,
boot system, BDOS/BIOS interface, I/O ports, disk formats, drive configuration, console
contract, and the testing required to prove a correct implementation.

The implementation in `components/cpm80Core/`, `main/cpm80Machine.c`,
`main/cpm80DriveConfig.c`, `main/cpm80Machine.h`, `main/cpm80DriveConfig.h`, and
`components/cpm80Core/os/` is the normative reference. Where this document and the code
appear to differ, the code is leading and this document must be corrected.

A senior programmer who reads this document plus [projectPrompt.md](./projectPrompt.md)
(common host services: menu, console, LittleFS, SD, image I/O, disk-activity LED) must be
able to implement an equivalent emulator without further interview.

### Requirement levels

- **MUST** — mandatory; an implementation that violates it is non-conforming.
- **SHOULD** — mandatory unless a documented, explicit deviation is accepted.
- **MAY** — optional.

## 2. Design goals

1. Execute genuine CP/M 2.2 CCP and BDOS binaries unmodified on a Z80 core inside the
   64 KiB Z80 address space.
2. Provide the BIOS as generated guest-resident code plus one virtual I/O port; the host
   never substitutes its own BDOS and never presents the host FAT filesystem as a guest
   filesystem.
3. Support up to six CP/M drives (A:–F:) with independent image file, geometry profile and
   access mode; A: boots from LittleFS, B:–F: from the SD card.
4. Keep the CPU/BIOS logic free of ESP-IDF dependencies so it compiles and runs natively for
   host-side testing; all filesystem, console and indicator ownership stays in the machine
   adapter.
5. Cooperate with FreeRTOS: bounded instruction batches and cooperative yields, so the
   task scheduler and watchdog are serviced during guest execution.
6. Reproducible media: every image the firmware accepts is produced by the checked-in host
   tools with byte-deterministic output.

Non-goals: cycle-accurate peripheral timing, physical printer/punch/reader devices,
hardware interrupts, sector skew translation, archival floppy-format conversion, and
terminal emulation inside the host console.

## 3. Responsibility boundary

| Layer | Owner | Provides |
| --- | --- | --- |
| Menu, machine registry, probe states | common host | Selection, lifecycle scheduling |
| USB Serial/JTAG byte-stream console | common host | `hostConsoleCharAvailable/GetChar/PutChar` |
| LittleFS mount, SD/FatFS mount | common host | VFS paths |
| Virtual image random access | common host | `imageOpen/imageReadAt/imageWriteAt/imageFlush/imageClose/imageSize`, `resourceSize` |
| Disk-activity RGB indicator (GPIO 38) | common host | `diskActivityRead()` green, `diskActivityWrite()` red, off 120 ms after last activity |
| Z80 core | `components/z80/` | Upstream instruction interpreter |
| CPU adapter, guest, BIOS, drives | CP/M-80 machine | Everything below this line |

The CP/M-80 machine MUST NOT start Wi-Fi or any HTTP server, MUST NOT open the console or
mount filesystems itself, and MUST NOT be active at the same time as the browser File
Transfer service (enforced by the common host menu).

## 4. Architecture and module boundaries

```text
machine registry (main/machineRegistry.c)
  -> cpm80MachineProbe / cpm80MachineInitialize / cpm80MachineRun   main/cpm80Machine.c
       -> cpm80DriveConfigLoad / profile resolution                 main/cpm80DriveConfig.c
       -> imageFile random-access I/O + diskActivity                (common host)
       -> hostConsole                                               (common host)
       -> cpm80Guest                                                components/cpm80Core/cpm80Guest.c
            -> cpm80Cpu                                              components/cpm80Core/cpm80Cpu.c
                 -> z80 core                                         components/z80/
            -> genuine CP/M 2.2 CCP ($C400) and BDOS ($CC00)
```

| Module | Responsibility |
| --- | --- |
| `main/cpm80Machine.c` | Registry callbacks; open/close images; build `cpm80HostOps`; run batches; log diagnostics. |
| `main/cpm80DriveConfig.c/.h` | Parse and validate `drives.cfg`; resolve the A: profile from image size. |
| `components/cpm80Core/cpm80Cpu.c` | Own the 64 KiB byte-addressed guest memory; wire Z80 memory/port callbacks; single-instruction step. |
| `components/cpm80Core/cpm80Guest.c` | Install CCP/BDOS/BIOS/page-zero; dispatch BIOS calls; serve the timer ports; run bounded instruction batches. |
| `components/cpm80Core/os/` | Checked-in CCP/BDOS sources and 64 KiB binary outputs, utilities, licenses. |

The CPU and guest modules MUST NOT include ESP-IDF headers except through the one
conditional allocation block described in §6, and MUST NOT call filesystem or console APIs
directly — only through `cpm80HostOps`.

## 5. Third-party dependencies

| Component | Provenance | Use |
| --- | --- | --- |
| Z80 core | `superzazu/z80`, pinned commit `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, MIT, vendored as `components/z80/` (`z80.c`, `z80.h`). | Instruction interpreter. |
| CP/M 2.2 CCP/BDOS | `brouhaha/cpm22`, pinned commit `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`, sources `ccp.asm`/`bdos.asm` plus checked-in outputs `ccp-64k.bin` (2,048 bytes, loads at `$C400`) and `bdos-64k.bin` (3,584 bytes, loads at `$CC00`), `components/cpm80Core/os/`. | Genuine guest operating system. |

Licenses and provenance records MUST be retained in-tree. Distribution rights for
separately obtained CP/M applications are tracked independently of these licenses.

## 6. CPU adapter (`cpm80Cpu`)

Exact interface (see `include/cpm80Cpu.h`):

```c
enum { cpm80MemorySize = 65536 };

typedef uint8_t (*cpm80PortInput)(void *context, uint8_t port);
typedef void (*cpm80PortOutput)(void *context, uint8_t port, uint8_t value);

bool cpm80CpuInitialize(cpm80Cpu *cpu, cpm80PortInput portInput,
                        cpm80PortOutput portOutput, void *portContext);
void cpm80CpuDestroy(cpm80Cpu *cpu);
bool cpm80CpuLoad(cpm80Cpu *cpu, uint16_t address, const uint8_t *data, size_t length);
bool cpm80CpuReadMemory(const cpm80Cpu *cpu, uint16_t address, uint8_t *value);
bool cpm80CpuWriteMemory(cpm80Cpu *cpu, uint16_t address, uint8_t value);
bool cpm80CpuStep(cpm80Cpu *cpu);
```

Requirements:

1. Allocate exactly 65,536 zero-initialized bytes for guest memory. On ESP-IDF targets
   allocate PSRAM first (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`), falling back to internal
   RAM; on the native host use `calloc`. A static assertion MUST enforce
   `cpm80MemorySize == UINT16_MAX + 1`.
2. Connect the Z80 core's `read_byte`/`write_byte` callbacks directly to that array
   (byte-addressed, full 16-bit range, no mirroring, no ROM regions) and `port_in`/`port_out`
   to the caller's callbacks with the caller's context pointer.
3. `cpm80CpuInitialize` MUST reject `NULL` cpu/port callbacks, an already-initialized
   instance, or a non-`NULL` memory pointer. Instances MUST be zero-initialized before
   first use and destroyed before re-initialization.
4. `cpm80CpuLoad` MUST reject `NULL` data with nonzero length and any range that would
   cross the end of the 64 KiB space (`length > cpm80MemorySize - address`).
5. `cpm80CpuStep` executes exactly one Z80 instruction and fails on a `NULL` or
   uninitialized instance.
6. `cpm80CpuDestroy` frees the memory and zeroes the structure so it can be initialized
   again.

## 7. Guest model (`cpm80Guest`)

Guest state (see `include/cpm80Guest.h`) holds: the `cpm80Cpu`, a copy of the
`cpm80HostOps`, private copies of the CCP image (2,048 B) and BDOS image (3,584 B),
`currentTrack`, `currentSector`, `dmaAddress`, per-drive `diskProfiles[6]`,
`selectedDrive`, one buffered console character plus availability flag, a
`skipLineFeed` state bit, a `millisecondsSnapshot` timer latch, and an `initialized` flag.

Initialization contract — `cpm80GuestInitialize(guest, host, ccpImage, bdosImage)`:

1. Reject (`cpm80GuestInitInvalidArg`) `NULL` guest/host/images, any `NULL` required
   `cpm80HostOps` callback (`consoleAvailable`, `consoleRead`, `consoleWrite`,
   `diskDriveAvailable`, `diskDriveProfile`, `diskReadRecord`, `diskWriteRecord`, `yield`),
   an already-initialized guest, or a guest with non-`NULL` CPU memory.
2. For every drive the host reports available, obtain its profile; an out-of-range profile
   value fails the whole initialization as `cpm80GuestInitConfig` (zeroing the guest).
3. Copy the CCP and BDOS images into guest-owned buffers; the originals may then be freed
   by the caller.
4. Initialize the CPU; on allocation failure return `cpm80GuestInitNoMem` (zeroing the
   guest). A distinct `cpm80GuestInitResult` per cause lets the caller report the right
   problem.
5. Install the full system (memory image + BIOS) and enter the initialized state.
   The drive profiles are sampled once at initialization; they MUST NOT change while the
   guest runs.

`cpm80GuestDestroy` destroys the CPU and zeroes the guest.

## 8. Host callback contract (`cpm80HostOps`)

```c
typedef struct
{
  bool (*consoleAvailable)(void *context);
  //-- <0 when none 
  int (*consoleRead)(void *context);                 
  void (*consoleWrite)(void *context, uint8_t character);
  bool (*diskDriveAvailable)(void *context, uint8_t drive);
  bool (*diskDriveProfile)(void *context, uint8_t drive, cpm80DiskProfile *profile);
  bool (*diskReadRecord)(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                         uint8_t record[128]);
  bool (*diskWriteRecord)(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                          const uint8_t record[128]);
  void (*yield)(void *context);
  void *context;
  //-- optional; NULL disables timer 
  hostReadMilliseconds readMilliseconds;             
  void *clockContext;
} cpm80HostOps;
```

Rules:

- All guest services flow through these callbacks; the guest never touches ESP-IDF,
  FatFS or LittleFS directly. This is what makes the core host-testable.
- `consoleRead` returns a byte 0–255, or a negative value when no character is available.
- Record callbacks transport exactly one 128-byte record per call.
- `yield` is called cooperatively (§10) and MUST return.
- `readMilliseconds(clockContext)` returns an unsigned 32-bit monotonically increasing
  millisecond count; the machine adapter supplies it from the ESP timer (µs / 1000).

## 9. Execution model

- One executed Z80 instruction per `cpm80CpuStep`. `cpm80GuestStep` refuses to step a
  halted processor.
- `cpm80GuestRunFor(guest, budget)` executes at most `budget` instructions, calling the
  host `yield` callback every 4096 executed instructions (`(instructions & 0xFFF) == 0`),
  and returns the number actually executed. It stops early when the CPU halts.
- The machine run loop starts the guest with `cpm80GuestColdBoot` and then runs batches of
  10,000 instructions with a FreeRTOS tick delay (`vTaskDelay(1)`) between batches, so the
  IDLE task and watchdog are serviced. When a batch returns 0 instructions (halted), the
  loop exits, the guest is destroyed, all images are closed, and control returns to the
  common host. Physical RESET always returns to the main menu (common host rule).
- The Z80 never receives interrupts: `int_pending`, `nmi_pending`, `iff1`, `iff2` and the
  interrupt mode are held at their reset values. CP/M 2.2 is interrupt-free, so no
  interrupt controller is modelled.
- There is no cycle-accurate timing model. The Z80 cycle counter is maintained by the core
  but no peripheral depends on it.

## 10. Guest memory map

| Address range | Contents | Installed by |
| --- | --- | --- |
| `$0000–$0002` | `JP $DA03` (BIOS WBOOT entry) — opcode `C3` + little-endian target. | `installSystem` |
| `$0003` | Console I/O byte, initialized `00`. | `installSystem` |
| `$0004` | Current drive number, A: = 0. | `installSystem` (preserved on warm boot) |
| `$0005–$0007` | `JP $CC06` (BDOS entry). | `installSystem` |
| `$0008–$005B` | Zero. `$0008` is the RST $08 debug trap (left empty). | zero fill |
| `$005C–$007F` | Default FCB (BDOS convention, zeroed). | zero fill |
| `$0080–$00FF` | Default 128-byte DMA buffer; default `dmaAddress = $0080`. Also the CCP stack region (`SP` starts at `$0100`). | zero fill / guest state |
| `$0100–$C3FF` | Transient program area (TPA). | guest BDOS |
| `$C400–$CBFF` | CCP image, 2,048 bytes. | private CCP copy |
| `$CC00–$D9FF` | BDOS image, 3,584 bytes; entry point `$CC06` (image base + 6). | private BDOS copy |
| `$DA00–$DA32` | BIOS jump table: 17 × 3-byte `JP` entries. | `installBios` |
| `$DA33–$DA87` | BIOS service stubs: 17 × 5-byte stubs. | `installBios` |
| `$DA90–$DA9F` | A: DPH (16 bytes). | `installBios` |
| `$DAA0–$DAAF` | A: DPB (16 bytes). | `installBios` |
| `$DAB0–$DB2F` | A: 128-byte directory buffer. | `installBios` |
| `$DB30–$DB3F` | A: SYSTEM CSV (16 bytes). | `installBios` |
| `$DB40–$DB5E` | A: SYSTEM ALV (31 bytes). | `installBios` |
| `$DB60–$DB6F` … per drive | B::`$DB60`, C::`$DC60`, D::`$DD60`, E::`$DE60` drive table slots, `$0100` bytes each. | `installBios` |
| `$DF60–$E05F` | F: drive table slot (includes BIG ALV up to `$E05F`). | `installBios` |
| `$E060–$E06F` | A: CSV when A: runs the LARGE profile (32 bytes) — the SYSTEM-sized A: slots are too small. | `installBios` |
| `$E080–$E09E` | A: ALV when A: runs the LARGE profile (31 bytes). | `installBios` |

All other addresses are ordinary RAM. Memory is a single flat byte array: ROM/RAM
distinction, write protection and banking are not implemented and not required.

## 11. System image format (A:)

`/littlefs/cpm80/system.dsk` is a raw CP/M disk image whose first 5,648 bytes have a fixed
layout:

| Image offset | Size | Contents |
| --- | ---: | --- |
| `$0000` | 2,048 | CCP binary (identical to `ccp-64k.bin`). |
| `$0800` | 3,584 | BDOS binary (identical to `bdos-64k.bin`). |
| `$1600` | 16 | `RETROCPM` resource header. |

Header bytes (16, exact): `R E T R O C P M` then `01 01`, then three little-endian
16-bit words `C400`, `CC00`, `DA00` (the guest load addresses). The remaining tracks
contain the CP/M directory (after the 2 reserved tracks of the profile) and file
allocations for the bundled software (§17). The BIOS is generated in guest memory at every
boot and MUST NOT be stored in the image.

Two A: profiles are bootable, distinguished by exact file size only:

| Profile | Exact image size |
| --- | ---: |
| SYSTEM | 256,256 bytes |
| LARGE | 512,512 bytes |

An A: image of any other size is invalid. The header is validated before every boot.

## 12. Boot specification

### 12.1 Cold boot

`cpm80GuestColdBoot` clears `millisecondsSnapshot`, resets the processor to
PC = `$DA00`, SP = `$0100`, all other registers and latches zero, `halted = false`. The
first executed instruction is the BIOS `BOOT` jump, which dispatches through port `$FE`
into `installSystem(cold = true)`:

1. Zero the entire 64 KiB.
2. Copy the private CCP image to `$C400` and BDOS image to `$CC00`.
3. Install the BIOS (§13): jump table, stubs, and all six drive DPH/DPB/CSV/ALV structures.
4. Install page zero: `JP $DA03` at `$0000`, I/O byte `00` at `$0003`, drive byte at
   `$0004` (forced 0 on cold boot, then replaced by the boot-drive fallback below), `JP $CC06`
   at `$0005`.
5. Reset guest state: `selectedDrive = 0`, `currentTrack = 0`, `currentSector = 0`,
   `dmaAddress = $0080`, and (cold boot only) clear the buffered console character, the
   `skipLineFeed` bit and the timer snapshot.
6. Reset the processor to PC = `$C400` (CCP entry) with C = current drive (the CCP reads
   the default drive from C), SP = `$0100`.

### 12.2 Warm boot (WBOOT)

The guest calls WBOOT (directly, or `JP $0000`, or Ctrl-C exit to command level). The host
performs the same install with `cold = false` with these differences:

1. Only `$0000–$00FF` is re-zeroed; TPA and the timer snapshot survive.
2. The current drive byte at `$0004` is preserved and reinstalled, after the boot-drive
   fallback below.
3. `selectedDrive`, `currentTrack`, `currentSector` and `dmaAddress` are reset as above;
   console buffering and `skipLineFeed` are NOT cleared.

**Boot-drive fallback.** The CCP entry (re-)selects the drive it receives in C. If that drive
is not configured, the BDOS `SELDSK` fails, reports `Bdos Err On d: Select`, waits for a key
and warm-boots; the warm boot re-selects the same lost drive, so the CCP never reaches a
prompt (the classic CP/M drive-select dead-lock). Because the warm-boot hand-over to the CCP
belongs to the BIOS, `installSystem` replaces an unavailable current drive with the first
configured drive (A: when it exists) before starting the CCP; `$0004` and C are set to that
drive. A configured drive is never replaced, so a warm boot on `C>` still returns to `C>`.

Warm boot returns control to CP/M command processing; it MUST NOT touch the host menu or
the physical ESP32.

## 13. BIOS specification

### 13.1 Call mechanism

17 jump-table entries at `$DA00 + 3·n` contain `C3 lo hi` pointing to a 5-byte stub at
`$DA33 + 5·n`:

```text
3E nn     LD  A,nn      ; BIOS function index
D3 FE     OUT ($FE),A   ; virtual BIOS service port
C9        RET
```

Port `$FE` is project-defined; it is not physical hardware. A write to `$FE` dispatches on
the value (function index) against Z80 registers and guest state. Function order is the
CP/M 2.2 BIOS order:

| n | Name | n | Name |
| --- | --- | --- | --- |
| 0 | BOOT | 9 | SELDSK |
| 1 | WBOOT | 10 | SETTRK |
| 2 | CONST | 11 | SETSEC |
| 3 | CONIN | 12 | SETDMA |
| 4 | CONOUT | 13 | READ |
| 5 | LIST | 14 | WRITE |
| 6 | PUNCH | 15 | LISTST |
| 7 | READER | 16 | SECTRAN |
| 8 | HOME | | |

A write to `$FE` with a value ≥ 17 (unreachable through stubs, reachable by guest `OUT`)
MUST stop the guest: set PC = 0 and `halted = true`.

### 13.2 Function semantics

| Function | Contract |
| --- | --- |
| BOOT | `installSystem(cold)`. |
| WBOOT | `installSystem(warm)` per §12.2. |
| CONST | If no character is buffered, pull one from the host console through the input conversion of §16 and buffer it. Return A = `$FF` if a character is (now) available, else A = 0. A buffered character survives for CONIN. |
| CONIN | If a character is buffered, return it in A and clear the buffer. Otherwise set PC back to the CONIN stub (so the stub re-executes) and call the host `yield` — a cooperative wait loop. |
| CONOUT | Send the low byte of register C to `consoleWrite`. |
| LIST | No-op (no printer). |
| PUNCH | No-op (no punch). |
| READER | Return A = `$1A` (CP/M EOF). |
| HOME | Set `currentTrack = 0` (sector unchanged). |
| SELDSK | If C < 6 and `diskDriveAvailable(C)`: `selectedDrive = C`, return HL = that drive's DPH address (§14.2). Otherwise return HL = 0 and leave `selectedDrive` unchanged. HL = 0 is the CP/M "bad drive" indication; the genuine BDOS reports its fatal drive error and must NOT alias to A:. |
| SETTRK | `currentTrack = BC`. |
| SETSEC | `currentSector = BC`. |
| SETDMA | `dmaAddress = BC`. |
| READ | Validate position and DMA (§15.3), read one 128-byte record via `diskReadRecord`, copy it to `memory + dmaAddress`, return A = 0. On any failure return A = 1 and leave the buffer untouched. |
| WRITE | Validate position and DMA, write 128 bytes from `memory + dmaAddress` via `diskWriteRecord`, return A = 0; on any failure (including host-reported read-only or I/O error) A = 1. |
| LISTST | Return A = 0 (printer never ready). |
| SECTRAN | Return HL = BC (identity sector translation). |

### 13.3 DPH and DPB byte layout

Each DPH is 16 bytes, zeroed then filled:

| DPH offset | Field |
| --- | --- |
| `$00–$07` | Zero (no offset, no allocation vector, no translation table). |
| `$08` | Directory buffer address (word). |
| `$0A` | DPB address (word). |
| `$0C` | CSV (checksum vector) address (word). |
| `$0E` | ALV (allocation vector) address (word). |

Each DPB is 16 bytes:

| DPB offset | Field | Encoding |
| --- | --- | --- |
| `$00` | SPT | word — 128-byte records per track |
| `$02` | BSH | byte — block shift factor |
| `$03` | BLM | byte — block mask |
| `$04` | EXM | byte — extent mask |
| `$05` | DSM | word — highest block number |
| `$07` | DRM | word — highest directory entry number |
| `$09` | AL0 | byte — directory block reservation, low |
| `$0A` | AL1 | byte |
| `$0B` | CKS | word — checksum vector size |
| `$0D` | OFF | word — reserved-track offset |

Addresses (all values fixed, from §10): A: DPH `$DA90`, DPB `$DAA0`, directory buffer
`$DAB0`, CSV `$DB30`, ALV `$DB40`; A: LARGE overrides CSV `$E060`, ALV `$E080`. Drives
B:–F: DPH at `$DB60 + 0x100·(drive−1)`, with DPB at +`$10`, directory buffer at +`$20`,
CSV at +`$A0`, ALV at +`$C0` relative to their DPH. Every directory buffer is 128 bytes.
CSV sizes: SYSTEM 16, LARGE 32, BIG 0 (CKS = 0). ALV sizes: SYSTEM/LARGE 31, BIG 64.
`installBios` zeroes the DPH, directory buffer, CSV and ALV of every drive at install.

## 14. Disk subsystem

### 14.1 Profiles

Images are raw, identity-ordered sequences of 128-byte records:

```text
//-- (track, sector zero-based)
recordOffset = ((track · sectorsPerTrack) + sector) · 128 
```

| Profile | Image size | Geometry (tracks × records/track) | Reserved tracks | Block size | Dir entries | DPB SPT,BSH,BLM,EXM,DSM,DRM,AL0,AL1,CKS,OFF |
| --- | ---: | --- | ---: | ---: | ---: | --- |
| SYSTEM | 256,256 | 77 × 26 | 2 | 1 KiB | 64 | `26, 3, 7, 0, 242, 63, $C0, 0, 16, 2` |
| LARGE | 512,512 | 77 × 52 | 2 | 2 KiB | 128 | `52, 4, 15, 0, 242, 127, $C0, 0, 32, 2` |
| BIG | 8,421,376 | 514 × 128 | 2 | 16 KiB | 512 | `128, 7, 127, 7, 511, 511, $80, 0, 0, 2` |

The profile geometry/DPB values above are the single source of truth (mirrored as named
constants in `cpm80Guest.h`/`cpm80Guest.c`, e.g. `cpm80BigImageSize = 8421376`,
`cpm80BigVirtualDpbExm = 7`). Identity SECTRAN means the image byte order equals the
CP/M logical record order; no skew is applied anywhere.

### 14.2 Per-drive state

Each drive has its own DPH/DPB/CSV/ALV (§13.3), profile (sampled at guest initialization),
open image handle and read-only flag, all owned by the machine adapter. Guest-side disk
state (`selectedDrive`, `currentTrack`, `currentSector`, `dmaAddress`) is per guest
instance, not per drive; BDOS sets position through SETTRK/SETSEC before each READ/WRITE.

### 14.3 Validation rules

The guest MUST reject a record access before calling the host when any of:

- `selectedDrive` ≥ 6,
- `currentTrack` ≥ profile track count,
- `currentSector` ≥ profile records-per-track,
- `dmaAddress > $FF80` (i.e. the 128-byte buffer would cross `$FFFF`).

The machine adapter MUST re-validate drive, track and sector against the mounted profile,
map `(track, sector)` through §14.1 to a file offset, signal the disk-activity indicator
(green read / red write), and use bounded random-access I/O only. A write is durable only
after a successful `imageFlush` of that image; `diskWriteRecord` returns success only when
both the write and the flush succeed. Record sizes are exactly 128 bytes end to end.

## 15. BDOS interface

The BDOS is the genuine CP/M 2.2 binary at `$CC00`; its entry point is `$CC06`, reached
through the page-zero vector. Transient programs call it with the CP/M convention:
`LD C,fn` / `LD DE,param` / `CALL 5`. All CP/M 2.2 documented functions (0–`$0D`: system
reset … process terminal EOF-style output; `$0E–$13`: select disk, read/write/open/close/
first/next search, delete, read/write random, login/logoff vector, random record read/write,
reset read-only; `$14–$23`: delete file, read/write sequential, reset directory, open file
create, rename, login vector, random block read/write, DRM select, allocate, guest jump,
parse FCB, rename FCB; `$24–$27`: random read with zero fill, write and extend; plus the
vendor-assignable range `$28–$3F` unused by this BIOS) behave exactly as implemented by the
upstream binary. The machine adds no BDOS emulation of its own. CP/M 2.2 fatal-error
behavior (e.g. BDOS error 04 on a null DPH from SELDSK) is produced by the genuine BDOS.

## 16. Console contract

The USB console is a byte-stream transport; the host MUST NOT interpret or generate
VT100/ANSI. The intended terminal personality is 80×24 VT100/ANSI, default workflow
Minicom on macOS. Guest output bytes are forwarded verbatim to `consoleWrite`.

Input conversion inside the guest (between `consoleRead` and the CP/M-visible character):

1. A returned CR is delivered as CR and sets `skipLineFeed`; an immediately following LF
   is suppressed (CR+LF delivers one CR).
2. A lone LF is delivered as CR.
3. The delivered character is masked to 7-bit ASCII (`& $7F`).
4. A negative `consoleRead` result means "none available".
5. CONST/CONIN share exactly one buffered character (§13.2).

Diagnostics/logging MUST NOT inject bytes into the guest console stream while the guest
runs.

## 17. A: system image contents

The generated `system.dsk` MUST contain, as CP/M directory entries and allocations:

- `HELLO.COM` — transient-program and warm-boot exercise.
- `WELCOME.TXT` — `TYPE` fixture.
- The utility set: `ASM.COM`, `DDT.COM`, `DUMP.COM`, `ED.COM`, `HELP.COM`, `HELP.HLP`,
  `LIB.COM`, `LINK.COM`, `LOAD.COM`, `MAC.COM`, `PIP.COM`, `RMAC.COM`, `STAT.COM`,
  `SUBMIT.COM`, `XREF.COM`, `XSUB.COM`, `ZSID.COM` (provenance and use scope in
  `components/cpm80Core/os/utilities/README.md`).

`DIR`, `ERA`, `REN`, `TYPE`, `USER` are resident CCP commands, not files. `SUBMIT`
requires a writable A: (it writes `$$$.SUB`).

## 18. Drive configuration (`drives.cfg`)

Path: `/littlefs/cpm80/drives.cfg`. Grammar, per non-comment line:

```text
drive=/path/to/image,RO|RW,SYSTEM|LARGE|BIG
```

Parser requirements (see `main/cpm80DriveConfig.c`):

- Blank lines and lines whose first non-blank character is `#` are comments. Spaces and
  tabs around fields are trimmed; trailing CR/LF is trimmed. Lines of 255 characters or
  more are rejected.
- Exactly one `=`; the drive name is one character `A`–`F` (case-sensitive uppercase);
  duplicates are rejected. The value is three comma-separated fields: path, mode
  (`RO`/`RW` exactly), profile (`SYSTEM`/`LARGE`/`BIG` exactly); a comma inside the
  profile name is rejected; an empty path is rejected.
- A: (`drive == A`) MUST name a LittleFS path under `/littlefs/cpm80/` and MUST NOT use
  BIG; its RO/RW mode is honored. B:–F: MUST name paths in the SD namespace
  `/retro/images/cpm80/`; the parser prefixes the stored path with `/microSD` to reach the
  VFS mount.
- Path safety (all drives): reject anything not starting with the required prefix, an
  empty name after the prefix, a trailing `/`, `//`, `/./`, `/../`, a final component `.`
  or `..`, backslashes, and any control character (< `$20` or `$7F`). Stored paths are
  limited to 192 bytes.
- Outcomes (`cpm80DriveConfigResult`): file missing → `Missing`, keep the built-in
  default table; any malformed line, over-long line or read error → `Invalid`, the whole
  file is discarded (partial entries are not kept) and the default table restored. A
  caller-visible error string identifies the failing line number.
- Built-in default table: A: = `/littlefs/cpm80/system.dsk`, RO, SYSTEM with
  size-detection enabled; B:–F: unconfigured.
- A: profile resolution (`cpm80DriveConfigResolveSystemProfile`): the effective profile
  comes from the exact image size (256,256 → SYSTEM, 512,512 → LARGE). When
  `drives.cfg` declares SYSTEM (or the default table is used), either size is accepted.
  An explicit `LARGE` on A: requires exactly 512,512 bytes. Any other size → A: is not
  mounted.
- Optional drives B:–F: mount only when configured, present, exactly the declared profile
  size, and openable with the requested access mode. A missing/mis-sized/unopenable
  optional image is simply absent to CP/M (SELDSK returns 0); it MUST NOT redirect to
  another drive.
- The `RO` flag governs emulator access to the image only. Editing images while the
  emulator is stopped is a common-host (Disk Image Manager) responsibility.

## 19. Machine lifecycle integration

Registry entry (in `main/machineRegistry.c`): display name `CP/M-80`, id `cpm80`, design
document `designCPM80.md`, implemented, SD not required for availability, resource
manifest entry for `/littlefs/cpm80/system.dsk`.

### 19.1 Probe (`cpm80MachineProbe`)

Fast, non-booting, never leaves files open:

1. Load `drives.cfg` (log missing/invalid; fall back to defaults).
2. Open the A: image with its configured mode. Open failure: file exists →
   `machineResourceInvalid`; file absent → `machineMissingResource`.
3. Resolve the A: profile by size; check the exact 16-byte `RETROCPM` header at `$1600`.
   Failure → `machineResourceInvalid` (with a specific log message).
4. Close the image; return `machineAvailable` when A: is RO, `machineWritable` when RW.

### 19.2 Initialize (`cpm80MachineInitialize`)

1. Refuse when the guest is already ready.
2. Load `drives.cfg`; open A:; validate profile + header (failure → close,
   `ESP_ERR_INVALID_SIZE`).
3. Allocate 5,632 bytes of staging (RAM), read CCP at offset 0 and BDOS at offset 2,048
   from A: into it (read failure → free, close, `ESP_FAIL`).
4. Open configured B:–F: images with exact-size validation, logging and skipping failures.
5. Build `cpm80HostOps` (console → `hostConsole*`, disk → `image*` with §14.3 checks,
   yield → `vTaskDelay(1)`, milliseconds → ESP timer µs/1000) and initialize the guest;
   map results: `Config` → `ESP_ERR_INVALID_STATE`, `InvalidArg` → `ESP_ERR_INVALID_ARG`,
   `NoMem` → `ESP_ERR_NO_MEM` (each with heap-usage logging), always closing images on
   failure.
6. Free the staging buffer; report ready.

### 19.3 Run (`cpm80MachineRun`)

Cold-boot the guest (fail → message, return to menu). Loop: `cpm80GuestRunFor(10000)`,
`vTaskDelay(1)`; exit when halted or when a batch executes 0 instructions. On exit:
destroy the guest, mark not ready, close every image (log close failures). Re-entering
CP/M from the menu runs initialize again from scratch.

## 20. Guest-visible timer (`RETRO80_TIMER_B1`)

Extension outside CP/M; it must not interfere with any CP/M program that does not use it.

| IN port | Behavior |
| --- | --- |
| `$F0` | Capability: `$B1` when a `readMilliseconds` callback is configured, else `$00`. Reading it MUST NOT sample the callback. |
| `$F1` | Latch a fresh snapshot from the callback and return bits 0–7. |
| `$F2`–`$F4` | Return bits 8–15 / 16–23 / 24–31 of the last latch, WITHOUT calling the callback. |
| other | `$FF` (open bus). |

With no callback all four ports read `$00`. OUT to these ports is a no-op, as is OUT to any
port other than `$FE`. The snapshot is guest-instance state: cleared by cold boot,
preserved by warm boot. The portable core depends only on the `hostReadMilliseconds`
function pointer so host tests inject deterministic values.

## 21. Host-side tools (media production requirements)

The firmware never formats media. These development-computer tools (all `python3`,
options via `--help`) MUST produce images that satisfy §11/§14 exactly:

| Tool | Requirement |
| --- | --- |
| `tools/createSystemDsk.py --os cpm80 --profile SMALL\|LARGE [--source-dir …]` | Build the A: image with CCP/BDOS/header at §11 offsets, directory after 2 reserved tracks, and the §17 file set; replaces the target image only on success. |
| `tools/buildDiskImage.py --os cpm80` | Deterministic curated composer for `littlefs/cpm80/system.dsk`. |
| `tools/diskImage.py --os cpm80 create/add/list/extract` | Create/inspect raw profile images; `add` validates 8.3 names, duplicates, extents, directory and block capacity, and writes atomically; `create` refuses an existing target without `--force`. |
| `tools/prepareSd.py` | Create the `/retro/…` tree and a sample `drives.cfg` without overwriting an existing one or formatting the card. |
| `tools/cpm80/cpm80HardwareTest.py` | Drive a running device over the console for the hardware procedures of §23. |

## 22. Verification: host tests

Run natively with ASan/UBSan and `-Wall -Wextra -Werror`:

```sh
cmake -S tests -B /tmp/retro-host-tests
cmake --build /tmp/retro-host-tests
ctest --test-dir /tmp/retro-host-tests -N
ctest --test-dir /tmp/retro-host-tests --output-on-failure -R '<name|name>'
```

ctest target names differ from file names; `tests/hostTests.c` runs as several named tests
(e.g. `hostCore`, `cpm80Help`), `tests/cpm80TimerTests.c` as `cpm80Timer`.

A conforming implementation MUST ship host tests covering each item below; the suite is
"done" only when every listed assertion passes deterministically with zero sanitizer
reports. Required coverage (current suites: `testCpm80Cpu`, `testCpm80GuestBoot`,
`testCpm80UnconfiguredDriveRecovery`, `testCpm80LargeSystemBoot`,
`testCpm80SystemProfileDetection`, `testCpm80DriveConfig`, `testCpm80Help`,
`testCpm80More`, `cpm80Timer`):

1. **CPU adapter:** init argument/state rejection, load range clamping at `$FFFF`,
   byte read/write over the full range, port callback wiring, destroy/re-init.
2. **Guest init:** every required-callback `NULL` rejected; unavailable-drive profiles
   ignored; out-of-range profile → `cpm80GuestInitConfig`; allocation failure path.
3. **Boot:** cold boot reaches the CCP (drive prompt on the injected console), genuine
   `DIR`/`TYPE` output; warm boot preserves `$0004` and the timer snapshot, cold boot
   clears them; `BOOT`/`WBOOT` reinstall page zero, CCP/BDOS and BIOS bytes; selecting an
   unconfigured drive reports one Select error and a following key warm-boots back to a
   prompt on the boot drive without a repeated error (`testCpm80UnconfiguredDriveRecovery`).
4. **BIOS ABI:** every stub at `$DA33+5n` equals `3E n D3 FE C9`; every vector equals
   `C3`+stub address; `CALL 5` reaches `$CC06`; unknown `$FE` function halts with PC = 0.
5. **Console:** CR/LF/CR+LF conversion, 7-bit masking, one-character CONST buffering with
   CONIN consumption, CONIN retry loop yields and eventually delivers.
6. **Disk:** for all three profiles — DPH/DPB/CSV/ALV bytes at the exact §13.3 addresses,
   first and last valid record read/write, out-of-range track/sector/drive and
   `dmaAddress $FF81` each produce A = 1 with the buffer unmodified, `$FF80` accepted;
   read-only drive write rejected A = 1; SELDSK on unavailable drive returns HL = 0
   without changing the selected drive; identity SECTRAN; persistence across
   close/reopen of the backing file.
7. **Drives.cfg:** defaults on missing/invalid, every §18 rejection rule (duplicate,
   unknown drive/profile/mode, long line, traversal forms, backslash, control chars,
   A: BIG, wrong prefixes), SD namespace prefixing, A: size-based profile detection
   (SYSTEM/LARGE/other), explicit-LARGE mismatch rejection, header mismatch rejection.
8. **Timer:** capability byte with/without callback, F1 latches (subsequent F2–F4 do not
   re-call the callback even when the injected value changes), zero bytes with no
   callback, writes ignored.

## 23. Verification: hardware acceptance

Acceptance is complete when every case below is executed on the target board (ESP32-S3,
LOLIN S3 Pro configuration, USB Serial/JTAG console, Minicom 80×24 on macOS, valid Retro
SD card) and each observation matches its pass criterion. Evidence per case records
firmware revision, ESP-IDF version, board, terminal, image profile, commands, expected and
actual behavior. A host-test pass or a successful build is never evidence of hardware
behavior.

| # | Procedure | Pass criterion |
| --- | --- | --- |
| H1 | RESET, menu shows CP/M-80; select it. | Probe reports A: correctly; machine starts and reaches the `A>` prompt. |
| H2 | `DIR`, `TYPE WELCOME.TXT`, `HELLO`. | Listings and text match image contents; HELLO runs and returns to `A>` (transient load + warm boot). |
| H3 | Warm boot from CP/M (`CTRL-C` at command level or guest WBOOT path). | Returns to `A>` on the same drive; timer snapshot survives. |
| H4 | A: configured RO: attempt a write (`PIP A:X.TXT=B:Y.TXT` style). | CP/M reports read-only failure; no crash; image bytes unchanged. |
| H5 | A: (or a B:–F: drive) configured RW: create/modify a file, exit to menu (RESET), re-enter. | Data persists byte-identically after restart. |
| H6 | Remove/rename one optional image; also supply a wrong-size image. | That drive is absent (`A>` on it gives the BDOS drive error); other drives and A: unaffected; no aliasing to A:. |
| H7 | Mount a BIG (8,421,376-byte) image on C: or D: and `DIR` it. | Full directory listed; reads of the last track/sector succeed (exercises 514×128 geometry). |
| H8 | Terminal checks with a full-screen program (`DDT`, `ED`). | CR/LF, backspace, erase, cursor and clear behave in the 80×24 terminal; host logs never corrupt guest output. |
| H9 | Timed guest workload via the timer ports (e.g. `BENCH80`). | F0 = `$B1`; elapsed values track wall-clock within tolerance of the injected batch scheduling. |
| H10 | Sustained run ≥ 30 minutes with disk activity (e.g. repeated `DIR`, `SUBMIT` loop). | No watchdog reset; RGB LED shows green/red per read/write and idles 120 ms after activity. |
| H11 | Repeated enter/exit cycles (≥ 20) and re-entry after guest halt. | Stable: initialize/run/destroy each time, no leaks (heap logging stays flat), menu remains usable. |

## 24. Definition of done

1. All §22 host tests pass (sanitizer-clean) and all §23 hardware cases are recorded as
   passing.
2. The guest is the genuine pinned CCP/BDOS; no BDOS logic exists in host code.
3. The core (`cpm80Cpu`/`cpm80Guest`) builds and tests natively with no ESP-IDF or
   filesystem/console dependencies beyond `cpm80HostOps`.
4. Every acceptance-gated behavior in this document is traceable to code: memory map
   §10, headers §11–§13, ports §14/§20, configuration §18, lifecycle §19.
5. Provenance, licenses and rights records for Z80 core, CCP/BDOS and all bundled
   applications are present in-tree.
