# designCPM86.md — CP/M-86 engineering memory

Revision: 2026-10-31. UPDATE: the CPU core has been replaced by a project-owned tiny8086-style interpreter (`components/cpm86Core/tiny8086`), see CPM86-VERIFY-043 and `migrate8086CPU.md`; the EMU86 sources were deleted from the tree in CPM86-VERIFY-044, so EMU86 statements below are historical. Status: RETRO86_V1 BIOS, CP/M-86 system resources, drive configuration, A:–F: firmware disk services and the `RETRO86_TIMER_B1` guest timer extension are implemented. The sanitized desktop host test boots the real CCP/BDOS to `A>` and successfully runs `DIR`; multiple-drive guest behavior and timer I/O are covered by CPU tests. The ESP-IDF 6.0.2 firmware build passes. A user-flashed build passed the scripted hardware suite 20/20 (2026-10-24, recorded in CPM86-VERIFY-046 and `docs/cpm86/assesmentCPM86.md`); PSRAM (8 MiB) is verified on hardware. A scripted log for the current post-review build has not yet been captured; timer-delta measurement, final-build BENCH86 numbers and a captured native editor/assembler/compile transcript remain outstanding, as do broad 8086 conformance and complete provenance/rights evidence. Worktree state 2026-10-31: uncommitted tiny8086 fixes (signed IDIV boundaries, 8Ch/8Eh segment-register field validation, PUSHF reserved bit, FE /2../7 rejection ordering) and an independent conformance suite `tests/cpm86ConformanceTests.c` that is not yet registered and does not yet compile; see CPM86-VERIFY-047 and the Remaining-work plan. CP/M-86 1.1 remains the initial release candidate.

## Provenance and purpose

Prepared from the user’s supplied designCPM80.md revision and the preceding discussion of mfld-fr/emu86, TheBrokenPipe/isbc8612 and adriancable/8086tiny. Repository documentation was inspected on 2026-10-06. Moving branch URLs are discovery references, not reproducible source pins. No commit pins, resource checksums, board measurements or successful executions are invented here.

Deliver a practical CP/M-86 software-development machine alongside the existing CP/M 2.2 machine: command line, native editors, an assembler and at least one compile/run workflow. Genuine CP/M-86 guest code must execute on an external 8086 core. Games, IBM PC compatibility, graphics adapters, DOS, protected mode and 8087 emulation are outside the initial acceptance scope.

This is an additional machine implementation alongside CP/M 2.2. It does not supersede the established CP/M 2.2 acceptance gates or silently change the existing CP/M-before-UCSD implementation order.

## Project-wide contract

projectPrompt.md remains the authority for host architecture, machine registry, manifests, storage, console, exchange and coding rules. Companion documents are designCPM80.md, designCPM86.md, designUCSD.md, designApple2.md and designSWTPC.md. The user-supplied CP/M design is the baseline for the integration requirements below; the actual current repository and canonical host document must be reconciled before implementation. This task creates only this design document.

## Menu integration

Preserve the canonical host menu numbers, especially CP/M-86 at 2 and File Transfer at 6. The host already reserves slot 2 for CP/M-86; do not add a second CP/M-86 entry at 7. The following menu is the existing canonical menu, not a proposed slot change:
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
	●	A user-flashed build passed the scripted hardware suite 20/20 and the PSRAM check (CPM86-VERIFY-046). The post-review build now passed the same scripted suite 39 passed / 0 failed / 2 manual, with a measured timer delta of 1093 ms and BENCH86 scale 1 figures of 4130/1554 ms (CPM86-VERIFY-049/050).

Still open: complete instruction coverage, remaining target BIOS/OS compatibility review, guest linker/map coverage, native editor/compiler operation, ESP32-S3 memory/speed measurements and writable-media reliability.

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

All gates below are OPEN at this revision. Existing CP/M 2.2 passes cannot close them.

|ID      |Deliverable and exit evidence                                                                                                                         |
|--------|------------------------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-M1|Canonical host/menu amendments reconciled; CPU/reference candidates pinned; rights and provenance established; reference boot independently reproduced|
|CPM86-M2|External core adapter passes CPU/bus/segment/interrupt tests, sanitizer runs and repeated lifecycle checks; missing-instruction coverage resolved     |
|CPM86-M3|Selected release manual audit, target guest BIOS, memory/linker map, boot entry and first disk geometry frozen and reproducible                       |
|CPM86-M4|Genuine target CCP/BDOS boots on desktop; DIR, HELLO.CMD and warm boot run; LittleFS resource budget and ESP-IDF build recorded                       |
|CPM86-M5|ESP32-S3 target boot plus real SD B:–F: selection, RO/RW errors, persistence and media-failure qualification                                          |
|CPM86-M6|Native 80×24 editor and assemble/compile/run workflow work on hardware                                                                                |
|CPM86-M7|Any expanded profile audited and independently formatted; first/last records, extents, directory-full/disk-full and all drive letters verified        |
|CPM86-M8|Reset/exit/fault recovery, repeated launches, memory/latency measurements, reproducible release and complete logs                                     |

Gate note (2026-10-31): no gate is formally closed. Observed so far: M2 partial (fixture-level plus a not-yet-integrated conformance suite), M4 partial on desktop (VERIFY-019/023/028), M5 partial on hardware (scripted 20/20 run, VERIFY-046), M8 partial (PSRAM verified, benchmark user-reported). A gate closes only when this log gains an entry with all required evidence fields and observed execution.

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
|CPM86-ISSUE-001|OPEN |Pinned core is only fixture-tested; complete instruction/segment conformance and independent reference boot remain unverified              |
|CPM86-ISSUE-002|OPEN |Reference code/assets rights and independent boot reproduction; do not inherit bundled-resource permission from repository availability|
|CPM86-ISSUE-003|OPEN |Exact CP/M-86 release, primary-manual ABI audit and boot resource provenance; do not claim guessed register conventions                |
|CPM86-ISSUE-004|OPEN |BIOS/linker map, segments, DMA and memory descriptor; do not transplant CP/M 2.2 page zero or jump table                               |
|CPM86-ISSUE-005|OPEN |Disk geometry/DPB/extent audit and independent formatter; do not reuse SYSTEM/LARGE labels based on size                               |
|CPM86-ISSUE-006|PARTIAL|LOLIN S3 Pro's 8 MiB octal PSRAM is now enabled for the configured target and the 640 KiB guest allocation is directed there; physical allocation and throughput still require hardware verification|
|CPM86-ISSUE-007|OPEN |Canonical menu/layout/GUI and host ABI reconciliation; preserve option 6 and existing machine contracts                                |
|CPM86-ISSUE-008|OPEN |Native applications, terminal personality and batch scratch-drive behaviour; Z80 COM or DOS binaries are not acceptance tools          |
|CPM86-ISSUE-010|OPEN |Real SD persistence, RO/RW errors, clean exit and reset durability; no host test closes a board gate                                   |
|CPM86-ISSUE-011|PARTIAL|RETRO86_V1 BIOS, system image and desktop boot are implemented in VERIFY-019/023. The separate CompuPro reference image remains unaudited for boot suitability; its declared-geometry/image-length discrepancy, OEM boot chain and rights remain open. The upstream default BIOS remains IBM PC XT.|
|CPM86-ISSUE-012|CLOSED 2026-11-01|Conformance suite registered and compiling; 109 vectors + 32 Jcc cases green under ASan/UBSan (CPM86-VERIFY-048). Unregistered tests still never count as evidence|
|CPM86-ISSUE-013|CLOSED 2026-11-01|IDIV/raiseInterrupt/82h fixes present with vectors, FE and 8Ch/8Eh validation present; unmodelled behaviours declared as profile rules (CPM86-DEC-021) and covered by vectors where observable (CPM86-VERIFY-048). Residual: TF single-step, external interrupt/HLT wake and PUSHA/PUSH SP have no executable guest-level test possible (declared inert/80186-like in DEC-021), so they stay profile rules, not tests. Amendment 2026-11-01: the 82h "fix" of this issue was wrong and was reverted the same day (CPM86-DEC-022, CPM86-VERIFY-049); the other four fixes stand|

