# designApple2.md

Revision: 2026-10-09. Status: phase 1 motherboard and selected slot-3 80×24 text-card profile integrated; ROM provenance, redistribution rights and hardware behavior remain unverified.

# Purpose and provenance

This document follows the engineering-memory structure of designCPM80.md and designCPM86.md and the latest recovered projectPrompt decisions. 

Apple II is menu option 4, started by 3 + [Enter]. Complete common HOST-M1, CP/M-80 and CP/M-86 acceptance before implementing this machine. The target is an expanded original Apple II workstation for Integer BASIC, disk-loaded Applesoft, editors and assemblers using an 80×24 ANSI/VT100 USB terminal.

The selected machine is an original Apple II (not IIe) with NMOS 6502, 48 KiB motherboard RAM, a 16 KiB Language Card, Integer BASIC ROM, an Autostart Monitor upgrade, a lowercase character-ROM modification, a separate 80×24 expansion card, and disk controllers. This is a target reconstruction based on owner recollections, not a verified historical bill of materials. No physical Shift wire is connected to the game port in this project setup. Do not substitute IIe auxiliary-memory or 80-column softswitch semantics.

# Project-wide contract

projectPrompt.md is the authority for host architecture and coding rules. Use the canonical name in the project. These documents specialize that contract, not replace it. Companion documents are 
- designCPM80.md, 
- designCPM86.md, 
- designUCSD.md,
- designSWTPC.md,
- timeSource.md.

## The fixed main menu is:

ESP32-S3 Retro Computer

```
1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800

6. File Transfer

Select system [1-6]:
```

Every selection requires [Enter]; a digit alone does nothing. Normalize CR/LF/CRLF so one submitted line is processed once. Invalid or disabled choices explain the reason and stay in the menu. During HOST-M1, emulator choices report Not implemented yet; no CPU emulator is implemented in that milestone. Runtime availability later requires a compiled implementation, valid resources and any mandatory media. Distinguish an unimplemented machine from missing or invalid resources.

HOST-M1 owns the machine registry, USB console, menu, LittleFS, SD detection and versioned /retro/layout.txt validation, image block storage, exchange service and browser File Transfer. A 32 GB FAT32 SD card is recommended. The manifest schema/version must come from the implemented host contract, not be independently invented by an emulator. Never silently format an unknown card or replace user images. Missing SD or wrong layout must be visible; optional missing media need not prevent an otherwise supported LittleFS-only recovery boot.

Paths beginning /retro/ are paths on the SD volume, independent of its ESP-IDF VFS mount prefix. /littlefs/ below is a proposed VFS mount name; reconcile it with HOST-M1 before implementation. Each resource manifest records format, size, checksum, origin, license, machine profile and read-only policy. Keep minimal boot resources in LittleFS; larger and writable images belong on SD. Do not store an entire disk image in RAM. Use bounded block buffers, checked offset arithmetic, explicit end-of-image checks and bounded resource ownership. Host capacity is not guest filesystem capacity.

File Transfer is a project-wide facility, not a server implemented inside each emulator. Only menu option 6 starts WiFi, using exclusively `michmich/esp-idf-wifi-provisioner` with the `^0.4.0` requirement; resolve and lock the actual tested version in the host dependency lock. 

The dedicated upload/download/delete webserver starts after networking is available and exposes only `/retro/exchange/` and `/retro/images/` on the physical SD card. The GUI selects machine and transfer type: loose files go under `/retro/exchange/apple2/`, while Apple II disk images go under `/retro/images/apple2/`. 

Other machine selections use their matching directory names. 

The GUI displays each file’s full VFS path. Disk-image files are transferred whole, with a basic progress indicator. Traversal, absolute-path escape, malformed names and unintended overwrite must be rejected. Uploads use bounded streaming and temporary files; incomplete uploads must not appear as completed files. 

Binary data must remain byte-identical. 

The transfer limit is 4 GiB minus 2 bytes: ESP-IDF’s HTTP parser reserves the maximum 32-bit Content-Length value as a sentinel. [Enter] exits File Transfer to the main menu; WiFi may then stop. 

Emulator execution and browser File Transfer are mutually exclusive in the initial design.

A guest transfer utility talks to the common local hostExchange service while the emulator runs; it does not start WiFi. The browser moves files between PC/Mac and exchange; the guest utility copies between exchange and the guest filesystem through native guest OS calls. The host must not modify mounted guest filesystems behind the guest’s back. Browser access to active disk images is outside this contract.

The exchange wire ABI is not recovered from the earlier conversation and is not claimed to exist. Before utility implementation, define it once in HOST-M1/common documentation: protocol version and capability query; directory enumeration; open/read/write/close/abort; transfer length and offset; bounded payload; explicit busy/EOF/error; handle ownership; overwrite policy; filename encoding; timeout/cancellation; optional metadata. Addresses/ports remain unassigned until checked against each machine. Each adapter restricts access to its own exchange root. 

On failure, close guest and host handles and preserve existing destinations. Size and checksum verification must accompany binary round trips.

Physical ESP32 reset returns to the host menu and never automatically resumes a guest. Guest warm boot/reset is a separate action. A host-controlled clean exit must flush/close media, reset terminal attributes and release ownership before returning. Do not consume ordinary guest Enter as a host exit command. Sudden power loss or physical reset cannot guarantee guest filesystem consistency; record the durability boundary and test recovery using disposable images.

## Implementation discipline

Use native ESP-IDF, CMake and the VSCode ESP-IDF workflow; no Arduino dependency. Target ESP32-S3 / LOLIN S3 Pro, with board pins, flash/PSRAM settings and USB routing verified against the actual board configuration. 

All project-owned code, identifiers, documentation and messages are English. 

