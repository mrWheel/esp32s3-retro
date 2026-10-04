# designUCSD.md — Z80 UCSD p-System II.0 engineering memory

Revision: 2026-10-04. Status: reconstructed design baseline; implementation unverified.

## Provenance and purpose

Reconstructed from the retrieved “ESP32 emulator” design decisions; the old attachment was unavailable. This preserves its architecture and CP/M-first development sequence, not its original wording. The selected target is Z80 UCSD p-System II.0, menu option 2, sharing the CP/M Z80 core. CP/M may assist historical bootstrap/build/reference work but must not be a runtime dependency. Use the historical Z80 p-code interpreter; do not write a new interpreter. Preserve UCSD volume semantics and milestones UCSD-M1 through UCSD-M10.

Deliver the real p-System command environment with editor, Pascal compiler and execution of compiled p-code. This is neither Turbo Pascal under CP/M nor an Apple Pascal variant.

## Project-wide contract

`projectPrompt.md` is the authority for host architecture and coding rules. Its latest recovered revision was delivered as `projectPrompt_v2.md`; use the canonical name in the project. These documents specialize that contract, not replace it. Companion documents are `designCP_M.md`, `designUCSD.md`, `designAppleII.md`, `designMPM.md` and `designSWTPC.md`. KIM-1 is removed.

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

File Transfer is a project-wide facility, not a server implemented inside each emulator. Only menu option 6 starts WiFi, using exclusively `michmich/esp-idf-wifi-provisioner` with the recovered `^0.4.0` requirement; resolve and lock the actual tested version in the host dependency lock. The dedicated upload/download/delete webserver starts after networking is available and exposes only `/retro/exchange/`. Traversal, absolute-path escape, malformed names and unintended overwrite must be rejected. Uploads use bounded streaming and temporary files; incomplete uploads must not appear as completed files. Binary data must remain byte-identical. `[Enter]` exits File Transfer to the main menu; WiFi may then stop. Emulator execution and browser File Transfer are mutually exclusive in the initial design.

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

## Layered architecture

```text
menu 2 + Enter
  → UCSD machine/bootstrap
  → shared external Z80 core + independent 64 KiB guest address space
  → historical II.0 Z80 p-code interpreter + p-System files
  → release-specific BIOS/device interface
  → host console / image blocks / exchange
```

Reuse the pinned `superzazu/z80` component chosen for CP/M. Reinitialize registers, RAM and I/O state when switching machines; no CP/M residency or inherited BIOS pointer is allowed. Host adapters may share implementation where their contracts agree, but UCSD disk units, block numbering and error conventions must be explicit.