Initial chain for CPM86-ISSUE-001: reference behaviour—an external core must execute the selected 8086 guest without host BDOS substitution; hypothesis—EMU86 can supply the required CPU/bus integration with limited adapter changes; experiment—pending pinned source audit and conformance fixtures; result—only moving-branch documentation/header review completed; conclusion—preferred candidate, not qualified; root cause—no failure observed; fix—pending evidence; regression verification—CPM86-VERIFY-002/003 planned; do not repeat—do not convert the earlier recommendation into a claimed boot or conformance PASS.

For every other issue append the complete chain and evidence before closure. Supersede decisions rather than deleting failed approaches.

## Verification log

|ID              |Status                   |Evidence / required next evidence                                                                                                                                                                                                                                                                                                          |
|----------------|-------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-VERIFY-001|DOCUMENTATION REVIEW ONLY|2026-10-06: inspected current EMU86 README and processor/memory-I/O headers, and isbc8612 README. Branches were not pinned; no source checkout/build/run occurred. Firmware, ESP-IDF, board, terminal and execution transcripts: N/A. Reference URLs are below. License inspection and complete CPU/manual audits remain open|
|CPM86-VERIFY-002|NOT RUN                  |Pin and reproduce reference boot with exact config, assets, command/input and console trace                                                                                                                                                                                                                                        |
|CPM86-VERIFY-003|PARTIAL                  |Fixture-level CPU and bus tests now run under host sanitizers; remaining: flags, broad segment cases, near/far control flow, INT/IRET, REP and independent 8086 conformance fixtures                                                                                                                                                  |
|CPM86-VERIFY-004|DESKTOP PARTIAL         |Cold boot to CCP prompt and native `DIR` are verified in the host fixture; native HELLO, warm restart and broader boot state remain open                                                                                                                                                                                                       |
|CPM86-VERIFY-005|NOT RUN                  |Desktop disk-profile, directory/extent, all drive letters, RO/RW, disk-full and boundary/fault matrix                                                                                                                                                                                                                                      |
|CPM86-VERIFY-006|FIRMWARE BUILD PASS / HARDWARE OPEN|ESP-IDF 6.0.2 build and 2 MiB LittleFS image generation pass; flash, board/terminal identity, PSRAM budget and genuine hardware boot transcript remain open                                                                                                                                                                                    |
|CPM86-VERIFY-007|NOT RUN                  |Real SD writable-drive persistence, missing/invalid resources and power/reset recovery on disposable media                                                                                                                                                                                                                                 |
|CPM86-VERIFY-008|NOT RUN                  |Native editor plus assemble/compile/run workflow and hardware performance measurements                                                                                                                                                                                                                                                     |
|CPM86-VERIFY-010|NOT RUN                  |Repeated launch/clean exit, ownership release, fault handling and CP/M 2.2/host-menu regression                                                                                                                                                                                                                                            |
|CPM86-VERIFY-011|HOST RECONCILIATION      |2026-10-06: confirmed the existing registry has CP/M-86 as option 2 and an unimplemented placeholder; SD layout preparation/validation and File Transfer selection already include `cpm86`. ESP-IDF 6.0.2 firmware build succeeded. Host tests passed after reconfiguring `build/host-tests`. No CP/M-86 CPU, guest OS, board execution or hardware verification was performed.|
|CPM86-VERIFY-012|SOURCE METADATA REVIEW  |2026-10-06: pinned the iSBC reference repository to `fc31925b444701abdc0bf4d84f555e5e52eab9e7`; it contains CP/M-86 1.1 system materials and June 1981 System/Programmers Guides. Pinned EMU86 to `81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac`; its MIT license is present, its README identifies unimplemented rare instructions, `emu-proc.c` uses file-static processor state, and `emu-main.c` includes POSIX headers. This metadata review was superseded for CPU-source integration by VERIFY-013; no reference boot or manual ABI audit was performed.|
|CPM86-VERIFY-013|PASS (PROTOTYPE ONLY)  |2026-10-06: vendored the pinned EMU86 CPU modules and MIT license with patch notes; integrated a single-active-core adapter, configurable mapped RAM (1..1 MiB), 20-bit physical wrapping, unmapped-memory faults and byte-port callbacks. Host tests pass under the configured sanitizers, including selected instruction, wrap, unsupported word-I/O, reset/singleton and unmapped-fetch fixtures. ESP-IDF 6.0.2 firmware build passes. No CP/M-86 BIOS/OS image, boot disk, full 8086 conformance suite, flash or hardware execution was performed.|
|CPM86-VERIFY-014|SOURCE/ENVIRONMENT REVIEW|2026-10-06: inspected `tsupplis/cpm86-kernel` at `00927e17f43ea4241cc0809531cf68ce835e9c16`; its source and bootable images target IBM PC/XT, and `pcbios.a86` is explicitly the IBM PC BIOS. Its `LICENSE.md` contains a rights-holder permission statement; the user selected a separate RETRO86_V1 BIOS. No `cpm86.img` exists in the project tree. At that review point, the native CP/M cross-build tools had not yet been brought up; see VERIFY-017. No target BIOS, target image or boot was produced.|
|CPM86-VERIFY-015|IMAGE IDENTIFICATION ONLY|2026-10-06: corrected VERIFY-014 after the user pointed out the root image named `cmp86.img` (not `cpm86.img`). It is 568,576 bytes; string data identifies CP/M-86 1.1NA and OEM copyright notices. This inspection did not establish geometry, image integrity, bootability or suitability as a RETRO86_V1 system disk; filesystem evidence was subsequently established in VERIFY-018. The image was not modified.|
|CPM86-VERIFY-016|PARTIAL DISK-TOOL TEST|2026-10-06: on macOS with cpmtools 2.23, confirmed upstream `base-160.img` is 163,840 bytes. On a temporary copy, `cpmcp -f ibmpc-514ss /tmp/retro86-cpm86-base-test.img base/pip.cmd 0:PIP.CMD` succeeded; extracting `0:PIP.CMD` produced bytes identical to `base/pip.cmd`. `cpmls -F -f ibmpc-514ss /tmp/retro86-cpm86-base-test.img '0:*.*'` still reported “No files found” despite the directory entry and successful extraction, so listing behaviour remains unresolved. A separate `mkfs.cpm` trial on an un-sized output created only a partial image; the upstream Makefile explicitly requires pre-filling before `mkfs.cpm`. Temporary images were removed. Firmware/core/reference commits, board, terminal and ESP-IDF: N/A; no guest boot was attempted.|
|CPM86-VERIFY-017|REFERENCE BUILD ONLY|2026-10-06: built the pinned source's temporary `emu2` and native `doscat`/`hexcom` helpers. A plain `make cpm.sys` without a pseudo-terminal falsely returned success after the guest assembler failed to access a TTY and emitted empty intermediate output. Re-running `make cpm.sys` and `make check` under `script -q /dev/null` completed all assembler passes with zero errors; `cpm.sys` and `cpmorg.sys` were byte-identical. This is the pinned upstream CP/M-86 kernel with its IBM PC BIOS, not a RETRO86_V1 system image. No project resources were changed and no guest boot was attempted. Desktop boot emulator, board, terminal and ESP-IDF: N/A.|
|CPM86-VERIFY-018|FILESYSTEM READ / PROFILE IDENTIFICATION|2026-10-06: the user-provided `assets/readCPM86img.sh` extracts `assets/cmp86.img` after a leading 11,520-byte offset and lists the directory successfully with cpmtools 2.23 and `assets/diskdefs`. The full image is 568,576 bytes; the extracted filesystem region is 557,056 bytes. The `comp86` definition declares 1,024-byte sectors, 70 tracks, 8 sectors/track, 1,024-byte blocks, 64 directory entries, skew 3, offset 11,520, zero boot tracks, OS 2.2. Reproduced the user's directory listing and extracted `CPM.SYS` (11,776 bytes), `LDBIOS.H86` (6,016 bytes) and `LOADER.H86` (14,080 bytes) to temporary storage for inspection. Important unresolved geometry check: the declared geometry spans 70×8×1,024 = 573,440 bytes, 16,384 bytes more than the extracted region; a successful directory listing does not validate the full disk geometry. The image is now identified as a readable CompuPro CP/M-86 filesystem/source candidate, but its boot path, system file semantics, rights, and suitability for RETRO86_V1 remain unverified. No guest boot or project image modification was performed. Firmware/core/reference commits, board, terminal and ESP-IDF: N/A.|
|CPM86-VERIFY-019|DESKTOP BOOT + DIR PASS|2026-10-06: assembled the project RETRO86 BIOS with CP/M-86 ASM-86 1.1N (zero errors), combined it with the pinned `cpm.h86` using native `doscat`, and generated CMD-format `cpm.sys` with GENCMD 1.1N using the kernel Makefile parameters `8080 "CODE[A51,M0000]"`. The initial desktop boot reached `A>`, but `DIR` displayed only `A:` because BIOS READ/WRITE added one to the BDOS track even though DPB OFF=1 already reserves raw track 0. Removing that extra increment made `DIR` list files. The `cpm.sys` is 10,240 bytes and the raw disk is 163,840 bytes. cpmtools 2.23 lists the five expected files. `tests/hostTests.c` now performs the real desktop cold boot, sends `DIR`, requires `ASM86` and `PIP`, and verifies all disk records completed without read errors. The test ran under AppleClang with AddressSanitizer/UndefinedBehaviorSanitizer via `cmake -S tests -B /tmp/retro86-host-tests`, `cmake --build /tmp/retro86-host-tests -j4`, and `ctest --test-dir /tmp/retro86-host-tests --output-on-failure`. No hardware boot, flash, native application run or guest disk write was performed.|
|CPM86-VERIFY-020|ESP-IDF BUILD PASS|2026-10-06: ESP-IDF 6.0.2 build succeeded for ESP32-S3. `retroHost.bin` is 0xE4920 bytes, with 0x21B6E0 bytes free in the smallest app partition. The build generated `build/bootfs.bin` at 2,097,152 bytes and reported adding `cpm86/cpm.sys`, `cpm86/system.dsk` and `cpm86/README.txt`. Extracting `CPM.SYS` from `system.dsk` with cpmtools 2.23 and `cmp` against `littlefs/cpm86/cpm.sys` confirms byte identity. No flash or hardware run was performed.|
|CPM86-VERIFY-021|PSRAM CONFIGURATION FIX / BUILD PASS|2026-10-06: the user's LOLIN S3 Pro boot log showed `cpm86CoreNoMemory` (result 3). Root cause confirmed in the active `sdkconfig`: `CONFIG_SPIRAM` was disabled, so the 640 KiB allocation fell back to internal DRAM and failed. Enabled the board's 8 MiB octal ESP-PSRAM64 at 80 MHz and explicit `MALLOC_CAP_SPIRAM` allocation in `sdkconfig.defaults`/`sdkconfig`; added exact free/largest-block diagnostics if core allocation still fails. ESP-IDF 6.0.2 firmware rebuild and sanitized host tests pass. Physical PSRAM initialization/allocation is not verified until the user flashes and boots the firmware.|
|CPM86-VERIFY-022|HARDWARE BOOT VERIFIED (ROOT CAUSE FIXED)|2026-10-06: on the ESP32-S3 the guest stopped with result 7 after 4,784 instructions. Traces showed the CCP looping on `call SELDRV` (0051:018B/00DA) until the stack overwrote CCP data. Root cause: Xtensa `char` is unsigned, but EMU86 sign-extends 8-bit displacements via `(short)(char)`, so the short jump at 0051:00D8 (`EB B1`) landed 0x100 too far. Fixed with `(signed char)` in op-class.c (4 places) and op-exec.c (1 place). Host tests now build with `-funsigned-char` to reproduce this class of bug. User-observed hardware result: boot reaches `A>`; `dir` lists CPM.SYS, ED, PIP, ASM86, GENCMD; `b:` and `stat` report `?` because those drive/commands are absent. The diagnostic trace (head/recent trace, execute guard over 0051:0800-09FF) remains in the firmware.|
|CPM86-VERIFY-023|MULTI-DRIVE HOST/BUILD PASS|2026-10-06: `drives.cfg` validates A:–F:; startup opens configured images, retains A: as the required LittleFS read-only drive, leaves missing optional images offline, and loads the separate H86 BIOS overlay. The sanitized host test boots the real guest, selects B: and executes `DIR`; path/parser regressions pass. Python tooling tests pass (20 tests), and temporary-card preparation generates the expected A:/E: mappings. ESP-IDF 6.0.2 build passes (`retroHost.bin` 0xE8000) and packages `cpm86/retro86bios.h86`. Physical SD access and writes are not verified.|
|CPM86-VERIFY-024|DAA HOST REGRESSION / BUILD PASS|2026-10-06: the pasted Turbo Pascal trace reports `no handler for op 40h`; decoded operation ID 40h is DAA and its execution-table entry had no handler. Implemented 8086 DAA and added a sanitized host regression for the reported `ADC AL,40h; DAA` sequence, checking the adjusted accumulator and CF/PF/AF/ZF. Host tests and the ESP-IDF 6.0.2 firmware build pass. No hardware execution was performed.|
|CPM86-VERIFY-025|ARITHMETIC FLAGS HOST REGRESSION / BUILD PASS|2026-10-06: the subsequent Turbo Pascal compile stopped after control flow reached invalid opcode bytes `FF E9`. Review found the 8086 core left PF/AF/OF incomplete for arithmetic and INC/DEC, even though its conditional-jump handlers consume those flags. Implemented SZP/AF/CF/OF updates for ADD/ADC/SUB/SBB/CMP, logic, INC/DEC and NEG. Sanitized host tests verify arithmetic flag values and parity-driven branching; the test suite and ESP-IDF 6.0.2 build pass. This does not yet prove that Turbo Pascal compilation succeeds on the ESP32-S3; no hardware execution was performed.|
|CPM86-VERIFY-026|PUSHA SEMANTICS HOST REGRESSION / BUILD PASS|2026-10-06: the next user log still stopped at `FF E9` after about 4.95 million instructions. Further core review found PUSHA saved the already-decremented SP instead of the original SP required by 80186 semantics. Fixed the saved stack word and added a sanitized fixture verifying all eight saved/restored registers and the original-SP slot. Host tests and ESP-IDF 6.0.2 build pass. This regression is corrected in the core; whether it resolves the Turbo Pascal run remains unverified on hardware.|
|CPM86-VERIFY-027|MULTIPLY/DIVIDE/SHIFT HOST REGRESSION / BUILD PASS|2026-10-06: after the PUSHA fix the user confirmed flashing the latest build, but reported the same failure PC and instruction count; the PUSHA change therefore did not resolve the report. Continued instruction audit found incorrect/missing MUL/IMUL CF/OF updates, signed division sign extension and overflow handling, missing SAL handling, and width/flag errors in byte shifts including SAR. Corrected these semantics and added sanitized host fixtures for signed byte/word division, multiply flags and byte shift results/flags. Host test and ESP-IDF build pass. The exact Turbo Pascal hardware run remains unverified.|
|CPM86-VERIFY-028|LARGE-DRIVE EXTENT MAPPING FIX / HOST PASS|2026-10-06: the `FF E9` stop at 0518:518D/518F was a symptom, not a CPU defect (VERIFY-024..027 did not address it). Reproduced on the host with TurboP.dsk: TURBO.CMD (0x919 paragraphs) is loaded by the BDOS program loader (0051:0E86), which ignores read errors; code from file offset 0x4000 onward was one repeated 16-byte paragraph, so execution reached garbage. Root cause: BIOS `DPB1` (RETRO86_DATA_LARGE_V1) declared EXM=1 (two 16 KiB logical extents per directory entry), whereas `tools/diskImageCpm.py` writes one 16 KiB extent (128 records, 8 two-KiB block pointers) per entry with extent numbers 0,1,2,...; the BDOS therefore could not find records 128 and up of any file larger than 16 KiB on a LARGE drive. Fix: `DPB1` EXM 01h -> 00h in `retro86bios.a86` and `retro86bios.h86` (record checksum recomputed). Regression: `testCpm86Boot` builds a LARGE image in the tool layout with a 19,600-byte two-extent text file and requires every line in `TYPE`; with the old overlay it fails at line 2341 (16 KiB boundary), with the fix it passes. With the patched overlay the host run of TURBO compiles LISTER.PAS (210 lines) and starts it. Caveat: files larger than 16 KiB written by the guest to a LARGE drive under the old EXM=1 DPB use a different directory layout and must be re-copied; `tools/cpm86/diskdefs` `retro86-large` (cpmtools infers EXM=1 for 2 KiB blocks) does not match this layout. Hardware run not yet verified.|
|CPM86-VERIFY-037|TIMER DESKTOP PASS / FIRMWARE BUILD PASS; HARDWARE NOT RUN|2026-10-08: baseline source commit `1ee7c24f3560e1b16f65566f554ce7cb00d97ffc`; timer changes uncommitted. Exact desktop command `cmake --build build-host && ctest --test-dir build-host --output-on-failure` passed (1/1, ASan/UBSan). Target build: ESP-IDF 6.0.2 VS Code ESP-IDF `build` command passed for ESP32-S3; `build/retroHost.bin` is 0xECBA0 bytes (69% app partition free). CPU tests exercised capability, 12345678h byte order, F0 snapshot stability, one callback per F1, next snapshot, reset without resetting the injected clock, absent callback, invalid writes/word I/O, the documented 8086 routine with BX preservation and FFFFFFF0h→00000018h subtraction yielding 40 ms. Conflict check found F0h/F1h only in an old generic test fixture, not the production CP/M-86 port map; that fixture moved to F6h/F7h. Physical board and terminal: NOT RUN; no firmware was flashed. Hardware follow-up: host-layer delay about 1 s, then verify a delta in 900–1100 ms.|
|CPM86-VERIFY-038|HOST TIMING INSTRUMENTATION IMPLEMENTED / BUILD PASS; HARDWARE MEASUREMENT OPEN|2026-10-08: `cpm86MachineRun` now accumulates wall time spent inside `cpm86CoreRun`, time spent in `vTaskDelay(1)`, instruction count and batch count. It logs cumulative metrics every 10 seconds and on guest exit. ESP-IDF 6.0.2 ESP32-S3 firmware build passed; `build/retroHost.bin` is 0xECBE0 bytes (69% app partition free). No device was flashed and no runtime measurements were collected. `elapsed` is total wall time; `run` and `delay` totals intentionally exclude scheduler-loop, instrumentation and log overhead, so do not expect them to sum exactly to elapsed. The approximately three-minute baseline is user-reported and not independently measured in this run.|
|CPM86-VERIFY-039|HOST STOP SHORTCUT IMPLEMENTED / BUILD PASS; HARDWARE NOT RUN|2026-10-08: CP/M-86 now treats Ctrl+] (ASCII 1Dh) as a host stop request, checking at batch boundaries and intercepting the byte if guest console input consumes it. The emulator exits through the existing cleanup path and returns to the menu's ENTER prompt. Other machines are unchanged. ESP-IDF 6.0.2 ESP32-S3 firmware build passed; no firmware was flashed and physical shortcut behavior was not tested.|
|CPM86-VERIFY-040|DIAGNOSTIC TRACE TOGGLE IMPLEMENTED / BUILD PASS; COMPARATIVE MEASUREMENT OPEN|2026-10-08: added `CONFIG_CPM86_INSTRUCTION_TRACE` (default enabled). Disabling it skips core instruction-history capture and EMU86 fetched-byte string formatting; batch size, `vTaskDelay(1)`, timing logs and execution semantics are unchanged. Desktop timer tests instantiate the core with tracing disabled and verify no recent trace is recorded. ESP-IDF build and host-test executable build passed. No device was flashed and no comparative workload was run; measure the same guest workload with the option enabled and disabled before concluding how much overhead trace generation contributed.|
|CPM86-VERIFY-041|TRACE-ON/OFF FIRMWARE BUILDS PASS; HOST SUITE PARTIAL; HARDWARE MEASUREMENT OPEN|2026-10-08: regenerated ESP-IDF configuration after adding the Kconfig option. ESP-IDF 6.0.2 ESP32-S3 builds passed with `CONFIG_CPM86_INSTRUCTION_TRACE=y` and with the option disabled; the final generated configuration was restored to enabled. `cmake --build build-host` passed. `ctest --test-dir build-host --output-on-failure` ran the CP/M-86 core tests, including the disabled-trace assertion, before aborting in the separate CP/M-86 boot fixture; therefore the complete host suite did not pass. No firmware was flashed and no hardware workload comparison was performed.|
|CPM86-VERIFY-042|TRACE-OVERHEAD PATH REMOVED WHEN DISABLED / BOTH FIRMWARE CONFIGURATIONS BUILD; BENCH86 HARDWARE COMPARISON OPEN|2026-10-08: source review confirmed `op_decode` formats fetched instruction bytes with `snprintf` and the core evaluates trace candidates for decoded instructions even when periodic logs do not print traces; trace history stores selected control-flow entries, not every instruction. With tracing disabled, opcode text clearing/formatting and trace checks, memory reads, history writes and trace-only expected-PC updates are bypassed. Decoder byte-count/opcode-length checks remain, as do decoding, memory/execute guards, invalid-opcode, I/O-fault and CPU execution paths. Added disabled-trace assertions for invalid-opcode and memory-fault detection. Desktop test executable build passed; CTest ran the CP/M-86 core tests before stopping in the separate CP/M-86 boot fixture, so the full suite did not pass. ESP-IDF 6.0.2 ESP32-S3 builds passed both with tracing on and off; final configuration restored to tracing on and CPU frequency remains 240 MHz. No device was flashed. User-provided 240 MHz BENCH86 baselines: INTEGER 89.604 ms, SIEVE 36.653 ms; trace-disabled results and PASS status remain unmeasured. The post-`@DONE PASS` console-polling observation is noted separately in `docs/cpm86/host.md`; no polling behavior or other performance setting changed.|
|CPM86-VERIFY-043|TINY8086 CORE HARDWARE BENCHMARKED (USER-REPORTED)|2026-10-24: replaced EMU86 with a project-owned tiny8086-style switch-case interpreter behind the `cpm86Core.h` API (full 8086 plus the 80186 opcodes used by the tests). Host tests pass; CP/M-86 boots to `A>` and BENCH86 passes on hardware at 240 MHz (user-reported). BENCH86 INTEGER/SIEVE: EMU86 89604/36653 ms; tiny8086 12801/4755 (trace on, per-batch delay) → 9599/3531 (time-sliced yield) → 8531/3004 (trace off) → 7682/2850 (inlining, IRAM linker fragment) → 6986/2593 (jump tables, parity table) → 6987/2591 (branch-free flags: no gain) → 6110/2308 (200 ms slice) → 5269/1993 (execute guard removed so the fast run loop is used) → 4972/1892 (dense opcode jump table, `-fno-bit-tests`) → 4078/1516 ms (always_inline `readByte`/`writeByte`/`aluOperation`/`setFlag`). Final index vs the 5 MHz model: 154 INTEGER, 296 SIEVE. Tried and reverted: caching the code-segment base for opcode fetch (slower: 4558/1765 ms). Not effective: `IRAM_ATTR` on a single function; guest RAM in internal RAM versus PSRAM.|
|CPM86-VERIFY-044|DEBUG/TRACE CODE REMOVED; HOST TESTS AND ESP-IDF BUILD PASS|2026-10-24: removed `CONFIG_CPM86_INSTRUCTION_TRACE`, the trace API (`cpm86CoreGetRecentTrace/GetHeadTrace`), `cpm86CoreSetExecuteGuard`, `captureInstructionTrace`, the opcode profiler, the CPU self-test and the timing output; deleted `components/cpm86Core/third_party/emu86/`. A guest stop still logs result, CS:IP and the instruction bytes, but no trace window. `CPM86_TINY8086_IRAM` now defaults to `y`. Superseded by this entry: the trace-related statements in CPM86-VERIFY-022 and CPM86-VERIFY-040..042. Host suite passes; an isolated ESP-IDF 6.0.2 ESP32-S3 build passes (image 0x123650 bytes, 62% of the app partition free). The final firmware was not flashed or re-measured by the assistant after the cleanup. Details: `migrate8086CPU.md`.|
|CPM86-VERIFY-045|BENCH86 TRACE-OFF COMPARISON (EMU86 ERA, SUPERSEDED); CORE PROFILING OPEN|2026-10-08: user reports trace-disabled BENCH86 times exactly match the 240 MHz baselines: INTEGER 89.604 ms and SIEVE 36.653 ms. This establishes no measurable change at the displayed precision, not zero internal overhead. Historical BENCH86 figures remain theoretical models. Next performance investigation is targeted profiling within `cpm86CoreRun`; no firmware changes, clocks, batch size or delay changed for this comparison. Post-`@DONE PASS` instruction-count growth remains a separate unconfirmed console-polling investigation. Renumbered from a second entry sharing the 043 id on 2026-10-31; superseded for the current core by CPM86-VERIFY-043 and CPM86-VERIFY-044.|
|CPM86-VERIFY-046|HARDWARE SCRIPTED RUN PASS (RECORDED 2026-10-31, RUN 2026-10-24)|2026-10-24 evidence recorded from `docs/cpm86/assesmentCPM86.md`: on the LOLIN S3 Pro (ESP32-S3 rev v0.2, 16 MB flash, 8 MiB octal PSRAM) a user-flashed build ran `tools/cpm86/cpm86HardwareTest.py` over the USB console: 20 passed, 0 failed, 4 manual. Covered: cold boot to `A>`, `DIR`/`STAT` on A:, `DIR` on B:/C:/D:, clean rejection of E:/F:, `PIP` copy A:->D: and D:->B:, persistence after Ctrl+] stop and relaunch, `ERA`, three stop/launch cycles, write to read-only A: refused recoverably. PSRAM menu line showed `PSRAM: 8192 KiB, free 8184 KiB`. Boot segment sizes matched `build/retroHost.bin` (circumstantial build identity). Firmware commit NOT RECORDED. This does not close CPM86-M5: timer delta, measured benchmark and a scripted run of the post-review build remain open.|
|CPM86-VERIFY-047|CODE REVIEW + CONFORMANCE SUITE STATE (NOT BUILDING)|2026-10-31: review of `tiny8086.c`, `cpm86Core.c`, `cpm86Machine.c`, `cpm86DriveConfig.c`, `cpm86BiosOverlay.c`, `retro86bios.a86` and `tests/cpm86ConformanceTests.c`. Findings: (1) the untracked conformance suite is not registered in `tests/CMakeLists.txt` and fails to compile — vectors with `checkFlags=false` supply two scalars where `checkFlags`/`flagsMask`/`flagsValue` need three, so the first `expectedMemory` entry lands in `flagsValue` under `-Werror`; (2) signed IDIV computes `int32_t quotient = signedDividend / divisor` before the `INT32_MIN / -1` guard (signed-overflow UB; observed in source, not yet observed to fail); (3) `raiseInterrupt` pushes flags without forcing reserved bit 1; (4) opcode 82h decodes as a byte operand instead of aliasing 83h; (5) `littlefs/cpm86/system copy.dsk` is tracked and packaged into `bootfs.bin`; (6) `docs/cpm86/virtualIo.md` claims reserved F8h/F9h ports (absent in code) and unmapped-port I/O errors (host returns idle FFh outside E0h..EFh). Uncommitted worktree: IDIV ±128/±32768 boundary fixes, 8Ch/8Eh reg-field validation, PUSHF reserved bit, FE /2../7 rejection moved before group handlers; `PROG_VERSION` v0.6.0. Host tests not re-run this session.|
|CPM86-VERIFY-048|CONFORMANCE SUITE INTEGRATED + CORE REVIEW FIXES VERIFIED (HOST)|2026-11-01: closes the compile/registration half of CPM86-VERIFY-047 (86-01) and verifies the core fixes (86-02). `tests/CMakeLists.txt` registers the `cpm86ConformanceTests` executable (`cpm86ConformanceTests.c`, `cpm86Core.c`, `tiny8086.c`; `-Wall -Wextra -Werror -funsigned-char`, ASan/UBSan) and `add_test(NAME cpm86Conformance ...)`. The vector-table arity defect is fixed (every row supplies `checkFlags`/`flagsMask`/`flagsValue`); one vector added this session: `idiv byte 8000h/FFh raises INT 0`, pinning the new byte `INT16_MIN / -1` guard. Commands: `cmake -S tests -B /tmp/retro-host-tests`, `cmake --build /tmp/retro-host-tests`, `ctest -R 'cpm86Conformance|hostCore'` — PASS (109 vectors + 32 Jcc cases; `hostCore` includes the real-guest boot/`DIR`/multi-drive fixtures). Full suite: 6 failures are the pre-existing Apple II/UCSD/guestFsDos33 fixtures documented in `projectPrompt.md`, unrelated to CP/M-86. Core fixes present in `tiny8086.c`: IDIV `INT32_MIN/-1` and `INT16_MIN/-1` guards before the C division, signed bounds accept `-0x8000`/`-0x80`, `raiseInterrupt` reserved bit 1, 82h aliases 83h, 8Ch/8Eh reg field 4..7 rejected, FE /2../7 rejected before shared handlers, PUSHF reserved bit 1; RCR-by-1 OF is `CF xor sign` (hostTests fixture repinned). Firmware: `idf.py -B build-agent-check build` PASS (ESP-IDF 6.0.2, `build/` had a stale venv; separate tree per `projectPrompt.md`). This is desktop + build evidence; hardware acceptance of this exact image is step 86-06 (CPM86-VERIFY-049).|
|CPM86-VERIFY-049|HARDWARE SUITE PASS + 82h REGRESSION FOUND AND FIXED (2026-11-01, step 86-06)|First scripted hardware run of the post-review build (flash via `idf.py -B build-agent-check -p /dev/tty.usbmodem202401 flash`). First run on the aa39faf image: 33 passed / 6 failed / 2 manual. `basic: STAT runs on A:` failed — STAT printed BDOS message-table strings (`Bad Directory on`, `Space Allocation Conflict:`, ...) between its own strings and then hung waiting for input; reproduced deterministically on the desktop by a new STAT step in `testCpm86Boot` (kept permanently as a regression guard). A 2x2 experiment (old core `33900b8` + current image = clean; current core + Oct-24 image = broken) exonerated the disk images; a hunk bisect of aa39faf proved the sole root cause is the `82h aliases 83h` decode change from CPM86-VERIFY-048: genuine DR CP/M-86 1.1 binaries execute 82h as the byte form (alias of 80h). Reverted per CPM86-DEC-022, conformance vector flipped to "82h aliases 80h", `ctest -R 'cpm86Conformance|hostCore'` PASS. The other five failures were test-design issues, not firmware: the free-space regex cannot parse `8,176k` (thousands comma) and `fillLimit 60` x ~7.5 KiB can never fill the 8 MiB D: volume — fixed in `tools/include/hwSuite.py`/`hwCpm.py`/`cpm86HardwareTest.py` (comma stripping; fill moved to the 516 KiB C: volume, limit 80). Final run 11:05-11:17 on the rebuilt image (reverted core + fixed TIMER86, `PROG_VERSION` v0.6.1): **39 passed, 0 failed, 2 skipped(manual)**, log `tools/cpm86/cpm86HardwareTest.log`, covering every VERIFY-046 item plus USER areas, disk full, read-only, timer and bench. Full host `ctest` in parallel: only the 6 pre-existing documented Apple II/UCSD/guestFsDos33 failures. This closes step 86-06; the earlier `82h` claim in CPM86-VERIFY-048 is superseded by CPM86-DEC-022.|
|CPM86-VERIFY-050|TIMER DELTA MEASURED + BENCH86 ON POST-REVIEW BUILD (2026-11-01, step 86-07)|New guest program `assets/cpm86/TIMER86/` (NASM `TIMER86.ASM` + `build.py`, pattern of BENCH86; registered in `tests/test_bootDisksBench.py`; packaged on A: via `bootDisks/cpm86/systemDsk/TIMER86.CMD`): checks F0h==B1h, samples t0, blocks on one host keystroke via BDOS function 1 (BIOS CONIN busy-polls port E0h), samples t1, prints raw `T0=`/`T1=` and `DELTA=`. First hardware result `DELTA=29033542 ms` looked like "uptime minus zero"; the raw-sample prints plus a manual byte-level disassembly of the CMD payload (llvm-objdump has no i8086 mode) proved it was a **program bug**: `MOV DX,deltaText` (the print argument) clobbered the high half of the DX:AX difference before `printUnsigned32`, printing `(labelOffset<<16)|lowWord`. The timer path was correct from the start. Fixed by reloading DX:AX from `[t1Low]`/`[t1High]` after the label print (the BENCH86 `reportTime` pattern); the DS/ES reload after the unwrapped BDOS call was kept as documentation of the convention. Hardware measurement with the scripted 1 s host-side key wait: **DELTA=1093 ms** (window 900-1100) — RETRO86_TIMER_B1 (F0h-B1h, F1h sample, F2h-F4h snapshot bytes) verified working on hardware. BENCH86 v2 scale 1 on the same build: **INTEGER 4130 ms / SIEVE 1554 ms** (2026-10-24 baseline 4078/1516; +1.3 %/+2.5 %; index vs 5 MHz model ~152/289). Both are now automated in the `Cpm86` adapter `extras()`; the old manual timer placeholder is gone (2 manual items remain). This closes step 86-07's measured-numbers exit evidence.|
|CPM86-VERIFY-051|BENCH86 REPORT BLOCK GROUPED LIKE BENCH80 (2026-11-01, user request / step 86-12)|The cpm80 suite logs the `A:BENCH80` call, all BENCH80 displays and its verdicts as one block before the next test starts; the cpm86 suite now does the same for BENCH86. `tools/cpm86/cpm86HardwareTest.py`: `benchTest()` moved from the silent low-level `guestRun()` to `cmd("A:BENCH86", 180)` (blocks on `A>` until the program ends or the timeout expires, logs command + collapsed answer), echoes every `BENCH86 v2`/`@BEGIN`/`@END`/`@DONE`/`Elapsed:`/`Index vs`/`Timer unavailable` line with a `bench86: ` prefix and records three inline verdicts via `expect()` (runs-and-completes / sections PASS / indexes recorded); `timerTest()` records its verdict inline too. Verified by a full scripted hardware run 11:34-11:38 on `/dev/tty.usbmodem202401`: **41 passed, 0 failed, 2 manual**, exit 0, log `tools/cpm86/cpm86HardwareTest.log` archived; measured **4140/1552 ms, index 151/289**, timer delta 1094 ms. `tests/test_bootDisksBench.py` OK (3 tests).|