Use 
- Allman braces, 
- two-space indentation
- lowerCamelCase. 
- Comments use //— comment on their own line above the relevant code. Preserve upstream style and licenses in vendored cores; isolate project adapters and keep local patches small and documented.

Pin upstream revisions. Do not write a CPU emulator, replace a guest OS with host-side syscall emulation, or patch binaries randomly until a prompt appears. Keep CPU, machine bus, guest BIOS/device adapter, image backend and terminal transport separable. 

ESP-IDF task scheduling and watchdog servicing must not alter guest instruction semantics. Allocate/check memory before launch, bound queues, and keep diagnostics out of the guest screen. Measure speed and memory on hardware; neither CPU frequency nor PSRAM size alone proves adequate performance.

## Engineering-memory rules

This document is a living design and development record. Read its decisions and unresolved issues before changing the emulator. Never erase a failed experiment or silently rewrite a previous conclusion. Supersede decisions with a new numbered record. A repeated experiment requires new evidence or a changed variable. Keep design approval, source inspection, desktop testing and ESP32 hardware verification distinct.

For every verification record capture date, firmware commit, upstream core revision, ESP-IDF version, board, terminal, exact command/input, expected result, actual result and evidence path. Only observed execution may be marked PASS. For every issue use this chain:

**Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat**.

Unknown causes stay unknown. A workaround is not a root cause. Store reproducible traces and small fixtures; include the first divergent CPU/bus/disk event when available. Guest test software and ROM redistribution require their own provenance; an open-source emulator license does not cover those assets.

## Machine profile and scope

|Element               |Required baseline                                                                                                                                          |
|----------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|
|CPU                   |NMOS MOS 6502, Apple II timing approximately 1.023 MHz; pin/test a proven external core (candidate: `floooh/chips` `m6502.h`)                              |
|Machine               |Original Apple II with optional later upgrades, **not** Apple IIe; 48 KiB motherboard RAM plus 16 KiB bank-switched Language Card                          |
|ROM                   |Integer BASIC ROM plus **Autostart Monitor** ROM upgrade; matching reset vectors and ROM images required                                                   |
|Floating-point BASIC  |Applesoft loaded **from system disk** into Language Card RAM when appropriate DOS disk software is present; do not assume it is permanently resident in ROM|
|Operating environments|DOS 3.x (DOS 3.3 primary), disk-booted UCSD Pascal; no ProDOS requirement                                                                                  |
|Screen                |Original 40×24 text plus independent expansion-card 80×24 text, rendered to USB ANSI/VT100 terminal                                                        |
|Characters/input      |Lowercase-modified character ROM; case-preserving terminal input; no Shift wire connected to game-port PB2                                               |
|Floppies              |Disk II 5¼-inch 13/16-sector profiles and optional custom 8-inch / large virtual disk controller profiles                                                  |
|Exchange              |Common hostExchange backend with Apple DOS/Pascal guest adapters to be designed; no ProDOS dependency                                                      |
|Graphics/audio        |Not initial acceptance; preserve required bus/softswitch semantics, document limitations                                                                   |

Owner recollections: original Apple II with Integer BASIC, automatic disk boot, disk-loaded Applesoft, 16 KiB extra RAM for UCSD Pascal, uppercase/lowercase character-ROM and Shift-wire modification, a TU Delft student-developed 80-column card of unknown interface, and later dual-drive 8-inch double-density double-sided floppy equipment. The 8-inch controller, format, sector size, and actual capacity have not been identified. No Z80 card. Physical drives will never be connected to the ESP32-S3.

Do not claim that a particular 80-column card (e.g. Videx VideoTerm), 8-inch controller, or 640 KiB geometry was historically installed without evidence. Choose a documented, separately identified compatible virtual card for the first implementation; preserve the ability to add another model.
## ROM, BIOS, Software

***Steve Wozniak*** has given all his and Apple's Apple2 firmware free for Non-Commercial Use

## CPU, ROM and reset

Use a proven NMOS 6502 core with correct decimal-mode, interrupt, stack, read/write and bus side effects. Run authentic firmware instructions; do not hardcode BASIC, DOS or Pascal prompts. The original Old Monitor could enter the Monitor (*) before Integer BASIC; the owner’s machine instead automatically booted disks, consistent with a later Autostart Monitor ROM upgrade. Keep Old Monitor optional for historical tests, but set Autostart as the default. A diskless reset path must be established by executing the selected ROM and comparing with a reference, not by assuming a prompt.

ROM layout and exact byte images must be matched and checksum-pinned. Integer BASIC and Monitor ROM occupy the upper address region, with 48 KiB RAM at $0000–$BFFF and I/O/slot firmware at $C000–$CFFF. The 16 KiB Language Card overlays portions of $D000–$FFFF when enabled. Do not confuse an original Apple II with a Plus or IIe ROM set.

## Language Card: 48 KiB + 16 KiB

Emulate a slot-0-compatible 16 KiB Language Card. It supplies two alternative 4 KiB banks mapped at $D000–$DFFF, plus shared 8 KiB RAM at $E000–$FFFF. The total physical RAM is 64 KiB, not 64 KiB simultaneously linearly visible to the 6502. The $C080–$C08F softswitch accesses control RAM read selection, bank selection, and write enable; reproduce the actual access-dependent write-enable sequencing (including the two-access requirement) and distinguish reads from writes where the hardware does. Derive a switch truth table from a primary hardware reference before coding. ROM remains visible when selected.

Acceptance: run a standalone bank-switch test that writes different signatures to both $D000 banks and the common upper RAM, verifies ROM/RAM selection, tests write protection and double-access behavior, and confirms correct restoration after reset. UCSD Pascal must be able to boot and use this RAM without host-side patching.

## Keyboard, lowercase text and ROM behavior

