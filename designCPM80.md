# CP/M-80 Machine Design

## 1. Overview

This document defines the CP/M-80 guest machine hosted by the ESP32-S3 Retro Computer. It describes the architecture, guest-visible hardware interface, memory map, boot system, BIOS, disk formats, drive configuration, and console behavior in enough detail to reproduce or independently implement the machine.

The CP/M-80 implementation runs genuine CP/M 2.2 CCP and BDOS binaries on a Z80 CPU core. Firmware provides the BIOS and virtual drives; it does not substitute a host-side BDOS or present the host FAT filesystem as the guest filesystem.

The common host requirements are defined by [projectPrompt.md](./projectPrompt.md). This document covers the CP/M-80 machine-specific behavior. The implementation in `components/cpm80Core/`, `main/cpm80Machine.c`, `main/cpm80DriveConfig.c`, `guest/cpm80/`, and the associated build and test tools is the reference for existing behavior.

## 2. Purpose and scope

CP/M-80 provides a Z80-based command-line computer for CP/M software development and execution. It is intended to run CP/M applications such as editors, assemblers, debuggers, utilities, and language tools against disk images with CP/M-compatible geometry.

The machine includes:

- A Z80 CPU and 64 KiB address space.
- Genuine CP/M 2.2 CCP and BDOS binaries.
- A project BIOS that maps CP/M calls onto the common USB console and image-file I/O.
- Up to six CP/M drives, A: through F:, with independent geometry, access mode, and image.
- A: boot resources in LittleFS and optional data drives on the physical SD card.

The common host owns the menu, machine lifecycle, USB Serial/JTAG console, LittleFS, SD/FAT, virtual-image I/O, Wi-Fi, File Transfer web server, and shared disk-activity indicator. The CP/M machine owns its CPU and guest memory, CP/M system installation, BIOS state, and drive table. The CP/M machine does not start Wi-Fi or an HTTP server.

The emulator and browser File Transfer service are mutually exclusive.

## 3. Architecture and source boundaries

```text
Host menu
  -> CP/M-80 resource probe
  -> cpm80Machine
       -> system-image and drives.cfg handling
       -> imageFile random-access I/O
       -> hostConsole
  -> cpm80Guest
       -> BIOS installation and BIOS-call dispatch
       -> cpm80Cpu
            -> pinned Z80 core
       -> genuine CP/M 2.2 CCP and BDOS
```