## Remaining-work plan (2026-10-31)

Derived from a code review on 2026-10-31 (`tiny8086.c`, `cpm86Core.c`, `cpm86Machine.c`,
`cpm86DriveConfig.c`, `cpm86BiosOverlay.c`, `retro86bios.a86`, `tests/cpm86ConformanceTests.c`,
`docs/cpm86/`); the code is the truth where this document differed. Each step below is one
focused work session with its own exit evidence, recorded as the next CPM86-VERIFY id. Use the
verified build/test commands from `projectPrompt.md`; hardware flashing is authorized per the
project memory (`/dev/tty.usbmodem202401`).

|Step|Goal|Main files|Exit evidence|
|----|----|----------|-------------|
|86-01|Integrate the conformance suite: add a `cpm86ConformanceTests` executable (sources `cpm86Core.c`, `tiny8086/tiny8086.c`, include dirs `components/cpm86Core/include`, `components/cpm86Core/tiny8086`, `components/hostCore/include`) and `add_test(NAME cpm86Conformance ...)`; fix the vector table so every row supplies `checkFlags`, `flagsMask`, `flagsValue` and `expectedMemory` (rows with `checkFlags=false` currently miss one scalar); keep the `-funsigned-char`/sanitizer build|`tests/CMakeLists.txt`, `tests/cpm86ConformanceTests.c`|`ctest -R cpm86Conformance` passes; full host suite unchanged|
|86-02|Fix the review findings in the core: move the signed IDIV `INT32_MIN / -1` and `divisor == -1` checks **before** the C division; force reserved bit 1 on the flags pushed by `raiseInterrupt`; make 82h alias 83h (16-bit operand, sign-extended imm8) or reject it; add conformance vectors for each; keep the uncommitted 8Ch/8Eh/FE/PUSHF/IDIV-boundary fixes|`components/cpm86Core/tiny8086/tiny8086.c`, `tests/cpm86ConformanceTests.c`|New vectors green under ASan/UBSan; `idf.py build` passes; VERIFY entry; suggested commit message|
|86-03|Declare unimplemented CPU behaviour as profile rules, not bugs: trap-flag single-step, external interrupts and HLT wake-up, LOCK ignored, ESC operands decoded-and-ignored, PUSH SP pushes the original SP (80186-like), unmasked CL shift counts; reconcile `docs/cpm86/virtualIo.md` with the code (remove or implement the claimed F8h/F9h reservation; document that unmapped ports outside E0h..EFh return idle FFh once-logged while E0h..EFh misuse faults)|`designCPM86.md`, `docs/cpm86/virtualIo.md`|Doc entries plus a decision-log line; no code change|
|86-04|Make `tools/cpm86/diskdefs` a trustworthy validator: align `retro86-large` with firmware DPB1 (EXM=0, per VERIFY-028) and audit `retro86-160` and the BIG geometry against DPB0/DPB2; prove with a `cpmcp` copy-in/copy-out round trip of one >16 KiB (multi-extent) and one boundary-sized file per profile|`tools/cpm86/diskdefs`|Byte-identical round trips; evidence file under `docs/cpm86/`|
|86-05|Storage housekeeping and drive-matrix host tests: confirm with the user and remove tracked `littlefs/cpm86/system copy.dsk` from the packaged LittleFS tree; add host tests for first/last valid track and record per profile, rejected track 0/overflow track, record ≥32, read-only write refusal propagation, and guest-visible disk-full/directory-full on disposable images|`littlefs/cpm86/`, `tests/hostTests.c`|Host suite green; `bootfs.bin` size recorded|
|86-06|Hardware acceptance of the current build (closes the M5 evidence gaps): build, flash, run `tools/cpm86/cpm86HardwareTest.py` against the flashed board and archive the script log; verify PSRAM line, boot to `A>`, `DIR`, B:/C:/D:, read-only refusal and Ctrl+] relaunch on the exact post-review image|`tools/cpm86/cpm86HardwareTest.py`, firmware|VERIFY entry with full transcript; no regression vs the 20/20 run of VERIFY-046|
|86-07|Measure timer and speed on hardware (M8 evidence): host-side ~1 s wait between two RETRO86_TIMER_B1 reads must yield a 900–1100 ms delta; run BENCH86 on the same final build and compare with the 2026-10-24 tiny8086 figures (4078/1516 ms at 240 MHz)|`tools/cpm86/`, firmware|VERIFY entry with measured values, not user reports|
|86-08|Native development workflow transcript (closes M6): scripted on-hardware run of the editor, `ASM86` assemble to a `.CMD`, execution of the produced program, `PIP` copy across drives, terminal checks (CR/LF, backspace, Ctrl-C)|`tools/cpm86/`|VERIFY-008 evidence path with captured transcript|
|86-09|Warm boot and lifecycle qualification: exercise the WBOOT path (it must not return to the host menu) and document which drive/user state survives; repeated init/run/Ctrl+] cycles release media and core ownership; a guest fault (invalid opcode, unmapped fetch) recovers to the menu without host reset|`tests/hostTests.c`, `main/cpm86Machine.c`, this document|Host fixture plus hardware script extension; VERIFY entry|
|86-10|Durability under interrupted writes: on disposable RW images, interrupt a multi-record guest write (Ctrl+] or reset mid-`PIP`), then reopen the image host-side and confirm the host stays healthy and the image remains parseable; crash-atomicity stays explicitly out of scope|`tools/cpm86/`, tests|VERIFY entry recording the observed states|
|86-11|Primary-manual ABI audit and rights evidence (closes ISSUE-002/003): record the exact manual editions and pages for the BDOS E0h call ABI, the BIOS entry-vector table (matching `retro86bios.a86`), DPB fields, memory-region table and segment-aware DMA of the shipped CP/M-86 1.1; list rights evidence per CPM86-DEC-014/017 and the tools actually shipped (`GENCMD`, `ASM86`, `PIP` provenance)|`docs/cpm86/abiAudit.md`, issue log|abiAudit.md with citations; issue log updated|

