# designApple2.md

Revision: 2026-10-09. Status: phase 1 motherboard implementation integrated; ROM provenance, redistribution rights and hardware behavior remain unverified.

# Purpose and provenance

This document follows the engineering-memory structure of designCPM80.md and designCPM86.md and the latest recovered projectPrompt decisions. 

Apple II is menu option 4, started by 3 + [Enter]. Complete common HOST-M1, CP/M-80 and CP/M-86 acceptance before implementing this machine. The target is an expanded original Apple II workstation for Integer BASIC, disk-loaded Applesoft, editors and assemblers using an 80×24 ANSI/VT100 USB terminal.

The selected machine is an original Apple II (not IIe) with NMOS 6502, 48 KiB motherboard RAM, a 16 KiB Language Card, Integer BASIC ROM, an Autostart Monitor upgrade, a lowercase character-ROM modification, Shift-key sensing via game input PB2, a separate 80×24 expansion card, and disk controllers. This is a target reconstruction based on owner recollections, not a verified historical bill of materials. Do not substitute IIe auxiliary-memory or 80-column softswitch semantics.

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
|Characters/input      |Lowercase-modified character ROM and Shift-key modification, with PB2 state readable at `$C063`                                                            |
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

## Keyboard, lowercase ROM and Shift modification

Model $C000 keyboard latch/data-ready and $C010 strobe-clearing behavior. Model the historical Shift-key modification as a selectable input on game switch PB2 ($C063, bit 7), with verified electrical polarity. The shift wire does not by itself convert the original keyboard to a modern lowercase keyboard: keep keyboard encoding, shift state and character glyph ROM as separate mechanisms. Specify the chosen lowercase keyboard encoding convention and modified character ROM image; compatibility depends on software and ROM pairing. Support Control combinations, Return, Backspace and reset-key mapping through the USB terminal. Do not treat terminal lowercase ASCII as proof that the guest hardware natively generated lowercase.

Provide configuration flags for original versus modified character ROM, original versus lowercase input mapping, and PB2 Shift wire enabled/disabled. Validate uppercase/lowercase, inverse and flashing glyph mapping using screen-memory test patterns. Character glyph mapping on original Apple II differs from IIe alternate-character ROM semantics.

## 40-column and 80-column display

Original 40×24 Apple II text uses non-linear screen-row address mapping and text pages at $0400–$07FF and $0800–$0BFF. Reproduce normal/inverse/flashing display attributes and relevant text/mixed/page softswitches. Do not implement 80 columns by interleaving Apple IIe main/auxiliary RAM, and do not use IIe $C00C/$C00D 80COL softswitches as the baseline.

80×24 is provided by a separate expansion card, provisionally in slot 3, with its own firmware and video RAM/character behavior according to the selected card specification. PR#3 may select the slot’s output routine and PR#0 may return to the normal output path only if verified for the selected card. The original TU Delft card’s exact design remains unknown. A Videx-compatible profile is a candidate, not a historical fact. Terminal output must reflect actual emulated display state, not merely intercept ROM print calls. Verify cursor, scrolling, inverse text, mode transitions and 80 distinct characters per row.

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
|APPLE-M4 |Keyboard latch/strobe, uppercase/lowercase ROM, Shift-to-PB2 and control keys tested                                                                |
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

Terminal refresh follow-up — 2026-10-09: the Apple II terminal renderer now compares each 40-character row with its last successful output and writes only changed rows, instead of continuously redrawing all 24 lines. USB writes handle short writes and retry boundedly when the transmit queue temporarily has no space; a real stalled write is logged once per failed call, and the affected row remains eligible for redraw.

This phase intentionally excludes the Language Card, expansion 80-column video card, disk controllers/drives and graphics rendering. It also does not implement the remembered lowercase character ROM or Shift/PB2 modification. Unmodeled `$Cxxx` I/O uses the last data-bus value; accurate floating-bus timing is not implemented.

The machine requires `/littlefs/apple2/apple2.rom`, exactly 12 KiB with a reset vector in `$D000–$FFFF`. The selected system ROM is `assets/apple.rom`; host-side execution reaches the `APPLE ][` prompt, accepts `PRINT 2+2`, and displays `4`. Its 12 KiB size and reset vector match the implemented ROM window. Redistribution rights have not been verified. A separate project-authored diagnostic fixture at `tests/fixtures/apple2-diagnostic.rom` can be regenerated with `python3 tools/buildApple2TestRom.py`. Phase 1 still does not establish DOS, Applesoft, Pascal, physical firmware behavior or ESP32-S3 performance.

