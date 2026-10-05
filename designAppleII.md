# designAppleII.md — Apple IIe text workstation engineering memory

Revision: 2026-10-04. Status: new design baseline; source-reviewed architecture, implementation unverified.

## Purpose and provenance

This new document follows the engineering-memory structure of `designCPM.md` and `designUCSD.md` and the latest recovered projectPrompt decisions. It is not a recovered earlier Apple II attachment. The original projectPrompt attachment was unavailable; this baseline uses its retrieved decision summary and the current explicit requirements. Resolve any additional rules in the actual projectPrompt before coding.

Apple II is menu option **3**, started by `3` + `[Enter]`. KIM-1 is removed. Complete common HOST-M1, CP/M and UCSD acceptance before implementing this machine. The target is an Apple II-family text workstation for BASIC, editors, assemblers and filesystem tools using an 80×24 ANSI/VT100 USB terminal.

The proposed concrete machine is an **unenhanced Apple IIe with NMOS 6502 and extended 80-column/auxiliary RAM configuration**. This is an engineering selection, not a claim that the original Apple II inherently had IIe 80-column hardware. An enhanced IIe/65C02 profile is a separate future decision requiring a suitable external core, matching ROMs and regression tests.

## Project-wide contract

`projectPrompt.md` is the authority for host architecture and coding rules. Its latest recovered revision was delivered as `projectPrompt_v2.md`; use the canonical name in the project. These documents specialize that contract, not replace it. Companion documents are `designCPM.md`, `designUCSD.md`, `designAppleII.md`, `designMPM.md` and `designSWTPC.md`. KIM-1 is removed.

The fixed main menu is:

```text
ESP32-S3 Retro Computer
1. CP/M 2.2
2. UCSD Pascal
3. Apple II
4. MP/M II
5. SWTPC 6800
6. File Transfer
Select system [1-6]:
```

Every selection requires `[Enter]`; a digit alone does nothing. Normalize CR/LF/CRLF so one submitted line is processed once. Invalid or disabled choices explain the reason and stay in the menu. During HOST-M1, emulator choices report `Not implemented yet`; no CPU emulator is implemented in that milestone. Runtime availability later requires a compiled implementation, valid resources and any mandatory media. Distinguish an unimplemented machine from missing or invalid resources.

HOST-M1 owns the machine registry, USB console, menu, LittleFS, SD detection and versioned `/retro/layout.txt` validation, image block storage, exchange service and browser File Transfer. A 32 GB FAT32 SD card is recommended. The manifest schema/version must come from the implemented host contract, not be independently invented by an emulator. Never silently format an unknown card or replace user images. Missing SD or wrong layout must be visible; optional missing media need not prevent an otherwise supported LittleFS-only recovery boot.

Paths beginning `/retro/` are paths on the SD volume, independent of its ESP-IDF VFS mount prefix. `/littlefs/` below is a proposed VFS mount name; reconcile it with HOST-M1 before implementation. Each resource manifest records format, size, checksum, origin, license, machine profile and read-only policy. Keep minimal boot resources in LittleFS; larger and writable images belong on SD. Do not store an entire disk image in RAM. Use bounded block buffers, checked offset arithmetic, explicit end-of-image checks and bounded resource ownership. Host capacity is not guest filesystem capacity.

File Transfer is a project-wide facility, not a server implemented inside each emulator. Only menu option 6 starts WiFi, using exclusively `michmich/esp-idf-wifi-provisioner` with the recovered `^0.4.0` requirement; resolve and lock the actual tested version in the host dependency lock. The dedicated upload/download/delete webserver starts after networking is available and exposes only `/retro/exchange/` and `/retro/images/` on the physical SD card. The GUI selects machine and transfer type: loose files go under `/retro/exchange/apple2/`, while Apple II disk images go under `/retro/images/apple2/`. Other machine selections use their matching directory names. The GUI displays each file's full VFS path. Disk-image files are transferred whole, with a basic progress indicator. Traversal, absolute-path escape, malformed names and unintended overwrite must be rejected. Uploads use bounded streaming and temporary files; incomplete uploads must not appear as completed files. Binary data must remain byte-identical. The transfer limit is 4 GiB minus 2 bytes: ESP-IDF's HTTP parser reserves the maximum 32-bit Content-Length value as a sentinel. `[Enter]` exits File Transfer to the main menu; WiFi may then stop. Emulator execution and browser File Transfer are mutually exclusive in the initial design.