Suggested order: 86-01 → 86-02 unblock any conformance claim (CPM86-M2); 86-03..05 are
host-only and can interleave; 86-06..10 require a final flashed build and should run after
86-02; 86-11 is independent. Do not promote a step result into a PASS outside the verification
log, and do not close an issue whose plan step is not recorded there.

## Session work log — 2026-11-01

Resumed an interrupted session after CPM86-VERIFY-047. Authorized this session: assistant
builds, flashes ( `/dev/tty.usbmodem202401`) and tests the hardware directly. Each step is
logged with its start and finish time; durable results are promoted into the verification
log (next free id CPM86-VERIFY-051) and the issue log, not left only here.

### Phase 86-01 — conformance suite integration

- Start 2026-11-01 08:34.
- 08:34 Confirmed the interrupted session had registered `cpm86ConformanceTests`
  (executable + `add_test(NAME cpm86Conformance ...)`) in `tests/CMakeLists.txt`; the suite
  sources are `cpm86ConformanceTests.c`, `cpm86Core.c`, `tiny8086/tiny8086.c` with
  `-Wall -Wextra -Werror -funsigned-char` and ASan/UBSan.
- 08:36 `cmake --build /tmp/retro-host-tests` builds the suite cleanly — the vector-table
  arity fix (every row supplies `checkFlags`, `flagsMask`, `flagsValue`) is present, so the
  CPM86-VERIFY-047 compile failure is resolved in the worktree.
