# designCPM86.md — CP/M-86 engineering memory

Revision: 2026-10-06. Status: initial integration design, not an implementation or a verification report. No CP/M-86 core has been vendored, no target BIOS or boot image has been built, and no desktop or ESP32-S3 execution has been performed for this document. The supplied CP/M 2.2 (CP/M-80) engineering memory reports a working hardware smoke test; that evidence does not establish CP/M-86 compatibility. EMU86 is the preferred CPU evaluation candidate, subject to instruction coverage, licensing, integration and regression gates. CP/M-86 1.1 is the initial release candidate; its exact OEM distribution remains to be selected.

## Provenance and purpose

Prepared from the user’s supplied designCPM80.md revision and the preceding discussion of mfld-fr/emu86, TheBrokenPipe/isbc8612 and adriancable/8086tiny. Repository documentation was inspected on 2026-10-06. Moving branch URLs are discovery references, not reproducible source pins. No commit hashes, resource checksums, board measurements or successful executions are invented here.

Deliver a practical CP/M-86 software-development machine alongside the existing CP/M 2.2 machine: command line, native editors, an assembler and at least one compile/run workflow. Genuine CP/M-86 guest code must execute on an external 8086 core. Games, IBM PC compatibility, graphics adapters, DOS, protected mode and 8087 emulation are outside the initial acceptance scope.

This is an additional design stream. It does not supersede the established CP/M 2.2 acceptance gates or silently change the existing CP/M-before-UCSD implementation order. Scheduling CP/M-86 into that order is a separate host planning decision.

## Project-wide contract

projectPrompt.md remains the authority for host architecture, machine registry, manifests, storage, console, exchange and coding rules. Companion documents are designCPM80.md, designCPM86.md, designUCSD.md, designAppleII.md, designMPM.md and designSWTPC.md. The user-supplied CP/M design is the baseline for the integration requirements below; the actual current repository and canonical host document must be reconciled before implementation. This task creates only this design document.

## Menu integration

Preserve existing numbers, especially File Transfer at 6. Reserve 7 for CP/M-86. The following is a proposed coordinated host-contract amendment, not a claim that the existing registry already supports it:
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
Update the canonical menu specification, range validation, registry, availability messages and host menu tests together. Do not reuse another machine’s slot. Before implementation, option 2 reports Not implemented yet; later distinguish that state from missing or invalid resources. Every choice requires Enter. Normalize CR/LF/CRLF so a line is submitted once. Invalid or unavailable choices explain the reason and remain in the menu.

HOST-M1 owns USB console, LittleFS, SD detection, versioned /retro/layout.txt, block storage, exchange and browser File Transfer. Add a cpm86 machine identity and storage roots through the implemented host schema. If that schema requires a version change, perform an explicit compatible migration; do not independently define a new layout version. A FAT32 32 GB SD card remains recommended. Never format an unknown card or replace user images automatically.

Paths beginning /retro/ are SD-volume paths independent of the VFS mount prefix. Use the host resolver; do not compile /microSD into the emulator. LittleFS uses /littlefs. Proposed CP/M-86 resource namespaces are:
```
/littlefs/cpm86/
/retro/images/cpm86/
/retro/exchange/cpm86/
```
Keep them independent of the existing cpm directories. A resource description records release, machine profile, format, exact size, SHA-256, origin, rights evidence and access policy using the common manifest contract. Minimal boot resources belong in LittleFS only if the measured partition budget permits them. Larger or writable images belong on SD. If minimal resources do not fit, explicitly require SD; do not promise an unavailable recovery boot.

File Transfer is the shared host service. Only option 6 starts WiFi, exclusively through michmich/esp-idf-wifi-provisioner, preserving the supplied ^0.4.0 requirement and locking the tested dependency version. Add CP/M-86 to the GUI machine selector: loose files use /retro/exchange/cpm86/; whole images use /retro/images/cpm86/. Display full VFS paths. Reuse bounded streaming, temporary uploads, progress, traversal rejection and explicit overwrite policy. The supplied transfer limit remains 4 GiB minus 2 bytes, subject to the canonical HTTP implementation. That limit does not imply that CP/M-86 supports a disk or file of that size.

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

Proposed ownership, to be reconciled with actual repository names:

|Location                  |Responsibility                                                                |
|--------------------------|------------------------------------------------------------------------------|
|`components/emu86/`       |Pinned external source, license and small documented patches                  |
|`components/cpm86Core/`   |CPU adapter, bus and machine state; desktop-testable                          |
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

Designed, but not implemented:

	●	Proposed registry identity cpm86, menu slot 2 and isolated resource roots.
	●	Preferred-core evaluation and reference-machine reproduction gates.
	●	Separate 20-bit bus, guest BIOS, CP/M-86 OS and common-host adapters.
	●	Planned native exchange utility, image tools and acceptance matrix.

Not established: core pin, complete instruction coverage, source isolation, target BIOS ABI, guest linker map, boot resource rights/hashes, disk geometry, desktop boot, firmware build, editor/compiler operation, hardware memory/speed and writable-media behaviour. Every executable test in this document is a future requirement.

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

Use a narrow project-defined virtual device interface between guest BIOS and host. Its port numbers, operation IDs, register payloads, asynchronous completion and reset semantics must be frozen in docs/cpm86/virtualIo.md before implementation. Keep BIOS service I/O separate from the exchange ports below. Guest-visible buffers must be validated against populated RAM, including segment and physical boundaries. Reject wrapping DMA if the profile disallows it; do not silently clamp it.

The BIOS may call host image-record primitives through virtual I/O. The host never interprets BDOS calls or guest directory operations. If a selected OEM BIOS is retained unchanged instead, emulate its actual device interface under a distinct profile.

## Drives and capacity

|Drive|Proposed storage                           |Role                             |Initial policy|
|-----|-------------------------------------------|---------------------------------|--------------|
|A:   |Minimal validated LittleFS image if it fits|System and native essential tools|RO            |
|B:   |Configured SD image                        |Language tools                   |RO, optional  |
|C:   |Configured SD image                        |Editors/assemblers               |RO, optional  |
|D:   |Configured SD image                        |Utilities                        |RO, optional  |
|E:   |Configured SD image                        |Source/build/results             |RW, optional  |
|F:   |Configured SD image                        |Archive/workspace                |RW, optional  |

A:–F: is the target design, not a claim about the reference’s two-drive controller or every application. Check selected BDOS and tools for all six drives. Reject unsupported letters rather than aliasing them.

Propose /retro/images/cpm86/drives.cfg with the host’s established line-oriented syntax:

  A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM_V1
  B=/retro/images/cpm86/languages.dsk,RO,RETRO86_DATA_V1
  C=/retro/images/cpm86/tools.dsk,RO,RETRO86_DATA_V1
  D=/retro/images/cpm86/utilities.dsk,RO,RETRO86_DATA_V1
  E=/retro/images/cpm86/work.dsk,RW,RETRO86_DATA_V1
  F=/retro/images/cpm86/archive.dsk,RW,RETRO86_DATA_V1

These profile identifiers are reserved names only. They do not define geometry or make the example mountable today. Reuse or extend the shared parser with machine-specific profile tables and root restrictions. Reject duplicate drives, path escapes, unsupported profile/access combinations and malformed lines as an entire invalid configuration. If a validated built-in A: exists, retain it as fallback and disable optional drives. Never create blank media during boot.

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

## Issue log

|ID             |State|Problem / next evidence / do not repeat                                                                                                |
|---------------|-----|---------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-ISSUE-001|OPEN |Core pin, dependency isolation and instruction coverage; do not equate embedded intent with a complete library                         |
|CPM86-ISSUE-002|OPEN |Reference code/assets rights and independent boot reproduction; do not inherit bundled-resource permission from repository availability|
|CPM86-ISSUE-003|OPEN |Exact CP/M-86 release, primary-manual ABI audit and boot resource provenance; do not claim guessed register conventions                |
|CPM86-ISSUE-004|OPEN |BIOS/linker map, segments, DMA and memory descriptor; do not transplant CP/M 2.2 page zero or jump table                               |
|CPM86-ISSUE-005|OPEN |Disk geometry/DPB/extent audit and independent formatter; do not reuse SYSTEM/LARGE labels based on size                               |
|CPM86-ISSUE-006|OPEN |Actual board PSRAM, LittleFS budget and throughput; do not promise 1 MiB RAM or useful speed without measurement                       |
|CPM86-ISSUE-007|OPEN |Canonical menu/layout/GUI and host ABI reconciliation; preserve option 6 and existing machine contracts                                |
|CPM86-ISSUE-008|OPEN |Native applications, terminal personality and batch scratch-drive behaviour; Z80 COM or DOS binaries are not acceptance tools          |
|CPM86-ISSUE-009|OPEN |Exchange segment safety, record semantics, metadata collisions and failure recovery; do not claim HOST.COM is portable machine code    |
|CPM86-ISSUE-010|OPEN |Real SD persistence, RO/RW errors, clean exit and reset durability; no host test closes a board gate                                   |