Model `$C000` keyboard data/data-ready and `$C010` strobe-clearing behavior. USB Serial/JTAG delivers terminal character bytes, not physical key-down or modifier events. Preserve the received character's case in keyboard data; keep the data-ready strobe as separate state. There is no physical Shift connection and `$C063` is not synthesized from character case. The optional lowercase text mapping decodes screen bytes `$E1–$FA` as lowercase a–z while preserving digit and punctuation codes `$B0–$BF`; uppercase text remains `$C1–$DA`. The stock system ROM normalizes lowercase keyboard input while echoing it. To preserve terminal key-echo case, the core applies a one-character screen-write correction after a lowercase key is acknowledged; it only converts the matching uppercase echo within a bounded CPU-cycle window. This fixes immediate key echo but does not yet preserve lowercase in Applesoft string output or stored/listed source. Acceptance requires exact lowercase and uppercase key echo, `PRINT "abcdefghijkm"` rendering the lowercase string, `10 PRINT "Pietje Puk"` retaining case in `LIST`, and all paths checked in motherboard and Videx modes. Inspect actual screen bytes and Applesoft output, not just the `$C000` latch.

Provide independent settings for original versus modified character-ROM decoding and original versus case-preserving lowercase input mapping; the current machine profile enables both modified settings. `$C063` reflects no connected Shift/game-port input and is not derived from ASCII case. Validate lowercase screen bytes and digits, inverse and flashing glyph mapping. Test stock-ROM lowercase input/echo separately because the stock ROM normalizes the keyboard input to uppercase. Character glyph mapping on original Apple II differs from IIe alternate-character ROM semantics.

## 40-column and 80-column display

Original 40×24 Apple II text uses non-linear screen-row address mapping and text pages at $0400–$07FF and $0800–$0BFF. Reproduce normal/inverse/flashing display attributes and relevant text/mixed/page softswitches. Do not implement 80 columns by interleaving Apple IIe main/auxiliary RAM, and do not use IIe $C00C/$C00D 80COL softswitches as the baseline.

80×24 is provided by a separate expansion card, provisionally in slot 3, with its own firmware and video RAM/character behavior according to the selected card specification. The current selected software profile is Videx-compatible; this is not a historical claim about the unidentified TU Delft card. Its slot-3 ROM implements a project-authored minimal `PR#3` output driver, while its CRTC registers, banked video RAM, and `$C800–$CDFF` memory map follow the Videx Videoterm profile. `PR#0` returns to motherboard text when the Monitor restores its normal output vector. Terminal output reflects the selected card's video RAM, not intercepted ROM print calls. The project driver supports 80 columns, carriage return, backspace and scrolling at row 24. The host terminal renders text/inverse state and positions a cursor at the next output cell; it does not emulate the card's raster/font ROM.

An 80×24 USB terminal is the host display; it does not magically grant the Apple II 80-column hardware. Preserve guest/host terminal separation, escape-sequence sanitation and reasonable paste pacing.

***Make all console output and input VT100/ANSI terminal compatible.*** 

## Disk controllers and images

Use the shared HOST-M1 image backend and `drives.cfg` conventions, preserving path, RO/RW, and image-profile semantics. For example, the existing CP/M configuration uses 

- A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM and 
- C=/retro/images/cpm86/Utility.dsk,RW,RETRO86_DATA_BIG_V1.

Apple-specific profile names and letter-to-slot mappings below are proposals, not claims about the current parser:

## Apple II virtual drives

### PR&lt;slot&gt;.&lt;drive&gt;=&lt;image&gt;,&lt;mode&gt;,&lt;profile&gt;

- PR6.1=/littlefs/apple2/system.dsk,RO,APPLE2_140K
- PR6.2=/retro/images/apple2/work.dsk,RW,APPLE2_640K
- PR5.1=/retro/images/apple2/data1.dsk,RW,APPLE2_140K
- PR5.2=/retro/images/apple2/data2.dsk,RW,APPLE2_140K
- PR5.3=/retro/images/apple2/data3.dsk,RW,APPLE2_640K
- PR5.4>=/retro/images/apple2/work.dsk,RW,APPLE2_640K

The host’s PR6.1, PR6.2, PR5.1 etc. labels are configuration handles only: original Apple DOS uses slot/drive notation (e.g. S6,D1), not CP/M-style guest drive letters. Explicitly map each host image to a guest controller slot, unit, profile, geometry and read-only policy. Avoid collisions with slot 0 Language Card and slot 3 video. Disk II is conventionally slot 6, drives 1 and 2.

For standard Disk II support, distinguish 35-track 13-sector media (35×13×256 = 116,480 bytes) from 16-sector DOS 3.3 media (35×16×256 = 143,360 bytes). Preserve DOS versus physical sector ordering and qualify .do, .po, .dsk, nibble and other formats separately; suffix alone does not prove ordering. Authentic Disk II boot software uses controller softswitches and timing: an image-sector API alone is not a compatible Disk II controller. Physical drive motors need not exist, but their guest-visible emulated controller state may still be required.

For the remembered double-sided double-density 8-inch drives, do not assume 640 KiB: capacities vary by tracks, heads, sectors and bytes/sector, as well as controller and filesystem. Keep geometry and a guest-visible controller interface configurable. The owner will address custom DOS/Pascal software support separately. Large virtual images do not become accessible to unmodified DOS 3.3 or UCSD Pascal simply by increasing host image size. No physical floppy controller pins, motors or real drives are required.

Do not introduce ProDOS, SmartPort, Apple IIe auxiliary RAM, or a ProDOS-specific harddisk as a required dependency. Optional future features require a new approved decision. Test image bounds, RO enforcement, error propagation, flush/reopen, interrupted writes and disk-full behavior. Never silently enlarge images on invalid guest requests.

## Boot and guest software

