# designCP_M.md — CP/M 2.2 engineering memory

Revision: 2026-10-04. Status: reconstructed design baseline; implementation unverified.

## Provenance and purpose

Reconstructed from the retrieved “ESP32 emulator” conversation and reconciled with its latest HOST-M1 decisions. The previous downloadable attachment itself was unavailable, so this is not a byte-for-byte recovery. Preserved decisions: menu 1, genuine CP/M 2.2 CCP/BDOS, shared external Z80 core, 64 KiB address space, LittleFS A:, SD B:–E:, BIOS/DPH/DPB design, CP-M1 through CP-M9 and permanent engineering logs. Later File Transfer and large-image requirements supersede the older USB-transfer and directory proposals.

Deliver a practical software-development CP/M machine: command line, editors, assemblers and language tools. Games and machine-specific graphics hardware are not acceptance targets. CP/M completion precedes UCSD implementation.

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

## Machine architecture

```text
menu 1 + Enter → availability check → cpmMachine
  → external Z80 core → 64 KiB guest memory
  → genuine CCP + BDOS + target BIOS
  → virtual console/disk/exchange I/O → common host services
```

Use `superzazu/z80` as the preserved preferred core. Its upstream repository describes C99, MIT licensing and zexdoc/zexall success; rerun suitable tests on the pinned integration. UCSD must reuse this core component but have independent machine state and boot resources. Do not substitute RunCPM or intercept BDOS to implement a FAT-backed fake CP/M. A small guest BIOS with explicit virtual I/O is permitted; CCP and BDOS remain genuine guest code. [Core source](https://github.com/superzazu/z80).

Guest addresses are 16-bit across `$0000–$FFFF`. Load the system at addresses determined by the selected 64K CP/M build, not guessed constants. Document CCP, BDOS, BIOS, stack, buffers, allocation vectors and transient-program boundary in a linker/map artifact. Page zero must contain the correct warm-boot and BDOS entry jumps, I/O byte/current-drive state and standard default FCB/DMA locations. Preserve a 128-byte default DMA area at `$0080`, and program loading at `$0100`. Protect host memory even when guest software supplies bad addresses; apply explicit guest wrap semantics where the CPU requires them.

### Startup and restart

1. HOST-M1 completes; `1` + `[Enter]` checks implementation and resource manifest.
2. Validate A: boot resource and all configured mandatory images. Report optional missing drives individually.
3. Acquire exclusive media/console ownership; allocate and initialize CPU, RAM and virtual controller state.
4. Execute the documented loader/bootstrap for the selected CP/M build. Record its entry PC, load ranges and BIOS ABI.
5. Execute cold BOOT, install page-zero vectors and reach a genuine `A>` prompt.
6. A guest warm boot reloads the required system portions, resets disk state as specified by BIOS and returns to CCP, not the host menu.
7. A controlled host exit flushes media. Physical ESP32 reset re-enters the main menu.

Do not accept printing `A>` from host code as a successful boot. Boot errors include resource path, expected/actual size or hash and a bounded diagnostic; they must not leave an infinite busy loop.

### BIOS contract

Implement the CP/M 2.2 jump-table order and calling conventions for BOOT, WBOOT, CONST, CONIN, CONOUT, LIST, PUNCH, READER, HOME, SELDSK, SETTRK, SETSEC, SETDMA, READ, WRITE, LISTST and SECTRAN. Keep unsupported peripheral behavior explicit and deterministic. CONST is nonblocking; CONIN waits cooperatively. Disk errors must return the documented failure status. An invalid drive must not alias A:.

The BIOS owns guest DPH/DPB structures; the host owns image byte I/O. Freeze virtual register/port semantics and guest assembly definitions together. READ/WRITE operate on CP/M logical 128-byte records; physical-sector translation or deblocking must be explicit. Audit track/sector numbering, reserved boot tracks, skew, DMA bounds, write type and read-only errors. A plain host offset calculation is valid only for the selected declared image format. Use the original Digital Research CP/M 2.2 alteration/BIOS documentation as the implementation authority; the [BIOS index](https://www.seasip.info/Cpm/bios.html) is a navigation aid, not a substitute for release-specific conventions.

### Drives and capacity

| Drive | Storage | Role | Initial access |
|---|---|---|---|
| A: | LittleFS minimal `cpm/system.dsk` | Boot, CCP/BDOS/BIOS and essential commands | RO |
| B: | `/retro/images/cpm/languages.dsk` | Language tools | RO or explicitly configured RW |
| C: | `/retro/images/cpm/tools.dsk` | Assemblers/editors/utilities | RO or explicitly configured RW |
| D: | `/retro/images/cpm/archive.dsk` | Larger software volume | Configured |
| E: | `/retro/images/cpm/work.dsk` | Source, builds, results | RW |

These role-based filenames are proposed canonical defaults, not recovered existing assets. Missing mandatory E: can disable the full development profile while an explicitly supported recovery profile may still boot A:. Programs that write their current drive must be run with a writable drive or configured output path; never silently write into LittleFS A:.

Each disk profile must define image byte length, sector ordering, logical SPT, BSH, BLM, EXM, DSM, DRM, AL0/AL1, CKS and OFF; corresponding DPH pointers and buffer lengths must agree. Derive capacity from the actual DPB and reserved tracks. Validate directory allocation, extent encoding and allocation-vector storage together. Document a matching host image-creation recipe and disk definition. Do not recycle a DPB from another image just because its size matches.

Large virtual disks are required but must stay within genuine CP/M 2.2 semantics. Earlier conversational examples of 32–64 MB CP/M volumes were not validated and are not requirements. Start with the proven reference geometry, then qualify the largest useful compatible profile against the original manual, formatter, BDOS and applications. Use multiple drives when a larger single filesystem cannot be represented safely. No claim of a maximum capacity becomes accepted until its DPB calculation and boundary tests are logged.

### Terminal and guest tools

The host provides an 80×24 ANSI/VT100-compatible USB terminal path. CP/M console output is a byte stream; applications must be configured for the corresponding terminal personality. Test CR/LF, backspace/delete, Ctrl-C, Ctrl-S/Ctrl-Q policy, escape keys, cursor addressing, erase and reverse video using the chosen editor. Do not insert line endings into binary file data. Keep debug logs on a separate sink or in bounded buffers.

A representative software set includes ASM/DDT, PIP/STAT and at least one editor and language toolchain selected from available licensed resources (for example BDS C or Turbo Pascal). Preserve exact versions and terminal patches in the resource manifest. Availability in an archive is not proof that a program runs on this machine.

## Guest exchange utility

Provide `HOST.COM`, not a replacement named `PIP.COM`. Proposed user interface:

```text
A>HOST DIR
A>HOST GET HELLO.C E:HELLO.C
A>HOST PUT E:RESULT.TXT RESULT.TXT
```

Its guest filesystem operations use BDOS. Its exchange transport uses reserved virtual Z80 I/O ports and the versioned common protocol. Only `/retro/exchange/cpm/` is accessible. Port numbers, utility binary and protocol are deliverables of CP-M8, not existing implemented features.

CP/M 2.2 file sizes are record-oriented. Do not promise arbitrary-byte-length binary round trips by trimming `$1A` or zero bytes. Preserve original byte length in a versioned common metadata record/transfer envelope when needed, tied to the file and invalidated if the guest changes it; otherwise explicitly export full 128-byte records. The acceptance suite must distinguish exact-byte mode from record-preserving mode. Text newline/EOF conversion is optional, explicit and never applied to COM/binary files. Handle 8.3 names, user areas, case collisions, existing targets, disk-full and aborted imports predictably.

## Milestones and acceptance gates

All statuses are PLANNED; this document records no executed emulator tests.

| ID | Deliverable and exit evidence |
|---|---|
| CP-M1 | HOST-M1 accepted; core, reference machine and image provenance pinned; desktop reference boots real CP/M |
| CP-M2 | External Z80 integrated; CPU tests and 64 KiB memory/port tests recorded |
| CP-M3 | BIOS ABI, memory map, DPH/DPB and loader validated against reference |
| CP-M4 | LittleFS A: reaches genuine A>; DIR, transient command and warm boot work |
| CP-M5 | SD B:–E: selection, RW, missing media and RO errors; persistence after clean restart |
| CP-M6 | 80×24 editor plus assemble/compile/run workflow works on ESP32 |
| CP-M7 | Large-disk profile justified; first/last records, extents, directory-full and disk-full tested |
| CP-M8 | HOST.COM import/export and browser staging; length metadata and binary hashes verified |
| CP-M9 | Fault handling, repeated resets, memory/performance measurement, reproducible build and completed logs |

Definition of Done: every gate has evidence, no unresolved data-corruption issue, genuine guest applications execute, media persist through clean restart, invalid media fail safely and all deployment resources are reproducible. Only then advance to UCSD. A desktop pass does not close ESP32 verification.

## Decision log

| ID | Status | Decision and rationale |
|---|---|---|
| CP-DEC-001 | Preserved | Genuine CP/M 2.2 CCP/BDOS, external Z80, 64 KiB; retain guest software semantics |
| CP-DEC-002 | Preserved | Minimal RO A: in LittleFS; software/work images on SD |
| CP-DEC-003 | Updated | Shared HOST-M1 File Transfer and local guest bridge supersede older USB-transfer proposals |
| CP-DEC-004 | Required | Guest-compatible DPB controls maximum size; 32/64 MB examples are not accepted geometries |
| CP-DEC-005 | Proposed | Exact-byte exchange metadata addresses CP/M record padding; common ABI must ratify details |

## Issue log

| ID | State | Problem / next evidence / do not repeat |
|---|---|---|
| CP-ISSUE-001 | OPEN | Select and hash the exact 64K system/BIOS image; do not mix unrelated boot and DPB configurations |
| CP-ISSUE-002 | OPEN | Derive and test large-disk geometry; do not infer capacity from host FAT32 |
| CP-ISSUE-003 | OPEN | Freeze exchange ports and exact-length metadata; never strip trailing binary bytes heuristically |
| CP-ISSUE-004 | OPEN | Verify editor terminal personality and physical USB behavior; no unmeasured speed promises |

For each issue append the full engineering-memory chain, evidence and regression record before closing it.

## Verification log

| ID | Status | Required evidence |
|---|---|---|
| CP-VERIFY-001 | SOURCE-REVIEW | Preferred core repository inspected 2026-10-04; upstream claims are not target test results |
| CP-VERIFY-002 | NOT RUN | CPU suite plus memory/port integration tests |
| CP-VERIFY-003 | NOT RUN | Cold boot, warm boot and command execution on desktop and ESP32 |
| CP-VERIFY-004 | NOT RUN | All drive boundaries, DPB consistency, RO and media failure |
| CP-VERIFY-005 | NOT RUN | Editor/compiler and exact-byte/record-mode transfers |
| CP-VERIFY-006 | NOT RUN | Persistence, storage fault recovery, repeated launch/exit and resource budget |

Development references: [superzazu/z80](https://github.com/superzazu/z80), [z80pack reference system](https://github.com/udo-munk/z80pack). Record the actual reference commit and image hashes before reproducing its behavior.