- 08:37 `ctest -R cpm86Conformance` PASS (108 vectors + 32 Jcc cases reported by the binary).
- 08:40 Full host suite: `cpm86Conformance` and `hostCore` (includes `testCpm86Boot`,
  `testCpm86Core`, timer/core fixtures) PASS. The 6 failing tests (`apple2DiskStream`,
  `ucsdPascalBoot`, two `apple2ProdosAutostart*`, `guestFsDos33*`) are the pre-existing
  Apple II/UCSD fixture failures documented in `projectPrompt.md`, unrelated to CP/M-86.
- Finish 2026-11-01 08:40. Exit evidence met: suite registered, compiles, passes; host suite
  unchanged apart from the intended RCR-OF fixture repin in `tests/hostTests.c`.

### Phase 86-02 — core review fixes and conformance vectors

- Start 2026-11-01 08:40.
- 08:40 Verified all review fixes from CPM86-VERIFY-047 are present uncommitted in
  `components/cpm86Core/tiny8086/tiny8086.c`: IDIV guards for `INT32_MIN / -1` and
  `INT16_MIN / -1` moved **before** the C division; word/byte overflow bounds corrected to
  accept `-0x8000`/`-0x80`; `raiseInterrupt` pushes flags with reserved bit 1; 82h aliases
  83h (16-bit r/m, sign-extended imm8); 8Ch/8Eh reject reg field 4..7; FE /2../7 rejected
  before the shared group handlers; PUSHF reports reserved bit 1. RCR-by-1 OF now
  `CF xor result sign` (hostTests fixture repinned accordingly).