Initial chain for CPM86-ISSUE-001: reference behaviour—an external core must execute the selected 8086 guest without host BDOS substitution; hypothesis—EMU86 can supply the required CPU/bus integration with limited adapter changes; experiment—pending pinned source audit and conformance fixtures; result—only moving-branch documentation/header review completed; conclusion—preferred candidate, not qualified; root cause—no failure observed; fix—pending evidence; regression verification—CPM86-VERIFY-002/003 planned; do not repeat—do not convert the earlier recommendation into a claimed boot or conformance PASS.

For every other issue append the complete chain and evidence before closure. Supersede decisions rather than deleting failed approaches.

## Verification log

|ID              |Status                   |Evidence / required next evidence                                                                                                                                                                                                                                                                                                          |
|----------------|-------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|CPM86-VERIFY-001|DOCUMENTATION REVIEW ONLY|2026-10-06: inspected current EMU86 README and processor/memory-I/O headers, and isbc8612 README. Branches were not pinned; no source checkout/build/run occurred. Firmware, ESP-IDF, board, terminal, image hashes and execution transcripts: N/A. Reference URLs are below. License inspection and complete CPU/manual audits remain open|
|CPM86-VERIFY-002|NOT RUN                  |Pin and reproduce reference boot with exact config, assets, hashes, command/input and console trace                                                                                                                                                                                                                                        |
|CPM86-VERIFY-003|NOT RUN                  |CPU fixtures and bus boundary tests: flags, segments, near/far control flow, INT/IRET, REP, HLT, I/O width, physical wrap and lifecycle under sanitizers                                                                                                                                                                                   |
|CPM86-VERIFY-004|NOT RUN                  |Target guest BIOS/OS cold boot, DIR, native HELLO and warm restart; freeze linker map and resource hashes                                                                                                                                                                                                                                  |
|CPM86-VERIFY-005|NOT RUN                  |Desktop disk-profile, directory/extent, all drive letters, RO/RW, disk-full and boundary/fault matrix                                                                                                                                                                                                                                      |
|CPM86-VERIFY-006|NOT RUN                  |ESP-IDF build, flash, board/terminal identity, LittleFS/PSRAM budget and genuine hardware boot transcript                                                                                                                                                                                                                                  |
|CPM86-VERIFY-007|NOT RUN                  |Real SD writable-drive persistence, missing/invalid resources and power/reset recovery on disposable media                                                                                                                                                                                                                                 |
|CPM86-VERIFY-008|NOT RUN                  |Native editor plus assemble/compile/run workflow and hardware performance measurements                                                                                                                                                                                                                                                     |
|CPM86-VERIFY-009|NOT RUN                  |HOST.CMD exact-byte/sidecar/cancellation tests on desktop and ESP32; SHA-256 round trips                                                                                                                                                                                                                                                   |
|CPM86-VERIFY-010|NOT RUN                  |Repeated launch/clean exit, ownership release, fault handling and CP/M 2.2/host-menu regression                                                                                                                                                                                                                                            |

## Development references and evidence policy

	●	EMU86 repository, README, processor API, memory/I/O API. Documentation review only; record immutable links after source selection.
	●	isbc8612 reference repository. Upstream boot claims require reproduction; code and asset rights require separate resolution.
	●	8086tiny fallback repository. Candidate only; no detailed source or ESP32 integration audit completed here.
	●	Primary references required before ABI freeze: the selected Digital Research CP/M-86 System Guide/Programmer’s Guide editions and Intel’s 8086 Family User’s Manual. Obtain, identify and cite exact editions/pages during CPM86-M1–M3. Full primary-manual inspection was not completed for this revision.
	●	The user-supplied designCPM80.md is integration context, not CP/M-86 test evidence. The actual current projectPrompt.md and repository must be read during implementation reconciliation.

**Keep this document current by appending numbered evidence and explicitly superseding decisions. Do not promote a design proposal, upstream claim or build into an observed guest execution result**.