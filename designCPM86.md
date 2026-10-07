# designCPM86.md — CP/M-86 engineering memory

Revision: 2026-10-06. Status: RETRO86_V1 BIOS, CP/M-86 system resources, drive configuration and A:–F: firmware disk services are implemented. The sanitized desktop host test boots the real CCP/BDOS to `A>` and successfully runs `DIR`; multiple-drive guest behavior is covered by the BIOS test. The ESP-IDF 6.0.2 firmware build passes. No new firmware has been flashed and CP/M-86 hardware access to configured SD drives remains unverified. This does not establish broad 8086 conformance, complete CP/M-86 compatibility, writable-media reliability, or a full native development workflow. The pinned EMU86 CPU sources remain integrated through a bounded-RAM, single-active-instance adapter. CP/M-86 1.1 remains the initial release candidate.

## Provenance and purpose

Prepared from the user’s supplied designCPM80.md revision and the preceding discussion of mfld-fr/emu86, TheBrokenPipe/isbc8612 and adriancable/8086tiny. Repository documentation was inspected on 2026-10-06. Moving branch URLs are discovery references, not reproducible source pins. No commit hashes, resource checksums, board measurements or successful executions are invented here.

Deliver a practical CP/M-86 software-development machine alongside the existing CP/M 2.2 machine: command line, native editors, an assembler and at least one compile/run workflow. Genuine CP/M-86 guest code must execute on an external 8086 core. Games, IBM PC compatibility, graphics adapters, DOS, protected mode and 8087 emulation are outside the initial acceptance scope.

This is an additional machine implementation alongside CP/M 2.2. It does not supersede the established CP/M 2.2 acceptance gates or silently change the existing CP/M-before-UCSD implementation order.

## Project-wide contract

