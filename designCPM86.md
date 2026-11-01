# designCPM86.md — CP/M-86 engineering memory

Revision: 2026-11-01 (logging narrative removed at the user's request; the document carries design content plus PASS / NOT PASSED entries only). The CPU core is the project-owned tiny8086-style interpreter (`components/cpm86Core/tiny8086`), see CPM86-VERIFY-043 and `migrate8086CPU.md`; the EMU86 sources were deleted from the tree in CPM86-VERIFY-044, so EMU86 statements below are historical. Status: RETRO86_V1 BIOS, CP/M-86 system resources, drive configuration, A:–F: firmware disk services and the `RETRO86_TIMER_B1` guest timer extension are implemented. The sanitized desktop host test boots the real CCP/BDOS to `A>` and successfully runs `DIR`; multiple-drive guest behavior and timer I/O are covered by CPU tests. The ESP-IDF 6.0.2 firmware build passes. The scripted hardware suite passes 59 / 0 / 1-skipped-manual on the current post-review build (CPM86-VERIFY-054), including the automated native ED/ASM86/GENCMD/run workflow, timer delta and BENCH86; PSRAM (8 MiB) is verified on hardware. The open work is exactly the NOT PASSED entries in the issue log. CP/M-86 1.1 remains the initial release candidate.

## Provenance and purpose

Deliver a practical CP/M-86 software-development machine alongside the existing CP/M 2.2 machine: command line, native editors, an assembler and at least one compile/run workflow. Genuine CP/M-86 guest code must execute on an external 8086 core. Games, IBM PC compatibility, graphics adapters, DOS, protected mode and 8087 emulation are outside the initial acceptance scope.

## Project-wide contract

projectPrompt.md remains the authority for host architecture, machine registry, manifests, storage, console, exchange and coding rules. Companion documents are designCPM80.md, designCPM86.md, designUCSD.md, designApple2.md and designSWTPC.md. The user-supplied CP/M design is the baseline for the integration requirements below; the actual current repository and canonical host document must be reconciled before implementation. This task creates only this design document.

## Menu integration

Preserve the canonical host menu numbers, especially CP/M-86 at 2 and File Transfer at 6. The host already reserves slot 2 for CP/M-86. The following menu is the existing canonical menu, not a proposed slot change:
```
ESP32-S3 Retro Computer

1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800

6. File Transfer

Select system [1-6]:
```
Keep the canonical menu specification and range validation at 1–6. CP/M-86 is now integrated at option 2; the registry distinguishes it from missing or invalid resources. Do not reuse another machine’s slot. Every choice requires Enter. Normalize CR/LF/CRLF so a line is submitted once. Invalid or unavailable choices explain the reason and remain in the menu.

HOST-M1 owns USB console, LittleFS, SD detection, versioned /retro/layout.txt, block storage, exchange and browser File Transfer. The current host already registers cpm86 and prepares/validates its storage roots; reuse that schema and APIs. If later changes require a version change, perform an explicit compatible migration; do not independently define a new layout version. A FAT32 32 GB SD card remains recommended. Never format an unknown card or replace user images automatically.

Paths beginning /retro/ are SD-volume paths independent of the VFS mount prefix. Use the host resolver; do not compile /microSD into the emulator. LittleFS uses /littlefs. Proposed CP/M-86 resource namespaces are:
```
/littlefs/cpm86/
/retro/images/cpm86/
/retro/backup/cpm86/
```
Keep them independent of the existing cpm directories. A resource description records release, machine profile, format, exact size, origin, rights evidence and access policy using the common manifest contract. Minimal boot resources belong in LittleFS only if the measured partition budget permits them. Larger or writable images belong on SD. If minimal resources do not fit, explicitly require SD; do not promise an unavailable recovery boot.

File Transfer is the shared host service. Only option 6 starts WiFi, exclusively through michmich/esp-idf-wifi-provisioner, preserving the supplied ^0.4.0 requirement and locking the tested dependency version. The current GUI already includes CP/M-86: whole images use /retro/images/cpm86/. Display full VFS paths. Reuse bounded streaming, temporary uploads, progress, traversal rejection and explicit overwrite policy. The supplied transfer limit remains 4 GiB minus 2 bytes, subject to the canonical HTTP implementation. That limit does not imply that CP/M-86 supports a disk or file of that size.

Browser File Transfer and guest execution are mutually exclusive initially. Guest files are managed through the browser GUI and filesystem adapters; the host must not edit a mounted guest filesystem while it is active. Image upload, conversion and replacement occur while the machine is stopped.

Physical reset returns to the host menu. Guest warm boot is separate. On CP/M-86, Ctrl+] (ASCII 1Dh) requests a host exit; it is intercepted rather than passed to the guest and stops execution at an instruction/batch boundary, then closes media before returning to the host's ENTER prompt. Ordinary Enter and other guest control keys are not host exit requests. This binding is CP/M-86-specific; other emulators require explicit stop-key integration. Physical reset or power loss can interrupt guest metadata writes; a completed host write is not a promise of crash-atomic guest filesystem updates.

## Implementation discipline

Use native ESP-IDF, CMake and VSCode ESP-IDF workflow; no Arduino dependency. Target ESP32-S3 / LOLIN S3 Pro. Verify board revision, USB routing, PSRAM mode, flash size and partition configuration against the actual board. Do not infer this board’s available memory from another S3 variant. ESP-IDF 6.0.2 is the supplied project’s baseline; record the actual version used for each test.

Project-owned code, identifiers, messages and documentation are English. Use Allman braces, two-space indentation, lowerCamelCase and comments on their own preceding line in the form //— comment. Preserve upstream source style, copyright and license; isolate local adapters and document every upstream patch.

Use an existing CPU core. Do not write another 8086 emulator, intercept BDOS to implement host-side CP/M, or patch arbitrary binary offsets until a prompt appears. Separate CPU, memory/bus, guest BIOS, boot loader, media backend and console. A small guest BIOS using documented virtual I/O is allowed; genuine CCP/BDOS performs command processing, program loading, memory management and filesystem operations.

Pin source commits and tools before integration. Bound memory, queues and handles; check allocations and image-offset arithmetic. Never load a whole disk into RAM. CPU stepping and scheduler yields must preserve guest-visible state. Debugger, POSIX, SDL and host process dependencies must not leak into the ESP-IDF build. Desktop sanitizer passes and firmware build success are distinct from hardware execution.

## Engineering-memory rules

Read this document’s decisions and open issues before changing the machine. Preserve failed experiments; supersede conclusions with new numbered records. Repeat an experiment only with a changed variable or new evidence. Distinguish proposed design, source review, reference execution, desktop target execution, firmware build and hardware verification.

Every verification record captures date, firmware commit, core commit, reference commit, ESP-IDF/tool versions, board, terminal, exact commands/input, expected result, actual result and evidence path. Use N/A only when a field does not apply; use NOT RECORDED when relevant information is missing. Only observed execution earns PASS.

Every issue follows:

***Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat***.

Unknown causes remain unknown. A workaround is not a root cause. Store reproducible fixtures and bounded traces, including the first divergent CS:IP, registers, bus event or disk record where available. Emulator source licenses do not automatically cover bundled operating systems, ROMs, test datasets or applications.

## Machine architecture

flowchart TD

- M["Menu 2 + Enter"] --> A["Availability and ownership check"]
- A --> G["cpm86Machine"]
- G --> C["External 8086 core"]
- C --> B["20-bit machine bus"]
- B --> R["Guest memory and boot resources"]
- B --> I["Guest BIOS virtual I/O"]
- I --> H["Shared console, image storage and file-transfer services"]

The target is a project-defined serial-console CP/M-86 machine, provisionally named RETRO86_V1. This name identifies a planned profile, not a finished disk geometry. It does not claim Intel iSBC or IBM PC hardware compatibility. Boot resources for another OEM machine require their original hardware or a reproducibly rebuilt BIOS; CPU compatibility alone is insufficient.

## CPU selection and reference machine