Autostart Monitor plus slot-6 Disk II boot firmware should start a valid inserted system disk automatically. DOS 3.3 and UCSD Pascal must boot through their genuine on-disk loaders. Integer BASIC is in ROM and typically presents > when entered. Applesoft presents ] when running; on the selected historical profile it is loaded from disk into Language Card RAM by a suitable DOS 3.3 system/master disk. Whether this happens automatically depends on the specific disk’s boot files and startup sequence; do not implement a universal rule that every DOS disk automatically loads Applesoft. FP/INT work only with the correct DOS/BASIC environment and loaded language image.

Do not emulate Pascal or Applesoft at host-command level. The 6502 executes authentic guest binaries, ROMs and disk bootstrap. Keep boot-media provenance, version, controller compatibility and license checks in resource manifests.

## Guest-side File Transfer utility and bridge

Retain the common local hostExchange ABI, staging paths and File Transfer workflow described in the project-wide contract. A native Apple utility must be implemented for DOS 3.3, not assumed to be a ProDOS utility. Each environment needs its own guest filesystem adapter and file metadata rules; DOS 3.3 binary load address, BASIC tokenization and text records must be preserved. Pascal volume and file structures differ from DOS 3.3. No WiFi is started inside the Apple II emulator.

The bridge needs a versioned capability query, bounded buffers, directory enumeration, open/read/write/close/abort, EOF/error status, collision-free guest peripheral interface, safe banked-memory access, filename and metadata encoding, and atomic/verified transfer behavior. Reserve a free slot only after the display, Disk II and optional 8-inch controllers are assigned. Do not silently mutate mounted guest images from the host. First qualify whole-image transfer through the existing browser facility; guest-side individual-file transfer is a later milestone.

## Reference and debugging strategy

Use an original Apple II with Autostart Monitor and Language Card as the behavioral desktop reference, not an unenhanced IIe. Reference emulator candidates include MAME’s original Apple II model and another emulator with independently verified Language Card/Disk II behavior. Inspect upstream implementation and primary manuals for exact memory/softswitch semantics; record revision and ROM checksums. A reference IIe test cannot prove original Apple II character or 80-column card behavior.

Capture CPU PC/registers, cycle count, ROM selection, Language Card bank/write-enable state, keyboard strobe, PB2 state, video-card registers, slot I/O, image unit and disk operation at the first divergence. Pin and license-review external CPU/device cores. Never mark source inspection as execution verification.

## Milestones and acceptance gates

All items remain PLANNED until observed desktop and ESP32-S3 evidence exists.

|ID       |Deliverable and acceptance evidence                                                                                                                 |
|---------|----------------------------------------------------------------------------------------------------------------------------------------------------|
|APPLE-M1 |Confirm original Apple II target, ROM provenance/checksums, Autostart/Integer BASIC and reference desktop machine; host prerequisite gates satisfied|
|APPLE-M2 |NMOS 6502 integrated; reset vectors, cycle/bus behavior and ROM Monitor/Integer BASIC verified                                                      |
|APPLE-M3 |48 KiB RAM + 16 KiB Language Card; both 4 KiB banks, shared 8 KiB, softswitch/write-enable regression                                               |
|APPLE-M4 |Keyboard latch/strobe, uppercase/lowercase ROM, control keys and any physically connected Shift input tested                                             |
|APPLE-M5 |Original 40×24 text, non-linear row addressing, inverse/flashing, ANSI rendering and terminal input qualified                                       |
|APPLE-M6 |Selected 80×24 slot card ROM/VRAM model, `PR#3`/`PR#0` behavior where supported, editor and mode-switch tests                                       |
|APPLE-M7 |Disk II slot-6 boot, 13/16-sector image profiles, DOS 3.3 read/write, RO and persistence tests                                                      |
|APPLE-M8 |DOS 3.3 System Master boots; disk-loaded Applesoft verified; `FP`/`INT` only where supported by media                                               |
|APPLE-M9 |UCSD Pascal system disk boots, exercises Language Card and editor/file operations                                                                   |
|APPLE-M10|Configurable virtual 8-inch/large-image backend and guest-visible controller contract, geometry/boundary/error tests                                |
|APPLE-M11|Common File Transfer whole-image round trip; DOS/Pascal guest file utility only after its adapter is implemented                                    |
|APPLE-M12|Recovery, resets, repeated boot, performance, ESP32-S3 resource measurements, compatibility matrix and documentation                                |

Definition of Done: menu option 4 boots original Apple II firmware and qualifying disk software; 40×24 and actual 80×24 card emulation work; Shift/lowercase, Language Card, disk-loaded Applesoft, DOS 3.3 and UCSD Pascal have observed acceptance evidence. Large images must have a documented guest-visible interface, not merely a host file. Do not imply every historic expansion board or disk format is supported.

## Phase 1 implementation record — 2026-10-09

Implemented the motherboard-only subset requested for phase 1. The firmware now integrates the pinned `floooh/chips` NMOS 6502 core, 48 KiB RAM, a 12 KiB ROM window at `$D000–$FFFF`, the keyboard latch/strobe at `$C000/$C010`, motherboard text/video softswitches `$C050–$C057`, and a 40×24 text-memory renderer. The core is clock-paced near 1.023 MHz. Host tests use a synthetic test ROM and do not establish authentic ROM behavior.

Terminal refresh follow-up — 2026-10-09: the Apple II terminal renderer compares each 40-character row with its last successful output and writes only changed rows, instead of continuously redrawing all 24 lines. USB writes handle short writes and retry boundedly when the transmit queue temporarily has no space; a real stalled write is logged once per failed call, and the affected row remains eligible for redraw.