| Subsystem | Implementation | Responsibility |
| --- | --- | --- |
| Machine integration | `main/cpm80Machine.c` | Probe A:, open images, construct host callbacks, schedule the guest, and close resources. |
| Drive configuration | `main/cpm80DriveConfig.c` and its header | Parse and validate `drives.cfg`; resolve the A: image profile. |
| CPU adapter | `components/cpm80Core/cpm80Cpu.c` | Own 64 KiB guest memory, connect Z80 memory and I/O callbacks, and step the processor. |
| Guest and BIOS | `components/cpm80Core/cpm80Guest.c` | Install CP/M memory structures and BIOS, dispatch BIOS calls, and run bounded instruction batches. |
| Z80 core | `components/z80/` | Upstream `superzazu/z80` core pinned to commit `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, MIT license. |
| CP/M operating system | `components/cpm80Core/os/` | CCP and BDOS sources/binaries from `brouhaha/cpm22`, pinned to commit `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`. |
| Image preparation | `tools/include/buildDiskImageCpm80.py`, `tools/include/diskImageCpm.py`, `tools/include/diskImageCpm80.py` | Build and manipulate raw CP/M images on a development computer. |

Third-party source licenses do not determine distribution rights for separately obtained application programs. Retain provenance and rights information for every independently sourced CP/M application.

## 4. CPU and guest execution model

### Z80 adapter

The CPU adapter allocates exactly 65,536 bytes of zero-initialized guest memory. It connects the vendored Z80 core's byte-read and byte-write callbacks directly to that memory and connects 8-bit port input/output to the guest adapter. The adapter executes one Z80 instruction per `cpm80CpuStep`.

Instances must be zero-initialized before `cpm80CpuInitialize` and released through `cpm80CpuDestroy`. Initialization rejects missing port callbacks and an already initialized instance. `cpm80CpuLoad` allows a bounded byte range; its range check prevents loading past the 64 KiB address space. Memory reads and writes are byte-addressed and accept the complete 16-bit address range.

There is no implemented cycle-accurate peripheral timing model. The guest scheduler limits each run to a requested instruction budget. `cpm80GuestRunFor` invokes the cooperative host yield callback every 4096 executed instructions; the machine loop runs batches of 10,000 instructions and delays between batches to service the ESP-IDF task scheduler and watchdog.

### Host callback contract

`cpm80HostOps` supplies the guest with:

- Console availability, input, and output.
- Drive availability and each mounted drive's profile.
- Reads and writes of exactly one 128-byte disk record.
- A cooperative yield callback and caller-owned context pointer.

The guest depends on these callbacks rather than on ESP-IDF APIs. This keeps the CPU and BIOS logic host-testable and leaves SD and console ownership with the common host.

## 5. Memory map and boot system

### Guest address map

| Address range | Guest contents |
| --- | --- |
| `$0000–$0002` | Page-zero `JP` to BIOS WBOOT at `$DA03`. |
| `$0003` | I/O byte, initialized to zero. |
| `$0004` | Current drive number; A: is zero. |
| `$0005–$0007` | BDOS `JP` to `$CC06`. |
| `$005C–$007F` | Standard default FCB area. |
| `$0080–$00FF` | Default 128-byte DMA area. |
| `$0100–$C3FF` | Transient program area. |
| `$C400–$CBFF` | CCP image, 2,048 bytes. |
| `$CC00–$D9FF` | BDOS image, 3,584 bytes. |
| `$DA00–$DA32` | BIOS jump table: 17 three-byte entries. |
| `$DA33–$DA87` | BIOS service stubs, five bytes each. |
| `$DA90–$DA9F` | A: DPH. |
| `$DAA0–$DAAE` | A: DPB. |
| `$DAB0–$DB2F` | A: 128-byte directory buffer. |
| `$DB30–$DB3F` | A: SYSTEM CSV area. |
| `$DB40–$DB5E` | A: allocation vector area. |
| `$DB60–$DF5F` | B:–E: per-drive BIOS table slots, `$0100` bytes each. |
| `$DF60–$E05F` | F: BIOS table slot, including BIG allocation-vector space. |
| `$E060–$E07F` | A: LARGE CSV area. |
| `$E080–$E09E` | A: LARGE allocation-vector area. |

The CCP and BDOS occupy the high-memory resident system area. Transient programs are loaded by the genuine CP/M BDOS into the TPA beginning at `$0100`. Page zero contains the conventional WBOOT and BDOS jump vectors and the current-drive byte.

### LittleFS system image

The raw system image is `/littlefs/cpm80/system.dsk`. The image begins with the CCP and BDOS binaries:

| Image offset | Contents |
| --- | --- |
| `$0000–$07FF` | CCP, 2,048 bytes. |
| `$0800–$15FF` | BDOS, 3,584 bytes. |
| `$1600–$160F` | 16-byte `RETROCPM` system-image header. |

The header is the eight ASCII bytes `RETROCPM`, version bytes `1,1`, followed by little-endian 16-bit addresses `$C400`, `$CC00`, and `$DA00`. The firmware checks the exact image size/profile and header before declaring the machine available. It reads the CCP and BDOS into guest-owned buffers during initialization. The BIOS is generated in guest memory and is not stored in the image.

The boot image also contains CP/M directory entries and allocation blocks for `HELLO.COM`, `WELCOME.TXT`, and the installed utility set. Its directory begins after the two reserved tracks for the image profile.

### Cold boot and warm boot

Cold boot clears guest memory, installs the system binaries and BIOS, initializes page zero, resets console and disk state, and starts through BIOS BOOT before entering the CCP. The machine run loop starts with `cpm80GuestColdBoot`.

Warm boot is a guest operation. The WBOOT service reinstalls the CCP, BDOS, BIOS, page-zero vectors, and default DMA state, preserves the current drive byte, and returns to CP/M command processing. It does not return to the host menu or reset the physical ESP32.

## 6. BIOS and guest-visible I/O

### BIOS call mechanism

The BIOS jump table uses the CP/M 2.2 order:

`BOOT`, `WBOOT`, `CONST`, `CONIN`, `CONOUT`, `LIST`, `PUNCH`, `READER`, `HOME`, `SELDSK`, `SETTRK`, `SETSEC`, `SETDMA`, `READ`, `WRITE`, `LISTST`, `SECTRAN`.

Each table entry jumps to a five-byte guest stub. The stub loads its BIOS function index into register A, executes `OUT ($FE),A`, then returns. Port `$FE` is a project-defined virtual BIOS callback, not a physical ESP32 peripheral. The port callback dispatches the requested BIOS service against guest registers and per-instance state.

### BIOS function behavior

| BIOS function | Implemented behavior |
| --- | --- |
| `BOOT` | Cold-start the CP/M system in guest memory. |
| `WBOOT` | Reinstall the CP/M system and restart at the CCP. |
| `CONST` | Nonblocking console status: return `$FF` in A if a character is available, otherwise zero. A pending character is retained for `CONIN`. |
| `CONIN` | Return one input character in A. If none is available, keep the guest at the `CONIN` stub, yield to the host, and retry. |
| `CONOUT` | Send the low byte of register C to the common USB console. |
| `LIST` | Deterministic no-op; no physical printer is implemented. |
| `PUNCH` | Deterministic no-op; no physical punch device is implemented. |
| `READER` | Return `$1A` in A (CP/M end-of-file). |
| `HOME` | Set the current track to zero. |
| `SELDSK` | Select an available drive and return its DPH address in HL. For an unavailable or out-of-range drive, return a null DPH and do not alias A:. |
| `SETTRK` | Set current track from BC. |
| `SETSEC` | Set current sector from BC. |
| `SETDMA` | Set the guest DMA address from BC. |
| `READ` | Read one 128-byte record into the DMA buffer. Return A=0 on success, A=1 on invalid position, invalid DMA range, or host read failure. |
| `WRITE` | Write one 128-byte record from the DMA buffer. Return A=0 on success and A=1 on invalid position, invalid DMA range, read-only drive, or host write failure. |
| `LISTST` | Return zero in A; no printer status is available. |
| `SECTRAN` | Identity translation: return BC unchanged in HL. |

Disk track and sector numbers are zero-based. The byte offset of a record is:

```text
recordOffset = ((track * sectorsPerTrack) + sector) * 128
```

The guest checks track, sector, drive, and DMA bounds before accessing the host callback. The machine maps each record operation to random-access image I/O and flushes each successful record write. Each drive has its own DPH, DPB, directory buffer, CSV/ALV storage, profile, image handle, and read-only setting. Successful record reads and writes signal the shared disk-activity indicator.

### DPH and DPB structures

Each Disk Parameter Header (DPH) has no translation table and points to its own directory buffer, Disk Parameter Block (DPB), checksum vector (CSV), and allocation vector (ALV).

- A: DPH is at `$DA90`; DPB at `$DAA0`; directory buffer at `$DAB0`.
- B: through F: DPH addresses are `$DB60`, `$DC60`, `$DD60`, `$DE60`, and `$DF60`.
- For B:–F:, DPB, directory buffer, CSV, and ALV are at offsets `$10`, `$20`, `$A0`, and `$C0` from the drive's DPH slot.
- SYSTEM/LARGE CSV sizes are 16/32 bytes; their ALVs occupy 31 bytes.
- BIG has no CSV and uses a 64-byte ALV.
- A: LARGE table storage is moved to `$E060` and `$E080` because the normal A: SYSTEM locations are too small.

The firmware constructs these in-memory CP/M structures from the mounted profile at boot. Their DPB fields are listed with the disk profiles below.

## 7. Disk profiles and drive configuration

### Supported profiles

Every image contains 128-byte records. Image geometry is raw, identity-ordered CP/M records; the firmware does not perform sector skew translation.

| Profile | Image size | Geometry | Reserved tracks | Block size | Directory entries | DPB: `SPT, BSH, BLM, EXM, DSM, DRM, AL0, AL1, CKS, OFF` |
| --- | ---: | --- | ---: | ---: | ---: | --- |
| SYSTEM | 256,256 bytes | 77 tracks × 26 records/track | 2 | 1 KiB | 64 | `26, 3, 7, 0, 242, 63, $C0, 0, 16, 2` |
| LARGE | 512,512 bytes | 77 tracks × 52 records/track | 2 | 2 KiB | 128 | `52, 4, 15, 0, 242, 127, $C0, 0, 32, 2` |
| BIG | 8,421,376 bytes | 514 tracks × 128 records/track | 2 | 16 KiB | 512 | `128, 7, 127, 7, 511, 511, $80, 0, 0, 2` |

Field meanings follow the CP/M DPB convention: SPT is 128-byte records per track; BSH and BLM define allocation-block addressing; EXM defines extent mask; DSM is the highest allocation-block number; DRM is the highest directory-entry number; AL0/AL1 reserve directory blocks; CKS is the directory checksum-vector size; OFF is the number of reserved tracks.

SYSTEM, LARGE, and BIG are supported for B:–F:. A: supports SYSTEM or LARGE, never BIG. The default A: entry specifies SYSTEM with profile detection by exact image size, so either the 256,256-byte SYSTEM image or the 512,512-byte LARGE image can boot. An explicit A: LARGE profile requires the LARGE image size. Data drives must match the exact size of their declared profile. Firmware does not format images and does not validate all guest directory/allocation structures when mounting them.

### Drive mapping

The CP/M drive table exposes A: through F:. A: is the read-only LittleFS system image. B:–F: are optional SD images. The checked-in mapping is:

```text
A=/littlefs/cpm80/system.dsk,RO,SYSTEM
B=/retro/images/cpm80/work1.dsk,RW,LARGE
C=/retro/images/cpm80/work2.dsk,RW,BIG
D=/retro/images/cpm80/languages.dsk,RW,BIG
E=/retro/images/cpm80/Utility.dsk,RW,LARGE
```

The configuration file is `/microSD/retro/images/cpm80/drives.cfg`. Each non-comment line has the form:

```text
drive=/path/to/image,RO|RW,SYSTEM|LARGE|BIG
```

The parser accepts A:–F:, blank lines, lines beginning with `#`, and spaces or tabs surrounding fields. It rejects duplicate drive entries, unknown drives, unsupported profiles or modes, malformed or overlong lines, control characters, and unsafe paths. Paths must not contain traversal, repeated separators, backslashes, or path components `.` and `..`.

