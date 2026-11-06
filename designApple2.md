# Apple II Machine Design

[← Back to the Apple II guide](assets/readMes/README_APPLE2.md)

## 1. Purpose and scope

This document is the complete technical design of the Apple II guest machine hosted by the
ESP32-S3 Retro Computer. It defines the architecture, the guest-visible hardware, the memory
map, the ROM/boot contract, the Language Card and optional memory models, the keyboard and
text display, the slot-3 80-column card, the Disk II and block-device controllers, the drive
configuration, the virtual add-on devices, the machine lifecycle and the testing required to
prove a correct implementation.


Menu option 4 (Apple II) and menu option 3 (UCSD Pascal) run the same machine implementation
with different `drives.cfg` profiles; the Pascal profile is defined by `designUCSD.md`. This document plus [projectPrompt.md](./projectPrompt.md) (common
host services: menu, console, LittleFS, SD, image I/O, disk-activity LED) describes the implementation of the Apple2e emulator.

### Requirement levels

- **MUST** — mandatory; an implementation that violates it is non-conforming.
- **SHOULD** — mandatory unless a documented, explicit deviation is accepted.
- **MAY** — optional.

## 2. Design goals

1. Execute authentic Apple II firmware instructions on an NMOS 6502 core inside a 48 KiB
   motherboard RAM plus 16 KiB bank-switched Language Card address space (128 KiB in the
   optional IIe and Saturn 128K models); the host never substitutes its own BASIC, DOS or
   Pascal at command level.
2. Provide the Disk II controller, the block-device (SmartPort/ProDOS) interface and the
   slot-3 80-column card as guest-visible devices driven by softswitches and slot ROM, with
   project-authored firmware where no redistributable ROM exists.
3. Support Disk II drives in slots 4–7 (two drives per controller) and up to four 512-byte
   block devices per slot, with independent image file, profile, sector order and access
   mode; the boot drive SD6.1 is required.
4. Keep the CPU core, device models and disk controller free of ESP-IDF dependencies so they
   compile and run natively for host-side testing; all filesystem, console and indicator
   ownership stays in the machine adapter.
5. Cooperate with FreeRTOS: bounded cycle batches with periodic yields so the task scheduler
   and watchdog are serviced during guest execution.
6. Render the 40-column motherboard text and the 80-column card text to the USB terminal as
   ANSI/VT100 without intercepting ROM print calls; terminal output reflects guest video
   memory.

Non-goals: graphics/high-resolution rendering, sound, paddle/Shift game-port input,
cycle-accurate raster timing, physical floppy hardware, and terminal emulation inside the
host console.

## 3. Responsibility boundary

| Layer | Owner | Provides |
| --- | --- | --- |
| Menu, machine registry, probe states | common host | Selection, lifecycle scheduling |
| USB Serial/JTAG byte-stream console | common host | `hostConsoleCharAvailable/PeekChar/GetChar/PutChar` |
| LittleFS mount, SD/FatFS mount, path resolver | common host | VFS paths, `storageResolveRetroPath` |
| Virtual image random access | common host | `imageOpen/imageReadAt/imageWriteAt/imageFlush/imageClose/imageSize` |
| Disk-activity RGB indicator (GPIO 38) | common host | `diskActivityRead()` green, `diskActivityWrite()` red |
| NMOS 6502 core | `components/apple2Core/third_party/chips/m6502.h` | Upstream cycle interpreter |
| Bus, devices, controllers, adapters | Apple II machine | Everything below this line |

The Apple II machine MUST NOT start Wi-Fi or any HTTP server, MUST NOT open the console or
mount filesystems itself, and MUST NOT be active at the same time as the browser File
Transfer service (enforced by the common host menu).

## 4. Architecture and module boundaries

```text
machine registry (main/machineRegistry.c)
  -> apple2MachineProbeWithConfig / apple2MachineInitializeWithConfig / apple2MachineRun
                                                                     main/apple2Machine.c
       -> apple2DriveConfigLoad / MACHINE / RAMCARD settings         main/apple2DriveConfig.c
       -> apple2DiskImage sector/block random access + probe         main/apple2DiskImage.c
       -> apple2Keyboard terminal translation                        main/apple2Keyboard.c
       -> apple2Info RETROAPPLE-SYSINFO/1 stream                     main/apple2SystemInfo.c
       -> imageFile, diskActivity, hostConsole                       (common host)
       -> apple2Core                                                 components/apple2Core/apple2Core.c
            -> m6502 cycle core + generated fast core                third_party/chips/m6502.h,
                                                                      apple2Fast6502.inc
            -> apple2Disk Disk II model                              components/apple2Core/apple2Disk.c
            -> generated slot ROMs and card firmware                 apple2DiskBootRom.h,
                                                                      apple2PascalCardRom.h, videxFirmware
```

| Module | Responsibility |
| --- | --- |
| `main/apple2Machine.c` | Registry callbacks; ROM load; open/attach/close images; input queue; ANSI renderer; run loop; disk-activity signalling. |
| `main/apple2DriveConfig.c/.h` | Parse and validate `drives.cfg`; `MACHINE=`/`RAMCARD=` settings; path confinement. |
| `main/apple2DiskImage.c/.h` | Profile sizing, sector/block mapping, `.po`/`.do` order selection, content probe, flush on write. |
| `main/apple2Keyboard.c/.h` | Terminal byte stream → Apple II key codes (cursor keys, CR/LF/DEL, ESC pairs). |
| `main/apple2SystemInfo.c/.h` | Slot-1 SYSINFO stream (`RETROAPPLE-SYSINFO/1`). |
| `components/apple2Core/apple2Core.c` | Bus decode, memory map, Language Card, IIe model, keyboard latch, Videx card, block-device execution, cycle/fast 6502 loop. |
| `components/apple2Core/apple2Disk.c` | Disk II controller: stepper, nibble rotation, GCR encode/decode, write capture. |

The core and device models MUST NOT include ESP-IDF headers except through the conditional
allocation block described in §6, and MUST NOT call filesystem or console APIs directly — the
core only sees the caller's sector/block callbacks.

## 5. Guest resources and provenance