This phase intentionally excludes the Language Card, expansion 80-column video card, disk controllers/drives and graphics rendering. It also does not implement the remembered lowercase character ROM or Shift/PB2 modification. Unmodeled `$Cxxx` I/O uses the last data-bus value; accurate floating-bus timing is not implemented.

The machine requires `/littlefs/apple2/apple2.rom`, exactly 12 KiB with a reset vector in `$D000–$FFFF`. The selected system ROM is `assets/apple.rom`; host-side execution reaches the `APPLE ][` prompt, accepts `PRINT 2+2`, and displays `4`. Its 12 KiB size and reset vector match the implemented ROM window. Redistribution rights have not been verified. A separate project-authored diagnostic fixture at `tests/fixtures/apple2-diagnostic.rom` can be regenerated with `python3 tools/buildApple2TestRom.py`. Phase 1 still does not establish DOS, Applesoft, Pascal, physical firmware behavior or ESP32-S3 performance.

The core source and its unmodified zlib license are pinned in `docs/thirdParty.md`. The implementation does not mark APPLE-M1 or the later execution/compatibility milestones as accepted.

This scoped coding work was explicitly requested before the design's HOST-M1, CP/M-80 and CP/M-86 acceptance prerequisites were closed. It does not waive those gates or establish that the integrated host has completed acceptance.

## Videx-compatible 80-column card — 2026-10-09

Added a slot-3 Videx-compatible device profile to the Apple II core. It models the `$C0B0–$C0BF` CRTC/bank-select I/O, 2 KiB of card VRAM in four 512-byte banks, the `$C300` slot ROM entry and `$C800` expansion-ROM window. The project-authored slot ROM supports `PR#3`, installs an output vector, configures an 80×24 CRTC text mode, and writes printable characters, carriage returns and backspaces into card VRAM. The host terminal switches to 80 columns only while that card output vector is selected; the 40-column motherboard renderer remains available.

PR#3 input follow-up — 2026-10-09: standard BASIC sets the output vector to the slot entry at `$C300`; it does not call the previous `$C303` initialization entry. The slot entry now initializes the card on its first output and then chains to the installed character routine while preserving that first character. The system-ROM host test types `PR#3` and then `PRINT 2+2`, verifying keyboard input, command echo and result on card VRAM.

Screen follow-up — 2026-10-09: when the project COUT driver advances beyond row 23, the card screen scrolls its 24×80 character buffer up one row and clears the new bottom row. Rendering clears the host terminal on machine entry and whenever BASIC changes the active output between `PR#0` and `PR#3`, redraws the selected guest screen, and positions the terminal cursor at the next COUT cell. The Videx host test drives output through a scroll and verifies retained lines, the new bottom row and cursor position; the system-ROM host test switches `PR#3` → `PR#0` → `PR#3` and checks output selection.

Applesoft output follow-up — 2026-10-09: the project COUT routine now preserves the incoming accumulator, including its high bit, around its screen write; Applesoft uses the returned value while expanding tokens for `LIST`, so masking it permanently truncated keywords to their first letter. Screen clearing now detects changes to either byte of the output vector, since `PR#0` changes the low byte before the high byte. The system-ROM regression confirms full `PRINT` output in both 40- and 80-column modes, clean screen contents after each transition, a visible prompt and an in-bounds cursor.

Scheduler follow-up — 2026-10-09: when a CPU batch exceeded its 1 ms pacing interval, `vTaskDelayUntil` could remain behind schedule and return immediately on subsequent iterations. That catch-up loop could starve CPU 0's idle task and trigger the task watchdog during long-running BASIC programs. The machine loop now yields for one RTOS tick when late and resets its pacing baseline instead of trying to catch up unboundedly. Hardware timing/performance still needs verification.

Host regression evidence: the synthetic 6502 test ROM executes the card's `PR#3` entry, writes 80 distinct characters, verifies carriage return output, exercises VRAM banks 2 and 3, scrolls multiple lines and checks the cursor, and switches the host-visible output state away from the card. The system-ROM test exercises full Applesoft `LIST` output in both display modes and clean mode transitions. These are not Videx-ROM or raster-accurate tests. The minimal driver uses private emulated state for Apple II zero-page bytes `$06–$0A` and does not implement character-generator fonts, CRTC cursor effects, or graphics. This is not evidence for behavior on the physical ESP32-S3 or the historical TU Delft card.

## Decision log

|ID           |Status                   |Decision / reason                                                                                                 |
|-------------|-------------------------|------------------------------------------------------------------------------------------------------------------|
|APPLE-DEC-001|Retained                 |Menu 3, native ESP-IDF, shared HOST-M1, KIM-1 removed                                                             |
|APPLE-DEC-002|**SUPERSEDED 2026-10-08**|Previous unenhanced IIe/auxiliary-RAM baseline replaced by expanded **original Apple II**                         |
|APPLE-DEC-003|Candidate                |External proven NMOS 6502 core, `floooh/chips` candidate; validate and pin                                        |
|APPLE-DEC-004|Revised                  |Language Card bank switching, **not IIe auxiliary video RAM**, supplies extra 16 KiB                              |
|APPLE-DEC-005|Retained                 |Terminal renderer reflects guest video memory/card state; not ROM print interception alone                        |
|APPLE-DEC-006|Retained                 |LittleFS minimal immutable resources, SD larger/writable images                                                   |
|APPLE-DEC-007|**SUPERSEDED 2026-10-08**|ProDOS-specific large-disk requirement removed; custom virtual controllers are independent of ProDOS              |
|APPLE-DEC-008|Revised                  |Shared hostExchange; guest file adapters for DOS 3.3 and/or UCSD Pascal, not ProDOS                               |
|APPLE-DEC-009|Revised                  |Versioned Apple DOS/Pascal metadata policy, exact encoding TBD                                                    |
|APPLE-DEC-010|Required                 |Autostart Monitor with Integer BASIC ROM; Applesoft may auto-load **from suitable system disk** into Language Card|
|APPLE-DEC-011|Required                 |80×24 via independent slot card; historical TU Delft card unidentified; Videx only a candidate                    |
|APPLE-DEC-012|Required                 |Lowercase character-ROM plus separate keyboard Shift/PB2 modification                                             |
|APPLE-DEC-013|Required                 |Virtual 5¼-inch and configurable larger/8-inch image profiles; no physical drives                                 |
|APPLE-DEC-014|Required                 |No Z80 and no ProDOS requirement                                                                                  |
|APPLE-DEC-015|Implemented subset       |Phase 1 is motherboard-only: 6502, 48 KiB RAM, ROM window, keyboard and 40-column text; no Language Card, expansion video or disk controller |
|APPLE-DEC-016|Selected 2026-10-09      |Videx-compatible slot-3 profile with project-authored minimal `PR#3` firmware; not a claim about the TU Delft card |
|APPLE-DEC-017|Implemented 2026-10-09   |Preserve the Applesoft COUT accumulator contract and clear screens on either output-vector byte changing         |