- A: must be read-only, must use a LittleFS path under `/littlefs/cpm80/`, and cannot use BIG.
- B:–F: must use the SD namespace `/retro/images/cpm80/`; the machine layer maps that namespace to the `/microSD` VFS mount.
- If the configuration is missing, only the built-in A: system image is configured.
- If any configuration content is invalid, the whole file is rejected and the built-in A:-only table is restored; valid partial entries are not retained.
- Optional drives are mounted only when configured, present, exactly the declared size, and openable with the requested access mode. A missing or invalid optional image remains unavailable to CP/M.
- The machine probe validates A:'s supported size and `RETROCPM` header. It does not validate the contents of optional drive directories.

Manager edits to SD images do not alter this mapping. The `RO` flag governs the emulator's access to the image; it does not prevent the separate File Transfer manager from modifying the image while the emulator is stopped.

## 8. Console and terminal contract

The USB Serial/JTAG console is a byte-stream transport. It does not interpret VT100/ANSI, render terminal graphics, or generate terminal sequences on behalf of the guest. Applications emit control bytes and the connected terminal interprets them. The default terminal personality is VT100/ANSI at 80 columns by 24 rows; Minicom on macOS is the documented default workflow.

CP/M input conversion:

- LF is converted to CR.
- CR is returned as CR, and a following LF is suppressed so CR+LF is one guest CR.
- Input is masked to 7-bit ASCII.
- `CONST` may buffer one character until `CONIN` consumes it.