projectPrompt.md remains the authority for host architecture, machine registry, manifests, storage, console, exchange and coding rules. Companion documents are designCPM80.md, designCPM86.md, designUCSD.md, designAppleII.md, designMPM.md and designSWTPC.md. The user-supplied CP/M design is the baseline for the integration requirements below; the actual current repository and canonical host document must be reconciled before implementation. This task creates only this design document.

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
/retro/exchange/cpm86/
```
Keep them independent of the existing cpm directories. A resource description records release, machine profile, format, exact size, SHA-256, origin, rights evidence and access policy using the common manifest contract. Minimal boot resources belong in LittleFS only if the measured partition budget permits them. Larger or writable images belong on SD. If minimal resources do not fit, explicitly require SD; do not promise an unavailable recovery boot.

File Transfer is the shared host service. Only option 6 starts WiFi, exclusively through michmich/esp-idf-wifi-provisioner, preserving the supplied ^0.4.0 requirement and locking the tested dependency version. The current GUI already includes CP/M-86: loose files use /retro/exchange/cpm86/; whole images use /retro/images/cpm86/. Display full VFS paths. Reuse bounded streaming, temporary uploads, progress, traversal rejection and explicit overwrite policy. The supplied transfer limit remains 4 GiB minus 2 bytes, subject to the canonical HTTP implementation. That limit does not imply that CP/M-86 supports a disk or file of that size.

Browser File Transfer and guest execution are mutually exclusive initially. Guest HOST.CMD talks only to the local common hostExchange service and never starts WiFi. The host must not edit a mounted guest filesystem behind the guest’s back. Image upload, conversion and replacement occur while the machine is stopped.

Physical reset returns to the host menu. Guest warm boot is separate. A clean host exit stops instruction execution at a safe boundary, aborts exchange, flushes/closes media, restores terminal state and releases ownership. Use the canonical host escape mechanism; ordinary Enter and normal guest control keys are not exit requests. If that mechanism is unspecified, resolve it in the host contract before hardware acceptance. Physical reset or power loss can interrupt guest metadata writes; a completed host write is not a promise of crash-atomic guest filesystem updates.

## Implementation discipline

Use native ESP-IDF, CMake and VSCode ESP-IDF workflow; no Arduino dependency. Target ESP32-S3 / LOLIN S3 Pro. Verify board revision, USB routing, PSRAM mode, flash size and partition configuration against the actual board. Do not infer this board’s available memory from another S3 variant. ESP-IDF 6.0.2 is the supplied project’s baseline; record the actual version used for each test.

Project-owned code, identifiers, messages and documentation are English. Use Allman braces, two-space indentation, lowerCamelCase and comments on their own preceding line in the form //— comment. Preserve upstream source style, copyright and license; isolate local adapters and document every upstream patch.

Use an existing CPU core. Do not write another 8086 emulator, intercept BDOS to implement host-side CP/M, or patch arbitrary binary offsets until a prompt appears. Separate CPU, memory/bus, guest BIOS, boot loader, media backend and console. A small guest BIOS using documented virtual I/O is allowed; genuine CCP/BDOS performs command processing, program loading, memory management and filesystem operations.

Pin source commits, tools and resource hashes before integration. Bound memory, queues and handles; check allocations and image-offset arithmetic. Never load a whole disk into RAM. CPU stepping and scheduler yields must preserve guest-visible state. Debugger, POSIX, SDL and host process dependencies must not leak into the ESP-IDF build. Desktop sanitizer passes and firmware build success are distinct from hardware execution.

## Engineering-memory rules

Read this document’s decisions and open issues before changing the machine. Preserve failed experiments; supersede conclusions with new numbered records. Repeat an experiment only with a changed variable or new evidence. Distinguish proposed design, source review, reference execution, desktop target execution, firmware build and hardware verification.

Every verification record captures date, firmware commit, core commit, reference commit, ESP-IDF/tool versions, board, terminal, image hashes, exact commands/input, expected result, actual result and evidence path. Use N/A only when a field does not apply; use NOT RECORDED when relevant information is missing. Only observed execution earns PASS.

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
- I --> H["Shared console, image and exchange services"]

The target is a project-defined serial-console CP/M-86 machine, provisionally named RETRO86_V1. This name identifies a planned profile, not a finished disk geometry. It does not claim Intel iSBC or IBM PC hardware compatibility. Boot resources for another OEM machine require their original hardware or a reproducibly rebuilt BIOS; CPU compatibility alone is insufficient.

## CPU selection and reference machine

|Candidate                                            |Intended role                      |Gate before use                                                                                         |
|-----------------------------------------------------|-----------------------------------|--------------------------------------------------------------------------------------------------------|
|[EMU86](https://github.com/mfld-fr/emu86)            |Preferred external CPU candidate   |Pin source, isolate CPU/bus dependencies, audit instruction coverage, verify tests and restart lifecycle|
|[isbc8612](https://github.com/TheBrokenPipe/isbc8612)|CP/M-86 reference-machine candidate|Pin source, verify reproduction and rights for code and every asset separately                          |
|[8086tiny](https://github.com/adriancable/8086tiny)  |Fallback candidate                 |Assess extraction effort, semantics, source license and BIOS/table dependencies                         |

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
|`components/cpm86Core/third_party/emu86/`|Pinned CPU source, license and documented local changes|
|`components/cpm86Core/`   |CPU adapter, bounded RAM and host byte-port interface                         |
|`components/cpm86Core/os/`|Guest BIOS/loader source, reproducible binaries, selected OS provenance       |
|`main/cpm86Machine.c`     |Registry integration, resource probe, host-service adapters and task lifecycle|
|`tools/buildCpm86Disk.py` |Deterministic target image composition once geometry is frozen                |
|`tools/cpm86DiskImage.py` |Profile-aware create/list/add/validate operations                             |
|`guest/cpm86/host/`       |8086 `HOST.CMD` source and build recipe                                       |
|`docs/cpm86/`             |Reference traces, ABI/profile audits and verification evidence                |

Do not duplicate shared image I/O, path resolution or exchange services. Extract reusable infrastructure where needed without changing CP/M 2.2 (CPM-80) behaviour. Reuse algorithms only after CP/M-86 filesystem semantics are checked; the existing CPM-80 image tool is not assumed compatible.

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

## Genuine operating system and program format

Select one exact CP/M-86 release and OEM resource set. Audit its Digital Research system/programming manuals and source before defining BIOS details. Record the manual edition and page references in docs/cpm86/abiAudit.md. OS/source/utility rights are individual gates; the CP/M rights clarification cited in the supplied CP/M design must be checked for the exact materials used here.

Native applications use CP/M-86 .CMD executables. Existing CP/M-80 .COM utilities, HOST.COM, HELLO, editors and compilers cannot be reused as executable machine code. DOS .COM/.EXE binaries are also outside this ABI. Text source may be portable, but language tools and runtime libraries must be native and qualified.

CP/M-86 BDOS entry uses software interrupt E0h (decimal 224). The CPU must execute the interrupt vector and real guest handler. Do not turn this into a host syscall dispatcher. Audit the selected release’s register arguments, returned values, pointer/segment conventions and memory-management functions from its primary manual. The documentation gate remains open; this document does not specify a complete BDOS register ABI.

## Current implementation status

CP/M-86 CPU/interface and machine integration:

	●	EMU86 CPU source pinned at `81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac`; see `components/cpm86Core/third_party/emu86/README.md` for provenance and local patch scope.
	●	Implemented reset, entry-point, bounded memory load/read/write, single-step and instruction-budget APIs. The configured RAM is 1..1 MiB; physical addresses wrap at 20 bits and unmapped accesses fault.
	●	Implemented byte I/O adapter: F8h input/output and F9h output connect to `hostExchange`; word I/O is rejected. Optional byte-port callbacks now allow machine-specific virtual devices without changing the exchange ports. This is not yet the guest BIOS device ABI.
	●	Host fixtures cover selected arithmetic/memory instructions, 20-bit physical wrap, HLT, QUERY byte-port exchange, custom port callbacks, unsupported word I/O, RAM bounds, unmapped fetch, reset and singleton enforcement. Host tests use sanitizers; ESP-IDF build also passes.
	●	Upstream CPU state is global/static, so the adapter enforces one active core; complete 8086 instruction/segment conformance is not established.
	●	The CP/M-86 machine is integrated at menu slot 2 with resource probing, a project BIOS, system files and a boot disk. Desktop host tests boot the genuine guest to `A>` and run `DIR`; the ESP-IDF firmware build succeeds.
	●	The firmware has not been flashed. Hardware boot and memory/performance validation remain outstanding.

Still open: complete instruction coverage, remaining target BIOS/OS compatibility review, guest linker/map coverage, native editor/compiler operation, ESP32-S3 memory/speed measurements and writable-media reliability.

## Current RETRO86_V1 implementation

The project-owned BIOS is assembled from `components/cpm86Core/bios/retro86bios.a86` into the separate H86 overlay `littlefs/cpm86/retro86bios.h86`; it is not linked into the pinned CP/M-86 CCP/BDOS CMD payload. The runtime loads `cpm.sys` at physical `00510h`, installs the BIOS overlay and BDOS interrupt vector, enters at `0051h:2500h`, and provides console and A:–F: disk access through byte ports. Port assignments are E0h/E1h/E2h for console status/input/output, E8h–EEh for drive, track, 128-byte record, command/status and record-data transfers, and EFh for selected-drive availability. F8h/F9h remain reserved for `hostExchange`.

The boot disk is a raw 160 KiB image: 40 tracks of eight 512-byte physical sectors, represented to CP/M-86 as 32 128-byte records per track. Raw track 0 is reserved; DPB OFF=1 already selects raw track 1 as the first CP/M filesystem track. Therefore the BIOS passes the selected CP/M track to the disk backend unchanged; adding another track would skip the directory. Drives with profile `RETRO86_DATA_LARGE_V1` instead use a 528,384-byte image (129 tracks, 2 KiB blocks, 128 directory entries) and a second DPB selected by SELDSK from the EFh answer; all other drives use this geometry and DPB. The host rejects track 0, tracks 40 and above (129 and above for the large profile), and sectors 32 and above.

`/retro/images/cpm86/drives.cfg` configures read-only A: from LittleFS and optional B:–F: images from the CP/M-86 image directory. The parser validates the entire file, drive letters, profile, access mode and root-constrained paths. Missing images remain offline; invalid configuration falls back to A: only. Each image must be exactly 163,840 bytes (528,384 for the large profile) and open successfully before the BIOS reports its DPH. The `CPM86` image-tool profile defines the matching raw geometry.

The desktop regression test copies the system image to a temporary file, verifies a real `A>` prompt, sends `DIR`, verifies `ASM86` and `PIP`, then selects configured B: and runs `DIR` there. These results are desktop CPU-core evidence only; they do not qualify hardware SD access, disk writes, clean guest exit or broad compatibility.

Startup and restart

	1.	Registry checks compiled implementation and selected profile; report unimplemented separately from resource failure.
	2.	Resolve configuration and required LittleFS/SD resources through host APIs; check exact format, size and hash policy before acquiring them.
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
|A:   |Minimal validated LittleFS image if it fits|System and native essential tools|RO            |
|B:   |Configured SD image                        |Language tools                   |RO, optional  |
|C:   |Configured SD image                        |Editors/assemblers               |RO, optional  |
|D:   |Configured SD image                        |Utilities                        |RO, optional  |
|E:   |Configured SD image                        |Source/build/results             |RW, optional  |
|F:   |Configured SD image                        |Archive/workspace                |RW, optional  |

A:–F: are supported by the RETRO86_V1 BIOS when configured images validate; this does not claim every CP/M-86 application supports six drives. Reject unsupported letters rather than aliasing them.

Use `/retro/images/cpm86/drives.cfg` with the host’s established line-oriented syntax:

  A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM_V1
  B=/retro/images/cpm86/languages.dsk,RO,RETRO86_DATA_V1
  C=/retro/images/cpm86/tools.dsk,RO,RETRO86_DATA_V1
  D=/retro/images/cpm86/utilities.dsk,RO,RETRO86_DATA_V1
  E=/retro/images/cpm86/work86.dsk,RW,RETRO86_DATA_V1
  F=/retro/images/cpm86/archive.dsk,RW,RETRO86_DATA_V1

`RETRO86_SYSTEM_V1` is permitted only for read-only A: at the fixed LittleFS system path. `RETRO86_DATA_V1` is permitted for B:–F: and currently uses the same verified 160 KiB geometry. Reject duplicate drives, path escapes, unsupported profile/access combinations and malformed lines as an entire invalid configuration. Retain the built-in A: as fallback and disable optional drives. Never create blank media during boot.

Freeze each profile’s exact image length, physical/logical sector layout, numbering, skew, reserved tracks, boot bytes, record size and filesystem parameters. Audit SPT, BSH, BLM, EXM, DSM, DRM, AL0/AL1, CKS and OFF where the chosen release uses them, together with pointer sizes, allocation-vector length, extent encoding and block-number width. Document physical-sector versus logical-record translation. CP/M-86 is not a license to enlarge a DPB arbitrarily.

The existing CP/M 2.2 SYSTEM/LARGE geometries and RETROCPM signature are not inherited. A matching image byte length alone is insufficient. Initial geometry should be conservative and independently verifiable. Qualify larger profiles only after filesystem capacity, directory/extents and application limits are proved. Host FAT32 and HTTP capacity remain separate.

A: read-only policy must be checked against native SUBMIT/batch scratch-file behaviour. Select a compatible utility or document use of a writable system image under a separate policy; never silently write through LittleFS protection.

## Preparing E: on macOS

Extend prepareSd.py to create the CP/M-86 directories without replacing existing configurations or files. Build a profile-aware image tool after the geometry is frozen. Required workstation operations are create, list, add and validate; exact CLI flags are pending and must not be advertised as working commands yet.

Creation refuses existing targets by default. Addition validates native 8.3 names, directory capacity, extents, allocation and duplicates, then writes atomically. Retain the original image on failure. Copy a validated work image to the configured E: path, eject safely, and test guest E: selection, DIR and a native copy/write/readback workflow on disposable media.

## Importing software or disk images

Preserve original downloads and record package/version, URLs, timestamp, SHA-256 and rights evidence. Select only native CP/M-86 programs. Archive presence is discovery evidence, not redistribution permission. Review proprietary compiler/editor packages individually.

Inspect archival formats before conversion. .IMD/.TD0 are not raw .DSK images. Use a documented converter, preserve the original, and verify resulting sector order and geometry. A structurally valid CP/M filesystem can still have the wrong OEM boot chain. Tools execute on a workstation; no firmware network downloader is required.

## Terminal and guest tools

Reuse the common 80×24 ANSI/VT100 USB byte stream. Choose terminal-aware native applications or configure their terminal personality reproducibly. Qualify CR/LF, backspace/delete, Ctrl-C, flow control, escape sequences, cursor movement, erase and reverse video. Keep logs off the guest screen.

The acceptance set includes genuine directory/type commands, a native transient HELLO.CMD, a native file-copy utility, an editor, an assembler and at least one compile/run toolchain. Determine names and behaviour from the selected distribution; do not simply append .CMD to the CP/M 2.2 utility list. Record any terminal patches and maintain original binaries.

Measure interactive response, instruction throughput, storage latency, guest-visible memory and application completion on the real board. A fast ESP32 CPU or ample PSRAM does not prove adequate emulation speed.

## Guest exchange utility

Provide native HOST.CMD, assembled for 8086 CP/M-86. Reuse common hostExchange version-1 operations/integrity rules, not the Z80 binary or guest BDOS code. Audit the actual canonical host ABI before coding; if it differs from the supplied CP/M design, resolve that difference centrally.

Project-authored source is in `guest/cpm86/host/HOST.A86`; its official
CP/M-86 BDOS audit and ASM-86/GENCMD build and installation procedure are in
`docs/cpm86/host.md`. ASM-86/GENCMD produced HOST.CMD, which is installed on
A:. The host CP/M-86 fixture passed DIR/GET/PUT exact-byte round-trip tests
against the checked-in system disk; hardware transfer acceptance remains open.
CPM86-M8 remains open.

Target usage:
```
A>HOST DIR
E>A:HOST GET HELLO.A86
E>A:HOST PUT RESULT.TXT
```
DIR lists /retro/exchange/cpm86/. GET/PUT operate on the current guest drive in USER 0 using genuine CP/M-86 BDOS file calls. Reject USER 1–15 explicitly for GET/PUT; DIR remains independent of guest user area. Verify drive-qualified execution and current-drive behaviour under this release before documenting the examples as tested.

## Proposed CP/M-86 transport mapping

Use byte-wide IN/OUT to exchange data port 00F8h and abort port 00F9h, mirroring the supplied CP/M transport. These are project virtual ports, not OEM hardware devices. Reserve them only in RETRO86_V1. With another OEM profile, audit conflicts and define its own mapping. Word I/O is not part of this exchange protocol and must not accidentally consume two stream bytes.

All multi-byte integers are unsigned little-endian. Version-1 wire proposal:

|Operation  |Request / response                                                                                             |
|-----------|---------------------------------------------------------------------------------------------------------------|
|QUERY (`0`)|Reply: status, version `1`, capability bits `07h` for DIR/GET/PUT                                              |
|DIR (`1`)  |Reply: status, printable NUL-terminated names, empty-string terminator, final status                           |
|GET (`2`)  |Request: filename; reply: status, exact uint32 length, payload, CRC-32, final status                           |
|PUT (`3`)  |Request: filename, exact uint32 length, expected CRC-32; reply initial status; then payload; reply final status|
|Abort      |`OUT 00F9h,AL` with AL=0 aborts the active transfer                                                            |

Filenames use uppercase space-padded 11-byte 8+3 representation. Accept flat ASCII names with letters, digits, _ and - within the machine’s exchange root. Status values remain 0=success, 1=invalid name, 2=unavailable/not found, 3=target exists, 4=I/O or checksum error, 5=busy. CRC-32 uses reflected polynomial EDB88320h, initial/final XOR FFFFFFFFh.

Reuse host streaming and bounded handles; never buffer an entire file. PUT does not overwrite an existing exchange destination. Commit only after exact byte count, checksum and close succeed. Guest reset, exit, abort or I/O failure closes handles and removes private temporary files. Bound stalled waits and establish cancellation behaviour without corrupting guest instruction state.

## Exact lengths and failure handling

Confirm the selected CP/M-86 file-record semantics before reusing the 128-byte sidecar format. The proposed compatibility format is one record named NAME.HST, with first 16 bytes: HST1, exact uint32 payload length, uint32 CRC-32 of padded guest records, uint32 CRC-32 of exact payload. All integers are little-endian. Freeze remaining record bytes deterministically in the implementation.

Use saved length only when the current padded-record checksum matches. Otherwise PUT clearly exports complete records. Never heuristically strip 1Ah, zeroes or newlines. Reserve .HST as metadata in the utility’s naming policy and explicitly reject payload names that would collide with reserved sidecars. Preserve originals and clean up only files created by the current GET. Existing payload or sidecar makes GET fail. Disk-full and partial guest writes must not delete a pre-existing file.

CRC-32 provides accidental-change detection, not cryptographic authentication. Verify complete browser→exchange→guest→exchange→browser round trips using SHA-256. Test exact lengths 0, 1, 127, 128, 129 and multi-record binary payloads, stale/corrupt metadata, pre-existing destinations, disk-full, cancellation, user-area rejection and restart.

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
|CPM86-M8|HOST.CMD and common browser staging pass binary, length, CRC, collision, cancellation and hardware transfer tests                                     |
|CPM86-M9|Reset/exit/fault recovery, repeated launches, memory/latency measurements, reproducible release and complete logs                                     |

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
|CPM86-DEC-009|Proposed ABI           |HOST.CMD maps common exchange v1 to byte ports F8h/F9h in RETRO86_V1; verify host contract and OEM conflicts       |
|CPM86-DEC-010|Required               |Preserve exact bytes with validated metadata; never strip EOF/padding heuristically                                |
|CPM86-DEC-011|Preserved host policy  |Physical reset enters menu; browser transfer and emulation are initially exclusive                                 |
|CPM86-DEC-012|Pending selection      |CP/M-86 1.1 is initial release candidate; exact OEM source/resource set not yet selected                           |
|CPM86-DEC-013|Host reconciliation   |Keep CP/M-86 at menu option 2 and File Transfer at 6, as already defined by projectPrompt.md and the host registry; supersedes the proposed option 7 in CPM86-DEC-005|
|CPM86-DEC-014|Selected reference set|Use CP/M-86 1.1 materials in [TheBrokenPipe/isbc8612](https://github.com/TheBrokenPipe/isbc8612/tree/fc31925b444701abdc0bf4d84f555e5e52eab9e7) at `fc31925b444701abdc0bf4d84f555e5e52eab9e7` as reference material only; user confirms CP/M-86 source/binaries are permitted for non-commercial use. Its iSBC BIOS is not the RETRO86_V1 BIOS|
|CPM86-DEC-015|Preferred core to evaluate|Evaluate [EMU86](https://github.com/mfld-fr/emu86/tree/81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac) at `81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac` first: embedded-oriented, modular and MIT-licensed at that revision. This is not a qualification; upstream documents unimplemented rare instructions, keeps processor state in a file-static singleton, and its host entry point uses POSIX APIs|
|CPM86-DEC-016|Selected target        |Build a RETRO86_V1 guest BIOS and target system image, not an IBM PC/XT machine profile|
|CPM86-DEC-017|Selected OS source     |Use [tsupplis/cpm86-kernel](https://github.com/tsupplis/cpm86-kernel/tree/00927e17f43ea4241cc0809531cf68ce835e9c16) at `00927e17f43ea4241cc0809531cf68ce835e9c16` as CP/M-86 1.1 source/reference under the repository's included rights statement and the user's non-commercial-use confirmation; do not silently ship its IBM PC XT BIOS as RETRO86_V1|
|CPM86-DEC-018|Local system reference|Use the user-provided root image `cmp86.img` as a CP/M-86 source/reference candidate; preserve it unchanged and validate its exact disk format, boot path and rights before building RETRO86_V1 resources|

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
|CPM86-ISSUE-009|OPEN |Exchange segment safety, record semantics, metadata collisions and failure recovery; do not claim HOST.COM is portable machine code    |
|CPM86-ISSUE-010|OPEN |Real SD persistence, RO/RW errors, clean exit and reset durability; no host test closes a board gate                                   |
|CPM86-ISSUE-011|PARTIAL|RETRO86_V1 BIOS, system image and desktop boot are implemented in VERIFY-019/023. The separate CompuPro reference image remains unaudited for boot suitability; its declared-geometry/image-length discrepancy, OEM boot chain and rights remain open. The upstream default BIOS remains IBM PC XT.|

Initial chain for CPM86-ISSUE-001: reference behaviour—an external core must execute the selected 8086 guest without host BDOS substitution; hypothesis—EMU86 can supply the required CPU/bus integration with limited adapter changes; experiment—pending pinned source audit and conformance fixtures; result—only moving-branch documentation/header review completed; conclusion—preferred candidate, not qualified; root cause—no failure observed; fix—pending evidence; regression verification—CPM86-VERIFY-002/003 planned; do not repeat—do not convert the earlier recommendation into a claimed boot or conformance PASS.

For every other issue append the complete chain and evidence before closure. Supersede decisions rather than deleting failed approaches.

## Verification log

|ID              |Status                   |Evidence / required next evidence                                                                                                                                                                                                                                                                                                          |
|----------------|-------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-VERIFY-001|DOCUMENTATION REVIEW ONLY|2026-10-06: inspected current EMU86 README and processor/memory-I/O headers, and isbc8612 README. Branches were not pinned; no source checkout/build/run occurred. Firmware, ESP-IDF, board, terminal, image hashes and execution transcripts: N/A. Reference URLs are below. License inspection and complete CPU/manual audits remain open|
|CPM86-VERIFY-002|NOT RUN                  |Pin and reproduce reference boot with exact config, assets, hashes, command/input and console trace                                                                                                                                                                                                                                        |
|CPM86-VERIFY-003|PARTIAL                  |Fixture-level CPU and bus tests now run under host sanitizers; remaining: flags, broad segment cases, near/far control flow, INT/IRET, REP and independent 8086 conformance fixtures                                                                                                                                                  |
|CPM86-VERIFY-004|DESKTOP PARTIAL         |Cold boot to CCP prompt and native `DIR` are verified in the host fixture; native HELLO, warm restart and broader boot state remain open                                                                                                                                                                                                       |
|CPM86-VERIFY-005|NOT RUN                  |Desktop disk-profile, directory/extent, all drive letters, RO/RW, disk-full and boundary/fault matrix                                                                                                                                                                                                                                      |
|CPM86-VERIFY-006|FIRMWARE BUILD PASS / HARDWARE OPEN|ESP-IDF 6.0.2 build and 2 MiB LittleFS image generation pass; flash, board/terminal identity, PSRAM budget and genuine hardware boot transcript remain open                                                                                                                                                                                    |
|CPM86-VERIFY-007|NOT RUN                  |Real SD writable-drive persistence, missing/invalid resources and power/reset recovery on disposable media                                                                                                                                                                                                                                 |
|CPM86-VERIFY-008|NOT RUN                  |Native editor plus assemble/compile/run workflow and hardware performance measurements                                                                                                                                                                                                                                                     |
|CPM86-VERIFY-009|NOT RUN                  |HOST.CMD exact-byte/sidecar/cancellation tests on desktop and ESP32; SHA-256 round trips                                                                                                                                                                                                                                                   |
|CPM86-VERIFY-010|NOT RUN                  |Repeated launch/clean exit, ownership release, fault handling and CP/M 2.2/host-menu regression                                                                                                                                                                                                                                            |
|CPM86-VERIFY-011|HOST RECONCILIATION      |2026-10-06: confirmed the existing registry has CP/M-86 as option 2 and an unimplemented placeholder; SD layout preparation/validation and File Transfer selection already include `cpm86`. ESP-IDF 6.0.2 firmware build succeeded. Host tests passed after reconfiguring `build/host-tests`. No CP/M-86 CPU, guest OS, board execution or hardware verification was performed.|
|CPM86-VERIFY-012|SOURCE METADATA REVIEW  |2026-10-06: pinned the iSBC reference repository to `fc31925b444701abdc0bf4d84f555e5e52eab9e7`; it contains CP/M-86 1.1 system materials and June 1981 System/Programmers Guides. Pinned EMU86 to `81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac`; its MIT license is present, its README identifies unimplemented rare instructions, `emu-proc.c` uses file-static processor state, and `emu-main.c` includes POSIX headers. This metadata review was superseded for CPU-source integration by VERIFY-013; no reference boot or manual ABI audit was performed.|
|CPM86-VERIFY-013|PASS (PROTOTYPE ONLY)  |2026-10-06: vendored the pinned EMU86 CPU modules and MIT license with patch notes; integrated a single-active-core adapter, configurable mapped RAM (1..1 MiB), 20-bit physical wrapping, unmapped-memory faults and F8h/F9h byte-port connection to `hostExchange`. Host tests pass under the configured sanitizers, including selected instruction, wrap, QUERY I/O, unsupported word-I/O, reset/singleton and unmapped-fetch fixtures. ESP-IDF 6.0.2 firmware build passes. No CP/M-86 BIOS/OS image, boot disk, full 8086 conformance suite, flash or hardware execution was performed.|
|CPM86-VERIFY-014|SOURCE/ENVIRONMENT REVIEW|2026-10-06: inspected `tsupplis/cpm86-kernel` at `00927e17f43ea4241cc0809531cf68ce835e9c16`; its source and bootable images target IBM PC/XT, and `pcbios.a86` is explicitly the IBM PC BIOS. Its `LICENSE.md` contains a rights-holder permission statement; the user selected a separate RETRO86_V1 BIOS. No `cpm86.img` exists in the project tree. At that review point, the native CP/M cross-build tools had not yet been brought up; see VERIFY-017. No target BIOS, target image or boot was produced.|
|CPM86-VERIFY-015|IMAGE IDENTIFICATION ONLY|2026-10-06: corrected VERIFY-014 after the user pointed out the root image named `cmp86.img` (not `cpm86.img`). It is 568,576 bytes, SHA-256 `99f1d31ac76bebf1bd6b3af6dc5527a0aa71c6eccef211b1317b1fbf7b3762f6`; string data identifies CP/M-86 1.1NA and OEM copyright notices. This inspection did not establish geometry, image integrity, bootability or suitability as a RETRO86_V1 system disk; filesystem evidence was subsequently established in VERIFY-018. The image was not modified.|
|CPM86-VERIFY-016|PARTIAL DISK-TOOL TEST|2026-10-06: on macOS with cpmtools 2.23, confirmed upstream `base-160.img` is 163,840 bytes (SHA-256 `85e033d684e5ef11dc786c28346291b5d36474ed5b8db836ded01aeb2b6592b2`). On a temporary copy, `cpmcp -f ibmpc-514ss ... base/pip.cmd 0:PIP.CMD` succeeded; extracting `0:PIP.CMD` produced bytes identical to `base/pip.cmd` (SHA-256 `595938e198c479f27aab40b04a332d59273779f2fea0ee9b11cebdc88bdbbfca`). `cpmls -F -f ibmpc-514ss ... '0:*.*'` still reported “No files found” despite the directory entry and successful extraction, so listing behaviour remains unresolved. A separate `mkfs.cpm` trial on an un-sized output created only a partial image; the upstream Makefile explicitly requires pre-filling before `mkfs.cpm`. Temporary images were removed. Firmware/core/reference commits, board, terminal and ESP-IDF: N/A; no guest boot was attempted.|
|CPM86-VERIFY-016|PARTIAL DISK-TOOL TEST|2026-10-06: on macOS with cpmtools 2.23, confirmed upstream `base-160.img` is 163,840 bytes (SHA-256 `85e033d684e5ef11dc786c28346291b5d36474ed5b8db836ded01aeb2b6592b2`). On a temporary copy, `cpmcp -f ibmpc-514ss /tmp/retro86-cpm86-base-test.img base/pip.cmd 0:PIP.CMD` succeeded; extracting `0:PIP.CMD` produced bytes identical to `base/pip.cmd` (SHA-256 `595938e198c479f27aab40b04a332d59273779f2fea0ee9b11cebdc88bdbbfca`). `cpmls -F -f ibmpc-514ss /tmp/retro86-cpm86-base-test.img '0:*.*'` still reported “No files found” despite the directory entry and successful extraction, so listing behaviour remains unresolved. A separate `mkfs.cpm` trial on an un-sized output created only a partial image; the upstream Makefile explicitly requires pre-filling before `mkfs.cpm`. Temporary images were removed. Firmware/core/reference commits, board, terminal and ESP-IDF: N/A; no guest boot was attempted.|
|CPM86-VERIFY-017|REFERENCE BUILD ONLY|2026-10-06: built the pinned source's temporary `emu2` and native `doscat`/`hexcom` helpers. A plain `make cpm.sys` without a pseudo-terminal falsely returned success after the guest assembler failed to access a TTY and emitted empty intermediate output. Re-running `make cpm.sys` and `make check` under `script -q /dev/null` completed all assembler passes with zero errors; `cpm.sys` and `cpmorg.sys` were byte-identical, SHA-256 `0e7fbb2bdb07e84169e2ce96bef56d1c16cf9bd66748b6cf2ebc37462f38c635`. This is the pinned upstream CP/M-86 kernel with its IBM PC BIOS, not a RETRO86_V1 system image. No project resources were changed and no guest boot was attempted. Desktop boot emulator, board, terminal and ESP-IDF: N/A.|
|CPM86-VERIFY-018|FILESYSTEM READ / PROFILE IDENTIFICATION|2026-10-06: the user-provided `assets/readCPM86img.sh` extracts `assets/cmp86.img` after a leading 11,520-byte offset and lists the directory successfully with cpmtools 2.23 and `assets/diskdefs`. The full image is 568,576 bytes (SHA-256 `99f1d31ac76bebf1bd6b3af6dc5527a0aa71c6eccef211b1317b1fbf7b3762f6`); the extracted filesystem region is 557,056 bytes (SHA-256 `4e16e341c0372c858ae3d25ac728c2d6dbb71e2a1ccb930507b07201d10b947c`). The `comp86` definition declares 1,024-byte sectors, 70 tracks, 8 sectors/track, 1,024-byte blocks, 64 directory entries, skew 3, offset 11,520, zero boot tracks, OS 2.2. Reproduced the user's directory listing and extracted `CPM.SYS` (11,776 bytes, SHA-256 `8f370e755d15f374caa417ad288de0182facf18b96b77de290d5d7bebbcd3c05`), `LDBIOS.H86` (6,016 bytes, SHA-256 `fc0e72eb383c226d16ce3d119d902d5c47c3cc1a1fb52fb68020820c06cb33c7`) and `LOADER.H86` (14,080 bytes, SHA-256 `56596ae2dc3891f8cfdecb108e5398d39d95f7db5040364a9dee6e530969fecb`) to temporary storage for inspection. Important unresolved geometry check: the declared geometry spans 70×8×1,024 = 573,440 bytes, 16,384 bytes more than the extracted region; a successful directory listing does not validate the full disk geometry. The image is now identified as a readable CompuPro CP/M-86 filesystem/source candidate, but its boot path, system file semantics, rights, and suitability for RETRO86_V1 remain unverified. No guest boot or project image modification was performed. Firmware/core/reference commits, board, terminal and ESP-IDF: N/A.|
|CPM86-VERIFY-019|DESKTOP BOOT + DIR PASS|2026-10-06: assembled the project RETRO86 BIOS with CP/M-86 ASM-86 1.1N (zero errors), combined it with the pinned `cpm.h86` using native `doscat`, and generated CMD-format `cpm.sys` with GENCMD 1.1N using the kernel Makefile parameters `8080 "CODE[A51,M0000]"`. The initial desktop boot reached `A>`, but `DIR` displayed only `A:` because BIOS READ/WRITE added one to the BDOS track even though DPB OFF=1 already reserves raw track 0. Removing that extra increment made `DIR` list files. The 10,240-byte `cpm.sys` SHA-256 is `72ad573b5c2126d17cca6b1fdf5babea8f8a3403b21453dec9d401adc81dfd56`; the 163,840-byte raw disk SHA-256 is `c86e2db315b29e9ab270066fb4bf87ac498e58484c686bac09c6b0f64f0268b9`. cpmtools 2.23 lists the five expected files. `tests/hostTests.c` now performs the real desktop cold boot, sends `DIR`, requires `ASM86` and `PIP`, and verifies all disk records completed without read errors. The test ran under AppleClang with AddressSanitizer/UndefinedBehaviorSanitizer via `cmake -S tests -B /tmp/retro86-host-tests`, `cmake --build /tmp/retro86-host-tests -j4`, and `ctest --test-dir /tmp/retro86-host-tests --output-on-failure`. No hardware boot, flash, native application run or guest disk write was performed.|
|CPM86-VERIFY-020|ESP-IDF BUILD PASS|2026-10-06: ESP-IDF 6.0.2 build succeeded for ESP32-S3. `retroHost.bin` is 0xE4920 bytes, with 0x21B6E0 bytes free in the smallest app partition. The build generated `build/bootfs.bin` at 2,097,152 bytes and reported adding `cpm86/cpm.sys`, `cpm86/system.dsk` and `cpm86/README.txt`. Extracting `CPM.SYS` from `system.dsk` with cpmtools 2.23 and `cmp` against `littlefs/cpm86/cpm.sys` confirms byte identity. No flash or hardware run was performed.|
|CPM86-VERIFY-021|PSRAM CONFIGURATION FIX / BUILD PASS|2026-10-06: the user's LOLIN S3 Pro boot log showed `cpm86CoreNoMemory` (result 3). Root cause confirmed in the active `sdkconfig`: `CONFIG_SPIRAM` was disabled, so the 640 KiB allocation fell back to internal DRAM and failed. Enabled the board's 8 MiB octal ESP-PSRAM64 at 80 MHz and explicit `MALLOC_CAP_SPIRAM` allocation in `sdkconfig.defaults`/`sdkconfig`; added exact free/largest-block diagnostics if core allocation still fails. ESP-IDF 6.0.2 firmware rebuild and sanitized host tests pass. Physical PSRAM initialization/allocation is not verified until the user flashes and boots the firmware.|
|CPM86-VERIFY-022|HARDWARE BOOT VERIFIED (ROOT CAUSE FIXED)|2026-10-06: on the ESP32-S3 the guest stopped with result 7 after 4,784 instructions. Traces showed the CCP looping on `call SELDRV` (0051:018B/00DA) until the stack overwrote CCP data. Root cause: Xtensa `char` is unsigned, but EMU86 sign-extends 8-bit displacements via `(short)(char)`, so the short jump at 0051:00D8 (`EB B1`) landed 0x100 too far. Fixed with `(signed char)` in op-class.c (4 places) and op-exec.c (1 place). Host tests now build with `-funsigned-char` to reproduce this class of bug. User-observed hardware result: boot reaches `A>`; `dir` lists CPM.SYS, ED, PIP, ASM86, GENCMD; `b:` and `stat` report `?` because those drive/commands are absent. The diagnostic trace (head/recent trace, execute guard over 0051:0800-09FF) remains in the firmware.|
|CPM86-VERIFY-023|MULTI-DRIVE HOST/BUILD PASS|2026-10-06: `drives.cfg` validates A:–F:; startup opens configured images, retains A: as the required LittleFS read-only drive, leaves missing optional images offline, and loads the separate H86 BIOS overlay. The sanitized host test boots the real guest, selects B: and executes `DIR`; path/parser regressions pass. Python tooling tests pass (20 tests), and temporary-card preparation generates the expected A:/E: mappings. ESP-IDF 6.0.2 build passes (`retroHost.bin` 0xE8000) and packages `cpm86/retro86bios.h86`. Physical SD access and writes are not verified.|
|CPM86-VERIFY-024|DAA HOST REGRESSION / BUILD PASS|2026-10-06: the pasted Turbo Pascal trace reports `no handler for op 40h`; decoded operation ID 40h is DAA and its execution-table entry had no handler. Implemented 8086 DAA and added a sanitized host regression for the reported `ADC AL,40h; DAA` sequence, checking the adjusted accumulator and CF/PF/AF/ZF. Host tests and the ESP-IDF 6.0.2 firmware build pass. No hardware execution was performed.|
|CPM86-VERIFY-025|ARITHMETIC FLAGS HOST REGRESSION / BUILD PASS|2026-10-06: the subsequent Turbo Pascal compile stopped after control flow reached invalid opcode bytes `FF E9`. Review found the 8086 core left PF/AF/OF incomplete for arithmetic and INC/DEC, even though its conditional-jump handlers consume those flags. Implemented SZP/AF/CF/OF updates for ADD/ADC/SUB/SBB/CMP, logic, INC/DEC and NEG. Sanitized host tests verify arithmetic flag values and parity-driven branching; the test suite and ESP-IDF 6.0.2 build pass. This does not yet prove that Turbo Pascal compilation succeeds on the ESP32-S3; no hardware execution was performed.|
|CPM86-VERIFY-026|PUSHA SEMANTICS HOST REGRESSION / BUILD PASS|2026-10-06: the next user log still stopped at `FF E9` after about 4.95 million instructions. Further core review found PUSHA saved the already-decremented SP instead of the original SP required by 80186 semantics. Fixed the saved stack word and added a sanitized fixture verifying all eight saved/restored registers and the original-SP slot. Host tests and ESP-IDF 6.0.2 build pass. This regression is corrected in the core; whether it resolves the Turbo Pascal run remains unverified on hardware.|
|CPM86-VERIFY-027|MULTIPLY/DIVIDE/SHIFT HOST REGRESSION / BUILD PASS|2026-10-06: after the PUSHA fix the user confirmed flashing the latest build, but reported the same failure PC and instruction count; the PUSHA change therefore did not resolve the report. Continued instruction audit found incorrect/missing MUL/IMUL CF/OF updates, signed division sign extension and overflow handling, missing SAL handling, and width/flag errors in byte shifts including SAR. Corrected these semantics and added sanitized host fixtures for signed byte/word division, multiply flags and byte shift results/flags. Host test and ESP-IDF build pass. The exact Turbo Pascal hardware run remains unverified.|
|CPM86-VERIFY-028|LARGE-DRIVE EXTENT MAPPING FIX / HOST PASS|2026-10-06: the `FF E9` stop at 0518:518D/518F was a symptom, not a CPU defect (VERIFY-024..027 did not address it). Reproduced on the host with TurboP.dsk: TURBO.CMD (0x919 paragraphs) is loaded by the BDOS program loader (0051:0E86), which ignores read errors; code from file offset 0x4000 onward was one repeated 16-byte paragraph, so execution reached garbage. Root cause: BIOS `DPB1` (RETRO86_DATA_LARGE_V1) declared EXM=1 (two 16 KiB logical extents per directory entry), whereas `tools/diskImageCpm.py` writes one 16 KiB extent (128 records, 8 two-KiB block pointers) per entry with extent numbers 0,1,2,...; the BDOS therefore could not find records 128 and up of any file larger than 16 KiB on a LARGE drive. Fix: `DPB1` EXM 01h -> 00h in `retro86bios.a86` and `retro86bios.h86` (record checksum recomputed; new SHA-256 in `littlefs/cpm86/README.txt`). Regression: `testCpm86Boot` builds a LARGE image in the tool layout with a 19,600-byte two-extent text file and requires every line in `TYPE`; with the old overlay it fails at line 2341 (16 KiB boundary), with the fix it passes. With the patched overlay the host run of TURBO compiles LISTER.PAS (210 lines) and starts it. Caveat: files larger than 16 KiB written by the guest to a LARGE drive under the old EXM=1 DPB use a different directory layout and must be re-copied; `tools/cpm86/diskdefs` `retro86-large` (cpmtools infers EXM=1 for 2 KiB blocks) does not match this layout. Hardware run not yet verified.|

|CPM86-VERIFY-029|HOST SOURCE / EXTRACTION TEST PASS; NATIVE CMD NOT BUILT OR INSTALLED|2026-10-07: added project-authored 8086 source and a documented ASM-86/GENCMD path. The focused and complete `tests/test_diskImage.py` suite passes, including multi-extent extraction and overwrite refusal. No native assembler was run, no HOST.CMD was produced, and no guest or hardware transfer test was performed; CPM86-M8 remains open.|
|CPM86-VERIFY-030|NATIVE HOST CMD / DESKTOP DIR-GET-PUT PASS; HARDWARE OPEN|2026-10-07: assembled `guest/cpm86/host/HOST.A86` with the A: disk's ASM86.CMD (zero errors) and GENCMD.CMD (3,584-byte CMD), installed it in `littlefs/cpm86/system.dsk`, and refreshed the resource inventory. `tests/hostTests.c` booted the checked-in system with the generated source on a fresh LARGE E: image; native DIR listed HELLO.A86, GET created the file and HST1 metadata, and PUT round-tripped the exact original bytes. Test exited 0. HOST.CMD SHA-256 `5ec615603dfdfa3d2cd5ab72b65f1ee399732652e90c6f168ce690d0f04793ed`; system image SHA-256 `04050eacdf37310f9ffc4ed6ffb47b0e8b03b458b2b7609a1e82cc437d8bf464`. The existing emulator diagnostic `insane POP CS (0Fh)` is printed by the broader CPU fixture but does not fail the suite; no firmware/hardware transfer or persistence test was performed.|
|CPM86-VERIFY-031|HARDWARE HOST ATTEMPT FAILED; FIRMWARE WIRING FIX BUILDS; RETEST OPEN|2026-10-07: after selecting CP/M-86 on the connected board, the first `HOST DIR` attempt stopped with `cpm86CoreIoError` at guest port F8h. Root cause: `cpm86MachineInitialize` passed `NULL` as the core's hostExchange service. Fixed it to initialize `/microSD/retro/exchange/cpm86`, pass the service to `cpm86CoreCreate`, and close it on failure/shutdown. ESP-IDF 6.0.2 build succeeds. The updated firmware was not flashed (per user preference); repeat HOST DIR/GET/PUT on updated firmware, with a writable guest drive and exchange files present, before claiming hardware transfer acceptance.|
|CPM86-VERIFY-032|MULTI-RECORD HOST PUT / IMAGE UPDATE PASS; HARDWARE RETEST OPEN|2026-10-07: the reported PUT crash was reproduced by the desktop guest fixture. The PUT byte loop had kept advancing SI beyond the 128-byte BDOS DMA record instead of fetching the next record. It now sends at most 128 bytes, then rereads the next record and resets SI. The regression sends and byte-compares a deterministic 43-record (5,504-byte) file. The fixture assembles the source on E: and runs the installed A:HOST.CMD from the rebuilt system image. Rebuilt the 160 KiB system image with HOST.CMD 3,456 bytes (SHA-256 `613be623092cc2cdb2c9a293ed4cc80a2246130a1b81f6691c5d412672b4d649`); image SHA-256 `aaf0a2a55cc0b0af9bc9de27deb351fff7ef4939b886d75b5da041564509fc3c`. The host CP/M-86 test passed against the rebuilt image and fresh LARGE E: disk; all seven disk-image tests passed. No device test or flash was performed; user should retest HOST PUT on hardware.|

## Development references and evidence policy

	●	[EMU86 at the pinned revision](https://github.com/mfld-fr/emu86/tree/81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac): MIT license and README reviewed; core APIs, instruction coverage and ESP-IDF isolation are not yet audited.
	●	[iSBC86/12 reference repository at the pinned revision](https://github.com/TheBrokenPipe/isbc8612/tree/fc31925b444701abdc0bf4d84f555e5e52eab9e7): CP/M-86 1.1 materials are reference candidates under the user-confirmed non-commercial-use permission. The emulator source has no established license and is not to be vendored or executed without separate permission.
	●	Reference manuals in that repository: [CP/M-86 System Guide, June 1981](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_System_Guide_Jun81.pdf), [Programmer’s Guide, June 1981](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_Programmers_Guide_Jun81.pdf), and [CP/M-86 1.1 release notes, 1982](https://github.com/TheBrokenPipe/isbc8612/blob/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_1.1_Release_Note_1982.pdf). ABI page audit is still required.
	●	8086tiny fallback repository. Candidate only; no detailed source or ESP32 integration audit completed here.
	●	Primary references required before ABI freeze: the selected Digital Research CP/M-86 System Guide/Programmer’s Guide editions and Intel’s 8086 Family User’s Manual. Obtain, identify and cite exact editions/pages during CPM86-M1–M3. Full primary-manual inspection was not completed for this revision.
	●	The user-supplied designCPM80.md is integration context, not CP/M-86 test evidence. The actual current projectPrompt.md and repository must be read during implementation reconciliation.

**Keep this document current by appending numbered evidence and explicitly superseding decisions. Do not promote a design proposal, upstream claim or build into an observed guest execution result**.