New decisions must include date, alternatives, evidence, consequences and superseded IDs. Historical owner recollection is evidence for requirements, not proof of a particular controller/ROM implementation.

APPLE-DEC-015 details: dated 2026-10-09; the alternatives were to keep Apple II as a placeholder until all acceptance prerequisites were closed, or implement the requested motherboard subset while retaining those acceptance gates. This implementation follows the explicit phase-1 request. Evidence is the host-side synthetic-ROM test and ESP-IDF build; neither authentic ROM execution nor hardware behavior has been observed. The generated phase-1 test ROM is a separate original diagnostic, not the historical ROM image. It supersedes no historical machine-profile decision.

APPLE-DEC-016 details: dated 2026-10-09; alternatives were to require a user-provided Videx ROM dump or implement a project-authored minimal slot driver. The selected profile follows the explicit user choice. The ROM source is project-authored; no Videx firmware dump is bundled. It supports the standard Videx memory/register layout and `PR#3` terminal output. Host tests validate the CPU-executed slot ROM, scrolling and 80-column VRAM, not original card firmware, raster output, the historical TU Delft card, or ESP32-S3 hardware. This supersedes the "Videx only a candidate" portion of APPLE-DEC-011 for software-profile selection; the historical card identity remains unknown.

APPLE-DEC-017 details: dated 2026-10-09; investigation reproduced two independent `PR#3` failures with the authentic system ROM: Applesoft `LIST` rendered tokenized BASIC words as their first letters, and switching output modes left stale screen contents. The first cause was that the project COUT routine stripped the high bit from the incoming accumulator and returned the altered value; Applesoft relies on that value while expanding tokens. The second cause was that transition detection watched only the high byte of the output vector, while `PR#0` changes its low byte first. The chosen fix preserves and restores the complete accumulator around the Videx write, isolates the card routine's `$06–$0A` workspace from motherboard RAM, and detects selection changes on writes to either output-vector byte. Alternatives were to special-case or post-process `LIST` output, or clear only the host terminal; these would not preserve the guest ROM's output contract or prevent stale guest memory from being redrawn. Host system-ROM regression evidence now confirms full `PRINT` text on both screens, clean contents after `PR#0`/`PR#3` switches, a visible prompt, and in-bounds cursors. This does not establish compatibility with unrelated third-party slot firmware or hardware.

Resource follow-up — 2026-10-09: the missing menu resource was `/littlefs/apple2/apple2.rom` (12 KiB). Added that path as a generated project-authored phase-1 diagnostic image so the implemented CPU, text screen and keyboard path can be exercised without copyrighted firmware. Its deterministic generator is `tools/buildApple2TestRom.py`. A host integration regression boots this exact image, checks its screen text and verifies a keyboard echo. This removes the missing-resource gate for phase-1 testing; it does not satisfy APPLE-M1 ROM provenance or later guest-software milestones.

System ROM follow-up — 2026-10-09: replaced the bundled runtime diagnostic image with the 12 KiB `assets/apple.rom` image (SHA-256 `378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249`), which matches the machine ROM window and boots the `APPLE ][` prompt in the NMOS 6502 core. The host regression types `PRINT 2+2`, verifies the echoed command, result `4`, and return to the BASIC prompt. The former diagnostic image remains under `tests/fixtures/`, and its generator now defaults there so it cannot overwrite the system ROM. This is host-core evidence only; no ESP32-S3 flash or hardware test was run, and redistribution rights for the source ROM remain unresolved.

## Issue log

|ID             |State|Issue / next experiment                                                                              |
|---------------|-----|-----------------------------------------------------------------------------------------------------|
|APPLE-ISSUE-001|OPEN |Obtain exact original Apple II Autostart/Integer BASIC ROM images and checksums/licensing            |
|APPLE-ISSUE-002|OPEN |Audit Language Card `$C080–$C08F` switch truth table and write-enable sequence from primary reference|
|APPLE-ISSUE-003|PARTIAL|Project lowercase text/input mapping selected and host-tested; identify the historical character ROM. PB2 Shift sensing is not connected in this project setup|
|APPLE-ISSUE-004|PARTIAL|Videx-compatible slot-3 profile selected; qualify full editor/scroll/mode behavior and actual TU Delft card remains unknown|
|APPLE-ISSUE-005|OPEN |Select and qualify Disk II controller implementation, sector ordering, 13/16-sector media and timing |
|APPLE-ISSUE-006|OPEN |Identify DOS 3.3 System Master image that disk-loads Applesoft into Language Card; trace startup     |
|APPLE-ISSUE-007|OPEN |Obtain bootable UCSD Pascal image and verify Language Card use                                       |
|APPLE-ISSUE-008|OPEN |Define 8-inch/custom large virtual controller contract and image geometries without assuming 640 KiB |
|APPLE-ISSUE-009|OPEN |Align proposed `drives.cfg` profiles/mappings with actual host parser and layout manifest            |
|APPLE-ISSUE-010|OPEN |Define DOS/Pascal exchange metadata and optional guest transfer utility ABI                          |
|APPLE-ISSUE-011|OPEN |Measure ESP32-S3 terminal throughput, keyboard latency and storage performance                       |
|APPLE-ISSUE-012|OPEN |Find a way to send BASIC break from minicom: Ctrl+C is intercepted locally; investigate a host escape such as Ctrl+] without changing guest key semantics |