|Candidate                                            |Intended role                      |Gate before use                                                                                         |
|-----------------------------------------------------|-----------------------------------|--------------------------------------------------------------------------------------------------------|
|[EMU86](https://github.com/mfld-fr/emu86)            |removed external CPU candidate   |Pin source, isolate CPU/bus dependencies, audit instruction coverage, verify tests and restart lifecycle|
|[isbc8612](https://github.com/TheBrokenPipe/isbc8612)|CP/M-86 reference-machine candidate|Pin source, verify reproduction and rights for code and every asset separately                          |
|[8086tiny](https://github.com/adriancable/8086tiny)  |Used (prefered) candidate                 |Assess extraction effort, semantics, source license and BIOS/table dependencies                         |

EMU86 describes a modular IA16 emulator under MIT, but its README also identifies unimplemented instructions. Its embedded-development purpose is not proof of an ESP-IDF-ready library or complete 8086 conformance. Its inspected headers expose processor and memory/I/O interfaces; the adapter API proposed below is project-owned and is not claimed to exist upstream. Sources: README, processor header, memory/I/O header.

The isbc8612 README reports CP/M-86 1.1 boot on an emulated iSBC 86/12 and floppy subsystem, with a handwritten core, two drives and polling-mode runtime. Those are upstream claims, not results reproduced here. Use its console/boot/disk behaviour as a reference after reproduction; do not mix its machine-dependent BIOS with a different bus without a documented port. No source license was established in this review. Do not vendor its source or redistribute its supplied assets until the applicable rights are resolved.

Two profiles must remain distinct:

	1.	Reference profile: reproduce the pinned reference’s original hardware and media on a workstation. Record its map, geometry, commands and traces.
	2.	Target profile: genuine selected CP/M-86 release with a purpose-built guest BIOS for RETRO86_V1, using common host services.

A temporary faithful reference-machine port is permitted only as a separately identified milestone. It must not silently become the target profile or imply that target B:–F: work. If EMU86 fails the selection gate, record the failure and evaluate a fallback with equivalent evidence rather than maintaining an expanding replacement CPU implementation.

## Components and adapter boundaries

Ownership, updated to reflect the prototype and remaining design:

|Location                  |Responsibility                                                                |
|--------------------------|------------------------------------------------------------------------------|
|`components/cpm86Core/tiny8086/`|Project-owned 8086 interpreter (8086tiny style); the former `third_party/emu86/` was removed in CPM86-VERIFY-044|
|`components/cpm86Core/`   |CPU adapter, bounded RAM and host byte-port interface                         |
|`components/cpm86Core/bios/retro86bios.a86`|Guest BIOS source (assembled with ASM86 into the separate H86 overlay)|
|`main/cpm86Machine.c`     |Registry integration, resource probe, host-service adapters and task lifecycle|
|`main/cpm86DriveConfig.c` |`drives.cfg` parsing, profile/size validation and path confinement            |
|`main/cpm86BiosOverlay.c` |Intel-Hex H86 overlay loader with record/checksum validation                  |
|`tools/cpm86/`            |Image tooling (`diskImageCpm86.py`, `buildDiskImageCpm86.py`, `extractCpm86.py`), cpmtools `diskdefs` and the scripted `cpm86HardwareTest.py`|
|`docs/cpm86/`             |Reference traces, ABI/profile audits and verification evidence                |

Do not duplicate shared image I/O or path resolution. Extract reusable infrastructure where needed without changing CP/M 2.2 (CPM-80) behaviour. Reuse algorithms only after CP/M-86 filesystem semantics are checked; the existing CPM-80 image tool is not assumed compatible.

The project adapter should provide explicit initialize, reset, execute-budget, snapshot and destroy operations. CPU state belongs to one machine instance. Audit upstream global/static state; if isolation requires invasive changes, document the cost and consider another core. A single active emulator does not excuse stale state across repeated launches.

Bus operations distinguish memory reads/writes and 8-/16-bit port accesses. Unsupported I/O is deterministic and traceable; documented harmless probes may return a declared idle value. Unexpected hardware accesses fail or stop diagnostically instead of masquerading as a successful device. Convert guest addresses through checked bus functions; never cast guest addresses to host pointers.

## Guest memory and CPU semantics

  Model the 8086 real-mode address space separately from usable RAM:
  physicalAddress = ((segment << 4) + offset) & 0xFFFFF
  address-space size = 1,048,576 bytes

Use a host integer wide enough for the unmasked calculation. Execute architectural little-endian accesses through the core’s correctly audited rules. Do not assume all multi-byte accesses, instruction fetches and string operations share identical segment-boundary behaviour. Test operand offset wrap and physical 20-bit wrap independently against the chosen CPU specification and fixtures.

## Initial memory plan:

|Region                       |Planned treatment                                                             |
|-----------------------------|------------------------------------------------------------------------------|
|`00000h–003FFh`              |Interrupt vector table; populated by guest initialization                     |
|Remaining lower address space|OS, guest BIOS, stacks and allocatable memory according to a frozen linker map|
|Boot/ROM window, if required |Declared read-only map with an explicit reset/boot entry                      |
|Unmapped regions             |Deterministic profile-specific bus behaviour; never advertised as usable RAM  |

No CCP, BDOS, BIOS or TPA addresses are frozen yet. Derive them from the exact release, linker map, memory-region table and boot chain; record segment bases, paragraph counts, buffer sizes and entry points before CPM86-M3 closes. Do not transplant the CP/M 2.2 $0100, page-zero, $C400 or $DA00 layout.

Prefer a contiguous 1 MiB PSRAM-backed address-space allocation if the real board and host budget allow it. Keep CPU registers, frequently accessed adapter state and bounded storage buffers in suitable internal memory. A smaller populated RAM region can be valid only if the guest memory descriptor and bus map agree. Failing allocation prevents launch with a useful host error. No swap or entire-image caching is required.

8086 and 8088 instruction-level compatibility does not imply matching bus timing or prefetch behaviour. Initial acceptance is functional, not cycle-accurate. Restrict guest software to the selected 8086 instruction set; 80186 features are not implicitly enabled because a candidate supports them. Audit flags, arithmetic, segment overrides, far calls/returns, stack, INT/IRET, string/REP behaviour, interrupt inhibition, HLT and divide behaviour. Define whether and how an external interrupt can wake HLT. Break long REP execution into safe scheduler work without changing instruction state or architectural interrupt behaviour.

### Declared RETRO86_V1 CPU profile rules (CPM86-DEC-021, 2026-10-31)

These behaviours are profile rules of the project-owned `tiny8086` core, not open
bugs. A gate or conformance claim must not silently assume full-chip semantics beyond
this list; guests must not depend on behaviours marked "not modelled".

  1.  Trap flag: TF is storable (PUSHF/POPF/IRET preserve it) but single-step is not
      implemented; setting TF has no architectural effect.
  2.  External interrupts: INTR/NMI are not modelled and no interrupt source exists
      (the production core has no `intHook`); IF is storable but inert. HLT is sticky:
      only a core reset/relaunch clears it, there is no wake-up path.
  3.  LOCK (F0h) is accepted and ignored (single core, no shared bus). REP/REPZ/REPNZ
      and segment prefixes are honoured; repeated prefixes of the same class keep the
      last one.
  4.  ESC (D8h..DFh) decodes the ModRM operand (including displacement) and ignores
      the operation; no 8087 state exists.
  5.  PUSHA/POPA and `PUSH SP` (FF /6) use the pre-decrement original SP value
      (80186-like), matching the previous EMU86-era guest expectations.
  6.  Shift/rotate counts come from the full CL (no 5-bit masking, 8086 rule); a count
      of zero executes no rotate and leaves all flags unchanged. `D0`/`D1`/`C0`/`C1`
      counts behave the same way.
  7.  Reserved flags: bit 1 always reads as 1 through PUSHF/POPF/IRET/INT frames. The
      8086's always-1 bits 12..15 are not modelled (they read and write as 0); guest
      code must not test them. Bit 5 is stored like an ordinary undefined bit.
  8.  Byte I/O only; word I/O (E5h/E7h/EDh/EFh forms) faults deterministically and
      records the port for diagnostics (see `docs/cpm86/virtualIo.md`).
  9.  REP runs to completion inside one core call; because no asynchronous interrupt
      source exists, this cannot observe or delay an interrupt. If external interrupts
      or HLT wake-up are ever added (rule 2), REP chunking must be revisited.

## Genuine operating system and program format

Select one exact CP/M-86 release and OEM resource set. Audit its Digital Research system/programming manuals and source before defining BIOS details. Record the manual edition and page references in docs/cpm86/abiAudit.md. OS/source/utility rights are individual gates; the CP/M rights clarification cited in the supplied CP/M design must be checked for the exact materials used here.

Native applications use CP/M-86 .CMD executables. Existing CP/M-80 .COM utilities, HELLO, editors and compilers cannot be reused as executable machine code. DOS .COM/.EXE binaries are also outside this ABI. Text source may be portable, but language tools and runtime libraries must be native and qualified.

CP/M-86 BDOS entry uses software interrupt E0h (decimal 224). The CPU must execute the interrupt vector and real guest handler. Do not turn this into a host syscall dispatcher. Audit the selected release’s register arguments, returned values, pointer/segment conventions and memory-management functions from its primary manual. The documentation gate remains open; this document does not specify a complete BDOS register ABI.

## Current implementation status

CP/M-86 CPU/interface and machine integration:

	●	The EMU86 CPU source (pinned at `81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac`) was replaced by the project-owned `tiny8086` core and removed from the tree; it remains in git history.
	●	Implemented reset, entry-point, bounded memory load/read/write, single-step and instruction-budget APIs. The configured RAM is 1..1 MiB; physical addresses wrap at 20 bits and unmapped accesses fault.
	●	Implemented byte I/O adapter with optional machine-specific port callbacks; word I/O is rejected. This is not yet the guest BIOS device ABI.
	●	Host fixtures cover selected arithmetic/memory instructions, 20-bit physical wrap, HLT, custom port callbacks, unsupported word I/O, RAM bounds, unmapped fetch, reset and singleton enforcement. Host tests use sanitizers; ESP-IDF build also passes.
	●	Upstream CPU state is global/static, so the adapter enforces one active core; complete 8086 instruction/segment conformance is not established.
	●	The CP/M-86 machine is integrated at menu slot 2 with resource probing, a project BIOS, system files and a boot disk. Desktop host tests boot the genuine guest to `A>` and run `DIR`; the ESP-IDF firmware build succeeds.
	●	A user-flashed build passed the scripted hardware suite 20/20 and the PSRAM check (CPM86-VERIFY-046). The post-review build now passes the same scripted suite with 59 passed / 0 failed / 1 skipped-manual, with a measured timer delta of 1093 ms, BENCH86 scale 1 figures of 4130/1554 ms and the automated native ED/ASM86/GENCMD/run workflow (CPM86-VERIFY-049/050/051/054).

Still open (mirrors the NOT PASSED issue-log entries): independent reference-core comparison, `GENCMD`/`ASM86` tool-binary provenance, independent formatter plus multi-extent LARGE/BIG proof, 80×24 escape-sequence/batch behaviour, real SD media qualification, reproducible release.

## Current RETRO86_V1 implementation

The project-owned BIOS is assembled from `components/cpm86Core/bios/retro86bios.a86` into the separate H86 overlay `littlefs/cpm86/retro86bios.h86`; it is not linked into the pinned CP/M-86 CCP/BDOS CMD payload. The runtime loads `cpm.sys` at physical `00510h`, installs the BIOS overlay and BDOS interrupt vector, enters at `0051h:2500h`, and provides console and A:–F: disk access through byte ports. Port assignments are E0h/E1h/E2h for console status/input/output, E8h–EEh for drive, track, 128-byte record, command/status and record-data transfers, and EFh for selected-drive availability.

The boot disk is a raw 160 KiB image: 40 tracks of eight 512-byte physical sectors, represented to CP/M-86 as 32 128-byte records per track. Raw track 0 is reserved; DPB OFF=1 already selects raw track 1 as the first CP/M filesystem track. Therefore the BIOS passes the selected CP/M track to the disk backend unchanged; adding another track would skip the directory. Drives with profile `RETRO86_DATA_LARGE_V1` instead use a 528,384-byte image (129 tracks, 2 KiB blocks, 128 directory entries) and a second DPB selected by SELDSK from the EFh answer; all other drives use this geometry and DPB. The host rejects track 0, tracks 40 and above (129 and above for the large profile), and sectors 32 and above.

`/littlefs/cpm86/drives.cfg` configures read-only A: from LittleFS and optional B:–F: images from the CP/M-86 image directory. The parser validates the entire file, drive letters, profile, access mode and root-constrained paths. Missing images remain offline; invalid configuration falls back to A: only. Each image must be exactly 163,840 bytes (528,384 for the large profile, 8,392,704 for the BIG profile `RETRO86_DATA_BIG_V1`: 2049 tracks, 16 KiB blocks, 512 directory entries, EXM 7, DPB2, EFh=02h) and open successfully before the BIOS reports its DPH. The `CPM86` image-tool profile defines the matching raw geometry.

The desktop regression test copies the system image to a temporary file, verifies a real `A>` prompt, sends `DIR`, verifies `ASM86` and `PIP`, then selects configured B: and runs `DIR` there. These results are desktop CPU-core evidence only; they do not qualify hardware SD access, disk writes, clean guest exit or broad compatibility.

Startup and restart

	1.	Registry checks compiled implementation and selected profile; report unimplemented separately from resource failure.
	2.	Resolve configuration and required LittleFS/SD resources through host APIs; check exact format and size before acquiring them.
	3.	Acquire exclusive media/console ownership; allocate memory and buffers. Unwind all prior acquisitions on failure.
	4.	Initialize CPU and bus, load approved boot/ROM/OS resources at declared addresses and apply the profile’s cold-start state.
	5.	Execute the real guest boot chain, install its interrupt vectors and enter genuine CCP. The host does not print A> as proof of boot.
	6.	Guest warm boot follows the selected release’s BIOS/OS path; it does not return to the host menu. Document which drive/user state survives.
	7.	Clean host exit releases resources. Physical reset returns to the menu without automatic guest resume.

Either a ROM/disk bootstrap or a deterministic direct loader may be selected for the target. A direct loader must reproduce documented initial guest state and boot entry without replacing guest OS services. Freeze the choice and test cold versus warm restart independently.

## BIOS contract

Build the guest BIOS in 8086 code for the exact chosen release. Its guest entry table, call style, register/segment preservation, return values and data structures come from that release’s manual/source. Do not copy CP/M-80 / CP/M 2.2’s 17-entry three-byte Z80 jump table, its $FE stub or its DPH pointer assumptions.

Required service families include cold/warm initialization, console status/input/output, declared optional peripherals, drive selection, track/sector positioning, DMA address/segment handling, record read/write and translation/allocation structures. Determine the exact list and conventions in the ABI audit. Especially verify CP/M-86 segment-aware DMA, memory-region reporting and BIOS data-pointer representation.

Console status is nonblocking; waiting for input yields cooperatively. READ/WRITE return the release-defined guest status. Invalid drive selection cannot alias A: or change the last valid selected drive. Each configured drive owns the necessary DPH/DPB, allocation and directory state. Read-only media failures propagate into the guest.

Use a narrow project-defined virtual device interface between guest BIOS and host. The implemented synchronous byte-port mapping, transfer status, image offsets and reset/ownership limits are documented in [docs/cpm86/virtualIo.md](./docs/cpm86/virtualIo.md). Keep BIOS service I/O separate from the exchange ports below. Guest-visible buffers must be validated against populated RAM, including segment and physical boundaries. Reject wrapping DMA if the profile disallows it; do not silently clamp it.

The BIOS may call host image-record primitives through virtual I/O. The host never interprets BDOS calls or guest directory operations. If a selected OEM BIOS is retained unchanged instead, emulate its actual device interface under a distinct profile.

## Drives and capacity

|Drive|Configured storage                         |Role                             |Current policy|
|-----|-------------------------------------------|---------------------------------|--------------|
|A:   |Minimal validated LittleFS image if it fits|System and native essential tools|RO by default; configurable RW|
|B:   |Configured SD image                        |Language tools                   |RO, optional  |
|C:   |Configured SD image                        |Editors/assemblers               |RO, optional  |
|D:   |Configured SD image                        |Utilities                        |RO, optional  |
|E:   |Configured SD image                        |Source/build/results             |RW, optional  |
|F:   |Configured SD image                        |Archive/workspace                |RW, optional  |

A:–F: are supported by the RETRO86_V1 BIOS when configured images validate; this does not claim every CP/M-86 application supports six drives. Reject unsupported letters rather than aliasing them.

Use `/littlefs/cpm86/drives.cfg` with the host’s established line-oriented syntax:
```
  A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM
  B=/retro/images/cpm86/language.dsk,RW,RETRO86_DATA_BIG_V1
  C=/retro/images/cpm86/work86-1.dsk,RW,RETRO86_DATA_LARGE_V1
  D=/retro/images/cpm86/work86-2.dsk,RW,RETRO86_DATA_BIG_V1
```

`RETRO86_SYSTEM` (the older name `RETRO86_SYSTEM_V1` is still accepted) is permitted for A: at the fixed LittleFS system path in RO or RW mode. The firmware chooses the A: geometry from the size of `/littlefs/cpm86/system.dsk`: 163,840 bytes selects the 160 KiB DPB (BIOS geometry code `0xFF`), 528,384 bytes selects the LARGE DPB (129 tracks × 8 × 512 bytes, 2 KiB blocks, 128 directory entries, DSM=255, code `0x01`); any other size is logged as an error and A: is not mounted. The BIOS needs no change because SELDSK already picks the DPB from the firmware geometry code. `RETRO86_DATA_V1` is permitted for B:–F: and currently uses the same verified 160 KiB geometry. Reject duplicate drives, path escapes, unsupported profile/access combinations and malformed lines as an entire invalid configuration. Retain the built-in A: as fallback and disable optional drives. Never create blank media during boot.

Freeze each profile’s exact image length, physical/logical sector layout, numbering, skew, reserved tracks, boot bytes, record size and filesystem parameters. Audit SPT, BSH, BLM, EXM, DSM, DRM, AL0/AL1, CKS and OFF where the chosen release uses them, together with pointer sizes, allocation-vector length, extent encoding and block-number width. Document physical-sector versus logical-record translation. CP/M-86 is not a license to enlarge a DPB arbitrarily.

The existing CP/M 2.2 SYSTEM/LARGE geometries and RETROCPM signature are not inherited. A matching image byte length alone is insufficient. Initial geometry should be conservative and independently verifiable. Qualify larger profiles only after filesystem capacity, directory/extents and application limits are proved. Host FAT32 and HTTP capacity remain separate.

A: defaults to read-only. When configured RW, record writes update the LittleFS system image; the next firmware build regenerates that image. The checked-in system disk remains RO.

## Preparing E: on macOS

Extend prepareSd.py to create the CP/M-86 directories without replacing existing configurations or files. Build a profile-aware image tool after the geometry is frozen. Required workstation operations are create, list, add and validate; exact CLI flags are pending and must not be advertised as working commands yet.

Creation refuses existing targets by default. Addition validates native 8.3 names, directory capacity, extents, allocation and duplicates, then writes atomically. Retain the original image on failure. Copy a validated work image to the configured E: path, eject safely, and test guest E: selection, DIR and a native copy/write/readback workflow on disposable media.

## Importing software or disk images

Preserve original downloads and record package/version, URLs, timestamp and rights evidence. Select only native CP/M-86 programs. Archive presence is discovery evidence, not redistribution permission. Review proprietary compiler/editor packages individually.

Inspect archival formats before conversion. .IMD/.TD0 are not raw .DSK images. Use a documented converter, preserve the original, and verify resulting sector order and geometry. A structurally valid CP/M filesystem can still have the wrong OEM boot chain. Tools execute on a workstation; no firmware network downloader is required.

## Terminal and guest tools

Reuse the common 80×24 ANSI/VT100 USB byte stream. Choose terminal-aware native applications or configure their terminal personality reproducibly. Qualify CR/LF, backspace/delete, Ctrl-C, flow control, escape sequences, cursor movement, erase and reverse video. Keep logs off the guest screen.

The acceptance set includes genuine directory/type commands, a native transient HELLO.CMD, a native file-copy utility, an editor, an assembler and at least one compile/run toolchain. Determine names and behaviour from the selected distribution; do not simply append .CMD to the CP/M 2.2 utility list. Record any terminal patches and maintain original binaries.

Measure interactive response, instruction throughput, storage latency, guest-visible memory and application completion on the real board. A fast ESP32 CPU or ample PSRAM does not prove adequate emulation speed.

## Milestones and acceptance gates

Gate status is binary. A gate is PASS only when every exit-evidence item has an observed entry in the verification log; otherwise NOT PASSED. Existing CP/M 2.2 passes cannot close these gates.

|ID      |Deliverable and exit evidence                                                                                                                         |Status|
|--------|------------------------------------------------------------------------------------------------------------------------------------------------------|------|
|CPM86-M1|Canonical host/menu amendments reconciled; CPU/reference candidates pinned; rights and provenance established; reference boot independently reproduced|NOT PASSED — reference boot not independently reproduced; `GENCMD`/`ASM86` provenance UNKNOWN (ISSUE-002/011)|
|CPM86-M2|External core adapter passes CPU/bus/segment/interrupt tests, sanitizer runs and repeated lifecycle checks; missing-instruction coverage resolved     |NOT PASSED — own vectors + genuine DR binaries pass, no independent reference-core comparison yet (ISSUE-001)|
|CPM86-M3|Selected release manual audit, target guest BIOS, memory/linker map, boot entry and first disk geometry frozen and reproducible                       |NOT PASSED — primary-manual ABI audit done (`docs/cpm86/abiAudit.md`); tool-binary provenance open (ISSUE-003)|
|CPM86-M4|Genuine target CCP/BDOS boots on desktop; DIR, HELLO.CMD and warm boot run; LittleFS resource budget and ESP-IDF build recorded                       |NOT PASSED — HELLO/warm boot proven on hardware (VERIFY-054), not yet on the desktop path|
|CPM86-M5|ESP32-S3 target boot plus real SD B:–F: selection, RO/RW errors, persistence and media-failure qualification                                          |NOT PASSED — no real SD media qualified; LittleFS-backed volumes only (ISSUE-010)|
|CPM86-M6|Native 80×24 editor and assemble/compile/run workflow work on hardware                                                                                |NOT PASSED — ED/ASM86/GENCMD/run automated green on hardware (VERIFY-054); 80×24 escape behaviour still manual (ISSUE-008)|
|CPM86-M7|Any expanded profile audited and independently formatted; first/last records, extents, directory-full/disk-full and all drive letters verified        |NOT PASSED — no independent formatter; LARGE multi-extent cpmtools-incompatible; BIG multi-extent unproven (ISSUE-005)|
|CPM86-M8|Reset/exit/fault recovery, repeated launches, memory/latency measurements, reproducible release and complete logs                                     |NOT PASSED — lifecycle and measurements automated green (VERIFY-050/051/054); reproducible release not established (ISSUE-015)|

Definition of Done: every applicable gate has observed evidence; genuine guest applications run; clean restart preserves writable media; invalid resources fail safely; no unresolved corruption issue remains; source/assets/tools and deployment can be reproduced legally. Desktop boot is not hardware acceptance. A deferred feature remains explicitly deferred, not PASS.

## Decision log

|ID           |Status                 |Decision and rationale                                                                                             |
|-------------|-----------------------|-------------------------------------------------------------------------------------------------------------------|
|CPM86-DEC-001|Selected design        |Add a separate genuine CP/M-86 machine with an external 8086 core; preserve the CP/M 2.2 machine                   |
|CPM86-DEC-002|Preferred candidate    |Evaluate EMU86 first; final selection requires instruction and integration evidence                                |
|CPM86-DEC-003|Reference candidate    |Reproduce isbc8612 independently; rights and target hardware differences remain gates                              |
|CPM86-DEC-004|Selected design        |Target a virtual serial-console BIOS, not mandatory IBM PC emulation                                               |
|CPM86-DEC-005|Proposed host amendment|Add menu option 7 and retain File Transfer at 6; update canonical host contract together                           |
|CPM86-DEC-006|Selected design        |Independent cpm86 images/exchange; reuse common bounded host services                                              |
|CPM86-DEC-007|Selected design        |Use 20-bit bus semantics and measured PSRAM allocation; usable RAM is a declared guest map                         |
|CPM86-DEC-008|Required               |Native CMD programs, segment-aware BIOS/BDOS audit and release-specific disk profiles; no borrowed Z80 binaries/map|
|CPM86-DEC-010|Required               |Preserve exact bytes with validated metadata; never strip EOF/padding heuristically                                |
|CPM86-DEC-011|Preserved host policy  |Physical reset enters menu; browser transfer and emulation are initially exclusive                                 |
|CPM86-DEC-012|Pending selection      |CP/M-86 1.1 is initial release candidate; exact OEM source/resource set not yet selected                           |
|CPM86-DEC-013|Host reconciliation   |Keep CP/M-86 at menu option 2 and File Transfer at 6, as already defined by projectPrompt.md and the host registry; supersedes the proposed option 7 in CPM86-DEC-005|
|CPM86-DEC-014|Selected reference set|Use CP/M-86 1.1 materials in [TheBrokenPipe/isbc8612](https://github.com/TheBrokenPipe/isbc8612/tree/fc31925b444701abdc0bf4d84f555e5e52eab9e7) at `fc31925b444701abdc0bf4d84f555e5e52eab9e7` as reference material only; user confirms CP/M-86 source/binaries are permitted for non-commercial use. Its iSBC BIOS is not the RETRO86_V1 BIOS|
|CPM86-DEC-015|Preferred core to evaluate|Evaluate [EMU86](https://github.com/mfld-fr/emu86/tree/81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac) at `81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac` first: embedded-oriented, modular and MIT-licensed at that revision. This is not a qualification; upstream documents unimplemented rare instructions, keeps processor state in a file-static singleton, and its host entry point uses POSIX APIs|
|CPM86-DEC-016|Selected target        |Build a RETRO86_V1 guest BIOS and target system image, not an IBM PC/XT machine profile|
|CPM86-DEC-017|Selected OS source     |Use [tsupplis/cpm86-kernel](https://github.com/tsupplis/cpm86-kernel/tree/00927e17f43ea4241cc0809531cf68ce835e9c16) at `00927e17f43ea4241cc0809531cf68ce835e9c16` as CP/M-86 1.1 source/reference under the repository's included rights statement and the user's non-commercial-use confirmation; do not silently ship its IBM PC XT BIOS as RETRO86_V1|
|CPM86-DEC-018|Local system reference|Use the user-provided root image `cmp86.img` as a CP/M-86 source/reference candidate; preserve it unchanged and validate its exact disk format, boot path and rights before building RETRO86_V1 resources|
|CPM86-DEC-019|Selected emulator extension|Expose a monotonic unsigned millisecond count on byte IN ports F0h–F4h under capability B1h (`RETRO86_TIMER_B1`); inject a generic host callback into the CP/M-86 core, snapshot only on F1h, and keep ESP-IDF out of the CPU core. Other CPU emulators require explicit integration.|
|CPM86-DEC-020|Implemented 2026-10-23|Honor the configured A: access mode instead of forcing the system disk read-only. Keep RO as the default; when configured RW, the menu displays `[Writable!]` and writes persist in LittleFS until the firmware rebuilds the image. This was chosen over the previous fixed-RO policy so the system disk can be modified by native guests. Host parser coverage was added; guest writes and ESP32-S3 persistence remain unverified.|
|CPM86-DEC-021|Declared 2026-10-31|Declare the unimplemented/unmodelled CPU behaviours (trap flag inert, no external interrupts and sticky HLT, LOCK ignored, ESC decoded-and-ignored, 80186-like original-SP on PUSHA/PUSH SP, unmasked CL shift counts, flags bits 12..15 not always-1) as RETRO86_V1 profile rules instead of open bugs; see "Declared RETRO86_V1 CPU profile rules". `docs/cpm86/virtualIo.md` was reconciled: the claimed F8h/F9h reservation is removed and unmapped-port behaviour is documented as in-range misuse faulting while out-of-range ports read idle FFh/ignore writes with a one-time notice.|
|CPM86-DEC-022|Declared 2026-11-01|tiny8086 decodes ALU-group opcode 82h as the **byte** immediate form (alias of 80h: `r/m8, imm8`), not as an alias of 83h. Evidence: the genuine Digital Research CP/M-86 1.1 `STAT.CMD` executes 82h as a byte add; decoding it 16-bit makes STAT print `Free Space: ` and hang, reproduced deterministically in the desktop host fixture and on the 2026-11-01 hardware run (CPM86-VERIFY-049). The VERIFY-047 review's opposite hypothesis is superseded; covered by the conformance vector "82h aliases 80h".|

## Issue log

|ID             |State|Problem / next evidence / do not repeat                                                                                                |
|---------------|-----|---------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-ISSUE-001|NOT PASSED|Pinned core is only fixture-tested; complete instruction/segment conformance and independent reference boot remain unverified. 2026-11-01: 109 vectors + 32 Jcc cases green under ASan/UBSan (CPM86-VERIFY-048, ISSUE-012), real DR guest boots and runs on host fixtures and on hardware (CPM86-VERIFY-049/051). Open: no INDEPENDENT reference core comparison has been run (only the project's own vectors + genuine DR binaries as witnesses)|
|CPM86-ISSUE-002|NOT PASSED|Reference code/assets rights and independent boot reproduction; do not inherit bundled-resource permission from repository availability. 2026-11-01 (`docs/cpm86/abiAudit.md` §8): manuals/guest system rights rest on CPM86-DEC-014/017 (user-confirmed non-commercial use of the iSBC86/12 materials); `PIP.CMD` provenance has an in-tree witness (byte-identical to the cpm86-kernel base `pip.cmd`); the `GENCMD`/`ASM86` binary provenance is recorded as UNKNOWN (no source, URL or version pin). Open: tool-binary provenance and independent boot reproduction|
|CPM86-ISSUE-003|NOT PASSED|Exact CP/M-86 release, primary-manual ABI audit and boot resource provenance; do not claim guessed register conventions. 2026-11-01 (`docs/cpm86/abiAudit.md`): exact release = CP/M-86 1.1 (System Guide June 1981 + 1.1 release notes 1982, pinned commit); primary-manual ABI audit complete with page citations. Key correction: the earlier working premise "BDOS argument in DE" is WRONG per SG p.23 — byte parameters go in DL, word parameters in DX (DX "performs the same function as DE in CP/M-80"); INT E0h and function-in-CL confirmed. Open: tool-binary provenance (see ISSUE-002); boot-resource chain provenance for `GENCMD`/`ASM86`|
|CPM86-ISSUE-004|PASS|BIOS/linker map, segments, DMA and memory descriptor; do not transplant CP/M 2.2 page zero or jump table. 2026-11-01 (`docs/cpm86/abiAudit.md` §§3-5): the 21-entry BIOS jump table in `retro86bios.a86` matches SG Table 5-1 (p.57) entry-for-entry; DPB field layout/semantics match Tables 6-2/6-4/6-5 (pp.67-71); memory-region table (`db 1; dw 0518h; dw 9AE8h`) and segment-aware SETDMAB/GETSEGB match Table 5-4 (pp.64-65). Documented deviations: READER never yields EOF 1Ah, READ/WRITE perform no retries, SELDSK ignores the DL bit-0 read-only hint, INIT's console/vector duties run in the host instead. Documented residual note (abiAudit §7): directory block-number width not verifiable from the manuals alone; kept as a note, not an open issue|
|CPM86-ISSUE-005|NOT PASSED|Disk geometry/DPB/extent audit and independent formatter; do not reuse SYSTEM/LARGE labels based on size. Audit 2026-11-01 (CPM86-VERIFY-052/053): all three diskdefs/DPB/tool geometries cross-checked (`docs/cpm86/diskdefsValidation.md`); cpmtools interop proven per profile (LARGE multi-extent incompatible, use `diskImageCpm86.py`); record-geometry rule extracted to `cpm86DriveRecordValid` with a host matrix. Open: DPB0 EXM=0 + 1 KiB blocks caps guest files at 8 KiB (tool refuses larger entries); multi-extent BIG guest read unproven on hardware; no independent formatter yet|
|CPM86-ISSUE-006|PASS|LOLIN S3 Pro's 8 MiB octal PSRAM enabled for the configured target, 640 KiB guest allocation directed there. Physical allocation verified on hardware: menu reports `PSRAM: 8192 KiB, free 8184 KiB` and the guest boots/runs from PSRAM-backed RAM (CPM86-VERIFY-046); throughput measured on hardware: BENCH86 4140/1552 ms and timer DELTA 1094 ms on the post-review build (CPM86-VERIFY-050/051)|
|CPM86-ISSUE-007|NOT PASSED|Canonical menu/layout/GUI and host ABI reconciliation; preserve option 6 and existing machine contracts                                |
|CPM86-ISSUE-008|NOT PASSED|Native applications, terminal personality and batch scratch-drive behaviour; Z80 COM or DOS binaries are not acceptance tools. 2026-11-01 (CPM86-VERIFY-054): native workflow automated on hardware (ED save per User's Guide sec.5, ASM86/GENCMD, produced CMDs run from their own drive, PIP copies, TIMER86/BENCH86/WBOOT86/BAD86); terminal personality measured: CR/LF pairs ok, Ctrl-H/0x7F erase ok, CCP 1.1 has NO Ctrl-C line abort (typed line executes; characterized in `terminalTest`), named `DIR` without an explicit file type always reports `NO FILE` (CCP `$DIR`/`$SYS` type default; `dirSyntaxTest`). Open: full-screen/escape-sequence behaviour stays manual; SUBMIT/batch scratch-drive untested|
|CPM86-ISSUE-010|NOT PASSED|Real SD persistence, RO/RW errors, clean exit and reset durability; no host test closes a board gate. 2026-11-01 (CPM86-VERIFY-054): clean exit and relaunch automated (Ctrl+] stop + boot, 3 repeated cycles); RTS reset mid-`PIP` recovers to the menu and the D: volume stays usable (re-copy + run `@DONE PASS`); the guest fault path returns to the menu on ENTER without a host reset; read-only write refusal and record-geometry guards proven at host level (VERIFY-053). Limitation recorded: Ctrl+] is polled only when the guest touches the console, so a stop requested mid-copy lands at copy end. Open: no real SD media on this board — LittleFS-backed volumes only; long-duration RW wear/durability untested|
|CPM86-ISSUE-011|NOT PASSED|RETRO86_V1 BIOS, system image and desktop boot are implemented in VERIFY-019/023. The separate CompuPro reference image remains unaudited for boot suitability; its declared-geometry/image-length discrepancy, OEM boot chain and rights remain open. The upstream default BIOS remains IBM PC XT.|
|CPM86-ISSUE-012|PASS|Conformance suite registered and compiling; 109 vectors + 32 Jcc cases green under ASan/UBSan (CPM86-VERIFY-048). Unregistered tests still never count as evidence|
|CPM86-ISSUE-013|PASS|IDIV/raiseInterrupt/82h fixes present with vectors, FE and 8Ch/8Eh validation present; unmodelled behaviours declared as profile rules (CPM86-DEC-021) and covered by vectors where observable (CPM86-VERIFY-048). Residual: TF single-step, external interrupt/HLT wake and PUSHA/PUSH SP have no executable guest-level test possible (declared inert/80186-like in DEC-021), so they stay profile rules, not tests. Amendment 2026-11-01: the 82h "fix" of this issue was wrong and was reverted the same day (CPM86-DEC-022, CPM86-VERIFY-049); the other four fixes stand|
|CPM86-ISSUE-014|NOT PASSED|`littlefs/cpm86/system copy.dsk` is tracked and packaged into `bootfs.bin` (found in CPM86-VERIFY-047); removal requires explicit user confirmation (memory rule: confirm before deleting). Awaiting the user's decision|
|CPM86-ISSUE-015|NOT PASSED|Repository hygiene outside CP/M-86 scope, user's call: the 6 pre-existing Apple II/UCSD/guestFsDos33 host-test failures documented in `projectPrompt.md` remain unfixed, and stray processes/files in the developer environment (e.g. hung `minicom` instances) are untouched. The 9 temporary `cpm-*-test-*` disk images committed in 7b980eb were deleted on 2026-11-01 at the user's request|

For every issue the complete chain (Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat) and evidence must be recorded before a NOT PASSED entry may become PASS. Supersede decisions rather than deleting failed approaches.

## Verification log

Only entries with observed results are kept (PASS). Unexecuted placeholders, review-only notes and entries fully superseded by later numbered entries were pruned on 2026-11-01 at the user's request; IDs are stable and never reused, so gaps in the numbering are expected.

|ID              |Status                   |Evidence / required next evidence                                                                                                                                                                                                                                                                                                          |
|----------------|-------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-VERIFY-013|PASS (desktop, EMU86 era — historical)|2026-10-06: vendored the pinned EMU86 CPU modules and MIT license with patch notes; integrated a single-active-core adapter, configurable mapped RAM (1..1 MiB), 20-bit physical wrapping, unmapped-memory faults and byte-port callbacks. Host tests pass under the configured sanitizers, including selected instruction, wrap, unsupported word-I/O, reset/singleton and unmapped-fetch fixtures. ESP-IDF 6.0.2 firmware build passes. No CP/M-86 BIOS/OS image, boot disk, full 8086 conformance suite, flash or hardware execution was performed.|
|CPM86-VERIFY-019|PASS (desktop)|2026-10-06: assembled the project RETRO86 BIOS with CP/M-86 ASM-86 1.1N (zero errors), combined it with the pinned `cpm.h86` using native `doscat`, and generated CMD-format `cpm.sys` with GENCMD 1.1N using the kernel Makefile parameters `8080 "CODE[A51,M0000]"`. The initial desktop boot reached `A>`, but `DIR` displayed only `A:` because BIOS READ/WRITE added one to the BDOS track even though DPB OFF=1 already reserves raw track 0. Removing that extra increment made `DIR` list files. The `cpm.sys` is 10,240 bytes and the raw disk is 163,840 bytes. cpmtools 2.23 lists the five expected files. `tests/hostTests.c` now performs the real desktop cold boot, sends `DIR`, requires `ASM86` and `PIP`, and verifies all disk records completed without read errors. The test ran under AppleClang with AddressSanitizer/UndefinedBehaviorSanitizer via `cmake -S tests -B /tmp/retro86-host-tests`, `cmake --build /tmp/retro86-host-tests -j4`, and `ctest --test-dir /tmp/retro86-host-tests --output-on-failure`. No hardware boot, flash, native application run or guest disk write was performed.|
|CPM86-VERIFY-020|PASS (build)|2026-10-06: ESP-IDF 6.0.2 build succeeded for ESP32-S3. `retroHost.bin` is 0xE4920 bytes, with 0x21B6E0 bytes free in the smallest app partition. The build generated `build/bootfs.bin` at 2,097,152 bytes and reported adding `cpm86/cpm.sys`, `cpm86/system.dsk` and `cpm86/README.txt`. Extracting `CPM.SYS` from `system.dsk` with cpmtools 2.23 and `cmp` against `littlefs/cpm86/cpm.sys` confirms byte identity. No flash or hardware run was performed.|
|CPM86-VERIFY-021|PASS (build + config fix)|2026-10-06: the user's LOLIN S3 Pro boot log showed `cpm86CoreNoMemory` (result 3). Root cause confirmed in the active `sdkconfig`: `CONFIG_SPIRAM` was disabled, so the 640 KiB allocation fell back to internal DRAM and failed. Enabled the board's 8 MiB octal ESP-PSRAM64 at 80 MHz and explicit `MALLOC_CAP_SPIRAM` allocation in `sdkconfig.defaults`/`sdkconfig`; added exact free/largest-block diagnostics if core allocation still fails. ESP-IDF 6.0.2 firmware rebuild and sanitized host tests pass. Physical PSRAM initialization/allocation is not verified until the user flashes and boots the firmware.|
|CPM86-VERIFY-022|PASS (hardware)|2026-10-06: on the ESP32-S3 the guest stopped with result 7 after 4,784 instructions. Traces showed the CCP looping on `call SELDRV` (0051:018B/00DA) until the stack overwrote CCP data. Root cause: Xtensa `char` is unsigned, but EMU86 sign-extends 8-bit displacements via `(short)(char)`, so the short jump at 0051:00D8 (`EB B1`) landed 0x100 too far. Fixed with `(signed char)` in op-class.c (4 places) and op-exec.c (1 place). Host tests now build with `-funsigned-char` to reproduce this class of bug. User-observed hardware result: boot reaches `A>`; `dir` lists CPM.SYS, ED, PIP, ASM86, GENCMD; `b:` and `stat` report `?` because those drive/commands are absent. The diagnostic trace (head/recent trace, execute guard over 0051:0800-09FF) remains in the firmware.|
|CPM86-VERIFY-023|PASS (host + build)|2026-10-06: `drives.cfg` validates A:–F:; startup opens configured images, retains A: as the required LittleFS read-only drive, leaves missing optional images offline, and loads the separate H86 BIOS overlay. The sanitized host test boots the real guest, selects B: and executes `DIR`; path/parser regressions pass. Python tooling tests pass (20 tests), and temporary-card preparation generates the expected A:/E: mappings. ESP-IDF 6.0.2 build passes (`retroHost.bin` 0xE8000) and packages `cpm86/retro86bios.h86`. Physical SD access and writes are not verified.|
|CPM86-VERIFY-024|PASS (host regression + build)|2026-10-06: the pasted Turbo Pascal trace reports `no handler for op 40h`; decoded operation ID 40h is DAA and its execution-table entry had no handler. Implemented 8086 DAA and added a sanitized host regression for the reported `ADC AL,40h; DAA` sequence, checking the adjusted accumulator and CF/PF/AF/ZF. Host tests and the ESP-IDF 6.0.2 firmware build pass. No hardware execution was performed.|
|CPM86-VERIFY-025|PASS (host regression + build)|2026-10-06: the subsequent Turbo Pascal compile stopped after control flow reached invalid opcode bytes `FF E9`. Review found the 8086 core left PF/AF/OF incomplete for arithmetic and INC/DEC, even though its conditional-jump handlers consume those flags. Implemented SZP/AF/CF/OF updates for ADD/ADC/SUB/SBB/CMP, logic, INC/DEC and NEG. Sanitized host tests verify arithmetic flag values and parity-driven branching; the test suite and ESP-IDF 6.0.2 build pass. This does not yet prove that Turbo Pascal compilation succeeds on the ESP32-S3; no hardware execution was performed.|
|CPM86-VERIFY-026|PASS (host regression + build)|2026-10-06: the next user log still stopped at `FF E9` after about 4.95 million instructions. Further core review found PUSHA saved the already-decremented SP instead of the original SP required by 80186 semantics. Fixed the saved stack word and added a sanitized fixture verifying all eight saved/restored registers and the original-SP slot. Host tests and ESP-IDF 6.0.2 build pass. This regression is corrected in the core; whether it resolves the Turbo Pascal run remains unverified on hardware.|
|CPM86-VERIFY-027|PASS (host regression + build)|2026-10-06: after the PUSHA fix the user confirmed flashing the latest build, but reported the same failure PC and instruction count; the PUSHA change therefore did not resolve the report. Continued instruction audit found incorrect/missing MUL/IMUL CF/OF updates, signed division sign extension and overflow handling, missing SAL handling, and width/flag errors in byte shifts including SAR. Corrected these semantics and added sanitized host fixtures for signed byte/word division, multiply flags and byte shift results/flags. Host test and ESP-IDF build pass. The exact Turbo Pascal hardware run remains unverified.|
|CPM86-VERIFY-028|PASS (host)|2026-10-06: the `FF E9` stop at 0518:518D/518F was a symptom, not a CPU defect (VERIFY-024..027 did not address it). Reproduced on the host with TurboP.dsk: TURBO.CMD (0x919 paragraphs) is loaded by the BDOS program loader (0051:0E86), which ignores read errors; code from file offset 0x4000 onward was one repeated 16-byte paragraph, so execution reached garbage. Root cause: BIOS `DPB1` (RETRO86_DATA_LARGE_V1) declared EXM=1 (two 16 KiB logical extents per directory entry), whereas `tools/diskImageCpm.py` writes one 16 KiB extent (128 records, 8 two-KiB block pointers) per entry with extent numbers 0,1,2,...; the BDOS therefore could not find records 128 and up of any file larger than 16 KiB on a LARGE drive. Fix: `DPB1` EXM 01h -> 00h in `retro86bios.a86` and `retro86bios.h86` (record checksum recomputed). Regression: `testCpm86Boot` builds a LARGE image in the tool layout with a 19,600-byte two-extent text file and requires every line in `TYPE`; with the old overlay it fails at line 2341 (16 KiB boundary), with the fix it passes. With the patched overlay the host run of TURBO compiles LISTER.PAS (210 lines) and starts it. Caveat: files larger than 16 KiB written by the guest to a LARGE drive under the old EXM=1 DPB use a different directory layout and must be re-copied; `tools/cpm86/diskdefs` `retro86-large` (cpmtools infers EXM=1 for 2 KiB blocks) does not match this layout. Hardware run not yet verified.|
|CPM86-VERIFY-043|PASS (hardware benchmark)|2026-10-24: replaced EMU86 with a project-owned tiny8086-style switch-case interpreter behind the `cpm86Core.h` API (full 8086 plus the 80186 opcodes used by the tests). Host tests pass; CP/M-86 boots to `A>` and BENCH86 passes on hardware at 240 MHz (user-reported). BENCH86 INTEGER/SIEVE: EMU86 89604/36653 ms; tiny8086 12801/4755 (trace on, per-batch delay) → 9599/3531 (time-sliced yield) → 8531/3004 (trace off) → 7682/2850 (inlining, IRAM linker fragment) → 6986/2593 (jump tables, parity table) → 6987/2591 (branch-free flags: no gain) → 6110/2308 (200 ms slice) → 5269/1993 (execute guard removed so the fast run loop is used) → 4972/1892 (dense opcode jump table, `-fno-bit-tests`) → 4078/1516 ms (always_inline `readByte`/`writeByte`/`aluOperation`/`setFlag`). Final index vs the 5 MHz model: 154 INTEGER, 296 SIEVE. Tried and reverted: caching the code-segment base for opcode fetch (slower: 4558/1765 ms). Not effective: `IRAM_ATTR` on a single function; guest RAM in internal RAM versus PSRAM.|
|CPM86-VERIFY-044|PASS (host + build)|2026-10-24: removed `CONFIG_CPM86_INSTRUCTION_TRACE`, the trace API (`cpm86CoreGetRecentTrace/GetHeadTrace`), `cpm86CoreSetExecuteGuard`, `captureInstructionTrace`, the opcode profiler, the CPU self-test and the timing output; deleted `components/cpm86Core/third_party/emu86/`. A guest stop still logs result, CS:IP and the instruction bytes, but no trace window. `CPM86_TINY8086_IRAM` now defaults to `y`. Superseded by this entry: the trace-related statements in CPM86-VERIFY-022 and CPM86-VERIFY-040..042. Host suite passes; an isolated ESP-IDF 6.0.2 ESP32-S3 build passes (image 0x123650 bytes, 62% of the app partition free). The final firmware was not flashed or re-measured by the assistant after the cleanup. Details: `migrate8086CPU.md`.|
|CPM86-VERIFY-046|PASS (hardware, run 2026-10-24)|2026-10-24 evidence recorded from `docs/cpm86/assesmentCPM86.md`: on the LOLIN S3 Pro (ESP32-S3 rev v0.2, 16 MB flash, 8 MiB octal PSRAM) a user-flashed build ran `tools/cpm86/cpm86HardwareTest.py` over the USB console: 20 passed, 0 failed, 4 manual. Covered: cold boot to `A>`, `DIR`/`STAT` on A:, `DIR` on B:/C:/D:, clean rejection of E:/F:, `PIP` copy A:->D: and D:->B:, persistence after Ctrl+] stop and relaunch, `ERA`, three stop/launch cycles, write to read-only A: refused recoverably. PSRAM menu line showed `PSRAM: 8192 KiB, free 8184 KiB`. Boot segment sizes matched `build/retroHost.bin` (circumstantial build identity). Firmware commit NOT RECORDED. At that record CPM86-M5 remained open; the timer delta, measured benchmark and post-review scripted run were closed later by CPM86-VERIFY-049/050/054.|
|CPM86-VERIFY-048|PASS (host + build)|2026-11-01: resolves the compile/registration findings of CPM86-VERIFY-047 and verifies the core fixes. `tests/CMakeLists.txt` registers the `cpm86ConformanceTests` executable (`cpm86ConformanceTests.c`, `cpm86Core.c`, `tiny8086.c`; `-Wall -Wextra -Werror -funsigned-char`, ASan/UBSan) and `add_test(NAME cpm86Conformance ...)`. The vector-table arity defect is fixed (every row supplies `checkFlags`/`flagsMask`/`flagsValue`); one vector added this session: `idiv byte 8000h/FFh raises INT 0`, pinning the new byte `INT16_MIN / -1` guard. Commands: `cmake -S tests -B /tmp/retro-host-tests`, `cmake --build /tmp/retro-host-tests`, `ctest -R 'cpm86Conformance|hostCore'` — PASS (109 vectors + 32 Jcc cases; `hostCore` includes the real-guest boot/`DIR`/multi-drive fixtures). Full suite: 6 failures are the pre-existing Apple II/UCSD/guestFsDos33 fixtures documented in `projectPrompt.md`, unrelated to CP/M-86. Core fixes present in `tiny8086.c`: IDIV `INT32_MIN/-1` and `INT16_MIN/-1` guards before the C division, signed bounds accept `-0x8000`/`-0x80`, `raiseInterrupt` reserved bit 1, 82h decoded as alias of 83h (**superseded the same day by CPM86-DEC-022: 82h is the byte form, alias of 80h — do not re-apply**), 8Ch/8Eh reg field 4..7 rejected, FE /2../7 rejected before shared handlers, PUSHF reserved bit 1; RCR-by-1 OF is `CF xor sign` (hostTests fixture repinned). Firmware: `idf.py -B build-agent-check build` PASS (ESP-IDF 6.0.2, `build/` had a stale venv; separate tree per `projectPrompt.md`). This is desktop + build evidence; hardware acceptance of this exact image is recorded in CPM86-VERIFY-049.|
|CPM86-VERIFY-049|PASS (hardware, 2026-11-01) — suite green + 82h regression found and fixed|First scripted hardware run of the post-review build (flash via `idf.py -B build-agent-check -p /dev/tty.usbmodem202401 flash`). First run on the aa39faf image: 33 passed / 6 failed / 2 manual. `basic: STAT runs on A:` failed — STAT printed BDOS message-table strings (`Bad Directory on`, `Space Allocation Conflict:`, ...) between its own strings and then hung waiting for input; reproduced deterministically on the desktop by a new STAT step in `testCpm86Boot` (kept permanently as a regression guard). A 2x2 experiment (old core `33900b8` + current image = clean; current core + Oct-24 image = broken) exonerated the disk images; a hunk bisect of aa39faf proved the sole root cause is the `82h aliases 83h` decode change from CPM86-VERIFY-048: genuine DR CP/M-86 1.1 binaries execute 82h as the byte form (alias of 80h). Reverted per CPM86-DEC-022, conformance vector flipped to "82h aliases 80h", `ctest -R 'cpm86Conformance|hostCore'` PASS. The other five failures were test-design issues, not firmware: the free-space regex cannot parse `8,176k` (thousands comma) and `fillLimit 60` x ~7.5 KiB can never fill the 8 MiB D: volume — fixed in `tools/include/hwSuite.py`/`hwCpm.py`/`cpm86HardwareTest.py` (comma stripping; fill moved to the 516 KiB C: volume, limit 80). Final run 11:05-11:17 on the rebuilt image (reverted core + fixed TIMER86, `PROG_VERSION` v0.6.1): **39 passed, 0 failed, 2 skipped(manual)**, log `tools/cpm86/cpm86HardwareTest.log`, covering every VERIFY-046 item plus USER areas, disk full, read-only, timer and bench. Full host `ctest` in parallel: only the 6 pre-existing documented Apple II/UCSD/guestFsDos33 failures. The earlier `82h` claim in CPM86-VERIFY-048 is superseded by CPM86-DEC-022.|
|CPM86-VERIFY-050|PASS (hardware, 2026-11-01) — timer delta + BENCH86 measured|New guest program `assets/cpm86/TIMER86/` (NASM `TIMER86.ASM` + `build.py`, pattern of BENCH86; registered in `tests/test_bootDisksBench.py`; packaged on A: via `bootDisks/cpm86/systemDsk/TIMER86.CMD`): checks F0h==B1h, samples t0, blocks on one host keystroke via BDOS function 1 (BIOS CONIN busy-polls port E0h), samples t1, prints raw `T0=`/`T1=` and `DELTA=`. First hardware result `DELTA=29033542 ms` looked like "uptime minus zero"; the raw-sample prints plus a manual byte-level disassembly of the CMD payload (llvm-objdump has no i8086 mode) proved it was a **program bug**: `MOV DX,deltaText` (the print argument) clobbered the high half of the DX:AX difference before `printUnsigned32`, printing `(labelOffset<<16)|lowWord`. The timer path was correct from the start. Fixed by reloading DX:AX from `[t1Low]`/`[t1High]` after the label print (the BENCH86 `reportTime` pattern); the DS/ES reload after the unwrapped BDOS call was kept as documentation of the convention. Hardware measurement with the scripted 1 s host-side key wait: **DELTA=1093 ms** (window 900-1100) — RETRO86_TIMER_B1 (F0h-B1h, F1h sample, F2h-F4h snapshot bytes) verified working on hardware. BENCH86 v2 scale 1 on the same build: **INTEGER 4130 ms / SIEVE 1554 ms** (2026-10-24 baseline 4078/1516; +1.3 %/+2.5 %; index vs 5 MHz model ~152/289). Both are now automated in the `Cpm86` adapter `extras()`; the old manual timer placeholder is gone.|
|CPM86-VERIFY-051|PASS (hardware, 2026-11-01) — BENCH86 report block grouped like BENCH80|The cpm80 suite logs the `A:BENCH80` call, all BENCH80 displays and its verdicts as one block before the next test starts; the cpm86 suite now does the same for BENCH86. `tools/cpm86/cpm86HardwareTest.py`: `benchTest()` moved from the silent low-level `guestRun()` to `cmd("A:BENCH86", 180)` (blocks on `A>` until the program ends or the timeout expires, logs command + collapsed answer), echoes every `BENCH86 v2`/`@BEGIN`/`@END`/`@DONE`/`Elapsed:`/`Index vs`/`Timer unavailable` line with a `bench86: ` prefix and records three inline verdicts via `expect()` (runs-and-completes / sections PASS / indexes recorded); `timerTest()` records its verdict inline too. Verified by a full scripted hardware run 11:34-11:38 on `/dev/tty.usbmodem202401`: **41 passed, 0 failed, 2 manual**, exit 0, log `tools/cpm86/cpm86HardwareTest.log` archived; measured **4140/1552 ms, index 151/289**, timer delta 1094 ms. `tests/test_bootDisksBench.py` OK (3 tests).|
|CPM86-VERIFY-052|PASS (host, 2026-11-01) — diskdefs validator; interop caveat in text|cpmtools 2.23 recipe established on this machine (mkfs.cpm ignores `$DISKDEF` - needs a CWD-relative `diskdefs`; target must pre-exist at exact size via `mkfile`; `cpmls`/`cpmcp` need `-T raw`; the in-repo CLI rewrites the suffix to `.dsk` and can shadow an `.img`). `retro86-big` diskdef added (DPB2: 2049 tracks, 16 KiB blocks, 512 entries). Geometry audit: all three diskdefs agree with DPB0/1/2 and `diskImageCpm86.py` on every auditable field (EXM itself is not expressible in cpmtools 2.23 diskdefs). `cpmcp` copy-in/copy-out byte-identical 11/11 across the three profiles; the 64 KiB file could not be placed on the 160 profile at all. Cross-validation with the firmware decoder (`inspect_image`/`extract_file`): all single-entry files and the 20 KiB multi-extent file extract byte-identical on 160 and BIG; on LARGE the 20 KiB/64 KiB files FAIL ("missing CP/M extent") - cpmtools writes a CP/M-3-style two-entries-per-32-KiB-group layout (EX=1/EX=3, block pointers spilling into bytes 24..27), incompatible with DPB1 EXM=0, empirically confirming the VERIFY-028 caveat. Verdict: `diskdefs` is trustworthy as a geometry validator only; multi-extent LARGE content must keep `diskImageCpm86.py` + the `testCpm86Boot` TYPE fixture as authority. New audit findings for ISSUE-005: DPB0 EXM=0 with 1 KiB blocks cannot address more than 8 blocks per directory entry (guest files >8 KiB on the 160 profile are suspect; the firmware tool refuses such entries), and multi-extent BIG content is still unproven on the real guest. Evidence: `docs/cpm86/diskdefsValidation.md`. No hashes recorded (memory rule).|
|CPM86-VERIFY-053|PASS (host + build, 2026-11-01) — geometry gate + storage guards|Extracted the BIOS record-geometry rule into a pure, unit-testable predicate `cpm86DriveRecordValid(tracks, track, sector)` in `main/cpm86DriveConfig.c/.h` (track 0 reserved boot track, sectors 0..31, profile track count); `cpm86Machine.c diskRecordOffset` and the host boot fixture's `cpm86BootDiskOffset` now both call it, so the fixture exercises the production gate. New host tests in `tests/hostTests.c`: `testCpm86DriveGeometryMatrix` (per profile 40/129/2049: first/last valid track, track 0 and overflow rejected, sector 31 accepted / 32 rejected / 0xFFFF rejected, degenerate track counts rejected); `testCpm86StorageGuards` (through the BIOS ports E8h-EEh: five out-of-range reads fail with status 1 and failure counters, last valid record reads, write/read-back of a 128-byte record, and a read-only-opened image refuses the write with status 1 leaving the data untouched); `testCpm86StorageFull` (guest-visible on disposable LARGE images: C: with all 252 blocks claimed by 126 files and D: with all 128 directory entries used - `PIP C:=A:STAT.CMD` creates nothing on either, no "NO FILE", DIR still lists 126/128 files, guest returns to a prompt in both cases; prompt waits count any drive prompt because a BDOS out-of-space may or may not warm boot). Finding during bring-up: `PIP C:NEW1.TXT=A:PIP.CMD` is rejected by the shipped PIP with "PIP?" while the proven `PIP C:=A:FILE` form works. Full host `ctest`: only the 6 pre-existing documented Apple II/UCSD/guestFsDos33 failures. Firmware `idf.py -B build-agent-check build` PASS: `bootfs.bin` 4194304 bytes (fixed LittleFS partition image, still includes `littlefs/cpm86/system copy.dsk` - removal pending user confirmation), `retroHost.bin` 1335216 bytes.|
|CPM86-VERIFY-054|PASS (hardware, 2026-11-01) — lifecycle + native workflow + abiAudit integration|Suite `tools/cpm86/cpm86HardwareTest.py` extended and fixed against five board probes: guest assets `WBOOT86`/`BAD86` (NASM + build.py per TIMER86 pattern, registered in `tests/test_bootDisksBench.py`); automated native workflow ED(`I`+CR, ESC, `E`+CR alone writes — User's Guide Dec81 sec.5, verified by DIR+TYPE on C:)/`A:ASM86 C:HELLO`/`A:GENCMD C:HELLO`/run produced CMD from its own drive/`PIP` full-name copy from A>/run from B:; warm boot via BDOS function 0 returns to `A>` without the host menu (launched under USER 0); fault `FF/7` -> register dump -> "Press ENTER to return to the main menu." -> ENTER -> menu, no host reset; interrupted writes: Ctrl+] and RTS reset mid-PIP both recover to menu and relaunch, D: stays usable (re-copy + `BENCH86` run from D: = `@DONE PASS`). Findings: (1) `?`-style transient errors are CCP current-drive/user-area lookup misses — SUPERSEDES the VERIFY-053 note ("PIP?" for `PIP C:...=A:...` and `WBOOT86?`/`A:WBOOT86?` were current-drive/USER-3 lookup misses, not syntax rejections); (2) named `DIR` without a file type always reports `NO FILE` (CCP `$DIR`/`$SYS` type default) — characterized as `dirSyntaxTest`; (3) CCP 1.1 has no Ctrl-C line abort (typed line executes; BIOS CONIN passes 0x03 through) — ISSUE-008; (4) Ctrl-H/0x7F erase work in the BIOS line editor; (5) Ctrl+] lands only when the guest touches the console (stop during a copy arrives at copy end) — ISSUE-010; (6) native CMDs must set their own SS:SP — two init variants without the HELP.A86 pattern crashed the guest inside the BDOS call (stack push on garbage SS into the unmapped video hole: "memory read at physical A00D0"/"A0000"); the packaged `HELP.CMD` observed RUNNING in-guest (banner + file list + `HELP>` prompt) is the witness that the HELP.A86 pattern (CSEG code, `MOV AX,DS`/`MOV SS,AX`/`MOV SP,OFFSET x`, DSEG data, bare `RB` stack closed by one initialized byte) assembles and loads; the DS=CS + CLI/STI/CLD variant was rejected by ASM86 ("NUMBER OF ERRORS: 1") — `helloSource` now copies HELP exactly; (7) console-typed ED input drops lines at random (one lost a single line, one lost everything after `DSEG`) — typing is now verified with TYPE and retried (`typeViaEd`), and the ASM86 verdict is asserted as `NUMBER OF ERRORS:` 0 with tolerant spacing (ASM86 pads the counter). Host: `testCpm86StorageFull` gained an empty LARGE control volume B: (identical copy must succeed there) + `PIP?` guards, removing the vacuous-pass risk; `hostCore` green 4.8 s. abiAudit integration: `docs/cpm86/abiAudit.md` verdicts recorded into ISSUE-002/003/004 (ISSUE-002/003 NOT PASSED with page citations; BDOS args are DL/DX, NOT DE, per SG p.23). Suite result on the current build: **59 passed, 0 failed, 1 skipped (manual escape sequences)** — full native workflow green (ED-verified write, ASM86 `ERRORS: 0`, GENCMD, HELLO runs from C:, PIP copy, runs from B:), warm boot green (`USER after warm boot=0`), lifecycle green (fault/interrupted writes/stop-launch cycles). A per-phase `ensureGuest()` guard now recovers the board to `A>` between phases (one guest fault left the host menu and cascaded into four unrelated timeouts).|

## Development references and evidence policy

	●	[EMU86 at the pinned revision](https://github.com/mfld-fr/emu86/tree/81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac): MIT license and README reviewed; core APIs, instruction coverage and ESP-IDF isolation are not yet audited.
	●	[iSBC86/12 reference repository at the pinned revision](https://github.com/TheBrokenPipe/isbc8612/tree/fc31925b444701abdc0bf4d84f555e5e52eab9e7): CP/M-86 1.1 materials are reference candidates under the user-confirmed non-commercial-use permission. The emulator source has no established license and is not to be vendored or executed without separate permission.
	●	Reference manuals in that repository: [CP/M-86 System Guide, June 1981](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_System_Guide_Jun81.pdf), [Programmer’s Guide, June 1981](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_Programmers_Guide_Jun81.pdf), and [CP/M-86 1.1 release notes, 1982](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_1.1_Release_Note_1982.pdf). ABI page audit is still required.
	●	8086tiny fallback repository. Candidate only; no detailed source or ESP32 integration audit completed here.
	●	Primary references required before ABI freeze: the selected Digital Research CP/M-86 System Guide/Programmer’s Guide editions and Intel’s 8086 Family User’s Manual. Obtain, identify and cite exact editions/pages during CPM86-M1–M3. Full primary-manual inspection was not completed for this revision.
	●	The user-supplied designCPM80.md is integration context, not CP/M-86 test evidence. The actual current projectPrompt.md and repository must be read during implementation reconciliation.

**Keep this document current by appending numbered evidence and explicitly superseding decisions. Do not promote a design proposal, upstream claim or build into an observed guest execution result**.