- 08:40 Gap found: the new byte-IDIV `INT16_MIN / -1` guard has no vector (only the word
  case `DX=8000h/AX=0/-1` is covered). Added vector `idiv byte 8000h/FFh raises INT 0`
  (`AX=8000h`, `BL=FFh`, `F6 /7`) expecting the INT 0 marker.
- 08:43 Rebuild + `ctest -R 'cpm86Conformance|hostCore'`: both PASS (now 109 vectors + 32
  Jcc cases).
- 08:43 ESP-IDF 6.0.2 build: the existing `build/` tree was configured with a stale venv, so
  per `projectPrompt.md` the check ran in the separate tree `build-agent-check` (no
  `fullclean`): `idf.py -B build-agent-check build` PASS.
- Finish 2026-11-01 08:47. Exit evidence met: all five review fixes verified in the core,
  each with at least one conformance vector, green under ASan/UBSan; firmware builds.
  Recorded as CPM86-VERIFY-048.

### Phase 86-06 — hardware acceptance of the post-review build

- Planned: `idf.py build`; flash `/dev/tty.usbmodem202401`; scripted
  `tools/cpm86/cpm86HardwareTest.py` run; archive log; compare with VERIFY-046 (20/20).
- Start: BLOCKED 2026-11-01 08:47. `idf.py flash` cannot open `/dev/tty.usbmodem202401`:
  the Tabby terminal (PID 1144) holds the serial device, and two hung `minicom` processes
  (PIDs 29379, 51068; ~100 % CPU each, running since Oct 20/23) are still present. Killing
  user processes is outside this session's mandate — the user must close the Tabby serial
  tab and/or authorize the kills. Flashing resumes as soon as the port is free.