Use: Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat.

## Verification log

|ID              |Status                |Evidence / remaining action                                                                                                                                     |
|----------------|----------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
|APPLE-VERIFY-001|HISTORICAL REQUIREMENT|2026-10-08 owner clarified original Apple II, Integer BASIC, auto-boot, 16 KiB Language Card, disk-loaded Applesoft, Shift/lowercase and TU Delft 80-column card|
|APPLE-VERIFY-002|HISTORICAL REQUIREMENT|2026-10-08 owner clarified double-sided/double-density 8-inch dual-drive setup; controller/geometry unknown; no physical drives planned                         |
|APPLE-VERIFY-003|DESIGN REVIEW         |2026-10-08 prior IIe/ProDOS design conflicts identified and superseded; no emulator tests performed                                                             |
|APPLE-VERIFY-004|HOST PASS (subset)   |`cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-system-rom`: ROM reset reaches `APPLE ][`; keyboard input runs `PRINT 2+2` and displays `4`. Hardware/Autostart acceptance remains open.|
|APPLE-VERIFY-005|PARTIAL HOST PASS     |Language Card and hardware acceptance not run; host tests cover lowercase glyph decoding, digit preservation and case-preserving keyboard input                 |
|APPLE-VERIFY-006|HOST PASS (subset)   |System-ROM host test verifies complete Applesoft `LIST` output, screen clearing across `PR#0`/`PR#3`, prompt visibility and in-bounds cursors; detailed reproducibility and environment are recorded under APPLE-VERIFY-010; hardware acceptance remains open.|
|APPLE-VERIFY-007|NOT RUN               |DOS 3.3 boot, disk-loaded Applesoft and UCSD Pascal                                                                                                             |
|APPLE-VERIFY-008|NOT RUN               |Virtual disks, exchange, durability, ESP32 hardware and recovery                                                                                                |
|APPLE-VERIFY-009|HOST PASS (subset)   |2026-10-09: `cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-videx-card`; synthetic 6502 ROM executes project `PR#3` firmware, verifies 80 distinct VRAM cells, CR, banked VRAM, scrolling, cursor and output-vector mode. Hardware, authentic Videx firmware/raster behavior and historical TU Delft card remain unverified.|
|APPLE-VERIFY-010|HOST + BUILD PASS    |2026-10-09: full host suite passed 4/4 and ESP-IDF build succeeded after the Applesoft/output-vector fixes; reproducibility and scope below. No flash or physical-board test was performed.|
|APPLE-VERIFY-012|SUPERSEDED           |Earlier keyboard behavior claims were incomplete; see APPLE-VERIFY-015.|
|APPLE-VERIFY-013|HOST + BUILD PASS    |2026-10-09: lowercase screen decoding, digits `1234567890`, and system-ROM `PR#3` path passed before adding virtual Shift sensing. No physical-board test.|
|APPLE-VERIFY-014|SUPERSEDED           |The tests showed lowercase commands execute, not that the stock ROM preserves lowercase screen output. The case-derived `$C063` behavior described here was removed; see APPLE-VERIFY-015.|
|APPLE-VERIFY-015|SUPERSEDED           |Keyboard data/strobe separation passed, but stock-ROM lowercase echo remained uppercase; the limitation is resolved in APPLE-VERIFY-016.|
|APPLE-VERIFY-016|PARTIAL HOST + BUILD|2026-10-09: immediate lowercase/uppercase key echo is verified in motherboard and Videx modes, and lowercase Applesoft command input still works. This does not prove lowercase Applesoft string output or LIST preservation; the remaining case loss is recorded in APPLE-VERIFY-017. Full host suite and ESP-IDF build passed. No board execution.|
|APPLE-VERIFY-017|PARKED / USER REPORTED|2026-10-09: user reports `print "abcdefghijkm"` renders `ABCDEFGHIJKM` and `10 print "Pietje Puk"` is stored/listed as `10  PRINT "PIETJE PUK"`. Immediate lowercase key echo works, but quoted string contents still lose case. No implementation or validation performed for this issue; resume with exact PRINT/LIST assertions in both display modes.|

APPLE-VERIFY-010 evidence — date: 2026-10-09. Firmware commit: none; this verification ran against the uncommitted worktree. Upstream CPU core: `floooh/chips` `ee88c35ad6427341aa6999c3b07233e1f8bd2396`. ESP-IDF: v6.0.2. Target configuration: project-configured ESP32-S3 / LOLIN S3 Pro; the firmware was built, not run on that board. Terminal: host-side test harness reading emulated 40×24 and 80×24 screen memory; no physical terminal or board was attached. Commands: `cmake --build build-host --parallel`; `ctest --test-dir build-host --output-on-failure`; ESP-IDF VS Code build command (`idf.py build`). Guest input in the ROM regression: `10 PRINT 1`, `LIST`, `PR#3`, `LIST`, `PR#0`, `PR#3`, `PRINT 2+2`. Expected: complete `PRINT` in both screen modes; a cleared destination screen and visible BASIC prompt on each mode change; cursor within the active screen; arithmetic result `4`; all host tests pass and firmware links. Actual: all expectations passed; CTest reported 4/4 passing and the ESP-IDF build completed. Evidence locations: `tests/hostTests.c` (`testApple2SystemRom`, `testApple2VidexCard`, `testApple2VidexScrolling`) and the host CTest target definitions in `tests/CMakeLists.txt`. This is host/build evidence only; hardware behavior remains unverified.