Output bytes from CP/M are passed to the host console. Terminal validation should cover line endings, backspace/delete, control keys, escape-key handling, cursor positioning, screen erase, and reverse video with the intended guest application. Binary file-transfer payloads do not pass through terminal line-ending conversion. Diagnostics must not corrupt guest terminal output.

## 9. Image creation and development tools

These Python tools run on a development computer. They are not run by the ESP32 firmware and do not format the physical SD card.

### Raw CP/M image operations

`tools/diskImage.py --os cpm80` dispatches to the CP/M-80 profile definitions in `tools/include/diskImageCpm80.py` and shared directory/extents logic in `tools/include/diskImageCpm.py`.

- `create` creates an empty image for a selected profile; it refuses to replace an existing image unless `--force` is given.
- `add` imports host files after validating CP/M 8.3 names, duplicate entries, extents, allocation blocks, directory capacity, and disk capacity. It updates the image atomically after validation.
- `list` validates and lists directory and allocation data.
- `extract` exports a named file from a specified user area.

The supported utility operates on raw CP/M images. It does not convert ImageDisk, TeleDisk, or other archival floppy formats. Geometry and sector ordering must be established before using an external image.

### System image construction

`tools/buildDiskImage.py --os cpm80` dispatches to `tools/include/buildDiskImageCpm80.py`. It combines the checked-in CCP and BDOS binaries, utility files, and support files into a deterministic SYSTEM image. It places the directory after the two reserved tracks, uses 32-byte CP/M directory entries, allocates file data in 1 KiB blocks, and rejects invalid names, empty files, directory overflow, or block overflow.