The core source and its unmodified zlib license are pinned in `docs/thirdParty.md`. The implementation does not mark APPLE-M1 or the later execution/compatibility milestones as accepted.

This scoped coding work was explicitly requested before the design's HOST-M1, CP/M-80 and CP/M-86 acceptance prerequisites were closed. It does not waive those gates or establish that the integrated host has completed acceptance.

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

New decisions must include date, alternatives, evidence, consequences and superseded IDs. Historical owner recollection is evidence for requirements, not proof of a particular controller/ROM implementation.

APPLE-DEC-015 details: dated 2026-10-09; the alternatives were to keep Apple II as a placeholder until all acceptance prerequisites were closed, or implement the requested motherboard subset while retaining those acceptance gates. This implementation follows the explicit phase-1 request. Evidence is the host-side synthetic-ROM test and ESP-IDF build; neither authentic ROM execution nor hardware behavior has been observed. The generated phase-1 test ROM is a separate original diagnostic, not the historical ROM image. It supersedes no historical machine-profile decision.

Resource follow-up — 2026-10-09: the missing menu resource was `/littlefs/apple2/apple2.rom` (12 KiB). Added that path as a generated project-authored phase-1 diagnostic image so the implemented CPU, text screen and keyboard path can be exercised without copyrighted firmware. Its deterministic generator is `tools/buildApple2TestRom.py`. A host integration regression boots this exact image, checks its screen text and verifies a keyboard echo. This removes the missing-resource gate for phase-1 testing; it does not satisfy APPLE-M1 ROM provenance or later guest-software milestones.

System ROM follow-up — 2026-10-09: replaced the bundled runtime diagnostic image with the 12 KiB `assets/apple.rom` image (SHA-256 `378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249`), which matches the machine ROM window and boots the `APPLE ][` prompt in the NMOS 6502 core. The host regression types `PRINT 2+2`, verifies the echoed command, result `4`, and return to the BASIC prompt. The former diagnostic image remains under `tests/fixtures/`, and its generator now defaults there so it cannot overwrite the system ROM. This is host-core evidence only; no ESP32-S3 flash or hardware test was run, and redistribution rights for the source ROM remain unresolved.

## Issue log

|ID             |State|Issue / next experiment                                                                              |
|---------------|-----|-----------------------------------------------------------------------------------------------------|
|APPLE-ISSUE-001|OPEN |Obtain exact original Apple II Autostart/Integer BASIC ROM images and checksums/licensing            |
|APPLE-ISSUE-002|OPEN |Audit Language Card `$C080–$C08F` switch truth table and write-enable sequence from primary reference|
|APPLE-ISSUE-003|OPEN |Select and verify lowercase character ROM plus keyboard code mapping and PB2 polarity                |
|APPLE-ISSUE-004|OPEN |Select actual emulated 80-column card, slot, firmware, VRAM and output behavior; Delft card unknown  |
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
|APPLE-VERIFY-005|NOT RUN               |Language Card bank switching, keyboard Shift/PB2, lowercase glyphs                                                                                              |
|APPLE-VERIFY-006|NOT RUN               |40×24/80×24 video and slot firmware                                                                                                                             |
|APPLE-VERIFY-007|NOT RUN               |DOS 3.3 boot, disk-loaded Applesoft and UCSD Pascal                                                                                                             |
|APPLE-VERIFY-008|NOT RUN               |Virtual disks, exchange, durability, ESP32 hardware and recovery                                                                                                |

Earlier 2026-10-04 Apple IIe/ProDOS source-review notes in the prior revision are not acceptance evidence for this new original-Apple-II baseline. Preserve the original document in version control for provenance.

## Next implementation session

Before accepting additional Apple II milestones, confirm HOST-M1/CP/M acceptance status and actual repository layout. Resolve ROM provenance and redistribution rights, verify the pinned CPU core against a reproducible original Apple II desktop reference, then complete one milestone at a time. Keep failed experiments and regression evidence; never relabel source review or a host-only test as a passing hardware test.