- Do not repeat: `build/` + active venv mismatch — use `idf.py -B build-agent-check` (see
  Phase 86-02 and `projectPrompt.md`); never `idf.py fullclean` here.
- 09:44 Second flash attempt after the TIMER86 build: same failure —
  `Could not exclusively lock port /dev/tty.usbmodem202401 [Errno 35]`, Tabby (PID 1144)
  still holds the device. `lsof` shows no other holder (the hung `minicom` processes no
  longer lock this port but still burn CPU). Board still runs the Oct 31 23:55 firmware;
  `build-agent-check/bootfs.bin` (09:40) already carries TIMER86.
- Start 2026-11-01 09:49 (unblocked: the user released the port and committed the session
  work as aa39faf; `PROG_VERSION` locally bumped to v0.6.1 by the user). Rebuild PASS at
  09:49, then flash and the scripted `cpm86HardwareTest.py` run.
- 09:50 Flash PASS (hash verified). First scripted run: **33 passed, 6 failed, 2 skipped**
  (log `tools/cpm86/cpm86HardwareTest.log`). Triage:
  - `basic: STAT runs on A:` FAIL — STAT prints BDOS message-table strings between its own
    strings (`A: RW, Free Space:  Bad Directory on  Space Allocation Conflict: ... 10k`),
    deterministic. **Reproduced on the desktop host fixture** (added STAT step to
    `testCpm86Boot`): prints `A: RW, Free Space: ` and then never returns to `A>`. So this
    is a core/BDOS-path bug, not ESP32-specific. VERIFY-046 (2026-10-24) had `STAT` clean,
    and no fault diagnostics logged during the run. Regression window narrows to the core
    commits after 33900b8 (Oct 24): cb573e0 (fault diagnostics / tolerate absent devices /
    PC BIOS stubs), 005dc11 (open bus above RAM) and aa39faf (review fixes) — bisecting on
    the host.
  - `timer: guest delta 900-1100 ms` FAIL — measured **29033542 ms**. First hypothesis
    (BDOS clobbering DS across the unwrapped function-1 call) was **wrong**; the reload of
    DS/ES from CS was kept (harmless, and the documented convention) but did not change
    the value. Resolved 10:30–11:05 by adding raw `T0=`/`T1=` sample prints to TIMER86
    and disassembling the built CMD: the real bug was **`MOV DX,deltaText` clobbering the
    high half of the DX:AX difference before `call printUnsigned32`** — every DELTA was
    `(deltaTextOffset << 16) | lowWord`, which only looked like "uptime minus zero". The
    samples were correct all along (T0=3145, T1=4234 -> real 1089 ms for the 1 s wait).
    Fixed by loading DX:AX from `[t1Low]`/`[t1High]` after the label print (the BENCH86
    `reportTime` pattern). **Timer ports RETRO86_TIMER_B1 verified working on hardware.**
    `llvm-objdump` has no i8086 support; manual byte-level disassembly of the CMD payload
    (offset 128) is the working method.
  - `bench: BENCH86 scale 1` PASS — elapsed **4201/1583 ms** (vs 4078/1516 on 2026-10-24,
    ~3 % slower; recorded for phase 86-07).
  - `disk full: filling D: ...` (3 FAILs) + `read-only: free space on A: CHANGED` — test
    design vs environment, not firmware: `work86-2.dsk` (D:) is 8 MB (unchanged since Oct 7)
    so `fillLimit 60` x ~7.5 KB files can never fill it, and `freePattern` cannot parse the
    thousands comma in `Free Space: 8,176k` (both `before`/`after` parse to None). The
    VERIFY-046 run predated the disk-full phase. Fix in the test script: parse comma
    thousands and size the fill to the volume (or use C: 516 KB for fill).