A guest transfer utility talks to the common local `hostExchange` service while the emulator runs; it does not start WiFi. The browser moves files between PC/Mac and exchange; the guest utility copies between exchange and the guest filesystem through native guest OS calls. The host must not modify mounted guest filesystems behind the guest's back. Browser access to active disk images is outside this contract.

The exchange wire ABI is not recovered from the earlier conversation and is not claimed to exist. Before utility implementation, define it once in HOST-M1/common documentation: protocol version and capability query; directory enumeration; open/read/write/close/abort; transfer length and offset; bounded payload; explicit busy/EOF/error; handle ownership; overwrite policy; filename encoding; timeout/cancellation; optional metadata. Addresses/ports remain unassigned until checked against each machine. Each adapter restricts access to its own exchange root. On failure, close guest and host handles and preserve existing destinations. Size and checksum verification must accompany binary round trips.

Physical ESP32 reset returns to the host menu and never automatically resumes a guest. Guest warm boot/reset is a separate action. A host-controlled clean exit must flush/close media, reset terminal attributes and release ownership before returning. Do not consume ordinary guest Enter as a host exit command. Sudden power loss or physical reset cannot guarantee guest filesystem consistency; record the durability boundary and test recovery using disposable images.

## Implementation discipline

Use native ESP-IDF, CMake and the VSCode ESP-IDF workflow; no Arduino dependency. Target ESP32-S3 / LOLIN S3 Pro, with board pins, flash/PSRAM settings and USB routing verified against the actual board configuration. All project-owned code, identifiers, documentation and messages are English. Use Allman braces, two-space indentation and lowerCamelCase. Comments use `//— comment` on their own line above the relevant code. Preserve upstream style and licenses in vendored cores; isolate project adapters and keep local patches small and documented.

Pin upstream revisions and resource hashes. Do not write a CPU emulator, replace a guest OS with host-side syscall emulation, or patch binaries randomly until a prompt appears. Keep CPU, machine bus, guest BIOS/device adapter, image backend and terminal transport separable. ESP-IDF task scheduling and watchdog servicing must not alter guest instruction semantics. Allocate/check memory before launch, bound queues, and keep diagnostics out of the guest screen. Measure speed and memory on hardware; neither CPU frequency nor PSRAM size alone proves adequate performance.

## Engineering-memory rules

This document is a living design and development record. Read its decisions and unresolved issues before changing the emulator. Never erase a failed experiment or silently rewrite a previous conclusion. Supersede decisions with a new numbered record. A repeated experiment requires new evidence or a changed variable. Keep design approval, source inspection, desktop testing and ESP32 hardware verification distinct.

For every verification record capture date, firmware commit, upstream core revision, ESP-IDF version, board, terminal, resource hashes, exact command/input, expected result, actual result and evidence path. Only observed execution may be marked PASS. For every issue use this chain:

**Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat.**

Unknown causes stay unknown. A workaround is not a root cause. Store reproducible traces and small fixtures; include the first divergent CPU/bus/disk event when available. Guest test software and ROM redistribution require their own provenance; an open-source emulator license does not cover those assets.

## Machine profile and scope

| Element | Baseline design |
|---|---|
| CPU | Existing open-source NMOS 6502 core; preferred `floooh/chips` `m6502.h` |
| Machine | Unenhanced Apple IIe, main plus extended auxiliary RAM, language-card behavior |
| Firmware | Matching unenhanced IIe system ROM, internal 80-column firmware and required slot firmware |
| Screen | Historical 40/80-column text state rendered into an 80×24 terminal |
| Input | Terminal keys translated to Apple keyboard latch/strobe behavior |
| Small disks | Ordinary sector-image floppy compatibility via a qualified Disk II controller model |
| Larger disks | Documented ProDOS block-device profile, qualified separately from Disk II |
| Exchange | Native guest utility with local hostExchange bridge, isolated to `/retro/exchange/apple2/` |
| Graphics/audio | Not initial acceptance features; limitations explicitly reported |

