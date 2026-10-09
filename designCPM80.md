# designCPM80.md — CP/M-80 engineering memory

Revision: 2026-10-04. Status: the user reports that CP/M boots on the ESP32-S3, `DIR` works, and `HELLO` runs. Host tests cover the utility-filled A: image, resident CCP commands, PIP/ERA/REN on writable E:, and BIOS selection/write protection. Mac-side Python tools now create a DPB-matched empty E: image and add local or selected archive files to it. Actual SD-card mounting and hardware drive tests remain outstanding; editor/compiler workflows and compatibility of every bundled utility are not yet verified.

## Provenance and purpose

Reconstructed from the retrieved “ESP32 emulator” conversation and reconciled with its latest HOST-M1 decisions. The previous downloadable attachment itself was unavailable, so this is not a byte-for-byte recovery. Preserved decisions: menu 1, genuine CP/M 2.2 CCP/BDOS, shared external Z80 core, 64 KiB address space, LittleFS A:, config-driven SD B:–F:, BIOS/DPH/DPB design, CPM80-M1 through CPM80-M9 and permanent engineering logs. Later File Transfer and large-image requirements supersede the older USB-transfer and directory proposals.

Deliver a practical software-development CP/M machine: command line, editors, assemblers and language tools. Games and machine-specific graphics hardware are not acceptance targets. CP/M completion precedes UCSD implementation.

## Project-wide contract

`projectPrompt.md` is the authority for host architecture and coding rules. Its latest recovered revision was delivered as `projectPrompt_v2.md`; use the canonical name in the project. These documents specialize that contract, not replace it. Companion documents are `designCPM80.md`, `designCPM86.md`, `designUCSD.md`, `designApple2.md`and `designSWTPC.md`.

The fixed main menu is:

```text
ESP32-S3 Retro Computer
1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800
6. File Transfer
Select system [1-6]:
```

Every selection requires `[Enter]`; a digit alone does nothing. Normalize CR/LF/CRLF so one submitted line is processed once. Invalid or disabled choices explain the reason and stay in the menu. During HOST-M1, emulator choices report `Not implemented yet`; no CPU emulator is implemented in that milestone. Runtime availability later requires a compiled implementation, valid resources and any mandatory media. Distinguish an unimplemented machine from missing or invalid resources.

HOST-M1 owns the machine registry, USB console, menu, LittleFS, SD detection and versioned `/retro/layout.txt` validation, image block storage, exchange service and browser File Transfer. A 32 GB FAT32 SD card is recommended. The manifest schema/version must come from the implemented host contract, not be independently invented by an emulator. Never silently format an unknown card or replace user images. Missing SD or wrong layout must be visible; optional missing media need not prevent an otherwise supported LittleFS-only recovery boot.

Paths beginning `/retro/` are paths on the SD volume, independent of its ESP-IDF VFS mount prefix. LittleFS is mounted at `/littlefs` by HOST-M1. Each resource manifest records format, size, checksum, origin, license, machine profile and read-only policy. Keep minimal boot resources in LittleFS; larger and writable images belong on SD. Do not store an entire disk image in RAM. Use bounded block buffers, checked offset arithmetic, explicit end-of-image checks and bounded resource ownership. Host capacity is not guest filesystem capacity.

File Transfer is a project-wide facility, not a server implemented inside each emulator. Only menu option 6 starts WiFi, using exclusively `michmich/esp-idf-wifi-provisioner` with the recovered `^0.4.0` requirement; resolve and lock the actual tested version in the host dependency lock. The dedicated upload/download/delete webserver starts after networking is available and exposes only `/retro/exchange/` and `/retro/images/` on the physical SD card. The GUI selects machine and transfer type: loose CP/M files go under `/retro/exchange/cpm80/` and are visible to `HOST DIR`, while CP/M disk images go under `/retro/images/cpm80/`. The other machine selections use their matching directory names. The GUI displays each file's full VFS path. Disk-image files are transferred whole, with a basic progress indicator. Traversal, absolute-path escape, malformed names and unintended overwrite must be rejected. Uploads use bounded streaming and temporary files; incomplete uploads must not appear as completed files. Binary data must remain byte-identical. The transfer limit is 4 GiB minus 2 bytes: ESP-IDF's HTTP parser reserves the maximum 32-bit Content-Length value as a sentinel. `[Enter]` exits File Transfer to the main menu; WiFi may then stop. Emulator execution and browser File Transfer are mutually exclusive in the initial design.

A guest transfer utility talks to the common local `hostExchange` service while the emulator runs; it does not start WiFi. The browser moves files between PC/Mac and exchange; the guest utility copies between exchange and the guest filesystem through native guest OS calls. The host must not modify mounted guest filesystems behind the guest's back. Browser access to active disk images is outside this contract.

The earlier conversation did not specify a wire ABI. The common version-1 operation and integrity contract is now defined in `projectPrompt.md` section 15. The CP/M-80 transport mapping, capability query, status values, ports, filename representation, record-length sidecar and overwrite/cancellation behavior are specified below. Each machine adapter remains restricted to its own exchange root and must close handles and preserve existing destinations on failure.

Physical ESP32 reset returns to the host menu and never automatically resumes a guest. Guest warm boot/reset is a separate action. A host-controlled clean exit must flush/close media, reset terminal attributes and release ownership before returning. Do not consume ordinary guest Enter as a host exit command. Sudden power loss or physical reset cannot guarantee guest filesystem consistency; record the durability boundary and test recovery using disposable images.

## Implementation discipline

Use native ESP-IDF, CMake and the VSCode ESP-IDF workflow; no Arduino dependency. Target ESP32-S3 / LOLIN S3 Pro, with board pins, flash/PSRAM settings and USB routing verified against the actual board configuration. All project-owned code, identifiers, documentation and messages are English. Use Allman braces, two-space indentation and lowerCamelCase. Comments use `//— comment` on their own line above the relevant code. Preserve upstream style and licenses in vendored cores; isolate project adapters and keep local patches small and documented.