- 10:05 STAT regression hunt, resolved by 10:35: added a STAT step to `testCpm86Boot`
  (kept as a permanent regression guard) — **reproduced on the desktop** (`A: RW, Free
  Space: ` then hang). 2x2 experiment: old core + current image = clean; current core +
  Oct-24 image = broken -> core-side, image exonerated. Hunk bisect of aa39faf against
  the 33900b8 core in `/tmp/cpm86old` (host build, no flashing): reverting the decode
  cluster (82h alias, 8Ch/8Eh reg-field, FE ordering) made STAT clean again; re-applying
  **only the 82h-as-83h alias brought the hang back** -> sole root cause. Genuine DR
  CP/M-86 binaries execute 82h as the byte form (alias of 80h). Reverted the alias in
  `tiny8086.c` (with a do-not-repeat comment), flipped the conformance vector to
  "82h aliases 80h" (expect AL-only add), registered as CPM86-DEC-022. `ctest -R
  'cpm86Conformance|hostCore'` PASS. Rebuild + flash 10:48: hardware STAT clean
  (`A: RW, Free Space:        10k`), `basic: STAT runs on A:` PASS.
- Do not repeat: do not re-apply "82h aliases 83h" — it hangs the genuine STAT.CMD
  (host fixture + hardware evidence, CPM86-VERIFY-049). The VERIFY-047 review claim that
  82h must alias 83h was based on the later-CPU convention and is empirically wrong for
  the shipped DR binaries; see CPM86-DEC-022.
- Final scripted run 11:05-11:17 on the rebuilt image: **39 passed, 0 failed, 2 manual**
  (log `tools/cpm86/cpm86HardwareTest.log`). Finish 2026-11-01 11:17. Exit evidence met:
  full suite green on the post-review build, log archived, VERIFY-046 (20/20) items all
  present and passing, PSRAM menu line verified at reset. Recorded as CPM86-VERIFY-049.

### Phase 86-07 — timer delta and BENCH86 on hardware

- Planned: RETRO86_TIMER_B1 host-side ~1 s delta (900–1100 ms expected); BENCH86 on the same
  build compared with the 2026-10-24 figures (4078/1516 ms at 240 MHz).
- Start 2026-11-01 09:21 (guest-program preparation; the hardware measurement itself waits
  on the port blocker above).
- 09:21 BIOS audit: `components/cpm86Core/bios/retro86bios.a86` CONIN busy-polls port E0h
  and reads E1h, so BDOS function 1 (console input, `CL=1`, `int 0E0h`) genuinely waits for
  the host keystroke — usable as the fixed-interval wait without a BIOS change.
- 09:21 Added `assets/cpm86/TIMER86/` (TIMER86.ASM + build.py + README.md, NASM pattern of
  BENCH86): checks F0h==B1h, prints `@BEGIN TIMER`, reads t0, waits for one key, reads t1,
  prints `DELTA=<n> MS` and `@DONE PASS`. Built to `TIMER86.CMD` and copied to
  `bootDisks/cpm86/systemDsk/` (LARGE profile has directory room: 54 of 128 entries).
- 09:27 Registered `TIMER86.CMD` in `tests/test_bootDisksBench.py` BENCH_FILES and extended
  `tools/cpm86/cpm86HardwareTest.py` with automated timer (900–1100 ms) and BENCH86
  (`@DONE PASS` + Elapsed lines) tests in `extras()`; removed the old manual timer
  placeholder from `manual()`. `python3 tests/test_bootDisksBench.py` OK (3 tests) and
  `idf.py -B build-agent-check build` PASS with the TIMER86-containing boot disk.
- 10:30-11:05 Timer hunt and fix (see the corrected triage note above): TIMER86 gained raw
  `T0=`/`T1=` prints; the DELTA artifact proved to be a `MOV DX,label` clobbering the DX:AX
  high half before `printUnsigned32`; fixed, rebuilt (1248-byte CMD), reflashed.
- 11:05-11:17 Measured on hardware inside the final suite run: **DELTA=1093 ms** (window
  900-1100) and BENCH86 scale 1 **4130/1554 ms** (2026-10-24: 4078/1516). Both automated in
  `Cpm86.extras()`. Finish 2026-11-01 11:17. Exit evidence met: timer delta measured,
  benchmark numbers recorded (CPM86-VERIFY-050).

### Phase 86-12 (user request, outside the plan) — BENCH86 report block grouped like bench80

- Start 2026-11-01 11:31.
- 11:31 Request: in `tools/cpm80/cpm80HardwareTest.log` the `A:BENCH80` call, all of
  BENCH80's displays and its verdicts form one block that finishes before the next test
  starts; the user wants the same report shape in `tools/cpm86/cpm86HardwareTest.py` for
  BENCH86 — start the program, wait for its end (or a timeout) and show the results there.
- 11:33 Cause found by comparing the two scripts: cpm80's `bench80()` runs through
  `console.command()` (logs the command, waits on `A>` with a long timeout, echoes the
  answer, then re-prints each measurement line with a `bench80: ` prefix and records each
  verdict immediately via `expect()`). cpm86's `benchTest()` used the low-level
  `guestRun()` (no answer echo at all) and returned its verdict as a tuple that
  `hwSuite.run()` only recorded after `extras()` had also executed the stop/launch
  cycles — so the bench verdict appeared detached after later `Command:` lines.
- 11:33 Fix: `benchTest()` rewritten in the bench80 style (`cmd("A:BENCH86", 180)`,
  wanted-line echo with `bench86: ` prefix, three inline verdicts); `timerTest()` records
  inline through `expect()` too so its verdict stops trailing the stop/launch commands.
- 11:34-11:38 Re-ran the full scripted hardware suite on `/dev/tty.usbmodem202401`:
  **41 passed, 0 failed, 2 manual** (exit 0, log archived). The new block reads exactly
  like the bench80 reference: `bench86: starting BENCH86, waiting up to 180 s for it to
  finish`, `Command: A:BENCH86` with its collapsed answer, the ten `bench86:` display
  lines, then the three verdicts — **elapsed 4140/1552 ms, index vs 5 MHz MODEL
  151,289** — all before the next `Command:` (stop/launch cycles). The timer verdict
  (1094 ms) is now printed directly under `Command: TIMER86` instead of trailing the
  section. `tests/test_bootDisksBench.py` OK (3 tests, closes the interrupted 86-07
  housekeeping). One `bench:` check became three `bench86:` checks (runs/completes,
  sections PASS, indexes recorded), so the suite total went 39 -> 41.
- Finish 2026-11-01 11:38. Exit evidence met: hardware-verified report shape, recorded
  as CPM86-VERIFY-051.

## Development references and evidence policy

	●	[EMU86 at the pinned revision](https://github.com/mfld-fr/emu86/tree/81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac): MIT license and README reviewed; core APIs, instruction coverage and ESP-IDF isolation are not yet audited.
	●	[iSBC86/12 reference repository at the pinned revision](https://github.com/TheBrokenPipe/isbc8612/tree/fc31925b444701abdc0bf4d84f555e5e52eab9e7): CP/M-86 1.1 materials are reference candidates under the user-confirmed non-commercial-use permission. The emulator source has no established license and is not to be vendored or executed without separate permission.
	●	Reference manuals in that repository: [CP/M-86 System Guide, June 1981](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_System_Guide_Jun81.pdf), [Programmer’s Guide, June 1981](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_Programmers_Guide_Jun81.pdf), and [CP/M-86 1.1 release notes, 1982](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_1.1_Release_Note_1982.pdf). ABI page audit is still required.
	●	8086tiny fallback repository. Candidate only; no detailed source or ESP32 integration audit completed here.
	●	Primary references required before ABI freeze: the selected Digital Research CP/M-86 System Guide/Programmer’s Guide editions and Intel’s 8086 Family User’s Manual. Obtain, identify and cite exact editions/pages during CPM86-M1–M3. Full primary-manual inspection was not completed for this revision.
	●	The user-supplied designCPM80.md is integration context, not CP/M-86 test evidence. The actual current projectPrompt.md and repository must be read during implementation reconciliation.

**Keep this document current by appending numbered evidence and explicitly superseding decisions. Do not promote a design proposal, upstream claim or build into an observed guest execution result**.