This is not a complete Apple II hardware compatibility claim. Copy-protected media, arbitrary timing demos, joystick games, raster effects, sound fidelity and every expansion card are outside the first milestone set. Memory and switch behavior required by accepted text applications must still be correct, including relevant effects of graphics-related switches on memory mapping.

### CPU integration

The preferred core exposes cycle-stepped 6502 bus behavior and is distributed with a zlib license. Integrate it through its documented pins/tick API; do not rewrite opcode execution. Configure it as the required 6502 variant, not a 6510 substitute with incompatible port mapping. Preserve upstream licensing and pin a reviewed revision. [Upstream core](https://github.com/floooh/chips/blob/master/chips/m6502.h).

Run an appropriate 6502 functional/decimal test suite on the selected integration, then test reset-vector fetch, IRQ/NMI, stack, page wrapping and memory-mapped I/O side effects. Upstream reputation does not certify our bus adapter. Record the core revision, variant, harness configuration and actual result. If an accepted application requires 65C02 instructions, reject that profile clearly or open a separate core/profile decision; do not add homemade opcodes.

The CPU worker advances emulated cycles in bounded batches. Disk and device timing derives from emulated cycles; terminal refresh is decoupled. A blocked USB connection must not permanently block the CPU or grow an unbounded output queue. Start with a documented nominal Apple IIe timing profile and measure achieved ESP32 throughput. Do not claim cycle-exact whole-machine behavior merely because the CPU core is cycle-stepped.

## Startup and ownership

```text
host menu → 3 + Enter → machine/resource validation
  → fresh IIe state and 6502 reset
  → matching ROM boot path and selected slot device
  → guest BASIC / DOS / ProDOS environment
  → guest controls keyboard, RAM and disk contents
```

1. Validate machine profile, ROM sizes/hashes, required boot image and configured SD layout.
2. Acquire exclusive host console and image access. Browser File Transfer must already be stopped.
3. Allocate main/auxiliary storage and device state. Initialize reset-visible switches according to the chosen profile and record cold-boot RAM policy.
4. Map real ROMs and device firmware, then execute the CPU's reset sequence and vector fetch.
5. Let ROM/guest code boot the declared controller/slot. Missing disk must have a defined recovery path to a usable monitor/BASIC prompt or controlled host error.
6. Render guest text memory and route terminal keys into the emulated keyboard.
7. A guest reset follows Apple machine behavior. Physical ESP32 reset returns to the host menu. A separate documented clean host exit flushes disks and restores terminal state.

Do not replace boot with a host-printed Applesoft or ProDOS prompt. A diagnostic ROM-only profile may be available without SD when declared in the machine registry. The full development profile requires its configured writable media. Menu numbering never changes with availability.

## Historical 80-column and auxiliary-memory design

### Source-reviewed conclusions

The IIe has distinct main/auxiliary memory controls. `80STORE` is a memory-mapping switch, separate from the `80COL` display switch. With 80STORE set, PAGE2 selects main/auxiliary access for text page 1 and, when HIRES is set, the corresponding high-resolution region. RAMRD/RAMWRT otherwise select read/write banks; ALTZP also affects zero page/stack and banked upper RAM. Slot-ROM selection is independent and must be modeled. These distinctions were checked against the maintained [AppleWin memory implementation](https://raw.githubusercontent.com/AppleWin/AppleWin/master/source/Memory.cpp) on 2026-10-04. This is source verification, not a hardware test.

AppleWin's 80-column renderer reads both banks for each text address. The intended left-to-right character order is auxiliary then main: zero-based columns 0,2,… use auxiliary memory; 1,3,… use main. Attribute decoding and flashing are also part of text rendering. [AppleWin renderer](https://raw.githubusercontent.com/AppleWin/AppleWin/master/source/NTSC.cpp).

The original Apple IIe Technical Reference and 80-Column Text Card manual remain the historical authorities. Attempts to retrieve the referenced manual PDFs in this session failed; their exact edition/page citations remain an explicit verification task. Do not label that manual audit complete. Reference links: [Technical Reference scan](https://www.applelogic.org/files/AIIETECHREF4.pdf), [80-Column Text Card manual](https://apple2online.com/web_documents/apple_iie_80-column_text_card_manual.pdf).

### Required implementation artifacts

Before writing the memory adapter, produce a complete read/write mapping table for the selected IIe revision from the manuals and a pinned reference emulator. Include:

- Main and auxiliary backing stores, language-card bank state and ROM overlays.
- Independent read/write bank selectors and their precedence exceptions.
- Zero-page/stack mapping, text-page mapping and graphics-region mapping.
- Switch write/read side effects and status-register values.
- Internal versus peripheral ROM selection, slot 3 behavior, expansion-ROM ownership and release.
- Keyboard latch/strobe interactions at addresses shared with write-only soft switches.

Do not implement one generic bank flag for all addresses. Do not interpret a keyboard-register read as a write to a memory-mode switch. Keep physical backing stores distinct from the CPU-visible mapping and the display-visible mapping. Direct auxiliary writes must invalidate the appropriate displayed cells even when the CPU's current read bank differs.

Use the historical non-linear text row layout, not `base + row * 80`. Verify a table covering every row and both text pages against reference memory dumps before accepting it. For each bank address produce two neighboring display cells in 80-column mode. Bank order must be verified with an asymmetric test pattern rather than a screen full of identical characters. Include boundary rows and all 80 columns.

The distinction between a basic 80-column card and an extended RAM configuration matters. The baseline promises the latter's memory profile; it must not advertise full auxiliary RAM while only allocating a text buffer. The 6502 address space remains 64 KiB even though more physical RAM exists behind switches.

### Terminal renderer and input

Read guest screen memory and display state; a ROM character-output hook alone misses direct screen writes and editors. Build a logical cell grid with glyph and attributes, compare it with the last transmitted grid and send bounded ANSI updates. Repaint on mode changes, reconnect and explicit redraw. This terminal layer is a host presentation adapter, not an Apple graphics card visible to the guest.

In 40-column mode, place the 40 guest cells consistently in the 80-column host grid and clear stale cells; the initial policy is left-aligned single-width text. Do not silently force guest 80-column mode on reset. Guest firmware/software must enable its historical mode. Test ROM-based activation, such as the appropriate slot-3 firmware path, against the exact ROM revision.

Map normal/inverse/flashing attributes deliberately. Implement flashing by controlled redraw if terminal blink is inconsistent. Define character mappings for the selected character ROM and document unsupported glyphs; do not claim enhanced-IIe MouseText on an unenhanced profile. Hide or control the host cursor so it does not create a second cursor over the guest's software cursor. Avoid bottom-right autowrap scrolling. A host terminal smaller than 80×24 must produce a clear setup message before launch.

Translate Enter, Backspace/Delete, Escape, arrows and control keys according to the selected Apple keyboard behavior. Parse ANSI input sequences with bounded timing, preserve ordinary Escape, define repeat/paste pacing and test high-bit keyboard strobe handling. Modifier combinations unavailable over a serial terminal need documented mappings, not fabricated permanent button states.

For unsupported graphics output, retain machine state and provide a controlled diagnostic outside the guest screen or during host inspection. A mixed-mode text subset may be implemented as a documented capability; do not present stale text as a faithful graphics display.

## Storage, ROM resources and devices

| Location | Contents | Access |
|---|---|---|
| LittleFS `apple2/rom/` | Matching system ROM and required firmware, manifest and hashes | RO |
| LittleFS `apple2/boot/` | Optional minimal diagnostic/boot resource that fits partition | RO |
| `/retro/images/apple2/` | Floppy and larger block-device images, software and work volumes | Declared per image |
| `/retro/exchange/apple2/` | Staged files plus versioned transfer metadata | Via common exchange service |

Do not embed an unverified ROM collection or assume emulator source licenses cover Apple firmware. Validate every resource's origin and distribution conditions. A manifest must identify machine/CPU revision, ROM roles, image ordering and any guest OS requirements. Keep writable images off the flash master. If guest startup writes its disk, use a configured SD copy with an explicit preparation step.

### Floppy profile

Qualify ordinary unprotected DOS 3.3/ProDOS-compatible sector images. Record tracks, sectors, sector size and logical-to-physical ordering; a `.dsk` suffix alone is ambiguous. The common 35 × 16 × 256-byte format is 143,360 bytes, but exact geometry and ordering must be checked for each accepted resource. Do not apply the same ordering to DOS-order and ProDOS-order images.

A Disk II ROM driver expects controller behavior, not a generic host sector API. Reuse a proven compatible controller implementation with license review, or implement a documented device adapter around verified behavior; the no-homemade-CPU rule still applies. Reproduce relevant motor, phase, data-latch, read/write and cycle behavior for accepted media. If an alternate paravirtual boot device is used during early bring-up, label it as such and do not count that as Disk II compatibility.

Reject unsupported nibble/flux/copy-protection formats explicitly until a separately qualified decoder/controller path exists. Keep conversion tools and format metadata outside the hot CPU path. Write errors and write protection must reach the guest.

### Large virtual disks

Use a separate documented ProDOS block-device/slot-firmware contract for larger disks. Pin a reference device implementation, specify slot and unit assignment, boot behavior, status/read/write commands, 512-byte block addressing, flags, return conventions and out-of-range errors. Do not assume ordinary Disk II firmware or DOS 3.3 can address a large image just because it resides on SD. SmartPort is not automatically promised by the unenhanced IIe profile; it requires an explicit compatible card/firmware contract.

The ProDOS filesystem documentation defines a two-byte total-block count and three-byte EOF. With 512-byte blocks, a conventional maximum-count volume is 65,535 blocks = 33,553,920 bytes (512 bytes below 32 MiB); the 24-bit EOF has a representable maximum of 16,777,215 bytes. These are format-derived ceilings, not acceptance guarantees for every OS/driver/tool. [ProDOS file organization](https://prodos8.com/docs/techref/file-organization/).

Start with a smaller qualified block volume, then test the largest compatible profile with the selected ProDOS release, firmware and utilities. Some OS/software releases require a 65C02; the baseline must choose an NMOS-compatible release and verify it. Larger host containers or multiple units/partitions do not increase a single guest filesystem's limits. DOS 3.3 retains its separately qualified floppy geometry.

At minimum test first/last block, allocation bitmap boundaries, file-size boundaries, disk-full, directory-full, read-only media, reopen after clean restart and truncated/corrupt images. Use checked host offsets and reject access beyond the declared image. Do not silently grow an image in response to an invalid guest request. Host flush completion is not a guarantee against power loss inside an SD card.

## Guest-side File Transfer utility and bridge

Provide a native Apple utility, initially for the selected ProDOS environment, with directory/import/export operations. Its exact executable name and user interface are implementation decisions; the suggested name is `HOSTXFER`. A DOS 3.3 utility is a later separate adapter if required. A ProDOS utility does not automatically work under DOS 3.3 or Apple Pascal.

The utility uses native ProDOS file operations; the bridge carries exchange bytes and metadata. Bind the common protocol to a reserved virtual peripheral slot/register range, with optional firmware entry points. Choose a collision-free slot only after assigning 80-column firmware, floppy and block storage. Do not overwrite slot 3/internal 80-column ROM or repurpose existing soft switches. Discovery and protocol-version checking must precede file operations; missing bridge returns an error, not an infinite poll loop.

The guest is confined to `/retro/exchange/apple2/`. It may not name arbitrary host paths or mount images. Implement length-bounded requests, handle validation, cancellation and safe guest-memory buffer access across bank changes. Freeze whether transfers copy through a small peripheral FIFO or use explicit guest buffers; do not dereference a guest address as a host pointer.

Preserve Apple file type, auxiliary type and exact EOF where relevant. A BASIC program, binary with a load address and text file cannot all be reconstructed from raw bytes plus an arbitrary extension. Choose and version a metadata sidecar/envelope in the common exchange contract, validate its association with the payload and define behavior when metadata is missing. Host text conversion is explicit; binary mode must never rewrite CR/LF, high bits, zero bytes or trailing data.

End-to-end workflow:

```text
PC/Mac browser → menu 6 File Transfer → exchange/apple2
exit File Transfer with Enter → menu 3 + Enter
HOSTXFER import → ProDOS creates the guest file on its own volume
edit/run/save in guest → HOSTXFER export → exchange/apple2
cleanly return to menu → menu 6 → browser download
```

No network activity is required for the local guest bridge. Test binary byte equality and metadata equality independently. Test overwrite refusal, interrupted transfer, missing media, full guest volume and full host exchange storage. A failed import must not destroy an existing guest file.

## Reference and debugging strategy

Use a known desktop Apple IIe emulator in the exact CPU/ROM/RAM/device profile as the behavioral reference. AppleWin is the first source-level reference inspected here; choose an executable desktop reference appropriate to the development host and record its version. Do not compare an enhanced 65C02 system against an NMOS baseline without noting the difference. Any borrowed GPL code needs license-compatible integration; inspecting behavior does not authorize removing its license.

Keep a small regression corpus: ROM boot, Applesoft input/output, a bank-switch diagnostic, asymmetric 80-column text, one full-screen editor, one disk save/load sequence and a ProDOS transfer utility. Record every ROM/image hash. On divergence capture CPU PC/registers, switch state, physical bank, bus access, disk unit/block and terminal cell coordinates. Diagnose the first divergence, not merely the final frozen screen.

Likely fault categories are wrong ROM/CPU pairing, incorrect switch precedence, reversed columns, stale auxiliary dirty tracking, ambiguous image ordering, missing controller timing, incompatible ProDOS version and incorrect bridge metadata. A CPU-core change requires evidence that the bus/device/input layer is not the cause.

## Milestones and acceptance gates

All statuses are PLANNED; source review below does not mark implementation milestones complete.

| ID | Deliverable and acceptance evidence |
|---|---|
| APPLE-M1 | HOST-M1/CP/M/UCSD gates satisfied; exact IIe profile, primary manuals, core and ROM provenance pinned |
| APPLE-M2 | CPU integrated and tested; reset, bus reads/writes and memory ownership verified |
| APPLE-M3 | ROM monitor/Applesoft boot, keyboard latch and basic 40-column rendering on desktop and ESP32 |
| APPLE-M4 | Historical main/aux/language-card mapping and 80-column firmware; switch truth table and asymmetric 80×24 tests |
| APPLE-M5 | ANSI renderer/input qualification: full-screen editor, attributes, wrap, reconnect and paste pacing |
| APPLE-M6 | Qualified floppy boot/read/write and image-order checks; persistence and read-only errors |
| APPLE-M7 | ProDOS block-device firmware plus larger volumes; capacity and boundary evidence |
| APPLE-M8 | Native HOSTXFER bridge; browser staging, binary and Apple metadata round trips |
| APPLE-M9 | Missing/corrupt resources, media loss, failed writes, resets, clean return and repeated mode switches |
| APPLE-M10 | Reproducible build/resources, measured budgets, compatibility matrix, complete logs and user instructions |

Definition of Done: menu 3 boots actual guest firmware/software, historical 80-column state is represented correctly, accepted editor/BASIC/disk workflows work, large volumes are qualified, exchange preserves content/metadata, resources persist across clean restarts and no unresolved corruption issue remains. State unsupported features explicitly. Both desktop and real ESP32-S3 evidence are required; this document itself supplies neither an executable nor test results.

## Decision log

| ID | Status | Decision / reason |
|---|---|---|
| APPLE-DEC-001 | Required | Menu 3, native ESP-IDF, shared HOST-M1, KIM-1 removed |
| APPLE-DEC-002 | Proposed baseline | Unenhanced IIe with extended auxiliary RAM, for historical 80-column support and NMOS core compatibility |
| APPLE-DEC-003 | Preferred candidate | floooh/chips m6502; external proven CPU, pin and validate before integration |
| APPLE-DEC-004 | Source-supported | Separate memory mapping from display selection; render both physical banks |
| APPLE-DEC-005 | Required | Terminal renderer reads screen memory; ROM output interception alone is insufficient |
| APPLE-DEC-006 | Required | LittleFS minimal immutable resources, SD larger/writable images |
| APPLE-DEC-007 | Proposed | Separate ProDOS block device for large volumes; no fake giant Disk II floppy |
| APPLE-DEC-008 | Required | Guest filesystem utility plus common local exchange; no per-emulator WiFi/server |
| APPLE-DEC-009 | Proposed | Versioned Apple metadata envelope/sidecar; common host ABI must ratify encoding |

For new decisions record date, alternatives, evidence, consequences and superseded IDs. “Proposed” items are implementation choices to qualify, not previously approved historical facts.

## Issue log

| ID | State | Issue / next experiment / do not repeat |
|---|---|---|
| APPLE-ISSUE-001 | OPEN | Obtain original manual edition/pages and close complete switch precedence audit; source review is not manual verification |
| APPLE-ISSUE-002 | OPEN | Choose hashes for NMOS-compatible ROM/OS; do not pair enhanced ROM/software with a 6502-only core |
| APPLE-ISSUE-003 | OPEN | Confirm every text row, bank order and page/mode combination against a reference diagnostic |
| APPLE-ISSUE-004 | OPEN | Select compatible Disk II device implementation/license; sector access alone does not emulate the controller |
| APPLE-ISSUE-005 | OPEN | Select block-device slot/firmware and freeze bridge slot without conflicts |
| APPLE-ISSUE-006 | OPEN | Define metadata/filename policy; do not infer Apple type or load address only from extension |
| APPLE-ISSUE-007 | OPEN | Measure renderer throughput, keyboard latency and storage timing on actual board |

Each issue must accumulate Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat. No issue has been experimentally resolved in this delivery.

## Verification log

| ID | Status | Evidence / remaining action |
|---|---|---|
| APPLE-VERIFY-001 | SOURCE-REVIEW | 2026-10-04: inspected m6502 upstream API/license; target core suite NOT RUN |
| APPLE-VERIFY-002 | SOURCE-REVIEW | 2026-10-04: AppleWin Memory.cpp confirms separate bank/mode controls; manual page audit remains OPEN |
| APPLE-VERIFY-003 | SOURCE-REVIEW | 2026-10-04: AppleWin NTSC.cpp 80-column path reads both banks; asymmetric device test NOT RUN |
| APPLE-VERIFY-004 | SOURCE-REVIEW | 2026-10-04: ProDOS file-format fields reviewed; target maximum-volume qualification NOT RUN |
| APPLE-VERIFY-005 | NOT RUN | ROM reset/boot, CPU tests and keyboard |
| APPLE-VERIFY-006 | NOT RUN | Full bank/switch truth table and 80×24 renderer/editor |
| APPLE-VERIFY-007 | NOT RUN | Floppy and block-device read/write boundaries, corruption rejection and persistence |
| APPLE-VERIFY-008 | NOT RUN | Guest/browser exchange, exact bytes and Apple metadata |
| APPLE-VERIFY-009 | NOT RUN | ESP32 budgets, repeated launch/reset, failure/recovery and terminal restoration |

## Next implementation session

Read the actual `projectPrompt.md` and these three design files. Inspect the repository and HOST-M1 completion evidence. Resolve APPLE-ISSUE-001/002 and pin a reproducible desktop profile before importing a core. Work one milestone at a time. Append evidence and failed experiments to this file at session end; never relabel a source review as a passing hardware test.