Pin upstream revisions. Do not write a CPU emulator, replace a guest OS with host-side syscall emulation, or patch binaries randomly until a prompt appears. Keep CPU, machine bus, guest BIOS/device adapter, image backend and terminal transport separable. ESP-IDF task scheduling and watchdog servicing must not alter guest instruction semantics. Allocate/check memory before launch, bound queues, and keep diagnostics out of the guest screen. Measure speed and memory on hardware; neither CPU frequency nor PSRAM size alone proves adequate performance.

## Engineering-memory rules

This document is a living design and development record. Read its decisions and unresolved issues before changing the emulator. Never erase a failed experiment or silently rewrite a previous conclusion. Supersede decisions with a new numbered record. A repeated experiment requires new evidence or a changed variable. Keep design approval, source inspection, desktop testing and ESP32 hardware verification distinct.

For every verification record capture date, firmware commit, upstream core revision, ESP-IDF version, board, terminal, exact command/input, expected result, actual result and evidence path. Only observed execution may be marked PASS. For every issue use this chain:

**Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat.**

Unknown causes stay unknown. A workaround is not a root cause. Store reproducible traces and small fixtures; include the first divergent CPU/bus/disk event when available. Guest test software and ROM redistribution require their own provenance; an open-source emulator license does not cover those assets.

## Machine architecture

```text
menu 1 + Enter → availability check → cpm80Machine
  → external Z80 core → 64 KiB guest memory
  → genuine CCP + BDOS + target BIOS
  → virtual console/disk/exchange I/O → common host services
```