Use z80pack as the first desktop reference candidate. Its maintainer distributes UCSD versions including II.0 and describes repaired II.0 source disks. Choose one complete, matching release, interpreter and disk set; record their hashes and bootstrap procedure. Do not combine I.4/I.5/II.0/IV.0 components based on filenames. Repository existence does not prove the selected images have already been tested. [Maintainer documentation](https://www.icl1900.co.uk/unix4fun/z80pack/index.html), [reference repository](https://github.com/udo-munk/z80pack).

### Mandatory desktop reference gate

Before ESP32-specific debugging, boot the chosen system on a desktop reference, enter the editor, create a small Pascal source, compile it and run its output. Save input transcript, screen captures, resource hashes and resulting files. Identify the actual Z80 interpreter, system volume, runtime files, compiler and terminal configuration. Reproduce any failure there before blaming the ESP32 core.

Document the reference's BIOS vectors, I/O ports, initial PC/SP, load ranges, memory reservations, boot sectors and required system files. `SYSTEM.PASCAL` or other familiar filenames alone are not a sufficient boot specification. Extract the actual contract from this release's loader/interpreter source and record the relevant symbols.

### Direct startup from the menu

1. `2` + `[Enter]` requests UCSD directly, after CP/M's acceptance gate and HOST-M1.
2. Validate matching LittleFS system resources and mandatory SD volumes.
3. Initialize a fresh Z80 machine and exclusive device ownership.
4. Run a reproducible direct bootstrap that loads the selected interpreter/system as its documented loader would.
5. Enter the native UCSD command environment without showing or requiring `A>` and without executing a resident CP/M CCP/BDOS.
6. Support normal p-System operations and guest restart according to that build. Physical ESP32 reset returns to the host menu.

If the source distribution initially launches through `PASCAL.COM`, trace which CP/M/BIOS services are used during loading and after handoff. Replace only the loader dependency with a documented direct bootstrap and provide the required BIOS hardware contract. Do not assume copying the COM file to `$0100` achieves CP/M independence. Any hidden BDOS dependency is a blocking issue until understood and removed from the runtime path. Sharing a CP/M-style BIOS interface is not the same as running under CP/M.

### Memory and I/O

Maintain the Z80's 16-bit address space; do not expand guest memory just because PSRAM is available. Record interpreter code/data, stack, heap, runtime workspace, BIOS tables and device buffers for the selected build. Keep p-code files and interpreter release compatible. Instrument invalid host buffer requests and unexpected I/O without changing guest program semantics.

Implement the selected reference's console status/input/output and disk/unit operations, including return values and register preservation. Freeze guest-visible ABI before porting to ESP32. If port numbers differ from CP/M, use a per-machine dispatcher. No random BIOS patches, swallowed errors or unconditional successful reads.

## Storage and volume semantics

| Host location | Intended contents | Policy |
|---|---|---|
| LittleFS `ucsd/` | Minimal matching system/boot volume(s), interpreter bootstrap and manifest | RO |
| `/retro/images/ucsd/` | Compiler, source, utilities and writable work volumes | Per-image RO/RW |
| `/retro/exchange/ucsd/` | Browser staging files for HOSTXFER | Host-managed exchange |

System resources must actually fit the declared LittleFS partition. Only minimal boot resources are mandatory there; move larger supplementary components to SD. If the selected system writes its boot volume, qualify a configured writable SD copy or a documented writable overlay rather than silently modifying the flash master. Treat that as an explicit deployment profile.

Publish a volume/unit table containing UCSD unit number, volume label, image file, mandatory flag, access mode, total blocks, block size, sector ordering and any reserved areas. User-visible UCSD names and units remain native; do not relabel volumes A:/B:/C: or send UCSD filenames to BDOS.

The logical block interface must be derived from the chosen II.0 implementation. Explicitly map logical blocks to image sectors and account for any interleave/skew/header. Verify multi-sector reads and first/last block. Host byte offsets use checked arithmetic; image size alone does not establish format compatibility.

### Larger volumes

Retain historical images for reference, but qualify larger writable work volumes. No universal 32/64/128 MB geometry is imposed. Audit this release's volume directory fields, block-number representation, formatter, interpreter device layer, file-size limits and allocation behavior together. Do not infer usable size from a 16-bit field without checking signedness and reserved values. Store the resulting capacity equation and formatter recipe in a decision record.

Test allocation near the end of the volume, directory saturation, file growth, deletion/reuse and fragmentation or contiguous-allocation restrictions of the selected filesystem. A filesystem that cannot grow a file safely must return an error rather than have the host extend the image behind it. Multiple validated volumes are the fallback when one larger volume is unsupported.

## Terminal and development workflow

Use the shared USB terminal at 80×24 with an ANSI/VT100-compatible presentation. Configure or adapt the p-System terminal description for the exact II.0 distribution. The full-screen editor must handle cursor motion, erase, backspace, insert/delete, escape and control keys; a boot banner or line-mode prompt is insufficient.

Avoid treating all terminal escape sequences as printable guest input. Specify key translations and escape timing. Keep guest terminal output free of host logs. Test a complete edit → compile → run → save → restart → reopen cycle. Compiler and runtime errors must remain readable, including cursor placement at source errors.

The sample program must perform console I/O and a file write/read, then be compiled on the guest. Save both source and compiled output in a writable UCSD volume and verify them after clean restart.

## Guest exchange utility

Provide a native `HOSTXFER` utility, written in a compatible Pascal/assembly combination for this p-System. It uses guest file services to read/write UCSD files and a small documented Z80 bridge to the common hostExchange ABI. Do not assume Pascal can issue Z80 IN/OUT without an appropriate release-specific assembly binding. Inspect and document calling conventions and clobbers.

Proposed operations are directory listing, import and export. Namespace is restricted to `/retro/exchange/ucsd/`. The guest path syntax must match the chosen system; command examples are not frozen before terminal/reference qualification. No FAT driver or webserver belongs inside the guest.

UCSD text/source formats may contain guest-specific encoding, page structure or length semantics. Define two distinct modes: raw native file preservation and explicitly converted host text. Verify the actual II.0 text format before claiming a host `.PAS` file can be compiled unchanged. Preserve native binary/code payloads and any required logical-length metadata; never strip padding heuristically. Enforce guest filenames, lengths and supported file kinds, with explicit collision/overwrite behavior.

## Milestones and acceptance gates

All milestones are PLANNED, not completed.

| ID | Deliverable and exit evidence |
|---|---|
| UCSD-M1 | CP/M accepted; II.0 reference build/distribution and resource hashes selected |
| UCSD-M2 | Desktop boot, editor, Pascal compile/run and file I/O demonstrated |
| UCSD-M3 | Loader, interpreter, memory map and BIOS/device ABI documented |
| UCSD-M4 | Direct bootstrap proven without runtime CCP/BDOS; menu 2 launches it |
| UCSD-M5 | LittleFS minimal system boots on ESP32; mandatory/optional resource failures handled |
| UCSD-M6 | SD native volumes read/write with correct units, labels and block translation |
| UCSD-M7 | Full 80×24 edit/compile/run/save/reopen workflow on ESP32 |
| UCSD-M8 | Larger-volume profile qualified including allocation/size boundaries |
| UCSD-M9 | HOSTXFER and browser staging, native binary hashes and explicit text conversion |
| UCSD-M10 | Reset/recovery, repeat launch, memory/performance, deployment instructions and engineering logs complete |

Definition of Done: direct independent p-System boot, functioning development environment, persistent native volumes, validated large-volume policy, usable exchange and completed desktop/ESP32 verification. No unresolved corruption or unexplained interpreter crash. Only then begin Apple II implementation.

## Decision log

| ID | Status | Decision |
|---|---|---|
| UCSD-DEC-001 | Preserved | Z80 p-System II.0, menu 2; not Turbo Pascal or Apple Pascal |
| UCSD-DEC-002 | Preserved | Shared proven Z80 core and historical p-code interpreter |
| UCSD-DEC-003 | Preserved | Desktop reference success precedes ESP32 debugging |
| UCSD-DEC-004 | Preserved | CP/M only for historical build/bootstrap/reference, not runtime |
| UCSD-DEC-005 | Updated | Minimal LittleFS boot, larger/writable native volumes on SD, common exchange |
| UCSD-DEC-006 | Required | Volume maximum derives from this release's complete stack, not host size |

## Issue log

| ID | State | Problem / next evidence / do not repeat |
|---|---|---|
| UCSD-ISSUE-001 | OPEN | Select exact matching II.0 interpreter and volumes; never mix release artifacts |
| UCSD-ISSUE-002 | OPEN | Trace PASCAL.COM loader and direct boot contract; do not assume BIOS implies resident CP/M |
| UCSD-ISSUE-003 | OPEN | Verify terminal configuration with editor; do not accept a banner as full-screen compatibility |
| UCSD-ISSUE-004 | OPEN | Derive volume/block limits and native text format from chosen release |
| UCSD-ISSUE-005 | OPEN | Freeze Pascal/Z80 exchange calling convention and metadata handling |

Append Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat to each issue as work occurs.

## Verification log

| ID | Status | Evidence required |
|---|---|---|
| UCSD-VERIFY-001 | SOURCE-REVIEW | z80pack repository and maintainer's II.0 availability reviewed 2026-10-04; no runtime claim |
| UCSD-VERIFY-002 | NOT RUN | Desktop edit/compile/run and exact resources |
| UCSD-VERIFY-003 | NOT RUN | Direct bootstrap, absence of runtime CP/M dependency |
| UCSD-VERIFY-004 | NOT RUN | ESP32 system boot and 80×24 editor |
| UCSD-VERIFY-005 | NOT RUN | Native volume persistence, large-volume limits and fault behavior |
| UCSD-VERIFY-006 | NOT RUN | Raw binary round trip, text conversion and interrupted transfer |
| UCSD-VERIFY-007 | NOT RUN | Repeated launch/reset, memory and performance |