`tools/createSystemDsk.py --os cpm80 --profile SMALL|LARGE` creates a CP/M-80 boot image with the selected boot profile and can add files from a supplied source directory. SMALL selects SYSTEM; SYSTEM and LARGE retain the CCP/BDOS/header offsets described above, with profile-specific exact image sizes.

`tools/prepareSd.py` prepares the SD directory layout. A generated image is copied into `/retro/images/cpm80/` at the path configured in `drives.cfg`; the card must be safely ejected from the development computer before use in the ESP32-S3.

### Optional software importer

`tools/fetchCpm80Software.py` lists links from an HTML index and imports explicitly selected files or ZIP members. It treats ZIP member names as archive data rather than extracting paths onto the host filesystem, limits downloads to 64 MiB, preserves original downloads, and writes a manifest with URL, timestamp, size, SHA-256, selected members, and user-supplied rights evidence. It requires rights evidence for network imports but does not decide whether that evidence authorizes distribution. Known Microsoft archive entries are blocked. Its complete-image copy operation validates image size and CP/M directory/allocation structure but does not establish sector ordering or convert foreign floppy-image formats.

## 10. Bundled software and compatibility

The system image includes these utility files:

`ASM.COM`, `DDT.COM`, `DUMP.COM`, `ED.COM`, `HELP.COM`, `HELP.HLP`, `LIB.COM`, `LINK.COM`, `LOAD.COM`, `MAC.COM`, `PIP.COM`, `RMAC.COM`, `STAT.COM`, `SUBMIT.COM`, `XREF.COM`, `XSUB.COM`, and `ZSID.COM`.

`DIR`, `ERA`, `REN`, `TYPE`, and `USER` are CCP commands, not separate COM files. `WELCOME.TXT` is a `TYPE` fixture. The upstream utility set and its usage scope are documented in `components/cpm80Core/os/utilities/README.md`.

Presence in the image is not proof that every bundled program is compatible with this BIOS or BDOS configuration. For example, compatibility assumptions must be tested against the actual guest, and application provenance and rights must be documented. The design does not require a particular compiler or BASIC implementation.

## 11. Development phases

1. **CPU and guest foundation:** Provide a bounded 64 KiB Z80 adapter, guest lifecycle, host callback interface, cooperative execution, and host tests for CPU memory and port behavior.
2. **CP/M boot and BIOS:** Integrate the genuine CCP/BDOS, construct the memory map and BIOS tables, implement console and 128-byte record services, and verify cold boot, WBOOT, and CCP/BDOS behavior.
3. **Disk formats and drive integration:** Implement SYSTEM, LARGE, and BIG DPBs; validate and parse drive mappings; connect independent LittleFS and SD images through bounded random-access I/O; verify read-only, geometry, and persistence behavior.
4. **Image tooling and validation:** Provide deterministic image-generation/import workflows and test their outputs against the firmware's expected disk geometry and layout.
5. **System acceptance:** Validate real SD media, terminal behavior, representative applications, transfer interruption/recovery, firmware resource usage, and boot/persistence on target ESP32-S3 hardware.