Use `superzazu/z80` as the preserved preferred core. Its upstream repository describes C99, MIT licensing and zexdoc/zexall success; rerun suitable tests on the pinned integration. UCSD must reuse this core component but have independent machine state and boot resources. Do not substitute RunCPM or intercept BDOS to implement a FAT-backed fake CP/M. A small guest BIOS with explicit virtual I/O is permitted; CCP and BDOS remain genuine guest code. [Core source](https://github.com/superzazu/z80).

The selected Z80 source is pinned to upstream commit `d64fe10a2274e5e40019b1086bf7d8990cbc5f23` (MIT, copyright 2019 Nicolas Allemand). The project keeps the upstream `z80.c`, `z80.h` and `LICENSE` unchanged in `components/z80/`; project integration lives in `components/cpm80Core/`. The CPU adapter allocates one 64 KiB address space, delegates port operations to explicit callbacks and executes one instruction per call. The CP/M guest layer implements the virtual BIOS contract below.

The original OEM redistribution disk was inspected but is not used as the target A: image: the 124,686-byte ImageDisk-format archive is not a compatible raw disk. The [2022 license clarification](https://web.archive.org/web/20231219054614/http://www.cpm.z80.de/license.html) reproduces Bryan Sparks' statement on behalf of DRDOS, Inc. granting nonexclusive rights to use, distribute, modify, enhance and otherwise make CP/M and its derivatives available.

The integrated CCP/BDOS sources are from [`brouhaha/cpm22`](https://github.com/brouhaha/cpm22/tree/01018abbccce0bdf4874b0b2ed1a048c5fcc2987), pinned at commit `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`; the license clarification is included in `components/cpm80Core/os/LICENSE.txt`. Project-authored CP/M-80 guest programs, including the HOST transfer utility source and binary, live in `guest/cpm80/`. The disk composer is `tools/buildDiskImageCpm80.py`; it builds the deterministic 256,256-byte `littlefs/cpm80/system.dsk`. The assembler itself is not bundled, so disk-image composition is reproducible from the checked-in assembled outputs; reassembling CCP/BDOS requires the upstream Macro Assembler AS / `p2bin` build tools. The host-wide resource-manifest schema is not yet defined; no machine-specific manifest format is claimed.

## Current implementation status

Implemented and verified:

- The unmodified `superzazu/z80` core is vendored with its MIT license at the pinned commit above.
- `components/cpm80Core/` supplies per-instance 64 KiB guest memory, bounded image loading, memory access, explicit input/output port callbacks, and one-instruction stepping. The adapter requires zero-initialized instances and provides explicit destruction.
- Host unit tests pass for bounded memory loading, both ends of the address space, instruction execution, port input/output, and adapter lifecycle. The test build enables AddressSanitizer and UndefinedBehaviorSanitizer.
- The upstream core's `prelim.com`, `zexdoc.cim`, and `zexall.cim` instruction exercisers pass.
- The complete ESP-IDF 6.0.2 project builds for ESP32-S3 with the CP/M core component included.
- `components/cpm80Core/cpm80Guest.c` installs and runs the genuine CCP and BDOS, CP/M page zero, the 17-entry BIOS jump table and the 8-inch single-sided single-density DPB.
- `main/cpm80Machine.c` probes and opens the immutable LittleFS A: image, loads optional B:–F: mappings from `drives.cfg`, connects USB console and disk-record callbacks, and schedules guest execution cooperatively.
- Host tests run the guest CCP/BDOS on the Z80 core through cold boot, `DIR`, `TYPE`, `USER`, `PIP`, `REN`, `ERA`, `HELLO.COM`, and WBOOT. They verify utility directory entries, copying a file to E:, separate drive DPHs, read-only A: writes, invalid-drive selection and a rejected out-of-range DMA.
- The ESP-IDF 6.0.2 ESP32-S3 firmware build completes with the LittleFS image; the image is 0xDD9E0 bytes, below the 3 MiB app partition.

The user has reported successful CP/M boot and command execution on their ESP32-S3: `A>` appears, `DIR` works, and `HELLO` runs. The configuration parser, B:–F: DPH/DPB setup and host-side LARGE image I/O are tested, but SD-drive support, writable media and application compatibility have not been tested on hardware.

Guest addresses span `$0000–$FFFF`. The assembled binary lengths and observed load addresses establish this 64 KiB layout:

| Guest range | Purpose |
|---|---|
| `$0000–$0002` | Page-zero warm-boot jump to BIOS WBOOT at `$DA03` |
| `$0003` | I/O byte, initialized to zero |
| `$0004` | Current drive, initialized to A: |
| `$0005–$0007` | BDOS jump to `$CC06` |
| `$005C–$007F` | Standard default FCB region |
| `$0080–$00FF` | 128-byte default DMA |
| `$0100–$C3FF` | Transient program area |
| `$C400–$CBFF` | Genuine CCP, 2,048 bytes; CCP initializes its stack |
| `$CC00–$D9FF` | Genuine BDOS, 3,584 bytes |
| `$DA00–$DA32` | BIOS jump table, 17 entries × 3 bytes |
| `$DA33–$DA87` | Guest BIOS service stubs issuing `OUT ($FE),A` |
| `$DA90–$DA9F` | A: DPH |
| `$DAA0–$DAAE` | A: DPB |
| `$DAB0–$DB2F` | A: 128-byte directory buffer |
| `$DB30–$DB3F` | A: 16-byte CSV |
| `$DB40–$DB5E` | A: 31-byte allocation vector |
| `$DB60–$DFBE` | B:–F: DPH, separate DPB, directory buffer, CSV and ALV, with a `$E0`-byte per-drive stride |
| `$DFBF–$FFFF` | Unused by this BIOS profile |

Each DPH has no translation table and points to its own DPB, directory buffer, CSV and ALV. The 128-byte default DMA and transient program entry at `$0100` are retained. The BIOS service port is project-defined and is not a physical hardware port.

The A: image is 77 tracks × 26 logical 128-byte records, two reserved tracks, 1 KiB allocation blocks, 64 directory entries, and read-only. Its DPB is SPT=26, BSH=3, BLM=7, EXM=0, DSM=242, DRM=63, AL0=`$C0`, AL1=0, CKS=16, OFF=2. The disk builder places the directory at absolute track 2, `HELLO.COM` in allocation block 2, and the included utilities in validated CP/M extents/blocks. All disk offsets are bounded to the image; the builder enforces the 243-block DPB capacity. The profile passes host tests, and the user reports that it boots and runs the smoke-test commands on the ESP32-S3. Its release-specific manual audit and full hardware acceptance remain open.

### Startup and restart

1. The registry loads `/microSD/retro/images/cpm80/drives.cfg`, if present, and probes the configured A: system image for its exact size and image signature. Without a valid config, A: falls back to `/littlefs/cpm80/system.dsk` and B:–F: are unavailable.
2. Initialization reads CCP/BDOS from A:, initializes the CPU and mounts each configured B:–F: image only if its declared profile size and access mode validate.
3. Cold BOOT initializes page zero and transfers control to the genuine CCP.
4. WBOOT reinstalls CCP, BDOS and BIOS state and returns to CCP, not the host menu.
5. Physical ESP32 reset re-enters the main menu.

Do not accept printing `A>` from host code as a successful boot. The probe validates exact size and signature; error logs identify the resource path and expected/actual size. The common host resource-manifest schema remains pending.

### BIOS contract

Implement the CP/M 2.2 jump-table order and calling conventions for BOOT, WBOOT, CONST, CONIN, CONOUT, LIST, PUNCH, READER, HOME, SELDSK, SETTRK, SETSEC, SETDMA, READ, WRITE, LISTST and SECTRAN. Keep unsupported peripheral behavior explicit and deterministic. CONST is nonblocking; CONIN waits cooperatively. Disk errors must return the documented failure status. An invalid drive must not alias A:.

The BIOS owns guest DPH/DPB structures; the host owns image byte I/O. BIOS functions use the CP/M 2.2 order and a guest `OUT ($FE),A` service stub, where A is the function index. READ uses bounded CP/M logical 128-byte records with zero-based track/sector offsets into the declared raw image. SECTRAN is identity because the DPH translation pointer is zero. WRITE fails for drives configured read-only and writes/flushed records for drives configured read/write. LIST/PUNCH are deterministic no-ops and READER returns EOF (`$1A`). CONST is nonblocking, while CONIN waits cooperatively. Each drive has its own DPH, DPB, directory buffer, CSV and ALV. SELDSK returns no DPH for unavailable or out-of-range drives, and a rejected selection does not change the currently selected drive. Host tests cover BIOS drive selection, both DPBs, CCP commands and a PIP copy to a writable drive. Track/sector conventions, all status paths and write behavior need the release-specific Digital Research BIOS manual audit; the [BIOS index](https://www.seasip.info/Cpm/bios.html) is a navigation aid, not a substitute.

### Drives and capacity

| Drive | Storage | Role | Initial access |
|---|---|---|---|
| A: | LittleFS minimal `cpm80/system.dsk` | Boot, CCP/BDOS/BIOS and essential commands | RO |
| B: | Configured in `drives.cfg` | Language tools | RO, optional |
| C: | Configured in `drives.cfg` | Assemblers/editors/utilities | RO, optional |
| D: | Configured in `drives.cfg` | Utilities | RO, optional |
| E: | Configured in `drives.cfg` | Source, builds, results | RW, optional |
| F: | Configured in `drives.cfg` | Archive | RW, optional |

`/retro/images/cpm80/drives.cfg` is a UTF-8/ASCII line-oriented table with one entry per configured drive:

```text
A=/littlefs/cpm80/system.dsk,RO,SYSTEM
B=/retro/images/cpm80/languages.dsk,RO,LARGE
C=/retro/images/cpm80/tools.dsk,RO,LARGE
D=/retro/images/cpm80/utilities.dsk,RO,LARGE
E=/retro/images/cpm80/work.dsk,RW,LARGE
F=/retro/images/cpm80/archive.dsk,RW,LARGE
```

Each row is `drive=path,RO|RW,SYSTEM|LARGE`; A: uses a LittleFS path, and SD image paths use the `/retro` volume namespace. The host maps `/retro/...` to its `/microSD/retro/...` VFS mount. Duplicate drives, unsupported profiles, invalid paths, and profile/access mismatches invalidate the entire file. A malformed or missing file leaves the built-in read-only A: image available and disables B:–F:. A configured A: entry may select another validated system image under `/littlefs/cpm80/`. A: accepts SYSTEM or LARGE (never BIG) and is always read-only. With `A=...,RO,SYSTEM` the firmware picks SYSTEM or LARGE from the file size (256,256 or 512,512 bytes); an explicit `LARGE` on A: is strict and must match the file size. Any other size is logged as an error and A: is not mounted. B:–F: may each use either profile: SYSTEM (256,256-byte standard image) or LARGE (512,512 bytes); the image size must match the declared profile. Data images must be under `/retro/images/cpm80/`; their names are not compiled into firmware. `prepareSd.py` writes the example only if no config exists and does not overwrite user edits.

`SYSTEM` is the 256,256-byte boot profile: 77 tracks × 26 logical 128-byte sectors, two reserved tracks, 1 KiB allocation blocks, 64 directory entries and the system DPB above. A: also requires its RETROCPM header. `LARGE` is a project-defined 512,512-byte profile: 77 tracks × 52 logical 128-byte sectors, two reserved tracks, 2 KiB allocation blocks, 128 directory entries and DPB SPT=52, BSH=4, BLM=15, EXM=0, DSM=242, DRM=127, AL0=`$C0`, AL1=0, CKS=32, OFF=2. The firmware rejects wrong-size images and applies the configured access mode. A LARGE A: keeps CCP/BDOS at image offset 0 and the RETROCPM header at `0x0E10`, exactly like SYSTEM; only its CSV (32 bytes) and ALV for drive A: are relocated to `0xE060`/`0xE080`, because the fixed SYSTEM slots at `0xDB30`/`0xDB40` are too small for the LARGE CSV. SD image creation remains a host-tool operation.

A: remains read-only. The system disk's `SUBMIT.COM` build expects `$$$.SUB` on A:, so batch submission cannot write its required scratch file with the current access policy.

#### Preparing E: on macOS

`tools/diskImageCpm80.py` creates a blank, preformatted CP/M filesystem image; it does not partition or format the physical SD card. On a Mac, first use `tools/prepareSd.py` on an already FAT32-formatted card to create the expected `/retro/images/cpm80/` directory tree and a default `drives.cfg`. Replace `/Volumes/SDCARD` in the examples with the card's actual mounted volume name:

```sh
python3 tools/prepareSd.py /Volumes/SDCARD
python3 tools/diskImageCpm80.py create --profile LARGE ~/Desktop/work.dsk
python3 tools/diskImageCpm80.py add ~/Desktop/work.dsk /path/to/MBASIC.COM
python3 tools/diskImageCpm80.py list ~/Desktop/work.dsk
cp ~/Desktop/work.dsk /Volumes/SDCARD/retro/images/cpm80/work.dsk
```

The resulting `LARGE` image is exactly 512,512 bytes and uses 77 tracks, 52 128-byte records per track, two reserved tracks, 2 KiB allocation blocks, a 128-entry directory and CP/M allocation block numbers 0–242. Omit `--profile LARGE` to create the 256,256-byte `SYSTEM` profile instead. `create` refuses to replace an existing file unless `--force` is supplied. `add` accepts local files whose host basenames meet the CP/M 8.3 filename restrictions; `--name MBASIC.COM` can assign a CP/M name when the host filename differs. Additions are validated and written atomically: invalid names, duplicate names, full directories and disk-full requests leave the original image unchanged. Use `list` to check contents before copying the image.

Copy the completed `work.dsk` to the path configured for E: in `drives.cfg`, eject it safely in macOS, and then install the card in the ESP32-S3. At the CP/M prompt, enter `E:` and run `DIR`; then run `PIP E:TEST.TXT=A:WELCOME.TXT` to check that E: accepts writes. Firmware mounts the image only if its profile size matches and the host file opens with the configured access mode. Do not use `PIP A:...=...` for a write test: A: is intentionally read-only. `STAT` may display R/W based on CP/M's in-memory protection vector and does not prove that a host file is writable; the BIOS and host file mode enforce actual write protection.

#### Importing software or disk images

`tools/fetchCpm80Software.py list INDEX_URL` displays links and adjacent descriptions on an HTML archive index. Its `add` command downloads one explicitly selected file or explicitly selected ZIP members, adds those files to an existing matching image, preserves the original download unchanged, and records the source/index URLs, fetch timestamp, sizes, selected member names, SHA-256 and supplied rights reference in `cpm-resource-manifest.json`. ZIP member paths are read as archive data and are never extracted to the host filesystem. For example:

```sh
python3 tools/fetchCpm80Software.py list http://cpmarchives.classiccmp.org/cpm/mirrors/www.retroarchive.org/cpm/lang/lang.htm
python3 tools/fetchCpm80Software.py add INDEX_URL --link SLR180.ZIP --member SLR180.COM \
  --image ~/Desktop/work.dsk --archive-dir ~/Desktop/cpm-archives \
  --rights-evidence "URL or citation for the terms applicable to this specific package"
```

The archive's presence is not evidence of a license. The importing commands require a user-supplied rights reference, but do not evaluate it. They block the known Microsoft listings, including `Mbasic.com`; MBASIC 5.21 must be supplied as a local file for which the user has valid rights and added with `diskImageCpm80.py add`. Do not download or redistribute proprietary software based only on an archive listing.

The `disk` subcommand can copy a selected complete raw CP/M image to a new destination only when its size equals the target image size and its directory/allocation structure passes validation; `--force` is required to replace an existing destination. This structural check does not prove a raw image's sector ordering or disk-profile agreement, so check its accompanying documentation before use. The utilities do not convert ImageDisk `.IMD`, TeleDisk `.TD0`, or other archival floppy formats. Do not rename those files to `.DSK` or mount them as raw images. All archive and image operations are workstation-side; no network download or SD-card operation occurs on the ESP32.

Each disk profile must define image byte length, sector ordering, logical SPT, BSH, BLM, EXM, DSM, DRM, AL0/AL1, CKS and OFF; corresponding DPH pointers and buffer lengths must agree. Derive capacity from the actual DPB and reserved tracks. Validate directory allocation, extent encoding and allocation-vector storage together. Document a matching host image-creation recipe and disk definition. Do not recycle a DPB from another image just because its size matches.

Larger virtual disks must stay within genuine CP/M 2.2 semantics. Earlier conversational examples of 32–64 MB CP/M volumes were not validated and are not requirements. The initial `LARGE` profile is now implemented at 512,512 bytes using 77 tracks × 52 logical sectors and 2 KiB allocation blocks. Host tests cover its DPB and last-record write boundary, but the profile still needs a release-manual audit, independent formatter comparison, and ESP32/application verification before claiming broad compatibility. Use multiple drives when a larger single filesystem cannot be represented safely.

### Terminal and guest tools

The host provides an 80×24 ANSI/VT100-compatible USB terminal path. CP/M console output is a byte stream; applications must be configured for the corresponding terminal personality. Test CR/LF, backspace/delete, Ctrl-C, Ctrl-S/Ctrl-Q policy, escape keys, cursor addressing, erase and reverse video using the chosen editor. Do not insert line endings into binary file data. Keep debug logs on a separate sink or in bounded buffers.

A representative software set includes ASM/DDT, PIP/STAT and at least one editor and language toolchain selected from available licensed resources (for example BDS C or Microsoft BASIC-80/MBASIC). Candidate places to research include the [Unofficial CP/M Web Site binary archive](https://www.cpm.z80.de/binary.html) and [RetroArchive's CP/M collection](https://www.retroarchive.org/cpm/). These are discovery sources, not blanket permissions: check the license or redistribution terms for each individual compiler, BASIC implementation, utility and disk image before downloading, modifying, or sharing it. The CP/M redistribution clarification does not automatically license unrelated applications.

The current A: image includes the DRI CP/M 2.2 command set from the pinned [RomWBW](https://github.com/wwarthen/RomWBW) utility collection: ASM, DDT, DUMP, ED, HELP, LIB, LINK, LOAD, MAC, PIP, RMAC, STAT, SUBMIT, XREF, XSUB and ZSID, plus HELP.HLP. CCP-resident `ERA`, `REN`, `TYPE`, `USER` and `DIR` are not separate COM files; `WELCOME.TXT` demonstrates `TYPE`. There is no separate `SDIR.COM` or `SHOW.COM` in this CP/M 2.2 set: use resident `DIR` for directory listing and `STAT` for disk status. See `components/cpm80Core/os/utilities/README.md` for source pin, compatibility notes and the scope of the DRI non-commercial utility use. The patched `SUBMIT.COM` targets A: for `$$$.SUB`; it remains present but cannot perform submission on read-only A: until replaced with a compatible variant.

Keep each download unchanged as an archival original and record its source URL, package/version, date and license evidence. `fetchCpm80Software.py` records this provenance for resources imported through it; manually obtained files added with `diskImageCpm80.py` must be documented separately. Identify the container and disk geometry before conversion. A `.IMD` or `.TD0` archival floppy image is not a raw image and must not be renamed to `.dsk`; inspect/convert it with a tool that supports its format, preserving an original copy. For loose `.COM`, `.BAS` or compiler files, use `diskImageCpm80.py add` to place the file in an image matching this BIOS profile, rather than copying files directly into a disk-image file. The [cpm80tools project](https://github.com/lipro-cpm4l/cpmtools) remains an alternative candidate, but its disk definition must match the target BIOS profile exactly.

The firmware loads optional SD-backed B:–F: images from `/retro/images/cpm80/drives.cfg`. To use prepared software, make sure the selected profile and access mode match the image, then select the configured drive in CP/M. This path is host-tested but still requires verification with a real SD card and ESP32-S3; successful host tests and image parsing are not hardware acceptance. Preserve exact versions, licensing, source, geometry and terminal patches in the resource documentation. Availability in an archive or a structurally valid image is not proof that a program runs on this machine.

## Guest exchange utility

Provide `HOST.COM`, not a replacement named `PIP.COM`. Version 1 accepts:

```text
A>HOST DIR
A>HOST GET HELLO.C
A>HOST PUT RESULT.TXT
A>HOST GET *.C
A>HOST PUT HELLO?.TXT
```

`DIR` lists files in `/retro/exchange/cpm80/`. `GET` creates the named file on the current CP/M drive in USER 0; `PUT` sends that file. GET and PUT accept CP/M `*` and `?` wildcards and process each matching file; PUT collects up to 128 distinct matches before transferring so BDOS directory searches are not disturbed. `.HST` sidecar files are excluded from wildcard transfers. A local GET collision or a rejected PUT destination is reported and the remaining matches are attempted; a stream/checksum failure aborts the batch. The utility uses BDOS for every guest file operation. A: is read-only, so select a writable drive such as E: before `GET`, then run the utility from A: (for example `A:HOST GET HELLO.C`). `PUT` reads from the current drive, and HOST returns to the drive it was started from. `DIR` works in every user area, while `GET` and `PUT` clearly reject USER 1–15. WiFi is not required.

CP/M v1 uses data port `0xF8` and abort port `0xF9`. The data port is a full-duplex byte stream; multi-byte integers are unsigned little-endian. `QUERY` is command `0`; its reply is status, version `1`, and capability bits `0x07` (DIR, GET and PUT). The remaining commands are `1=DIR`, `2=GET`, `3=PUT`, and `4=PUT-overwrite` (capability bits unchanged). Filenames are the CP/M 11-byte 8+3 FCB representation, uppercase, with space padding. The host accepts only flat ASCII names containing letters, digits, `_` or `-`, and confines all access to `/retro/exchange/cpm80/`.

`DIR` returns status, NUL-terminated printable names, an empty-string terminator, and final status. `GET` sends a filename and receives status, exact byte length, that many bytes, CRC-32, and final status. `PUT` sends a filename, exact length and expected CRC-32, receives an initial status, sends exactly that many bytes, then receives final status. CRC-32 uses polynomial `0xEDB88320` with initial/final XOR `0xFFFFFFFF`. Status values are `0=success`, `1=invalid name`, `2=unavailable/not found`, `3=target exists`, `4=I/O or checksum error`, and `5=busy`. `OUT (0xF9),0` aborts an active transfer; an interrupted guest session also closes the service and removes its temporary file.

PUT never replaces an existing host file unless the guest uses `HOST PUT name.ext O`, which sends command `4` (PUT-overwrite); the old file is then removed only after the byte count and CRC-32 of the new data are verified. The host streams to a private temporary file and exposes the target only after the byte count and CRC-32 match. GET and PUT stream one byte at a time through the port; neither operation buffers a whole host file.

CP/M 2.2 file sizes are record-oriented; HOST never trims `$1A` or zero bytes heuristically. The `NAME.HST` sidecar preserves an imported file's exact length only while the padded-record checksum still matches; absent or invalid metadata makes PUT explicitly transfer full 128-byte records. Text newline/EOF conversion is not performed. `HOST DIR` works in every user area; `HOST GET` and `HOST PUT` are restricted to USER 0 because the flat host exchange directory has no collision-free mapping for CP/M user areas.

HOST stores an adjacent `NAME.HST` sidecar for files received with GET. GET refuses to proceed if either the destination or its sidecar already exists, and failure cleanup removes only files created by that GET. `HOST GET name.ext O` (also `HOST GET *.* O`) instead deletes an existing destination and its sidecar once the host has accepted the transfer, then creates them anew; the option is also accepted for PUT (`HOST PUT name.ext O`) and a read-only (R/O) existing file is not handled specially. HOST prints one `.` per 16 records (2 KiB) while a file is transferred, and a full CP/M drive is reported as `disk full` or `directory full` (the partial file and its sidecar are removed) instead of a generic I/O error. The sidecar's first 16 bytes begin `HST1`, followed by exact byte length, CRC-32 of the padded CP/M records, and CRC-32 of the exact payload (all 32-bit little-endian); CP/M stores it in one 128-byte record. The guest uses the saved length only when the padded-record checksum still matches; edits invalidate the metadata, and PUT then exports all complete 128-byte records. The sidecar is CP/M-visible and reserved for HOST; do not edit or remove it independently. CRC-32 detects accidental changes but is not a cryptographic identity guarantee.

## Milestones and acceptance gates

The genuine CP/M guest boot, A: disk profile and BIOS pass the host guest tests; the ESP32-S3 firmware build succeeds. Hardware execution and the remaining machine gates are still open. The immediate next milestone is to verify this image on the ESP32-S3.

| ID | Deliverable and exit evidence |
|---|---|
| CPM80-M1 | HOST-M1 accepted; core, reference machine and image provenance pinned; host guest boots genuine CP/M; user reports hardware A: smoke test pass |
| CPM80-M2 | External Z80 integrated; CPU tests, 64 KiB memory/port and genuine guest boot tests recorded |
| CPM80-M3 | BIOS ABI, memory map, DPH/DPB and loader host-validated; release-manual and hardware audit open |
| CPM80-M4 | Host-tested LittleFS A: reaches genuine A>; DIR, transient command and warm boot work; ESP32 test open |
| CPM80-M5 | Host-tested config-driven B:–F: mapping and LARGE profile are implemented; test real SD images on ESP32-S3, then qualify missing media, RO/RW errors, persistence and hardware write recovery |
| CPM80-M6 | 80×24 editor plus assemble/compile/run workflow works on ESP32 |
| CPM80-M7 | Large-disk profile justified; first/last records, extents, directory-full and disk-full tested |
| CPM80-M8 | Host-tested HOST.COM import/export and browser staging; exact-length sidecar handling is verified on the host; ESP32 transfer testing remains open |
| CPM80-M9 | Fault handling, repeated resets, memory/performance measurement, reproducible build and completed logs |

Definition of Done: every gate has evidence, no unresolved data-corruption issue, genuine guest applications execute, media persist through clean restart, invalid media fail safely and all deployment resources are reproducible. Only then advance to UCSD. A desktop pass does not close ESP32 verification.

## Decision log

| ID | Status | Decision and rationale |
|---|---|---|
| CPM80-DEC-001 | Preserved | Genuine CP/M 2.2 CCP/BDOS, external Z80, 64 KiB; retain guest software semantics |
| CPM80-DEC-002 | Preserved | Minimal RO A: in LittleFS; software/work images on SD |
| CPM80-DEC-003 | Updated | Shared HOST-M1 File Transfer and local guest bridge supersede older USB-transfer proposals |
| CPM80-DEC-004 | Required | Guest-compatible DPB controls maximum size; 32/64 MB examples are not accepted geometries |
| CPM80-DEC-005 | Accepted | HOST preserves exact imported length in a guest-side `NAME.HST` record and validates the padded CP/M records before using it; stale or missing metadata exports full records |
| CPM80-DEC-006 | Selected | Pin `superzazu/z80` at `d64fe10a2274e5e40019b1086bf7d8990cbc5f23` under MIT; retain the unmodified shared core and keep the CP/M adapter separate |
| CPM80-DEC-007 | Candidate | Evaluate the licensed CP/M 2.2 OEM redistribution disk (`cpm22red.zip`); do not treat the 8-inch ImageDisk archive as a target-ready system disk |
| CPM80-DEC-008 | Confirmed by user report | CP/M reaches `A>` on the user's ESP32-S3; `DIR` and `HELLO` work. Record as user-observed hardware smoke test, not as independently reproduced full acceptance |
| CPM80-DEC-009 | Superseded by CPM80-DEC-010 | The initial host implementation mapped optional B:–E: images to fixed filenames. The configuration-driven B:–F: design below replaces that filename map; hardware validation remains pending |
| CPM80-DEC-010 | Selected | Use `/retro/images/cpm80/drives.cfg` for configured A:–F: paths, RO/RW policy and SYSTEM/LARGE profiles. Keep a built-in LittleFS A: fallback when SD/config is unavailable. The initial larger profile is 512,512 bytes; broader compatibility remains unclaimed until manual, formatter and hardware tests pass |

## Issue log

| ID | State | Problem / next evidence / do not repeat |
|---|---|---|
| CPM80-ISSUE-001 | HOST PASS; HARDWARE OPEN | The OEM archive was inspected and rejected as an incompatible raw target image. Genuine CCP/BDOS source and binaries are pinned; the target BIOS and A: image pass host boot tests. Complete the manual audit and test on ESP32; do not install the ImageDisk archive as a raw disk image |
| CPM80-ISSUE-002 | LARGE PROFILE HOST-TESTED; MANUAL/HARDWARE OPEN | Implemented the 512,512-byte LARGE DPB and tool profile. Audit its CP/M 2.2 field values against the release manual and a separate formatter, and test it on ESP32; do not infer larger capacity from host FAT32 |
| CPM80-ISSUE-003 | HOST DIR/GET/PUT PASS; HARDWARE/FAULT ACCEPTANCE OPEN | CP/M version-1 uses ports `0xF8`/`0xF9`, QUERY/DIR/GET/PUT, exact length and CRC-32, and a validated `NAME.HST` sidecar. Genuine guest tests now cover DIR, multi-record exact-byte GET/PUT, existing-target refusal, and pre-existing-sidecar preservation. Still test USER-area rejection, disk-full, abort, malformed/corrupt metadata, and the SD path on ESP32. Never strip trailing bytes heuristically |
| CPM80-ISSUE-004 | OPEN | Verify editor terminal personality and physical USB behavior; no unmeasured speed promises |
| CPM80-ISSUE-005 | CONFIG/PATH AND BIOS HOST-TESTED; HARDWARE OPEN | B:–F: mappings now come from `drives.cfg`; wrong-profile sizes are rejected, and the guest uses per-drive DPBs. Validate actual SD images, directory listing, transient loading, first/last record reads and configured read-only failures on ESP32-S3 |
| CPM80-ISSUE-006 | OPEN | Select individually licensed compiler/BASIC packages and record source, version and terms; do not assume CP/M OS rights cover application software |
| CPM80-ISSUE-007 | OPEN | The bundled PIP displayed `PIP?` for a host-test attempt to copy `HELLO.COM` to F:, although F: selection, directory read and direct last-record BIOS write pass. Determine whether this PIP build limits drive letters or whether a BDOS/profile interaction remains; do not claim PIP-to-F compatibility yet |

For CPM80-ISSUE-001 the observed sequence was: reference behaviour—genuine CCP/BDOS should present `A>` and execute internal `DIR` and a transient command; hypothesis—the pinned assembled CCP/BDOS and 8-inch SSSD profile can run with the new BIOS; experiment—run the real guest under the vendored Z80, with disk and console callbacks; result—`A>`, `DIR`, `HELLO.COM`, and WBOOT passed in the host suite; conclusion—the host BIOS/image contract is sufficient for this test; root cause of the earlier missing boot was the absent guest/BIOS/resource integration; fix—add the guest BIOS, fixed read-only system image, and registry integration; regression verification—CPM80-VERIFY-007; do not repeat—do not treat the archived ImageDisk file as the target raw image or claim hardware acceptance from a host run.

For every other issue append the full engineering-memory chain, evidence and regression record before closing it.

## Verification log

| ID | Status | Required evidence |
|---|---|---|
| CPM80-VERIFY-001 | SOURCE-REVIEW | On 2026-10-04, inspected `superzazu/z80` README, API and MIT license at commit `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`; inspected the CP/M OEM candidate listing and the 2022 rights clarification. Neither source review nor upstream rights statement is an ESP32 test result |
| CPM80-VERIFY-002 | HOST PASS; ESP32 NOT RUN | macOS host, based on workspace commit `7acf63f3fcf473bb438c4c0dff154fa29afe466f`: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`; additionally, in the pinned upstream checkout, `make && ./z80_tests` passed preliminary, zexdoc and zexall suites. The new adapter test covers full 16-bit memory endpoints, bounded loading, instruction execution and port callbacks. No board, terminal or CP/M resource was used; the firmware changes are uncommitted, and this does not close ESP32 CPU verification |
| CPM80-VERIFY-003 | HOST PASS; USER-REPORTED HARDWARE SMOKE TEST PASS | Cold boot, warm boot, `DIR` and transient command pass under the host Z80 test. On 2026-10-04 the user reported that CP/M boots on their ESP32-S3, `DIR` works, and `HELLO` runs. Board revision, firmware commit, ESP-IDF version, terminal, and captured serial log were not supplied; reproduce and record these details for a complete hardware verification |
| CPM80-VERIFY-004 | PARTIAL HOST PASS; ESP32 NOT RUN | DPB pointers/fields, invalid drive selection and out-of-range DMA are tested; full disk boundary and media-failure matrix remains |
| CPM80-VERIFY-005 | HOST TRANSFERS PASS; EDITOR/COMPILER AND ESP32 NOT RUN | Exact-byte guest transfers are recorded in CPM80-VERIFY-012; editor/compiler and physical ESP32 transfer validation remain open |
| CPM80-VERIFY-006 | NOT RUN | Persistence, storage fault recovery, repeated launch/exit and resource budget |
| CPM80-VERIFY-007 | HOST PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-04, macOS host, project Z80 commit `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, CP/M source commit `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`, ESP-IDF 6.0.2, board/terminal not used: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure` passed under ASan/UBSan. It executed genuine CCP/BDOS cold boot, `DIR`, `HELLO.COM` output and WBOOT, plus page-zero/DPB and invalid-drive/DMA checks. A subsequent ESP-IDF `build` succeeded; `retroHost.bin` was 0xDD9E0 bytes. No flash, physical terminal, throughput, or hardware boot test was performed |
| CPM80-VERIFY-008 | USER-REPORTED HARDWARE SMOKE TEST PASS; DETAILS INCOMPLETE | On 2026-10-04 the user reported: boot reaches CP/M, `DIR` works and `HELLO` runs on the ESP32-S3. Expected result was `A>` followed by the requested commands completing; actual result was reported as successful. Board model/revision, firmware commit, ESP-IDF version, terminal, exact input transcript and evidence path were not supplied. Do not infer B:–E: support, writable media or full application compatibility from this smoke test |
| CPM80-VERIFY-009 | HOST PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-04, macOS host and ESP-IDF 6.0.2 build: the host test selects B: through the BIOS, runs the genuine CCP's `B:` and `DIR` commands using the A: image as a fixture, verifies separate DPH buffer pointers for B:–E:, selects E:, and confirms unavailable C: and out-of-range F: are rejected without changing the selected drive. No actual SD image, SD card, board or terminal was used; B:–E: hardware operation remains open |
| CPM80-VERIFY-010 | HOST PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-04, macOS host: regenerated `system.dsk` with 19 named files across 20 extents and 147 allocated blocks; image size/header and source-file reconstruction were checked. ASan/UBSan host suite passed genuine CCP `DIR`, `TYPE`, `USER`, PIP copy to E:, and `REN`/`ERA` on E:, along with A: write protection and drive selection. The ESP-IDF 6.0.2 ESP32-S3 build passed (`retroHost.bin` 0xDDE90 bytes). See `docs/verification.md`. No SD card, board, or terminal was used; `HELP.COM` compatibility and SUBMIT's read-only-A scratch-file constraint remain open |
| CPM80-VERIFY-011 | HOST PASS; SD/HARDWARE NOT RUN | 2026-10-04, macOS host: Python tools create the exact 256,256-byte empty CP/M image, add local files into CP/M extents, reject invalid names, duplicates and disk-full writes without damaging the existing image, and validate selected disk-image geometry. Archive tooling lists the specified RetroArchive index, selects same-host links, reads explicitly chosen ZIP members without extracting paths, preserves original downloads, and records source/license evidence. Microsoft archive entries are blocked; no proprietary binary was downloaded. Thirteen Python unit tests passed. No SD card or hardware was used |
| CPM80-VERIFY-012 | HOST PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-04, macOS host, ESP-IDF 6.0.2 / ESP32-S3: rebuilt `HOST.COM` with z80asm 1.8 and confirmed byte-identical output. Regenerated the 256,256-byte `system.dsk`. `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure` passed under ASan/UBSan. The genuine CCP/BDOS test ran HOST DIR, 777-byte binary GET/PUT with exact round trip, rejected an existing target, and rejected a pre-existing `.HST` sidecar while confirming the sidecar remained on E:. ESP-IDF `build` passed; `retroHost.bin` is 0xDEA00 bytes, within the 3 MiB app partition. No board, SD card or terminal was used; nothing was flashed |
| CPM80-VERIFY-013 | HOST PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-05, macOS host, ESP-IDF 6.0.2: `cmake --build build/host-tests --parallel && ctest --test-dir build/host-tests --output-on-failure` passed under ASan/UBSan. Coverage includes drives.cfg parsing, LittleFS A: fallback, traversal rejection, separate SYSTEM/LARGE DPBs, F: selection, a genuine CCP `F:`/`DIR` scan and a direct write to the final LARGE-profile record. Fifteen Python unit tests passed, including LARGE image creation/addition and prepareSd config preservation. ESP-IDF `build` passed with `retroHost.bin` size 0xE0490 bytes. Exploratory `PIP F:=A:HELLO.COM` printed `PIP?`, so PIP compatibility on F: remains open. No board, SD card or terminal was used; nothing was flashed |
| CPM80-VERIFY-014 | HOST WILDCARD TRANSFERS PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-07, macOS host, ESP-IDF 6.0.2: aligned CP/M-80 `HOST.COM` user-visible GET/PUT behaviour with the CP/M-86 utility, including wildcard result lines, no-match/list-full handling, reserved `.HST` reporting, and completion wording while preserving CP/M-80-specific local-I/O text. The genuine CCP/BDOS test imports multiple exchange files with `HOST GET *.*`, handles an existing local target while continuing to the other match, exercises `HOST GET INP?T.BIN`, rejects a pre-existing `.HST` sidecar, selects local files for `HOST PUT *.BIN`, continues after a host destination collision, and checks exact bytes for the successful 257-byte transfer; `HOST PUT INP?T.BIN` also exercises single-character matching. `cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure` passed under ASan/UBSan; `PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_diskImage.py' -v` passed all 7 tests. Reassembled `HOST.COM` is 6,950 bytes; `system.dsk` is 256,256 bytes. ESP-IDF `build` passed (`retroHost.bin` 0xE8410 bytes). No board, SD card or terminal was used; nothing was flashed |
| CPM80-VERIFY-015 | LARGE A: HOST PASS; FIRMWARE BUILD PASS; HARDWARE NOT RUN | 2026-10-08, macOS host, ESP-IDF 6.0.2: `tools/createSystemDsk.py --os cpm80 --profile LARGE` image (512,512 bytes) boots as A: on the host guest to `A>`, `DIR` lists HOST/ASM/ZSID/WELCOME and `TYPE WELCOME.TXT` works. A:'s profile is detected from the file size (256,256 / 512,512; other sizes are rejected with an error and A: is not mounted). `ctest` hostCore passes; `idf.py build` passes (not flashed). |
| CPM80-VERIFY-016 | LARGE-FILE HOST / ON-EMULATOR BUILD PASS; HARDWARE NOT RUN | 2026-10-08, macOS host: HOST now reports `disk full` / `directory full` instead of a generic transfer error when the CP/M drive fills up, prints a dot per 16 records during GET and PUT, and accepts `O` on PUT (exchange command 4). `guest/cpm80/host/HOST.ASM` was rewritten as CR+LF 8080 source for the standard `ASM.COM` + `LOAD.COM`; `hostTests` assembles it inside the emulated CP/M-80 (`A:ASM HOST`, `A:LOAD HOST`, no assembler errors) and requires the result to be byte-identical to `bootDisks/cpm80/systemDsk/HOST.COM` (5,760 bytes). Host tests pass, including a 70,000-byte GET/PUT round trip on a BIG drive and the disk-full case on a SYSTEM drive. Nothing flashed. |

Development references: [superzazu/z80](https://github.com/superzazu/z80), [z80pack reference system](https://github.com/udo-munk/z80pack). Record the actual reference commit before reproducing its behavior.