| Resource | Provenance | Use |
| --- | --- | --- |
| `m6502.h` | `floooh/chips`, pinned commit `ee88c35ad6427341aa6999c3b07233e1f8bd2396`, zlib/libpng, vendored under `components/apple2Core/third_party/chips/`. | NMOS 6502 cycle interpreter. |
| `littlefs/apple2/apple2.rom` | 12 KiB Apple II Autostart Monitor + Integer BASIC image (`assets/apple2_roms/apple.rom`); Apple firmware released by Steve Wozniak for non-commercial use. | Default system ROM (window `$D000–$FFFF`). |
| `littlefs/apple2/apple2e.rom` | Upper 16 KiB of the unenhanced NMOS Apple IIe ROM (`assets/apple2_roms/`). | ROM for the optional `MACHINE=IIE` model (window `$C000–$FFFF`). |
| `littlefs/apple2/system.dsk` | Built by `tools/diskManager.py --type SYSTEMDSK --os apple2` (ProDOS `/SYSTEM` volume). | Boot drive SD6.1 image. |
| `apple2DiskBootRom.h` | Project-authored, generated by `tools/apple2/buildApple2DiskBootRom.py`. Do not edit by hand. | Disk II boot ROMs for slots 4–7. |
| `apple2PascalCardRom.h` | Project-authored, generated by `tools/ucsd/buildApple2PascalCardRom.py`. Do not edit by hand. | Slot-3 card slot ROM and Apple Pascal firmware-protocol layer. |
| `videxFirmware[]` (in `apple2Core.c`) | Project-authored. | Slot-3 80-column `PR#3` COUT driver and CRTC init. |
| `apple2Fast6502.inc` | Generated by `tools/genM6502Fast.py` from `m6502.h`. Do not edit by hand. | Instruction-stepped fast 6502 path. |

Emulated `$Cxxx` I/O on unmodelled addresses uses the last data-bus value; an accurate
floating-bus model is not implemented and not required.

## 6. CPU core (`apple2Core`)

Exact interface (see `include/apple2Core.h`):

```c
enum
{
  apple2RamSize = 48 * 1024,
  apple2RomSize = 12 * 1024,
  apple2IIeRomSize = 16 * 1024,
  apple2TextRows = 24,
  apple2TextColumns = 40,
  apple2VidexTextRows = 24,
  apple2VidexTextColumns = 80,
  apple2DiskFirstSlot = 4,
  apple2DiskLastSlot = 7,
  apple2DiskDrivesPerSlot = 2,
  apple2DiskMaxTracks = 200,
  apple2SmartPortSlot = 5,
  apple2SmartPortDevices = 4,
  apple2SmartPortBlockSize = 512,
  apple2TimerSlot = 2,
  apple2TimerCapabilityAddress = 0xC0A0,
  apple2TimerLowAddress = 0xC0A1,
  apple2TimerHighAddress = 0xC0A4,
  apple2TimerCapability = 0xC1
};

typedef enum
{
  apple2CoreOk, apple2CoreInvalidArgument, apple2CoreNoMemory,
  apple2CoreInvalidRom, apple2CoreKeyBusy
} apple2CoreResult;

apple2CoreResult apple2CoreCreate(apple2Core** core);
void apple2CoreDestroy(apple2Core* core);
apple2CoreResult apple2CoreSetTimeSource(apple2Core* core, hostReadMilliseconds readMilliseconds,
                                         void* context);
apple2CoreResult apple2CoreSetRamCard(apple2Core* core, apple2RamCard ramCard);
apple2CoreResult apple2CoreEnableIIe(apple2Core* core);
bool apple2CoreIsIIe(const apple2Core* core);
apple2CoreResult apple2CoreLoadRom(apple2Core* core, const uint8_t* rom, size_t size);
apple2CoreResult apple2CoreReset(apple2Core* core);
apple2CoreResult apple2CoreRunCycles(apple2Core* core, size_t cycles);
apple2CoreResult apple2CoreReadMemory(apple2Core* core, uint16_t address, uint8_t* value);
apple2CoreResult apple2CoreWriteMemory(apple2Core* core, uint16_t address, uint8_t value);
apple2CoreResult apple2CoreSetCharacterOptions(apple2Core* core, bool lowercaseCharacterRom,
                                               bool lowercaseKeyboard);
apple2CoreResult apple2CoreSetBootIn80Columns(apple2Core* core, bool enabled);
char apple2CoreDecodeTextCharacter(const apple2Core* core, uint8_t value);
apple2CoreResult apple2CorePressKey(apple2Core* core, uint8_t character);
bool apple2CoreKeyPending(const apple2Core* core);
```

Requirements:

1. `apple2CoreCreate` allocates the 48 KiB main RAM and the Language Card RAM. On ESP-IDF
   targets allocate PSRAM first (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`), falling back to
   internal RAM; on the native host use `calloc`. The bus value starts at `$FF`.
2. `apple2CoreSetRamCard` replaces the card RAM (16 KiB `apple2RamCardLanguage16` default,
   128 KiB `apple2RamCardSaturn128`) and clears it; call before `apple2CoreLoadRom`.
3. `apple2CoreEnableIIe` allocates 48 KiB auxiliary RAM, a 32 KiB Language Card (main +
   auxiliary copy) and the 16 KiB IIe ROM image; it clears the Saturn selection and the
   boot-in-80-columns flag. Call before `apple2CoreLoadRom`.
4. `apple2CoreLoadRom` requires the exact size (12,288 bytes for the Apple II, 16,384 for
   the IIe). The reset vector must be inside the ROM window: at file offset `$2FFC` (12 KiB)
   it must read ≥ `$D000`; at file offset `$3FFC` (16 KiB) it must read ≥ `$C100`. A bad
   vector returns `apple2CoreInvalidRom`. Load performs a reset.
5. `apple2CoreReset` initialises: video state (`textMode = true`, all else false), Videx
   registers/workspace/output-selection cleared, keyboard data/strobe cleared, timer
   snapshot cleared, Language Card bank 2 selected with reads from ROM and writes disabled,
   all IIe flags cleared, bus value `$FF`, every Disk II controller's switches reset. With
   `bootIn80Columns` and the Apple II model the card is activated as in §12.4. The 6502 is
   re-initialised via `m6502_init` with decimal mode enabled.
6. `apple2CoreRunCycles` runs the CPU for exactly `cycles` bus cycles. Two paths must behave
   identically: the cycle-stepped reference loop (`m6502_tick` plus `readAddress`/
   `writeAddress`) and the instruction-stepped fast core generated from `m6502.h`. The fast
   core MUST bail to the reference path whenever the address falls in `$C000–$CFFF`, the
   Videx zero-page workspace is active, or a lowercase echo correction is pending. The
   test switch `apple2CoreDisableFast6502` forces the reference path everywhere.
7. `apple2CorePressKey` rejects characters > `$7F` and returns `apple2CoreKeyBusy` while the
   data-ready strobe is still set. With `lowercaseKeyboard` false, letters are forced to
   uppercase before latching.
8. The machine adapter owns the time source; `apple2CoreSetTimeSource` enables the timer
   card of §17 and reserves slot 2 from generic slot-I/O devices.

## 7. Guest memory map (Apple II / II+ model)

| Address range | Contents | Notes |
| --- | --- | --- |
| `$0000–$BFFF` | 48 KiB motherboard RAM. | Zero page and stack page in RAM. |
| `$0006–$000D` | Zero page. | Shadowed to the card workspace while the slot-3 firmware executes (§12.5). |
| `$0024/$0025` | CH / CV (text cursor column/row). | Followed by the 80-column card; §12. |
| `$0028/$0029` | BASL/BASH (text output base address). | |
| `$0036/$0037` | CSW output vector. | `$FDF0` motherboard COUT1, `$C8B3` card COUT. |
| `$0400–$07FF` / `$0800–$0BFF` | Text page 1 / page 2. | Non-linear row map, §11.1. |
| `$C000` | Keyboard data (bit 7 = data ready). | §10. |
| `$C010` | Keyboard strobe clear (read or write). | |
| `$C050–$C057` | Video softswitches. | §11.2. |
| `$C080–$C08F` | Slot-0 Language Card / Saturn switches. | §8. |
| `$C090–$C09F` | Slot-1 I/O (SYSINFO device). | §17.2. |
| `$C0A0–$C0AF` | Slot-2 I/O (timer card at `$C0A0–$C0A4`). | §17.1. |
| `$C0B0–$C0BF` | Slot-3 80-column card CRTC/bank I/O. | §12.1. |
| `$C0C0–$C0FF` | Disk II I/O, slot n at `$C080 + n·$10`. | §13.2. |
| `$C100–$C3FF` | Slot ROM space; slot-3 card ROM at `$C300–$C3FF`. | §12.2. |
| `$C400–$C7FF` | Slot ROM space for disk/block slots 4–7 at `$Cn00–$CnFF`. | §13.6, §14. |
| `$C800–$C970` | Slot-3 card firmware (369 bytes, project-authored). | §12.3. |
| `$CA00–$CABE` | Slot-3 Pascal firmware layer (475 bytes, generated). | §12.7. |
| `$CC00–$CDFF` | Card video RAM window (512 bytes of the banked 2 KiB). | §12.1. |
| `$D000–$DFFF` | Language Card bank 2 (default) or bank 1 (`$C088–$C08F`); ROM when reads disabled. | §8. |
| `$E000–$FFFF` | Language Card shared 8 KiB; system ROM when reads disabled. | §8. |

The 12 KiB system ROM occupies `$D000–$FFFF`. Memory is a flat byte array plus the card RAM;
there is no write protection other than the Language Card switches. Unmodelled `$Cxxx` reads
return the last data-bus value.

## 8. Language Card and 128 KiB options

### 8.1 `$C080–$C08F` switch semantics (Apple II model)

`accessLanguageCardSwitch` decodes the offset bits:

- Bank select: bank 2 (default) when bit 3 clear, bank 1 when bit 3 set.
- Read mapping: Language Card RAM when `(address & $03)` is `$00` or `$03`; ROM otherwise
  (`(address & $03)` `$01` or `$02`).
- Even offsets (bit 0 clear): disable writes and clear the prewrite latch.
- Odd offset write: clear the prewrite latch (writes do not arm).
- Odd offset read: if prewrite was set, enable writes; set prewrite. (Two successive odd
  reads enable writes; writes stay enabled even while ROM is selected for reads.)

Card layout: two 4 KiB banks at `$D000–$DFFF` (bank 1 low half, bank 2 high half) followed
by one shared 8 KiB area at `$E000–$FFFF`. Reset selects bank 2, maps ROM and disables
writes without erasing card RAM.

### 8.2 Saturn 128K slot-0 card (`RAMCARD=SATURN128`, Apple II model only)

Eight 16 KiB banks, each with the Language Card layout. Offsets with bit 2 set only select
a bank (`$C084–$C087` → banks 0–3, `$C08C–$C08F` → banks 4–7); a bank select clears the
prewrite latch and does not change the read/write state. All other offsets behave as §8.1.
Reset selects bank 0, ROM active, writes off. The card RAM lives in one heap allocation
(PSRAM on the ESP).

### 8.3 Apple IIe model (`MACHINE=IIE`)

Selected by `MACHINE=IIE` in `drives.cfg`; `RAMCARD` is ignored. 64 KiB main + 64 KiB
auxiliary RAM (48 KiB + 16 KiB Language Card each), built-in Language Card and 80-column
softswitches. The Videx, Pascal-card and lowercase-echo behaviours are off; the ROM is the
16 KiB unenhanced IIe image.

- Writes `$C000–$C00F` (low bit = on, pair by `(address & $0E)`): `$C000/$C001` 80STORE,
  `$C002/$C003` RAMRD, `$C004/$C005` RAMWRT, `$C006/$C007` INTCXROM, `$C008/$C009` ALTZP
  (also moves the Language Card to its auxiliary copy), `$C00A/$C00B` SLOTC3ROM,
  `$C00C/$C00D` 80COL, `$C00E/$C00F` ALTCHARSET.
- Reads `$C011–$C01F` return the flag in bit 7 plus `keyboardData & $7F`: `$C011` BSRBANK2,
  `$C012` BSRREADRAM, `$C013` RAMRD, `$C014` RAMWRT, `$C015` INTCXROM, `$C016` ALTZP,
  `$C017` SLOTC3ROM, `$C018` 80STORE, `$C019` VBL (`cycles % 17030 ≥ 4550`), `$C01A` TEXT,
  `$C01B` MIXED, `$C01C` PAGE2, `$C01D` HIRES, `$C01E` ALTCHARSET, `$C01F` 80COL.
- `$0000–$01FF` follows ALTZP; under 80STORE, `$0400–$07FF` (and `$2000–$3FFF` when HIRES)
  select auxiliary when PAGE2 is set, main otherwise; `$0200–$BFFF` follows RAMRD (reads) /
  RAMWRT (writes).
- `$C100–$CFFF`: with INTCXROM the built-in ROM. Otherwise `$C800+` returns ROM while
  INTC8ROM is latched (an access at `$C300–$C3FF` sets it, a read of `$CFFF` clears it);
  `$C100–$C7FF` returns the disk boot ROMs or block-device ROMs of attached slots 4–7,
  then the bus value.
- `$C061–$C063` read 0. Text: with 80COL, even columns come from auxiliary memory, odd
  columns from main memory.

## 9. Machine models at a glance

| Model | RAM | ROM | Selected by |
| --- | --- | --- | --- |
| Apple II / II+ (default) | 48 KiB + 16 KiB Language Card | 12 KiB at `$D000–$FFFF` | `MACHINE=II` or missing |
| Apple II + Saturn 128K | 48 KiB + 128 KiB card | same | `RAMCARD=SATURN128` |
| Apple IIe (unenhanced) | 64 KiB main + 64 KiB aux | 16 KiB at `$C000–$FFFF` | `MACHINE=IIE` |

## 10. Keyboard and lowercase

The host delivers characters, not key/modifier events.

- `$C000` read returns `keyboardData` OR'd with `$80` while the strobe is set; `$C010`
  (read or write) clears the strobe. On the IIe `$C010` reads return the data with the
  pre-clear strobe bit.
- `apple2CorePressKey` accepts only 7-bit characters and refuses while a key is pending.
- The input translator (`main/apple2Keyboard.c`) converts the terminal byte stream: CR and a
  lone LF both deliver CR (CR+LF delivers one CR); `$08` and `$7F` deliver `$08`; bytes >
  `$7F` are dropped; `ESC [` / `ESC O` + `A–D` deliver Up `$0B`, Down `$0A`, Right `$15`,
  Left `$08`; other parameters (`0–9 ; <space>–?`) are consumed (max 8), a malformed
  sequence is dropped; `ESC` followed by an ordinary byte delivers both bytes so Apple ESC
  editing still works; a lone `ESC` is delivered after a 50 ms timeout by the machine
  adapter.
- Text decoding (`apple2CoreDecodeTextCharacter`): value & `$3F`; `$01–$1F` → `@`-based
  uppercase, `$20` → space, `$21–$3F` → ASCII. With the lowercase character ROM enabled,
  `$E1–$FA` decode to `a–z`. On the IIe with ALTCHARSET, `value & $7F` is direct ASCII
  (`$7F` → space). The machine profile enables both lowercase options.
- The stock ROM uppercases its key echo; to preserve terminal echo case, after a lowercase
  key is acknowledged the core converts the single matching uppercase echo write into the
  lowercase screen code (`$E1–` for motherboard text, the plain lowercase byte on the card)
  within a bounded window of 4096 cycles. This corrects the echo only; Applesoft string
  output and stored/listed program text are not modified.
- There is no Shift/PB2 sensing; `$C063` reads `$00` and is never derived from character
  case.

## 11. 40-column text display

### 11.1 Screen addressing

```text
address = (page2 ? $0800 : $0400) + ((row & 7) << 7) + ((row >> 3) * 40) + column
```

for rows 0–23, columns 0–39 (`apple2CoreTextAddress`).

### 11.2 Video softswitches

`$C050` TEXT off, `$C051` TEXT on, `$C052` MIXED off, `$C053` MIXED on, `$C054` PAGE 1,
`$C055` PAGE 2, `$C056` HIRES off, `$C057` HIRES on. Reads and writes both toggle (reads
have the same side effect as writes). The renderer shows text rows when TEXT is set, or
rows 20–23 when MIXED is set; a non-text screen renders `GRAPHICS DISPLAY NOT IMPLEMENTED`
on row 0.

### 11.3 Renderer contract

The machine adapter redraws at 10 Hz. Each 40/80-character row is compared with the last
successful output and only changed rows are written, positioned with `\x1b[<row>;1H`.
Inverse (`value & $C0 == $00`) uses `\x1b[7m`/`\x1b[27m`, flashing (`value & $C0 == $40`)
uses `\x1b[5m`/`\x1b[25m` and alternates glyph/space on a 500 ms phase; on the IIe
ALTCHARSET inverse is bit 7 clear and there is no flashing set. The cursor is positioned at
CH/CV (bounded to the screen) with `\x1b[?25h`. Entering the machine and switching between
40 and 80 columns clears the terminal (`\x1b[2J\x1b[H`).

## 12. Slot-3 80-column card (Videx-compatible profile)

The card is a Videx Videoterm-compatible model with project-authored firmware; this is a
software profile, not a historical claim. It is used only on the Apple II model.

### 12.1 I/O and video RAM

`$C0B0–$C0BF`: bank select is `((offset >> 2) & 3) * 512`; offset 0 (write) selects the
CRTC register (`value & $1F`), offset 1 writes the selected register or (read) returns it;
other offsets return the bus value. The card has 32 CRTC registers, 2 KiB of VRAM in four
512-byte banks (window `$CC00–$CDFF`) and an 8-byte private zero-page workspace.
`videxTextMode` holds when register 1 == 80 and register 6 == 24. `apple2CoreReadVidexTextCell`
reads VRAM at the start address formed from registers 12/13.

### 12.2 Slot ROM

`$C300–$C3FF` returns the generated Pascal card slot ROM: `$C300` `JMP $CA00` (card entry /
output chain), `$C303` `JMP $C800` (card init), `$C320` `JMP $CA08` (Pascal init entry),
driver entry table at `$C330–$C345`.

### 12.3 Card firmware (project-authored)

At `$C800` (369 bytes): the init routine programs the CRTC registers
`{62,50,50,28,19,00,18,18,00,0F,20,0F,00,00,00,00}`, sets CSW (`$36/$37`) to `$C8B3` and
clears the screen; the COUT routine preserves the incoming accumulator (including its high
bit — Applesoft relies on it while expanding `LIST` tokens), handles CR (column 0, next row,
scroll at row 24), backspace (column − 1), and writes printable bytes into the banked VRAM
using the row/bank lookup tables at `$C941`/`$C959`. `PR#3` sets CSW to `$C300`; the entry
initialises the card on first output and then chains to `$C8B3` while preserving the first
character.

### 12.4 Boot in 80 columns

`apple2CoreSetBootIn80Columns(true)` (called by the machine adapter on the Apple II model)
makes every reset program the CRTC registers, set CSW to `$C8B3`, select and clear the card,
and makes the monitor SETVID routine (`$FE93–$FEAF`, entered by reset and by DOS boot code)
store the card vector instead of `$FDF0`. While SETVID is active, writes to `$0036/$0037`
are redirected to `$C8B3/$C8`. In this mode a non-monitor write of `$FDF0` (e.g. ProDOS
BASIC.SYSTEM's reset) is rewritten to `$C8B3`, but the monitor (PR#0 through OUTPORT
`$FE95`) still selects the 40-column screen.

### 12.5 Output selection and workspace

Output selection follows CSW: the card is selected when the vector is `$C8B3`, or `$9EBD`
(Apple DOS hook) with the real vector `$C8B3` preserved at `$AA53/$AA54`, or a ProDOS
BASIC.SYSTEM hook in `$9000–$BEFF` with the real vector at `$BE30/$BE31` equal to `$C8B3`.
Half-written vectors (one byte matching a known vector) are ignored. A change of selection
clears the newly selected screen (and resets CH/CV/BASL/BASH when returning to 40 columns).
The monitor HOME routine (`$FC58`) also clears the card screen, but only while the monitor
ROM is visible (not when `$FC58` is Language Card RAM).

While the 6502 program counter is inside the card firmware, accesses to `$0006–$000D` use
the card's private workspace, seeded from `$0024/$0025` (CH/CV) so `HTAB`/`VTAB`/`POS` and
`HOME` move the card cursor too.

### 12.6 Applesoft LIST wrap

Applesoft wraps program listing at CH ≥ 33 (`CMP #$21` at `$D707`/`$D708`) which is correct
only for 40 columns. When the monitor ROM is visible, the byte at `$D708` is `$21`, the card
is the selected output in 80-column text mode and the Pascal card is not active, `readAddress`
returns 73 (80 minus the ROM's 7-column margin) for that operand. The ROM image is not
modified; `PR#0` keeps the original wrap.

### 12.7 Pascal card path

An opcode fetch at `$C320` (`apple2PascalInitEntry`) activates the Pascal card mode: the
card becomes the console (`videxOutputSelected` stays true), the core stops touching
Pascal's zero page and screen memory (no CH/CV following, no HOME trap, no CSW rewrites, no
lowercase echo correction), and the firmware at `$CA00` serves Apple Pascal 1.1's
character-output protocol.

## 13. Disk II controller

### 13.1 Model constants

```c
enum
{
  apple2DiskSectorsPerTrack = 16,
  apple2DiskSectorSize = 256,
  apple2DiskTrackCount = 35,
  apple2DiskNibblesPerSector = 416,
  apple2DiskNibblesPerTrack = 16 * 416,
  apple2DiskWriteDataSize = 343,
  apple2DiskCyclesPerNibble = 32,
  apple2DiskMaxHalfTrack = 69,
  apple2DiskMotorCoastCycles = 1000000,
  apple2DiskVolumeNumber = 254
};
```

Controllers exist for slots 4–7, two drives per controller, each drive with its own head
position (half-track 0–69), sector order and track count. A controller answers I/O and
exposes its boot ROM only when at least one drive has a medium.

### 13.2 Softswitch decode (`$C080 + slot·$10`, offset 0–$F)

`offset >> 1` selects the line; the low bit is on/off:

- Offsets 0–7: phase magnets 0–3. A phase-on event steps the selected drive's head one
  half-track toward the net pull of the energised phases (clamped to the medium's last
  half-track).
- `$C0E8/$C0E9` (offset 8/9): motor off/on. On motor-off the medium keeps spinning for
  1,000,000 cycles (coast-down); on motor-on the coast-down is cancelled.
- `$C0EA/$C0EB`: drive 2 select (a change aborts any active write capture and invalidates
  the nibble latch).
- `$C0EC/$C0ED`: Q6. `$C0EE/$C0EF`: Q7 (Q7-off aborts write capture).

### 13.3 Read path

With Q7 low, Q6 low and offset `$C` (read), the data latch is returned: the rotational
position is `(cycles / 32) % 6656`, never reset. A read in a new 32-cycle window loads the
next nibble (bit 7 set); repeated reads in the same window return `latch & $7F`. Odd
half-tracks and tracks ≥ the drive's track count read `$FF`. Motor stopped (after coast) or
no medium reads `$00`. Q7 high reads `$00`; Q6 high reads `$80` for read-only media and
`$00` for writable media.

### 13.4 Nibble format (16-sector 6-and-2)

`encodeSector` lays out 416 nibbles: self-sync `$FF`s, address prologue `D5 AA 96`, the
volume/track/sector/checksum as 4-and-4 pairs (`x | $AA`), address epilogue `DE AA EB`,
data prologue `D5 AA AD`, 342 GCR data nibbles + checksum nibble (the 64-entry
`gcrWriteTable` in `apple2Disk.c` is the authority), data epilogue `DE AA EB`. The 256 data
bytes become 86 low-bit-2 groups (bit-swapped) plus 256 high-2-bit values, XOR-encoded
against the previous value.

Sector skew (physical on-track sector → image sector), from `imageSectorOfPhysical`:

- DOS order (`.do`/`.dsk`): `0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15`
- ProDOS/Pascal order (`.po`): `0,8,1,9,2,10,3,11,4,12,5,13,6,14,7,15`

### 13.5 Write path

With Q7 high and Q6 low, writes to offset `$C` feed the bus value into the nibble state
machine: it scans for the data prologue `D5 AA AD`, records the head's track and the
rotating physical sector (`(cycles / 32) % 6656 / 416`), then collects 343 nibbles
(342 data + checksum). On completion the 6-and-2 decode and checksum are verified; on
success the sector is mapped through the drive's order table and written via the host
callback (which must flush); failures increment `writeFailures`. Every completed attempt
increments `writeAttempts`.

### 13.6 Boot ROMs

Each disk slot answers `$Cn00–$CnFF` with a generated project-authored boot ROM
(`apple2DiskBootRoms[slot − 4]`, 255 of 256 bytes): autostart signature
`24 20 24 00 24 03 24 3C` (so the Autostart ROM boots the highest slot with media),
`LDX #$<slot·16>`, head recalibration, a nibble reader that decodes track 0 sector 0 into
`$0200`, then `JMP $0801`. Regenerate with `tools/apple2/buildApple2DiskBootRom.py`; never
edit the header by hand.

## 14. Block devices (SmartPort / ProDOS)

A slot hosts either Disk II drives or block devices, never both. Up to four 512-byte units
per slot (units 1–4). Block n maps to image sectors `2·(n % 8)` and `2·(n % 8) + 1` of
track `n / 8` in ProDOS order.

### 14.1 Slot ROM

The generated `smartPortSlotRom` provides the signature bytes, `$Cn07` = `$3C` (autostart
block device — the Autostart ROM boots unit 1 at reset) or `00` (plain SmartPort interface),
`$Cn0A` ProDOS block entry, `$Cn0D` SmartPort call entry, capabilities byte at `$CnFE`
(`$C3 | (units−1)<<4`, `| $04` when writable), driver entry `$CnFF = $0A`, and boot code at
`$Cn6B` that reads block 0 of unit 1 into `$0800` through the ProDOS entry and jumps to
`$0801` with `X = slot<<4`.

### 14.2 Execution contract

The SmartPort trap fires only on an opcode fetch (SYNC) at `$Cn0A` or `$Cn0D` of an attached
block slot (suppressed when the IIe INTCXROM is set).

- `$Cn0D` (SmartPort): the stacked return address points at a command byte (`$40` bit =
  extended 9-byte parameter block); the parameter block's byte 0 must be `$3`. Command 0 =
  STATUS (unit 0 code 0 returns the 8-byte controller status whose first byte is the number
  of units, X=8; code 0 of a unit returns 4 bytes: characteristics `$F0`/`$B4` when
  writable/read-only + 24-bit block count; code 3 returns the 24-byte device info block,
  vendor `ESP32-S3 SMART`), 1 = READ BLOCK, 2 = WRITE BLOCK. Errors: `$01` bad command,
  `$04` bad count, `$11` bad unit, `$27` I/O error, `$2B` write protected, `$2D` bad block.
  After a successful READ the core sets X=0, Y=2.
- `$Cn0A` (ProDOS block driver): zero-page parameter block `$42` command (0 block count →
  X/Y, 1 read, 2 write), `$43` unit number (`slot<<4`, bit 7 selects unit 2, low nibble
  must be 0), `$44/$45` buffer pointer, `$46/$47` block number.

Block buffers must lie in main RAM (`$0000–$BFFF`) or the Language Card area
(`$D000–$FFFF`, honouring the write-enable switch); `$C000–$CFFF` is rejected.

## 15. Disk images and profiles

Raw images of 16 × 256-byte sectors per track; the size must match the profile exactly:

| Profile | Tracks | Image size | Blocks (512 B) |
| --- | ---: | ---: | ---: |
| `APPLE2_140K` | 35 | 143,360 | 280 |
| `APPLE2_640K` | 160 | 655,360 | 1,280 |
| `APPLE2_800K` | 200 | 819,200 | 1,600 |

Sector order is chosen from the file extension (`.po` → ProDOS/Pascal, `.do` → DOS), else
from the content probe, else DOS. The probe tests both orders for an Apple Pascal volume
header (directory block 2: first block 0, next block 6, kind 0, 1–7-character volume name,
block and file counts) or a ProDOS volume-directory entry in block 2, and reports the
detected name/block count; a disagreement between the chosen order and the probe is logged
as a warning (`orderSuspect`). A write is durable only after the image layer's flush
succeeds. The firmware never creates or enlarges images.

## 16. Drive configuration (`drives.cfg`)

The Apple II profile uses `/littlefs/apple2/drives.cfg`; the UCSD profile uses
`/littlefs/ucsd/drives.cfg`. `apple2MachineInitializeWithConfig` accepts only these two
paths. Grammar, per non-comment line (`#` or blank lines are ignored; spaces/tabs trimmed;
CR/LF trimmed):

```text
SD<slot>.<drive>=/path/to/image,RO|RW,APPLE2_140K|APPLE2_640K|APPLE2_800K
SP<slot>.<unit>=/path/to/image,RO|RW,<same profiles>
MACHINE=II|IIE
RAMCARD=LC16|SATURN128
```

Parser requirements (see `main/apple2DriveConfig.c`):

- Slots 4–7; `SD` units 1–2, `SP` units 1–4. Duplicate device lines and mixing `SD` and
  `SP` in one slot are rejected. Lines filling the 256-byte line buffer without a newline
  are rejected as too long.
- Exactly one `=` (not at the start); the value is three comma-separated fields: path, mode
  (`RO`/`RW` exactly), profile (exact, no embedded comma).
- Allowed image prefixes: `/littlefs/apple2/`, `/littlefs/ucsd/`, `/retro/images/apple2/`,
  `/retro/images/ucsd/` (SD paths are resolved through `storageResolveRetroPath` to the
  `/microSD` VFS mount). Path safety: longer than the prefix, no trailing `/`, no `//`,
  `/./`, `/../`, final `/.` or `/..`, no backslash, comma, or control character (< `$20` or
  `$7F`); stored paths limited to 191 characters.
- LittleFS images may be RW, except UCSD LittleFS images which may only be writable as the
  SD6.1 boot image.
- There is no default drive: every drive without a line stays empty. Outcomes: file missing
  → `apple2DriveConfigMissing`; any malformed line, over-long line or read error →
  `apple2DriveConfigInvalid`, the whole file is discarded (partial entries are not kept) and
  the caller-visible error names the failing line.
- The boot drive SD6.1 MUST be configured and its image openable; otherwise
  `apple2MachineInitialize` prints the reason and returns `ESP_ERR_NOT_FOUND` so the menu is
  shown again.

Per-slot mode selection (`attachDisks`): any `SP` entry makes the slot SmartPort; otherwise
any opened image whose probe finds ProDOS content makes the slot a block device with
autostart; otherwise an 800K Pascal boot image on SD6.1 also uses the block-device slot ROM;
otherwise the slot is Disk II.

## 17. Virtual add-on devices

### 17.1 Monotonic timer (`APPLE2_TIMER_C1`, slot 2, `$C0A0–$C0A4`)

| Address | Behaviour |
| --- | --- |
| `$C0A0` | Capability: `$C1` when a `readMilliseconds` callback is configured, else `$00`. Reading it MUST NOT sample the callback. |
| `$C0A1` | Latch a fresh unsigned 32-bit millisecond snapshot and return bits 0–7. |
| `$C0A2–$C0A4` | Return bits 8–15 / 16–23 / 24–31 of the last latch, WITHOUT calling the callback. |

Writes are ignored; reset clears only the snapshot. Configuring the timer reserves slot 2
from generic slot-I/O devices.

### 17.2 Emulator information interface (`RETROAPPLE-SYSINFO/1`, slot 1)

`$C090` reads return the magic `$A6` when supported; a write of `$01` builds a fresh
snapshot; reads of `$C091` drain the snapshot stream (zero after the last byte), which
starts with the signature `RETROAPPLE-SYSINFO/1`. The snapshot reports firmware version,
profile/model line, RAM description, PSRAM size, uptime, executed 6502 cycles and the
mounted volumes (slot, unit, RO/RW, SmartPort flag, byte size, path, volume name). It is an
add-on: a busy slot 1 never prevents the machine from starting.

## 18. Machine lifecycle integration

Registry entries (in `main/machineRegistry.c`): display name `Apple II`, id `apple2`, design
document `designApple2.md`; display name `UCSD Pascal`, id `ucsd`, design document
`designUCSD.md` (same run function). Both use the shared Apple II machine; SD is not
required for availability.

### 18.1 Probe (`apple2MachineProbeWithConfig`)

Fast, non-booting, never leaves files open:

1. Load `drives.cfg` (missing → `machineMissingResource`, invalid → `machineResourceInvalid`).
2. Probe the ROM matching `MACHINE=`: `apple2.rom` (12 KiB, vector at `$2FFC` ≥ `$D000`) or
   `apple2e.rom` (16 KiB, vector at `$3FFC` ≥ `$C100`); absent → `machineMissingResource`,
   wrong size/vector → `machineResourceInvalid`.
3. The SD6.1 image must exist with the size its profile names; otherwise
   `machineMissingResource` / `machineResourceInvalid`.
4. Close and reopen the boot image with its mode; return `machineWritable` when RW, else
   `machineAvailable`.

### 18.2 Initialize (`apple2MachineInitializeWithConfig`)

1. Refuse a configuration path other than the two LittleFS profile paths.
2. Read the selected ROM from LittleFS (exact size + EOF check); destroy any previous core.
3. Create the core; set the millisecond time source; enable IIe or configure Saturn per the
   settings; on the Apple II model enable boot-in-80-columns; set both lowercase options.
4. Load the ROM (reset runs).
5. Attach every configured drive per §16; SD6.1 must open or the machine is destroyed and
   `ESP_ERR_NOT_FOUND` returned.
6. Attach the SYSINFO interface to slot 1 (a failure only logs a warning).

### 18.3 Run (`apple2MachineRun`)

Clear the terminal, prime the keyboard translator (a leading LF suppresses its CR), then
loop: feed queued/host console input into the key latch (one pending key at a time), run
one batch of 6502 bus cycles (`cyclesPerPeriod` derived from 1.023 MHz), service the
watchdog with `vTaskDelay(1)` at least every 100 ms (default: run as fast as the host
allows; `APPLE2_REALTIME_PACING` paces at 1.023 MHz instead), signal the disk-activity LED
(green on every sector/block read via the host callbacks, red whenever the total Disk II
`writeAttempts` increases), and re-render the screen every 100 ms. The loop never exits
under guest control; physical RESET returns to the main menu (common host rule).

## 19. Development phases

Each phase is complete only when its acceptance test passes deterministically with zero
sanitizer reports, built with `-Wall -Wextra -Werror`. The test must fail before the phase
is implemented and pass after; later phases must keep earlier tests passing.

| Phase | Deliverables | Acceptance test |
| --- | --- | --- |
| 1 — CPU, ROM, keyboard, 40-column text | 6502 integration (cycle core + fast core with identical bus behaviour), 48 KiB RAM + 12 KiB ROM window with reset-vector validation, keyboard latch/strobe, video switches, text addressing/decoding, ANSI renderer, input translator. | `apple2SystemRom`: the genuine 12 KiB Autostart/Integer BASIC ROM boots to the `APPLE ][` prompt, the typed `PRINT 2+2` echoes and displays `4`, and the prompt returns (screen bytes and keyboard latch asserted). |
| 2 — Memory expansion | Language Card bank switching and two-read write enable, Saturn 128K option, IIe model (aux RAM, softswitches, status reads, ROM mapping, 80-column text page). | `apple2LanguageCard`: every `$C080–$C08F` behaviour via the memory API and executed 6502 cycles, bank isolation, shared-RAM boundary, write-enable retention across reset, Saturn per-bank isolation, IIe aux/main selection, ALTZP, 80COL column split and the 128K Pascal boot check. |
| 3 — Mass storage | Disk II controller (phases, motor coast, nibble rotation, GCR read/write), per-slot boot ROMs, image backend with profiles/order/probe and flush-on-write, `drives.cfg`, block devices (SmartPort/ProDOS) with trap and autostart. | `apple2ProdosAutostart`: ProDOS 8 boots from the SD6.1 block-device slot with the Autostart ROM (no `PR#`), `CATALOG,S6,D2` lists the 800K volume, a `SAVE` is found in the volume directory of the image file, and `apple2Disk`/`apple2MultiDisk` pass for the sector-level read/write, RO enforcement and persistence behaviour. |
| 4 — 80-column card and add-on devices | Slot-3 card I/O/CRTC/VRAM, project COUT driver with CR/backspace/scroll, output-vector tracking (DOS/ProDOS hooks), boot-in-80 + SETVID/HOME handling, Applesoft LIST wrap, Pascal card path, timer card, SYSINFO. | `apple2VidexCard`: a 6502 program executes the card firmware — init, 80 distinct cells in banked VRAM, CR, banks 2/3, multi-line scrolling and cursor — and output selection moves away from the card; supported by `apple2ProdosAutostart80Columns`, `apple2Sysview` and the timer assertions in `hostCore`. |

## 20. Host-side tools (media production requirements)

The firmware never formats media. These development-computer tools (all `python3`, options
via `--help`) MUST produce inputs that satisfy this document exactly:

| Tool | Requirement |
| --- | --- |
| `tools/genM6502Fast.py` | Regenerates `apple2Fast6502.inc` from the pinned `m6502.h`. |
| `tools/apple2/buildApple2DiskBootRom.py` | Regenerates the per-slot Disk II boot ROMs in `apple2DiskBootRom.h`. |
| `tools/ucsd/buildApple2PascalCardRom.py` | Regenerates `apple2PascalCardRom.h` (slot ROM + Pascal firmware layer). |
| `tools/apple2/buildApple2TestRom.py` | Regenerates the host-test diagnostic ROM fixture. |
| `tools/diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS[_640K]` | Deterministic ProDOS `/SYSTEM` boot disk composer for `littlefs/apple2/system.dsk`. |
| `tools/diskManager.py --type WORKDSK --os apple2 --profile 140K\|640K\|800K <image>` | Create empty volumes; refuses an existing target without `--force`. |
| `tools/extractFiles.py --os apple2 <image> --add/--extract/--remove` | Inspect and edit files inside supported images. |
| `tools/prepareSd.py --os apple2` | Create the `/retro/…` tree and a sample `drives.cfg` without overwriting an existing one or formatting the card. |
| `tools/apple2/apple2HardwareTest.py` | Drive a running device over the console for the hardware procedures of §23. |

## 21. Verification: host tests

Run natively with ASan/UBSan and `-Wall -Wextra -Werror -funsigned-char`:

```sh
cmake -S tests -B /tmp/retro-host-tests
cmake --build /tmp/retro-host-tests
ctest --test-dir /tmp/retro-host-tests -N
ctest --test-dir /tmp/retro-host-tests --output-on-failure -R '<name|name>'
```

ctest target names differ from file names. Required suites: `apple2SystemRom`, `apple2Core`,
`apple2LanguageCard`, `apple2VidexCard`, `apple2Fast6502` (fast-vs-reference equivalence
under `apple2CoreDisableFast6502`), `apple2Disk`, `apple2DiskStream`, `apple2MultiDisk`,
`apple2ProdosSystemImage`, `apple2ProdosDataImage`, `apple2ProdosAutostart`,
`apple2ProdosAutostart80Columns`, `apple2ProdosSmartPort`, `apple2Sysview`, and the
UCSD-profile boot tests (`ucsdPascalBoot`, `ucsdPascalIIeTextOpen` when the media fixtures
exist). A conforming implementation MUST ship host tests covering at least:

1. **CPU/bus:** ROM size and reset-vector rejection, reset contract of §6.5, fast core and
   reference core executing identical instruction/bus sequences.
2. **Language Card:** every switch-row of §8.1 through both the memory API and executed
   6502 cycles; Saturn bank isolation; IIe RAMRD/RAMWRT/ALTZP/80STORE/80COL behaviour and
   status reads.
3. **Keyboard:** latch/strobe separation, `apple2CoreKeyBusy`, uppercase forcing, echo
   correction window, translator cases of §10 (cursor keys, ESC pairs, CR/LF/DEL).
4. **Display:** text-address map for both pages, inverse/flash decode, lowercase codes and
   digits, Videx bank/select/register behaviour, scrolling, cursor bounds.
5. **Disk II:** nibble stream equality against a real image, skew tables both orders, motor
   coast, phase stepping, Q6/Q7 read semantics including `$80` on read-only media, write
   capture with valid and malformed checksums, persistence after reopen, RO refusal.
6. **Block devices:** SmartPort STATUS codes 0/3, controller status, READ/WRITE with bounds,
   write-protect and bad-block errors, buffer rules, ProDOS entry unit/command handling.
7. **drives.cfg:** every §16 rejection rule, path resolution, no-default behaviour,
   boot-drive-required failure, mode selection per slot.
8. **Add-ons:** timer capability/latch/no-resample/write-ignore rules; SYSINFO signature and
   drain.

## 22. Verification: hardware acceptance

Acceptance is complete when every case below is executed on the target board (ESP32-S3,
LOLIN S3 Pro configuration, USB Serial/JTAG console, 80×24 terminal, valid Retro SD card)
and each observation matches its pass criterion. A host-test pass or a successful build is
never evidence of hardware behaviour. The scripted driver is
`tools/apple2/apple2HardwareTest.py`.

| # | Procedure | Pass criterion |
| --- | --- | --- |
| H1 | RESET, menu shows Apple II (and UCSD Pascal); select it. | Machine starts, boots the system disk and shows the prompt on the 80-column card; the terminal is cleared and sized correctly. |
| H2 | ProDOS `CATALOG,S6,D1` and `CATALOG,S6,D2`; `BASIC.SYSTEM` runs STARTUP. | Volumes listed with their names; the 80-column rendering (inverse where used) is stable. |
| H3 | Save a file (BASIC `SAVE` or Pascal edit+save) on an RW volume, RESET, re-enter, re-read it. | The file persists byte-identically; RO volumes refuse writes and stay unchanged. |
| H4 | Boot a 140K Disk II image on a Disk II slot and `LOAD`/`RUN` a program. | Controller reads succeed; no watchdog reset; sector reads show green LED pulses. |
| H5 | Enter lowercase text; check key echo; run the timer benchmark (`assets/apple2/BENCH2`). | Lowercase echo preserved; benchmark elapsed time tracks wall clock reasonably. |
| H6 | PR#0 / PR#3 switching and HOME under DOS/Pascal/ProDOS. | Screen clears are correct, no stale content, cursor in bounds. |
| H7 | SYSINFO drain from the guest (slot 1). | Stream starts with `RETROAPPLE-SYSINFO/1` and reports the mounted volumes correctly. |
| H8 | Sustained run ≥ 30 minutes with disk activity. | No watchdog reset; LED shows green on reads and red only on write attempts (red held ≥ 300 ms). |
| H9 | Repeated enter/exit cycles (≥ 20) and re-entry after RESET mid-session. | Stable; media flushed/closed; menu remains usable; heap stays flat. |

## 23. Definition of done

1. All §21 host tests pass (sanitizer-clean) and all §23 hardware cases are recorded as
   passing; each phase's acceptance test of §19 is part of the passing suite.
2. The guest executes authentic ROM and disk software; no BASIC, DOS or Pascal behaviour is
   synthesised in host code (the project-authored slot ROMs and boot ROMs of §5 are the only
   host-authored guest code, and each has a checked-in generator).
3. The core (`apple2Core`, `apple2Disk`) builds and tests natively with no ESP-IDF or
   filesystem/console dependencies beyond the injected callbacks.
4. Every acceptance-gated behaviour in this document is traceable to code: memory map §7,
   card switches §8, IIe §8.3, keyboard §10, display §11–§12, Disk II §13, block devices
   §14, images/profiles §15, configuration §16, add-ons §17, lifecycle §18.
5. Provenance, licences and rights records for the 6502 core, ROMs and all bundled guest
   software are present in-tree (`docs/thirdParty.md` and the asset directories).

---

[← Back to the Apple II guide](assets/readMes/README_APPLE2.md)