## 12. Known limitations

- The emulator does not implement cycle-accurate Z80 peripheral timing or physical printer, punch, or reader devices. LIST/PUNCH are no-ops, READER returns EOF, and LISTST reports not ready.
- The BIOS uses identity sector translation and this project's raw-image geometry. Interleaved, skewed, or archival floppy formats are not automatically converted.
- A: accepts only SYSTEM and LARGE raw images with the expected size and system header. B:–F: are checked for exact profile size and openability, but their full CP/M directory/allocation structures are not validated at mount time.
- CP/M files are stored in 128-byte records. The filesystem does not encode the exact byte length of the last record, so trailing padding cannot always be distinguished from file content.
- A: is always read-only. Optional drives must be explicitly configured and successfully mounted. Missing or invalid optional drives are unavailable rather than redirected to another drive.
- The physical USB Serial/JTAG console does not itself render ANSI/VT100. Display behavior depends on the terminal program and its configuration.
- Host tests and a successful firmware build do not establish hardware behavior, SD-card compatibility, terminal compatibility, or application compatibility.
- The upstream CCP/BDOS assembly outputs are checked in, but regenerating them requires the upstream assembler/build tools; the exact assembler tool revision used for those binaries is not pinned.
- The system image and optional user images have separate storage and lifecycle requirements. The manager's ability to edit SD images while the emulator is stopped does not imply that LittleFS system resources can be edited through the manager.

## 13. Possible improvements

- Add a CP/M image inspection step that validates directory entries, extent chains, allocation ownership, and disk parameter consistency before an optional drive is mounted.
- Expand host tests for corrupt images, extent edge cases, first/last records, interrupted writes, and recovery from file-system or SD errors.
- Add precise performance and watchdog measurements on the target ESP32-S3 for each image profile and representative workloads.
- Document and test compatibility of specific bundled utilities and representative editors, assemblers, debuggers, and language tools.
- Improve the console workflow with repeatable terminal configuration guidance and a recorded terminal compatibility matrix, without moving terminal emulation into the common byte-stream service.
- Pin and document the assembler toolchain needed to reproduce the CCP/BDOS binaries, and make image generation verify the complete resource set and provenance manifest.
- Add optional explicit console or host-side diagnostics that can be enabled without injecting text into the guest terminal stream.
- Define a portable metadata or archive convention for workflows that need to preserve exact host-file lengths beyond CP/M's record-oriented representation.

## 14. Verification criteria

Host-side tests should exercise the CPU adapter, guest initialization and destruction, genuine CCP/BDOS boot, BIOS call ABI, console conversion, each disk geometry, drive configuration parsing, CP/M commands, disk-record bounds, and read-only rejection. Tests should include first and last valid records, invalid tracks/sectors, missing drives, DMA boundary cases, persistence after close/reopen, and malformed image/configuration cases.

Hardware acceptance should separately verify:

1. The firmware recognizes the intended ESP32-S3 configuration and probes the A: system resource correctly.
2. Cold boot reaches the CP/M prompt; `DIR`, `TYPE`, a transient program, and WBOOT work.
3. A: is read-only; mounted SD drives use the configured profile and RO/RW behavior, with record writes persisting after machine shutdown and restart.
4. Missing and malformed optional images remain unavailable without aliasing to A:.
5. Console input/output, VT100/ANSI sequences, 80×24 terminal behavior, and the intended Minicom workflow behave correctly.
6. Repeated guest entry/exit, long execution, large images, and sustained disk activity do not trigger watchdog failures or resource leaks.

Acceptance records should identify firmware revision, ESP-IDF version, board, terminal, image profile, test commands, expected and actual behavior, and retained evidence. Do not describe host-test or build results as hardware results.