Lowercase follow-up — 2026-10-09: the core independently selects lowercase text decoding and lowercase keyboard input. The custom display decoder maps `$C1–$DA` to uppercase A–Z and `$E1–$FA` to lowercase a–z. Host unit tests verify character decoding and that keyboard data retains both cases. This project mapping is not a verified dump of a historical lowercase character ROM.

Keyboard correction — 2026-10-09: USB Serial/JTAG carries characters, not physical key/modifier events; no Shift signal is wired to the Apple II game port. The core does not synthesize Shift on `$C063`. It stores keyboard data separately from the `$C000` ready strobe, which `$C010` clears. The stock ROM normalizes lowercase input to uppercase in its echo path. APPLE-VERIFY-016 adds a bounded, case-preserving correction only to the matching screen write after the keyboard strobe is acknowledged; it leaves the incoming keyboard data and ROM unchanged. This correction covers the key echo only, not Applesoft string printing or the case stored in BASIC program text.

Text-code correction — 2026-10-09: lowercase mapping now recognizes lowercase screen bytes `$E1–$FA` before stripping Apple II text attributes. The earlier low-six-bit conversion mistakenly treated digit codes `$B0–$B9` as lowercase letters, rendering `1234567890` incorrectly. Digits and punctuation remain decoded through the standard character mapping. Regressions cover all ten digits and all 26 lowercase letters. Existing system-ROM host tests type `PR#3` and exercise the Videx output path; the digit rendering bug made terminal echo unreliable to read.

APPLE-VERIFY-011 evidence — date: 2026-10-09. Firmware commit: none; tests ran against the uncommitted worktree. Upstream CPU core: `floooh/chips` `ee88c35ad6427341aa6999c3b07233e1f8bd2396`. ESP-IDF: v6.0.2. Target configuration: project-configured ESP32-S3 / LOLIN S3 Pro; host tests only, no board execution. Terminal: host-side test harness; no physical terminal attached. Commands: `cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-core`; `ctest --test-dir build-host --output-on-failure`; ESP-IDF extension `build` command. Expected: preserve uppercase behavior, read lowercase and uppercase through the keyboard latch, decode lowercase text bytes only when the lowercase character mapping is enabled, pass all host regressions and link firmware. Actual: targeted Apple II core test passed; full host suite passed 4/4; ESP-IDF build succeeded. Evidence location: `tests/hostTests.c` (`testApple2Core`); hardware behavior remains unverified.

APPLE-VERIFY-012 evidence — date: 2026-10-09. Earlier tests did not exercise keyboard firmware with case-derived virtual Shift sensing; see correction in APPLE-VERIFY-015.

APPLE-VERIFY-013 evidence — date: 2026-10-09. Lowercase screen codes `$E1–$FA`, digits `$B0–$B9`, and uppercase system-ROM `PR#3` input passed before the virtual Shift-sense shim. It does not demonstrate lowercase guest keyboard handling.

APPLE-VERIFY-014 evidence — date: 2026-10-09. This record was superseded: successful execution of lowercase BASIC commands did not prove lowercase echo/display, and its expected case-derived `$C063` state was erroneous. See APPLE-VERIFY-015.

APPLE-VERIFY-015 evidence — date: 2026-10-09. This intermediate result was superseded: it verified case-preserving keyboard data and strobe clearing but did not implement lowercase echo; see APPLE-VERIFY-016.

APPLE-VERIFY-016 evidence — date: 2026-10-09. Firmware commit: none; tests/build ran against the uncommitted worktree. Target configuration: project-configured ESP32-S3 / LOLIN S3 Pro; host tests and firmware build only, no board execution. Commands: `cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-core && ./build-host/hostTests --apple2-system-rom`; `ctest --test-dir build-host --output-on-failure`; ESP-IDF extension `build` command. The system-ROM test reads back exact text-cell bytes after terminal `a` and `A` input (`$E1` and `$C1`) in motherboard mode and verifies lowercase Videx echo. It also verifies lowercase Applesoft command input, `PR#3`, digits, strobe clearing, and that the host keyboard bytes are preserved. Full CTest and ESP-IDF build pass. This confirms host-emulated behavior only; no physical-board execution was performed.

APPLE-VERIFY-017 evidence — date: 2026-10-09. User-reported device/terminal behavior; not independently reproduced in the host harness. Input `print "abcdefghijkm"` displayed `ABCDEFGHIJKM`. Entering `10 print "Pietje Puk"` and then `list` displayed `10  PRINT "PIETJE PUK"`. The already-verified case-preserving character echo therefore does not preserve lowercase through Applesoft `PRINT` string output or program storage/listing. Work is explicitly parked at the user's request. On resumption, add regression coverage for literal string output and stored/listed string case before changing the ROM/core interception; test motherboard and Videx output and retain existing uppercase, digits, lowercase command parsing and `PR#3` regressions.

Earlier 2026-10-04 Apple IIe/ProDOS source-review notes in the prior revision are not acceptance evidence for this new original-Apple-II baseline. Preserve the original document in version control for provenance.

## Next implementation session

Before accepting additional Apple II milestones, confirm HOST-M1/CP/M acceptance status and actual repository layout. Resolve ROM provenance and redistribution rights, verify the pinned CPU core against a reproducible original Apple II desktop reference, then complete one milestone at a time. Keep failed experiments and regression evidence; never relabel source review or a host-only test as a passing hardware test.