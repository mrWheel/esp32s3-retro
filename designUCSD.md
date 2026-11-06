# UCSD Pascal (Apple Pascal) Machine Design

[← Back to the UCSD Pascal guide](assets/readMes/README_UCSD.md)

## 1. Purpose and scope

This document is the complete technical design of the UCSD Pascal (Apple Pascal) guest
profile hosted by the ESP32-S3 Retro Computer. It defines the Pascal profile of the shared
Apple II machine: the profile resources and machine model, the boot contract to the Pascal
command line, the Pascal media and block-device volumes, the console contract, the
Pascal-specific slot firmware, the virtual add-on devices, the host-side media tools and
the testing required to prove a correct implementation.

Menu option 3 (UCSD Pascal) and menu option 4 (Apple II) start exactly the same Apple II
6502 machine; the selected `drives.cfg` alone defines the mounted disks, and the boot disk
determines whether Applesoft/ProDOS or Apple Pascal runs. This document plus
[projectPrompt.md](./projectPrompt.md) (common host services: menu, console, LittleFS, SD,
image I/O, disk-activity LED) and [designApple2.md](./designApple2.md) (CPU core, memory
map, Language Card, IIe model, keyboard, text display, Disk II, block devices, drive
configuration grammar, machine lifecycle and that machine's own development phases) is the
basis for implementing the UCSD Pascal profile. Where this document and `designApple2.md`
both describe a shared behaviour, the requirements are identical by construction.

There is no separate UCSD machine emulator, no CP/M core, no Z80 interpreter, no host-side
p-code VM and no host-synthesised Pascal prompt: the guest's own 6502 p-code interpreter,
on-disk loaders and filesystem code run unmodified from original Apple Pascal images.

### Requirement levels

- **MUST** — mandatory; an implementation that violates it is non-conforming.
- **SHOULD** — mandatory unless a documented, explicit deviation is accepted.
- **MAY** — optional.

## 2. Design goals

1. Boot original, matching Apple Pascal system images through the shared Apple II
   reset/autostart and slot-controller interfaces and their own on-disk loaders; the host
   never substitutes its own Pascal, command interpreter or p-code execution.
2. Support the Apple Pascal 1.3 128K system on the Apple IIe model (64 KiB main + 64 KiB
   auxiliary RAM) as the active profile, and the 64K Pascal 1.1 system on the Apple II
   model with the slot-3 80-column card as the second supported configuration.
3. Mount Pascal system and work volumes as 512-byte block devices (SmartPort/ProDOS block
   interface) with independent image file, profile, sector order and access mode; the boot
   drive SD6.1 is required.
4. Present the 80×24 Pascal console (IIe 80-column text page, or the slot-3 card on the
   Apple II model) to the USB terminal as ANSI/VT100 without intercepting ROM print calls;
   terminal output reflects guest video memory.
5. Keep the CPU core, device models and controllers free of ESP-IDF dependencies so they
   compile and run natively for host-side testing; all filesystem, console and indicator
   ownership stays in the machine adapter.
6. Reproducible media: every Pascal image the firmware accepts is produced by the
   checked-in host tools from checked-in source images and guest files.

Non-goals: any Z80/CP/M p-system backend, host-side Pascal compilation or p-code
execution, graphics/high-resolution rendering, sound, paddle input, cycle-accurate raster
timing, physical floppy hardware, larger-than-800K Pascal volumes, and terminal emulation
inside the host console.

## 3. Responsibility boundary

| Layer | Owner | Provides |
| --- | --- | --- |
| Menu, machine registry, probe states | common host | Selection, lifecycle scheduling |
| USB Serial/JTAG byte-stream console | common host | `hostConsoleCharAvailable/PeekChar/GetChar/PutChar` |
| LittleFS mount, SD/FatFS mount, path resolver | common host | VFS paths, `storageResolveRetroPath` |
| Virtual image random access | common host | `imageOpen/imageReadAt/imageWriteAt/imageFlush/imageClose/imageSize` |
| Disk-activity RGB indicator (GPIO 38) | common host | `diskActivityRead()` green, `diskActivityWrite()` red |
| Apple II machine (CPU, bus, IIe model, Disk II, block devices, Videx card, drive config) | Apple II machine — see `designApple2.md` | Shared machine implementation below this line's profile |
| UCSD profile selection, Pascal media, guest tools | UCSD Pascal profile | Everything specified in this document |

The UCSD profile MUST NOT start Wi-Fi or any HTTP server, MUST NOT open the console or
mount filesystems itself, and MUST NOT be active at the same time as the browser File
Transfer service (enforced by the common host menu). It MUST NOT introduce an OS selector,
an `isPascal` flag, or any menu-dependent controller, boot or terminal override: device
behaviour follows the configured `drives.cfg` devices and validated media only.

## 4. Architecture and module boundaries

```text
machine registry (main/machineRegistry.c)
  -> probeUcsd / initializeUcsd / apple2MachineRun                    main/machineRegistry.c
       -> apple2MachineProbeWithConfig("/littlefs/ucsd/drives.cfg")   main/apple2Machine.c
       -> apple2DriveConfigLoad / MACHINE=IIE settings                main/apple2DriveConfig.c
       -> apple2DiskImage profiles + Pascal content probe             main/apple2DiskImage.c
       -> apple2Keyboard, apple2SystemInfo, ANSI renderer              (see designApple2.md)
       -> imageFile, diskActivity, hostConsole                         (common host)
       -> apple2Core (6502, IIe model, slot-3 Pascal card, block trap) components/apple2Core/apple2Core.c
            -> generated Pascal card ROM + firmware                    apple2PascalCardRom.h
```

| Module | Responsibility (UCSD view) |
| --- | --- |
| `main/machineRegistry.c` | Registry entry `{ "UCSD Pascal", "ucsd", "designUCSD.md" }` with probe/initialize bound to `/littlefs/ucsd/drives.cfg` and the shared `apple2MachineRun`. |
| `main/apple2Machine.c` | Profile-aware probe and initialize; IIe wiring; disk attach policy of §9.3; run loop; SYSINFO snapshot (`UCSD Pascal profile (menu 3)`). |
| `main/apple2DriveConfig.c/.h` | `drives.cfg` parsing including the UCSD path prefixes and the LittleFS write rule of §10. |
| `main/apple2DiskImage.c/.h` | Profile sizing, ProDOS/Pascal sector order, Pascal volume probe (directory block 2), block read/write callbacks. |
| `components/apple2Core/apple2Core.c` | IIe auxiliary memory and softswitches, 80-column IIe text page, block-device execution, slot-3 Pascal card mode (§12). |
| `components/apple2Core/apple2PascalCardRom.h` | Generated slot ROM and Pascal 1.1 character-output firmware layer; never edited by hand. |
| `tools/ucsd/` | Work-volume builder, Pascal image adapter, hardware acceptance driver. |

The core and device models MUST NOT include ESP-IDF headers except through the conditional
allocation block described in `designApple2.md` §6, and MUST NOT call filesystem or console
APIs directly — the core only sees the caller's sector/block callbacks.

## 5. Guest resources and provenance

| Resource | Provenance | Use |
| --- | --- | --- |
| `m6502.h` | `floooh/chips`, pinned commit `ee88c35ad6427341aa6999c3b07233e1f8bd2396`, zlib/libpng, vendored under `components/apple2Core/third_party/chips/`. | NMOS 6502 cycle interpreter (shared). |
| `littlefs/apple2/apple2.rom` | 12 KiB Apple II Autostart Monitor + Integer BASIC image. | System ROM of the Pascal 1.1 profile (Apple II model). |
| `littlefs/apple2/apple2e.rom` | Upper 16 KiB of the unenhanced NMOS Apple IIe ROM. | System ROM of the 128K profile (`MACHINE=IIE`). |
| `assets/ucsd/ucsdDiskImages/Apple Pascal 1.3 3.5.po` | Original Apple Pascal 1.3 3.5-inch system disk (800K, 1600 blocks). | Base image of `littlefs/ucsd/system.po`. |
| `bootDisks/ucsd/systemDsk/` | Checked-in guest files: `128K.APPLE`, `128K.PASCAL`, `BENCHUCSD.CODE`, `SYSVIEW.CODE`, `SYSVIEW.TEXT`, `VT100TEST.TEXT`. | Placed onto the generated system disk (§14). |
| `littlefs/ucsd/system.po` | Built by `tools/diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K` from the asset above plus `bootDisks/ucsd/systemDsk/` (§14). | Boot drive SD6.1 image. |
| `assets/ucsd/ucsdDiskImages/ApplePascal2-640K.po`, `pascalWork1.po`, `pascalWork2.po` | Formatted Apple Pascal 640K volumes (1280 blocks). | Backup/work volumes mounted per `drives.cfg`. |
| `assets/ucsd/BENCHUCSD/`, `assets/ucsd/SYSVIEW/`, `assets/ucsd/LOANCALC/` | Project-authored Pascal source (guest-compiled) plus prebuilt `.CODE`. | Guest benchmark, system report, sample workflow. |
| `Apple Pascal 1 - 680-0004-01.dsk` (140K, DOS-order `.dsk`) | Original Apple Pascal 1.1 disk (`assets/apple2Downloads/appleDisks/`), copied to `littlefs/ucsd/ApplePascal1-680-0004-01.dsk` for the host test. | Pascal 1.1 profile and its boot test. |
| `ApplePascal1-140K.dsk` | 140K Pascal 1.3 boot disk carrying `128K.APPLE`/`128K.PASCAL`; host-test fixture at `littlefs/ucsd/ApplePascal1-140K.dsk`. | IIe 128K Disk-II boot test fixture. |
| `apple2PascalCardRom.h` | Project-authored, generated by `tools/ucsd/buildApple2PascalCardRom.py`. Do not edit by hand. | Slot-3 card slot ROM + Pascal 1.1 firmware protocol layer (Apple II model only). |

Guest ROM and disk-image redistribution rights MUST be recorded in-tree
(the asset directories) separately from the emulator licence.
The firmware MUST NOT fabricate or enlarge any image of this table.

## 6. Machine profile and startup

| Profile | RAM | ROM | Selected by |
| --- | --- | --- | --- |
| Pascal 1.3 128K (active) | 64 KiB main + 64 KiB auxiliary (IIe) | 16 KiB IIe at `$C000–$FFFF` | `MACHINE=IIE` in `/littlefs/ucsd/drives.cfg` |
| Pascal 1.1 64K | 48 KiB motherboard + 16 KiB Language Card | 12 KiB at `$D000–$FFFF` | `MACHINE=II` (default) |

The Pascal 1.3 `128K.APPLE`/`128K.PASCAL` system files are the Apple IIe variant: they use
the IIe softswitches `$C001–$C00F` and status reads `$C011–$C01F` (IIe model,
`designApple2.md` §8.3) and reject an original Apple II with `128K memory required`. The
profile therefore MUST select the IIe model for the 128K system; faking auxiliary memory
on the Apple II model is not conforming. The 64K `SYSTEM.APPLE`/`SYSTEM.PASCAL` variants
run on the Apple II model with the slot-3 card (§12). The same 800K image booted through
the Disk II path stays at the `Apple ][` prompt — the 3.5-inch system requires the
block-device boot (§9.3).

Registry entry (in `main/machineRegistry.c`): display name `UCSD Pascal`, id `ucsd`, design
document `designUCSD.md`, implemented, SD not required for availability.

### 6.1 Probe (`apple2MachineProbeWithConfig` with `/littlefs/ucsd/drives.cfg`)

Fast, non-booting, never leaves files open:

1. Load `drives.cfg` (missing → `machineMissingResource`, invalid → `machineResourceInvalid`).
2. Probe the ROM matching `MACHINE=`: `apple2e.rom` (16 KiB, reset vector at file offset
   `$3FFC` ≥ `$C100`) or `apple2.rom` (12 KiB, vector at `$2FFC` ≥ `$D000`); absent →
   `machineMissingResource`, wrong size/vector → `machineResourceInvalid`.
3. The SD6.1 image MUST be configured, exist as a regular file and match its profile size
   exactly (143,360 / 655,360 / 819,200 bytes); otherwise `machineMissingResource` /
   `machineResourceInvalid`.
4. If SD6.1 is RW, reopen it with update access (failure → `machineResourceInvalid`) and
   return `machineWritable`, else `machineAvailable`.

### 6.2 Initialize (`apple2MachineInitializeWithConfig`)

1. Refuse any configuration path other than `/littlefs/ucsd/drives.cfg` or
   `/littlefs/apple2/drives.cfg` (`ESP_ERR_INVALID_ARG`).
2. Read the selected ROM from LittleFS (exact size + EOF check); destroy any previous core.
3. Create the core; set the millisecond time source; on `MACHINE=IIE` enable the IIe model
   (allocates 48 KiB auxiliary + Language Card extension and the 16 KiB IIe ROM image, and
   clears the Saturn and boot-in-80-columns flags); set both lowercase options
   (`apple2CoreSetCharacterOptions(core, true, true)`); on the Apple II model only, enable
   boot-in-80-columns.
4. Load the ROM (the reset runs).
5. Attach every configured drive per §9.3; SD6.1 must open or the machine is destroyed and
   `ESP_ERR_NOT_FOUND` returned so the menu is shown again — there is no fallback disk and
   no silent substitution.
6. Attach the SYSINFO interface to slot 1 (a failure only logs a warning).

### 6.3 Run (`apple2MachineRun`)

Shared with the Apple II profile; see `designApple2.md` §18.3: clear the terminal, feed the
key queue, run bounded 1.023 MHz cycle batches with `vTaskDelay(1)` at least every 100 ms,
signal the disk-activity LED (green on every sector/block read, red on Disk II write
attempts and block writes), and re-render the screen every 100 ms. The loop never exits
under guest control; physical RESET returns to the main menu (common host rule).

## 7. Boot specification

### 7.1 Boot paths

The controller mode per slot is decided by §9.3; the Pascal boot disk therefore enters
through one of two project-authored slot ROMs:

- **800K Pascal 3.5-inch system (block device):** SD6.1 holds an 800K image whose content
  probe (§9.4) reports Apple Pascal; the slot answers `$Cn00–$CnFF` with the block-device
  slot ROM whose byte `$Cn07 = $3C` (autostart block device). The Autostart ROM boots unit
  1 without any keyboard input: the ROM reads block 0 of unit 1 into `$0800` through the
  ProDOS block entry `$Cn0A` and jumps to `$0801` with `X = slot << 4`.
- **140K Pascal 5.25-inch system (Disk II):** SD6.1 holds a 140K DOS-order `.dsk`; the slot
  answers with the Disk II boot ROM (autostart signature `24 20 24 00 24 03 24 3C`,
  nibble reader decoding track 0 sector 0 into `$0200`, then `JMP $0801`). The 6-and-2
  translate table MUST be located at `$02D6` (the Pascal boot code decodes directory
  sectors with `EOR $02D6,Y`) and the sector index MUST advance after every read; with a
  misplaced table every sector decodes as zeros with a matching checksum and the loader
  never finds `SYSTEM.APPLE`.

### 7.2 Loader and system selection

The on-disk loader reads the volume directory (block 2, ProDOS/Pascal block order) through
the same controller interface that carried block 0, then loads the system files. The boot
volume MUST contain `SYSTEM.APPLE` and `SYSTEM.PASCAL`; the generated `system.po` places
the checked-in `128K.APPLE`/`128K.PASCAL` under those names (§14), so the active profile
boots the 128K system. On the IIe model the loader writes into auxiliary memory as the
IIe softswitches dictate; every block transfer MUST go through the core's memory mapping
and never bypass bank selection or write protection.

### 7.3 Command-line contract

A conforming boot reaches the Pascal command interpreter without host intervention. The
host MUST NOT synthesise any of these strings; they are produced by the guest and asserted
by the acceptance tests:

- Banner: `Welcome APPLE1, to Apple II Pascal 1.3` (Pascal 1.1: `... Pascal 1.1`), plus
  `Pascal system size is 128K` (IIe 128K system) or `Pascal system size is 64K` (64K
  system), and the absence of `128K memory required` when the matching system variant is
  booted.
- Command line: the interpreter prints its help line and prompt; the acceptance tests wait
  for `Command:`. Pascal 1.1 shows first
  `Command: E(dit, R(un, F(ile, C(omp, L(ink, X(ecute, A(ssem, D(ebug,? [1.1]` and, after
  a `?`, `Command: U(ser restart, I(nitialize, H(alt, S(wap, M(ake exec` on row 0 with the
  remainder of the row cleared (columns 70 and 79 blank). Pascal 1.3 shows
  `Command: F(ile, E(dit, R(un, C(omp, L(ink, X(ecute, A(ssem, ?  [1.3]`.
- The Filer opens with `F` (waiting for `Filer:`); `V` lists the mounted volumes (with a
  `Prefix volume` prompt), `L` lists one volume (`what volume` prompt, paging on Enter,
  summary line `... files <listed/in dir>`, free space `N unused`), `T` transfers
  (`Transfer what file` → `To where` → `-->`), `R` removes (`Remove what file` →
  `Update directory` → `Y`, missing files report `No such`).
- Guest test programs start from the `Command:` prompt through `R(un)` (for example
  `BENCHUCSD`, `SYSVIEW`); the editor/compiler workflow is `E(dit)`, `C(omp)`,
  `R(un)`.

At boot the adapter MUST report zero Disk II sector-read failures and zero Disk II write
attempts for a read-only boot (asserted through `apple2CoreGetDiskState`).

## 8. Pascal-relevant guest memory map

The full map is `designApple2.md` §7 (Apple II model) and §8.3 (IIe model). The Pascal
profile additionally relies on:

| Address range | Contents | Profile |
| --- | --- | --- |
| `$0800–$0BFF` | Boot loader load area (block 0 / track 0 sector 0), entered at `$0801`. | both |
| `$C001–$C00F`, `$C011–$C01F` | IIe softswitches/status (RAMRD/RAMWRT, INTCXROM, ALTZP, 80COL, ALTCHARSET, …). | 128K system |
| `$C090–$C091` | Slot-1 SYSINFO interface (`RETROAPPLE-SYSINFO/1`, §13.2). | both |
| `$C0A0–$C0A4` | Slot-2 timer card (`APPLE2_TIMER_C1`, §13.1). | both |
| `$C0B0–$C0BF` | Slot-3 card CRTC/bank I/O. | Pascal 1.1 only |
| `$C300–$C3FF` | Slot-3 card slot ROM (§12.2); IIe built-in 80-column firmware on the IIe. | both (different source) |
| `$C800–$C970` | Slot-3 card COUT/init firmware (project-authored, 369 bytes). | Pascal 1.1 only |
| `$CA00–$CABE` | Slot-3 Pascal 1.1 firmware protocol layer (475 bytes, generated). | Pascal 1.1 only |
| `$CC00–$CDFF` | Card video RAM window (banked 2 KiB). | Pascal 1.1 only |
| `$D000–$FFFF` | Language Card (Pascal system/code on the 64K system). | Pascal 1.1 |

Unmodelled `$Cxxx` addresses return the last data-bus value; no floating-bus model is
required.

## 9. Storage and volumes

### 9.1 Image profiles

Raw `.po`/`.do` images of 16 × 256-byte sectors per track; the size must match the profile
exactly (`designApple2.md` §15):

| Profile | Tracks | Image size | Blocks (512 B) | Pascal role |
| --- | ---: | ---: | ---: | --- |
| `APPLE2_140K` | 35 | 143,360 | 280 | 5.25-inch Pascal boot disk (Disk II path) |
| `APPLE2_640K` | 160 | 655,360 | 1,280 | Pascal work/backup volume |
| `APPLE2_800K` | 200 | 819,200 | 1,600 | Pascal 3.5-inch system disk (block boot) |

Sector order comes from the extension (`.po` → ProDOS/Pascal, `.do` → DOS), else from the
content probe, else DOS. Larger Pascal volumes are out of scope; the firmware never
creates or enlarges images, and a write is durable only after the image layer's flush
succeeds.

### 9.2 Block mapping and units

Block `n` of an image maps to that image's sectors `2·(n % 8)` and `2·(n % 8) + 1` of
track `n / 8`, read in ProDOS/Pascal order (`designApple2.md` §14). Up to four 512-byte
units per slot are implemented, units 1–4. Pascal 1.3's ProDOS block entry `$Cn0A` selects
the unit through zero-page `$43` = `slot<<4`, bit 7 selecting unit 2 and the low nibble
required to be `0` — so Pascal sees only **two units per slot**; additional Pascal volumes
MUST be assigned to another free slot rather than unit 3/4. The SmartPort command entry
`$Cn0D` still serves all four units for software using that interface.

### 9.3 Controller selection (attach policy)

Per slot 4–7 (`apple2Machine.c` attach logic), evaluated after opening every configured
image of that slot:

1. Any `SP` entry → the whole slot is SmartPort (units 1–4).
2. Else any opened image whose probe reports ProDOS content → the slot is a block device
   with autostart.
3. Else the boot slot (6) whose SD6.1 image is profile `APPLE2_800K` and probes as Apple
   Pascal → the slot is a block device with autostart (the 3.5-inch boot rule).
4. Else the slot is Disk II (drives 1–2).

A slot MUST NOT mix `SP` and `SD` lines (rejected by the parser) and all opened images of
a slot share the chosen mode; in block mode `SDx.1`/`SDx.2` become block units 1/2 of that
slot. The boot drive SD6.1 MUST be configured and its image openable; otherwise the
machine is destroyed and `ESP_ERR_NOT_FOUND` is returned (menu shown again).

Pascal work volumes with native `.TEXT` files MUST be attached as block devices: the
guest's TEXT file access goes through the block interface, and a block-formatted volume
attached through the Disk II sector mapping cannot open its TEXT files (the guest reports
`ERROR: Reading file`). The active profile mounts `PASCAL:` (SD6.1 block unit 1),
`BACKUP:` (SD6.2 block unit 2), `WORK1:` (SP5.1) and `WORK2:` (SP5.2).

### 9.4 Content probe

The image layer tests both sector orders for a Pascal volume header in directory block 2
(first block 0, next block 6, kind 0, 1–7-character volume name, block and file counts) or
a ProDOS volume-directory entry, reporting Pascal / ProDOS / unknown content with the
detected volume name and block count. A disagreement between extension-chosen order and
the probe is logged as a warning (`orderSuspect`). The probe proves volume identity only,
never bootability.

## 10. Drive configuration (`/littlefs/ucsd/drives.cfg`)

Grammar and shared rules are `designApple2.md` §16 (`SD<slot>.<drive>` / `SP<slot>.<unit>`
= `path,RO|RW,profile`, `MACHINE=`, `RAMCARD=`). The UCSD-specific requirements:

- Allowed image prefixes: `/littlefs/apple2/`, `/littlefs/ucsd/`,
  `/retro/images/apple2/`, `/retro/images/ucsd/` (SD paths resolved through
  `storageResolveRetroPath` to the `/microSD` mount).
- Only the SD6.1 boot image may be writable when stored under `/littlefs/ucsd/`; any
  other UCSD LittleFS image configured `RW` is rejected with the error "only the SD6.1
  UCSD boot image may be writable from LittleFS".
- Slots 4–7; `SD` units 1–2, `SP` units 1–4; a slot MUST NOT mix `SD` and `SP` lines and
  device lines MUST NOT duplicate.
- Any malformed line, over-long line (the 256-byte line buffer filled without a newline)
  or read error discards the whole file (the failing line is reported); a missing file is
  a missing resource. There is no default drive.
- The parser validates paths, profile/size pairing, modes and controller exclusivity; it
  does NOT validate image contents or Pascal volume labels.

The checked-in Pascal profile is:

```text
MACHINE=IIE
SD6.1=/littlefs/ucsd/system.po,RO,APPLE2_800K
SD6.2=/retro/images/ucsd/ApplePascal2-640K.po,RO,APPLE2_640K
SP5.1=/retro/images/ucsd/pascalWork1.po,RW,APPLE2_640K
SP5.2=/retro/images/ucsd/pascalWork2.po,RW,APPLE2_640K
```

(`SP4.x` lines are commented placeholders for additional volumes; more Pascal volumes
require another free slot, never `SP5.3`/`SP5.4`.) The SD card must contain
`ApplePascal2-640K.po`, `pascalWork1.po` and `pascalWork2.po` under
`/retro/images/ucsd/` (copy from `assets/ucsd/ucsdDiskImages/`). `/retro/exchange/ucsd/`
is browser file staging only; guest files inside images are managed through the File
Transfer GUI's Pascal adapter while the guest is stopped.

## 11. Console contract

The USB console is a byte-stream transport; the host renders guest video memory to the
terminal and MUST NOT interpret or generate control sequences beyond the documented
renderer behaviour (`designApple2.md` §11.3): redraw at 10 Hz, per-row diffing, inverse
and flash attributes, cursor at CH/CV bounded to the screen, clear on entry and on 40↔80
switches. Host logs MUST NOT inject bytes into the guest screen.

- **128K profile (IIe):** the console is the IIe 80-column text page — `80COL` set, even
  columns from auxiliary memory, odd columns from main memory; `ALTCHARSET` gives direct
  7-bit ASCII decoding (`$7F` → space) with inverse as bit 7 clear and no flashing. The
  profile enables both lowercase options (lowercase character ROM and lowercase keyboard).
  The Videx card, its Pascal-card path and the lowercase echo correction are inactive on
  this model.
- **Pascal 1.1 profile (Apple II model):** the console is the slot-3 card selected at boot
  (§12); the 40-column motherboard screen remains available through `PR#0`.
- Keyboard: the shared translator (`designApple2.md` §10) delivers CR for CR and lone LF,
  `$08` for `$08`/`$7F`, cursor keys as `$0B/$0A/$15/$08`, ESC pairs preserved, a lone ESC
  after 50 ms, and drops bytes > `$7F`. The Pascal editor relies on these mappings; there
  is no Shift/PB2 sensing (`$C063` reads `$00`).

## 12. Slot-3 Pascal card (Pascal 1.1 profile, Apple II model)

The card is a Videx Videoterm-compatible model with project-authored firmware
(`designApple2.md` §12); the profile adds the Pascal 1.1 firmware protocol layer. It is
inactive on the IIe model.

### 12.1 Pascal discovery

Pascal 1.1 scans slots `$C7…$C1` at startup, checksums each ROM page and accepts a card
only when the slot ROM carries the Pascal firmware-protocol identification:
`$Cn05 = $38`, `$Cn07 = $18` (type 4) and `$Cn0B = $01` (type 6, Pascal 1.1 protocol);
the selected slot type is stored in the guest device table at `$BE67 + $Cn` (slot 3 →
`$BF2A`). Without these bytes Pascal falls back to its built-in 40-column driver.

### 12.2 Slot ROM and firmware layer

`$C300–$C3FF` returns the generated Pascal card slot ROM (`apple2PascalSlotRom`):
`$C300 JMP $CA00` (output chain entry), `$C303 JMP $C800` (card init), `$C320 JMP $CA08`
(Pascal init entry), driver entry offsets `$Cn0D–$Cn10` (init/read/write/status) and the
driver entry table `$C330–$C345`. The firmware layer at `$CA00` serves the Pascal 1.1
character-output protocol (control codes CR, LF with scroll, BS, FS, US, EM, FF, VT, GS,
RS GOTOXY, ESC). The init routine at `$C800` programs the CRTC registers
`{62,50,50,28,19,00,18,18,00,0F,20,0F,00,00,00,00}`, sets CSW (`$36/$37`) to `$C8B3` and
clears the screen; the COUT routine preserves the incoming accumulator (Applesoft relies
on its high bit while expanding `LIST` tokens), handles CR/backspace/scroll at row 24, and
writes into the banked VRAM via the tables at `$C941`/`$C959`. Generated by
`tools/ucsd/buildApple2PascalCardRom.py`; the header MUST NOT be edited by hand.

### 12.3 Pascal card mode

An opcode fetch (SYNC) at `$C320` (`apple2PascalInitEntry`) activates Pascal card mode:
the card becomes the permanently selected console, and the core stops touching Pascal's
zero page and screen memory — no CH/CV following, no HOME trap at `$FC58`, no CSW
rewrites, no lowercase echo correction. From then on the guest owns CH/CV/BASL/BASH and
the card is driven only through the firmware protocol and the CRTC/bank registers at
`$C0B0–$C0BF`. `apple2CoreSetBootIn80Columns` (machinery of `designApple2.md` §12.4) is
enabled only on this model and selects the card at every reset until the guest takes over.

## 13. Virtual add-on devices

### 13.1 Monotonic timer (`APPLE2_TIMER_C1`, slot 2, `$C0A0–$C0A4`)

Shared with the Apple II profile (`designApple2.md` §17.1): `$C0A0` reads `$C1` when a
`readMilliseconds` callback is configured (reading MUST NOT sample it), `$C0A1` latches a
fresh unsigned 32-bit millisecond snapshot and returns bits 0–7, `$C0A2–$C0A4` replay bits
8–15 / 16–23 / 24–31 without calling the callback; writes are ignored, reset clears only
the snapshot, and slot 2 is reserved from generic slot-I/O.

`assets/ucsd/BENCHUCSD/BENCHUCSD.TEXT` is guest-compiled Pascal source that reads the
timer through a variant-record pointer (`$C0A0` = `-16224`; Apple Pascal has no `MEM[]`).
It prints `@BEGIN`/`@END` around INTEGER and SIEVE sections, `Elapsed`, a
`P-CODE MODEL ONLY (not measured)` line, `Index vs 1.023MHz MODEL` and `@DONE PASS`;
scale 1 takes about 5 s per section measured on the cycle-exact core.

### 13.2 Emulator information interface (`RETROAPPLE-SYSINFO/1`, slot 1)

`$C090` reads the magic `$A6` when supported (foreign bytes ignored); a write of `$01`
builds a fresh snapshot; reads of `$C091` drain the stream (zero after the last byte),
which starts with `RETROAPPLE-SYSINFO/1`. The snapshot reports firmware version, the
profile line (`UCSD Pascal profile (menu 3), Apple IIe model`), RAM description
(`128 KiB (64 KiB main + 64 KiB auxiliary)` on the IIe), PSRAM size, uptime, executed 6502
cycles and the mounted volumes (slot, unit, RO/RW, SmartPort flag, byte size, path, Pascal
volume name); control characters in paths are sanitised to `?`. `assets/ucsd/SYSVIEW/`
(`SYSVIEW.TEXT`, prebuilt `SYSVIEW.CODE`, also placed on the system disk) is the guest
Pascal program that arms and drains this stream and prints the report. It is an add-on: a
busy slot 1 never prevents the machine from starting.

## 14. Host-side tools (media production requirements)

The firmware never formats media. These development-computer tools (all `python3`, options
via `--help`) MUST produce inputs that satisfy this document exactly:

| Tool | Requirement |
| --- | --- |
| `tools/diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K` | Copies `assets/ucsd/ucsdDiskImages/Apple Pascal 1.3 3.5.po` and adds every file of `bootDisks/ucsd/systemDsk/`, mapping `128K.APPLE→SYSTEM.APPLE` and `128K.PASCAL→SYSTEM.PASCAL`, producing `littlefs/ucsd/system.po`. The system disk is part of the firmware image: a new one requires a rebuild and a flash. |
| `tools/diskManager.py --type WORKDSK --os ucsd --profile 640K\|140K --name <NAME>` | Creates an empty formatted Pascal volume (1280/280 blocks, directory blocks 2–5, first file block 6, volume name 1–7 characters upper-cased) at `<sd-root>/retro/images/ucsd/<NAME>.po` by default; refuses an existing target without `--force`. |
| `tools/extractFiles.py --os ucsd <image> --add/--extract/--remove` | Inspect and edit files inside Pascal volumes (block order) through the Pascal filesystem adapter. |
| `tools/ucsd/buildApple2PascalCardRom.py` | Regenerates `apple2PascalCardRom.h` (slot ROM + Pascal 1.1 firmware layer). Do not edit the header by hand. |
| `tools/prepareSd.py --os ucsd` | Create the `/retro/…` tree (including `/retro/images/ucsd/`) without overwriting an existing `drives.cfg` or formatting the card. |
| `tools/ucsd/ucsdHardwareTest.py` | Scripted driver of the hardware procedures of §17 over the USB console (it resets the board; it never flashes). |

## 15. Development phases

Each phase is complete only when its acceptance test passes deterministically with zero
sanitizer reports, built with `-Wall -Wextra -Werror -funsigned-char`. The test MUST fail
before the phase is implemented and pass after; later phases MUST keep earlier tests
passing, and the complete `designApple2.md` §19 suite remains green throughout.

| Phase | Deliverables | Acceptance test |
| --- | --- | --- |
| 1 — Profile resources and boot to the command line | `ucsd` registry entry bound to `/littlefs/ucsd/drives.cfg`; UCSD path prefixes and the LittleFS write rule in the drive-config parser; IIe model wiring (`MACHINE=IIE`, 16 KiB ROM validation); block-device autostart of the generated 800K `system.po`; run-loop integration. | `ucsdPascalIIeBoot` (registered ctest names for the `apple2DiskTests --pascal-iie-boot` and `--pascal-iie-boot-800k-block` modes, with the 140K Pascal 1.3 Disk II fixture and IIe ROM): with no keyboard input the guest reaches `Command:`; the screen shows `Pascal 1.3` and `Pascal system size is 128K` (140K Disk II variant, without `128K memory required`) or `Pascal system size is 64K` (800K block variant); Disk II sector-read failures and write attempts are zero. |
| 2 — Pascal volumes through the block interface | Content probe for Pascal volumes; SmartPort/block attach policy of §9.3 including the two-units-per-slot rule; work volumes across slots; flush-on-write persistence. | `ucsdPascalIIeTextOpen`: three Pascal 640K block volumes mounted across slots (including unit 2 of slot 5) are all enumerated by the Filer (`DATA3:`, `WORK1:`, `WORK:`), and the editor opens `WORK1:SPIRODEMO.TEXT` through the SmartPort interface showing `PROGRAM SPIRODEMO;` with no `ERROR: Reading file`. |
| 3 — Pascal 1.1 on the Apple II model via the slot-3 card | Pascal card mode (`$C320` fetch, `$CA00` protocol, core hands over zero page/screen), generated card ROM/firmware with the identification bytes, boot-in-80 on the Apple II model. | `ucsdPascalBoot`: the original Pascal 1.1 140K disk boots on the Apple II model to `Command: E(dit, R(un, F(ile, C(omp, L(ink, X(ecute, A(ssem, D(ebug,? [1.1]` on an 80-column row; the card text mode is active and `Welcome APPLE1, to Apple II Pascal 1.1` is shown; `$C305/$C307/$C30B/$BF2A` read `$38/$18/$01/$06`; `?` pages the second command line `Command: U(ser restart, I(nitialize, H(alt, S(wap, M(ake exec` with cleared remainder (columns 70 and 79 blank); zero read failures and write attempts. |
| 4 — Console, timer and system report add-ons | IIe 80-column/ALTCHARSET rendering and lowercase keyboard path for the Pascal console; `APPLE2_TIMER_C1`; `RETROAPPLE-SYSINFO/1` snapshot; guest tools `BENCHUCSD` and `SYSVIEW` on the system disk. | `apple2Sysview` (probe/arm/drain on the plain and IIe address paths, executed 6502 drain, path sanitising) plus the timer assertions of `hostCore` (`$C0A0` capability without sampling, `$C0A1` latch, `$C0A2–$C0A4` replay); on the cycle-exact core a guest-run `BENCHUCSD` compiles without errors and prints `@DONE PASS` (`tests/test_benchucsd.py`). |

## 16. Verification: host tests

Run natively with ASan/UBSan and `-Wall -Wextra -Werror -funsigned-char`:

```sh
cmake -S tests -B /tmp/retro-host-tests
cmake --build /tmp/retro-host-tests
ctest --test-dir /tmp/retro-host-tests -N
ctest --test-dir /tmp/retro-host-tests --output-on-failure -R '<name|name>'
```

ctest target names differ from file names (`ucsdPascalBoot`, `ucsdPascalIIeTextOpen` and
the `apple2DiskTests` modes are registered in `tests/CMakeLists.txt`; targets whose image
fixtures are absent are conditional and MUST be restored together with their fixtures). A
conforming implementation MUST ship host tests covering at least:

1. **Boot:** both boot paths (§7.1), 64K vs 128K system selection and banners, absence of
   `128K memory required` for the matching variant, `Command:` reached with no injected
   keys, zero sector-read failures and write attempts on a read-only boot.
2. **Card protocol (Pascal 1.1):** `$C320` opcode-fetch activation, card text mode,
   identification bytes `$C305/$C307/$C30B/$BF2A`, the `?` second command line drawn with
   GOTOXY and clear-to-end-of-line, and the boot-in-80 selection on the Apple II model.
3. **Volumes:** Pascal content probe (block 2 header fields, volume name, block count) for
   both sector orders; enumeration of Pascal volumes across slots with unit 2 reachable on
   a slot through the ProDOS entry and units 3–4 reachable only via SmartPort; TEXT file
   open through the block interface only (§9.3); RO refusal; last-block read/write of a
   1600-block image with out-of-range rejection.
4. **drives.cfg:** every §10 rejection rule (mixed SD/SP, duplicates, unknown profile/mode,
   over-long line, traversal forms, wrong prefixes, and the "only the SD6.1 UCSD boot
   image may be writable from LittleFS" rule in both directions).
5. **Block execution:** `$Cn0A`/`$Cn0D` contracts of `designApple2.md` §14.2 on the Pascal
   unit encoding (bit 7 unit 2, low nibble 0), bounds and error codes.
6. **Add-ons:** timer capability/latch/no-resample/write-ignore rules; SYSINFO magic,
   re-arm, drain order, snapshot fields (profile line, volume paths and Pascal volume
   names), control-character sanitising.
7. **Guest filesystem tooling:** Pascal volume read/write through the shared guest
   filesystem adapter (`guestFsPascalTests`) and the Python image/filler/test suites
   (`test_diskImageApple2Pascal.py`, `test_extractUcsd.py`, `test_systemDisk.py`,
   `test_benchucsd.py`, `test_bootDisksBench.py`).

## 17. Verification: hardware acceptance

Acceptance is complete when every case below is executed on the target board (ESP32-S3,
LOLIN S3 Pro configuration, USB Serial/JTAG console, 80×24 terminal, valid Retro SD card
with the §10 images under `/retro/images/ucsd/`) and each observation matches its pass
criterion. A host-test pass or a successful build is never evidence of hardware behaviour.
The scripted driver is `tools/ucsd/ucsdHardwareTest.py`.

| # | Procedure | Pass criterion |
| --- | --- | --- |
| H1 | RESET, menu shows UCSD Pascal; select 3. | The machine starts without keyboard input and shows `Command:` within 90 s with the Pascal 1.3 banner and `Pascal system size is 128K`; the terminal is cleared and sized correctly. |
| H2 | `F` enters the Filer; `V` lists volumes. | `Filer:` appears; the volume list shows `PASCAL:`, `BACKUP:`, `WORK1:` and `WORK2:`. |
| H3 | `L` lists each volume (paging with Enter to the summary line). | Every volume lists to its `files <listed/in dir>` summary with a plausible `N unused` count; listing completes without watchdog reset. |
| H4 | Transfer a file onto `WORK1:`/`WORK2:` (`T`, answer both prompts), RESET, re-enter, list. | `-->` confirms the transfer; the file persists byte-identically after restart. |
| H5 | Transfer a file onto the RO `BACKUP:` volume; copy between work volumes; remove a file (`R`, confirm `Y`). | The RO volume refuses (no `-->`, protection/`full` reported) and stays unchanged; copy succeeds; remove reports `Update directory`/`No such` and leaves the volume usable. |
| H6 | List a volume that is not configured (`NOSUCH:`). | A clean `No such`/`no file` error; the Filer survives. |
| H7 | Manual editor/compiler workflow: `E` a small program, `C` compile it, start it with `R(un)`. | Source, compilation and execution complete; lowercase in source and strings survives edit + save; clean return to `Command:`. |
| H8 | Run `BENCHUCSD` (scale 1) via `R(un)` on an RW volume. | `@BEGIN`/`@END` for INTEGER and SIEVE, `Elapsed` and index lines are printed and the run ends with `@DONE PASS`; values track the model line reasonably. |
| H9 | Run `SYSVIEW` via `R(un)`. | The report is printed from the `RETROAPPLE-SYSINFO/1` stream with the UCSD profile line and the mounted volumes with their Pascal volume names. |
| H10 | Sustained run ≥ 30 minutes with disk activity (listing/transfers loops). | No watchdog reset; RGB LED shows green on reads and red on write attempts (red held ≥ 300 ms). |
| H11 | Repeated enter/exit cycles (≥ 20) and re-entry after RESET mid-session. | Stable; media flushed/closed; menu remains usable; heap stays flat. |

## 18. Definition of done

1. All §16 host tests pass (sanitizer-clean), all §17 hardware cases are recorded as
   passing, and each phase's acceptance test of §15 is part of the passing suite.
2. The guest executes authentic Apple Pascal ROMs, loaders and filesystem code; no
   Pascal behaviour is synthesised in host code (the project-authored card ROM/firmware
   and boot ROMs are the only host-authored guest code, each with a checked-in
   generator).
3. The profile adds no OS selector, no menu-dependent device behaviour and no host-side
   p-code execution: identical configurations entered from either menu entry behave
   identically, and swapping the disk configurations swaps the environments without a
   code change.
4. Every acceptance-gated behaviour in this document is traceable to code: profile
   selection §6, boot §7, memory §8, storage §9, configuration §10, console §11, card
   §12, add-ons §13.
5. Provenance, licences and rights records for the 6502 core, the ROMs and all bundled
   Pascal images and guest programs are present in-tree.

---

[← Back to the UCSD Pascal guide](assets/readMes/README_UCSD.md)
