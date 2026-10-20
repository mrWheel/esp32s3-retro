# CP/M-80 machine design

## 1. Purpose and authority

This document specifies the CP/M-80 machine hosted by the ESP32-S3 Retro
Computer. It is intended to preserve the requirements and the implemented
machine behavior needed to reproduce the design.

`projectPrompt.md` defines the common host, menu, storage, console, and
File Transfer contracts. This document defines only the CP/M-80 guest:
its CPU integration, memory map, CP/M system image, BIOS, disk profiles,
drive configuration, terminal behavior, and CP/M-specific `HOST.COM`
adapter.

The checked-in implementation is the authority for claims about behavior
that has already been built. Requirements that are not proven by the
implementation or its tests are stated as acceptance requirements, not as
implemented features. Update this document when the implementation changes.
Do not use this document as a chronological development log.

## 2. Goals and boundaries

CP/M-80 is a genuine CP/M 2.2 machine intended for command-line software
development: editors, assemblers, debuggers, utilities, and language tools.
The guest operating system is not replaced with a host-side filesystem or
BDOS implementation.

The host owns the USB Serial/JTAG console, menu and machine lifecycle,
LittleFS, SD/FAT storage, disk-image file access, File Transfer, and shared
status indicators. The CP/M-80 machine owns its Z80 instance, 64 KiB guest
memory, CP/M system image, virtual BIOS and disk geometry, and guest-side
exchange utility. It does not start WiFi or an HTTP server.

Browser File Transfer and emulator execution are mutually exclusive under the
common host design. The guest `HOST.COM` utility accesses the common local
exchange service while CP/M is running; it is not the browser file server.

## 3. Architecture and pinned components

```text
Host menu selection 1
  -> CP/M-80 resource probe
  -> cpm80Machine
  -> cpm80Guest
  -> cpm80Cpu
  -> pinned Z80 core
  -> genuine CP/M 2.2 CCP and BDOS
  -> project CP/M BIOS
  -> host console, bounded disk-record I/O, and hostExchange
```

- CPU core: `superzazu/z80`, upstream commit
  `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, MIT. The upstream core files
  and license are vendored under `components/z80/`; CP/M integration is in
  `components/cpm80Core/`.
- CP/M system: genuine CCP and BDOS from `brouhaha/cpm22`, commit
  `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`. The corresponding licensing
  clarification and source notes are in
  `components/cpm80Core/os/LICENSE.txt` and `README.md`.
- CP/M utility binaries: the unmodified utility set from
  `wwarthen/RomWBW`, commit `bbe7fd84ebdbf6e64929e81c09cf4887bfcce4d9`.
  The project-authored `HOST.COM` is not part of that set. See
  `components/cpm80Core/os/utilities/README.md` for provenance and usage
  scope.

Third-party CPU and operating-system licenses do not establish distribution
rights for application software. Keep source, version, provenance, and
applicable rights evidence for every separately obtained CP/M program.

## 4. CPU and guest instance

The machine uses one Z80 processor instance and a full 64 KiB address space.
The CPU adapter:

- allocates and owns 65,536 bytes of guest memory;
- supplies memory read/write and 8-bit I/O port callbacks to the Z80 core;
- exposes bounded memory loading and byte-level memory access for the guest
  and tests;
- executes one instruction per step;
- requires zero-initialized instances and explicit destruction.

The guest adapter copies the CCP and BDOS images into its own instance state,
installs the BIOS and page zero, and provides host callbacks for console
input/output, drive availability/profile, 128-byte disk records, exchange
ports, and cooperative yielding. Guest execution is scheduled in bounded
instruction batches; it yields periodically so the firmware task and
watchdog are serviced.

No cycle-accurate peripheral timing is specified by this implementation.
Instruction scheduling must not change guest instruction semantics.

## 5. Guest memory map and boot image

The current CP/M-80 profile uses this 64 KiB map:

| Address range | Contents |
|---|---|
| `$0000–$0002` | Page-zero jump to BIOS WBOOT at `$DA03` |
| `$0003` | I/O byte, initialized to zero |
| `$0004` | Current drive number; A: is zero |
| `$0005–$0007` | BDOS jump to `$CC06` |
| `$005C–$007F` | Standard default FCB area |
| `$0080–$00FF` | 128-byte default DMA area |
| `$0100–$C3FF` | Transient program area |
| `$C400–$CBFF` | Genuine CCP, 2,048 bytes |
| `$CC00–$D9FF` | Genuine BDOS, 3,584 bytes |
| `$DA00–$DA32` | BIOS jump table, 17 three-byte entries |
| `$DA33–$DA87` | BIOS service stubs (`OUT ($FE),A`, then return) |
| `$DA90–$DA9F` | A: DPH |
| `$DAA0–$DAAE` | A: DPB |
| `$DAB0–$DB2F` | A: 128-byte directory buffer |
| `$DB30–$DB3F` | A: SYSTEM CSV |
| `$DB40–$DB5E` | A: allocation vector |
| `$DB60–$DF5F` | B:–E: DPHs and per-drive BIOS tables, one `$0100`-byte slot each |
| `$DF60–$E05F` | F: BIOS table slot, including its BIG-profile allocation vector |
| `$E060–$E07F` | A: LARGE CSV |
| `$E080–$E09E` | A: LARGE allocation vector |

Each DPH has no translation table and points to its own directory buffer,
DPB, CSV, and allocation vector. For B:–F:, the DPH addresses are `$DB60`,
`$DC60`, `$DD60`, `$DE60`, and `$DF60`; the DPB, directory buffer, CSV, and
allocation vector are at offsets `$10`, `$20`, `$A0`, and `$C0` within the
drive slot. SYSTEM/LARGE CSV sizes are 16/32 bytes and their allocation
vectors are 31 bytes. BIG uses no CSV and uses a 64-byte allocation vector.
The A: LARGE table locations are moved above the B:–F: slots to fit their
larger CSV and allocation vector.

The raw LittleFS A: image contains:

| Image offset | Contents |
|---|---|
| `$0000–$07FF` | CCP image |
| `$0800–$15FF` | BDOS image |
| `$1600–$160F` | 16-byte `RETROCPM` image header |

The header bytes are `RETROCPM`, version bytes `1,1`, then little-endian
addresses `$C400`, `$CC00`, and `$DA00`. The BIOS is generated in guest memory;
it is not stored in the disk image. The disk directory begins after the two
reserved tracks defined by the selected profile.

Cold boot initializes page zero, CCP, BDOS, BIOS, and processor state before
entering CP/M. WBOOT reinstalls CCP, BDOS, and BIOS state and returns to the
CCP; it is a guest warm boot and does not return to the host menu. Physical
ESP32 reset returns to the host menu. The machine probe checks A: file size
and the image header; initialization reads the CCP and BDOS from A:.

## 6. CP/M BIOS

The BIOS jump-table order is the CP/M 2.2 order:

`BOOT`, `WBOOT`, `CONST`, `CONIN`, `CONOUT`, `LIST`, `PUNCH`, `READER`,
`HOME`, `SELDSK`, `SETTRK`, `SETSEC`, `SETDMA`, `READ`, `WRITE`, `LISTST`,
`SECTRAN`.

Each table entry jumps to a small guest stub. The stub places the function
index in A, outputs it on virtual I/O port `$FE`, then returns. `$FE` is a
project-defined Z80 callback port, not a physical ESP32 peripheral. The
callback uses the CP/M registers and per-guest state to perform the service.

Implemented service behavior:

- `BOOT` cold-starts the guest; `WBOOT` reinstalls the CP/M system and
  restarts at the CCP.
- `CONST` is nonblocking and returns `$FF` when input is available, otherwise
  zero. `CONIN` waits cooperatively for input.
- `CONOUT` sends the low byte in C to the common USB console.
- `LIST` and `PUNCH` are deterministic no-ops; `READER` returns `$1A`;
  `LISTST` returns zero.
- `HOME` sets the current track to zero.
- `SELDSK` returns the selected drive's DPH in HL when available. An
  unavailable or out-of-range drive returns a null DPH and does not alias
  A:.
- `SETTRK`, `SETSEC`, and `SETDMA` set the current track, sector, and DMA
  address from BC.
- `READ` and `WRITE` transfer one 128-byte record through the host callbacks.
  A:–F: use zero-based track and sector values and identity sector
  translation. Record offsets are `((track * sectorsPerTrack) + sector) *
  128`; requests outside the drive geometry or guest DMA memory fail with
  status 1 in A. Successful operations return status 0 in A.
- `WRITE` obeys each image's read-only setting and flushes successful records
  to the image file. Each drive has independent DPH/DPB and directory state.

The common disk activity indicator is signaled by CP/M record reads and
writes. Host filesystem activity outside guest disk-record operations is not
represented by this indicator.

## 7. Disk profiles and drive mapping

The guest presents six CP/M drives, A: through F:. A: is the boot/system
drive, stored in LittleFS and always opened read-only. B:–F: are optional SD
images configured independently as read-only or read/write.

| Profile | Image bytes | Geometry | Reserved tracks | Block size | Directory entries | DPB fields |
|---|---:|---|---:|---:|---:|---|
| SYSTEM | 256,256 | 77 tracks × 26 × 128-byte records | 2 | 1 KiB | 64 | SPT=26, BSH=3, BLM=7, EXM=0, DSM=242, DRM=63, AL0=`$C0`, AL1=0, CKS=16, OFF=2 |
| LARGE | 512,512 | 77 tracks × 52 × 128-byte records | 2 | 2 KiB | 128 | SPT=52, BSH=4, BLM=15, EXM=0, DSM=242, DRM=127, AL0=`$C0`, AL1=0, CKS=32, OFF=2 |
| BIG | 8,421,376 | 514 tracks × 128 × 128-byte records | 2 | 16 KiB | 512 | SPT=128, BSH=7, BLM=127, EXM=7, DSM=511, DRM=511, AL0=`$80`, AL1=0, CKS=0, OFF=2 |

SYSTEM, LARGE, and BIG are supported data-drive profiles for B:–F:. A: may
use SYSTEM or LARGE, but never BIG. For A: configured as `SYSTEM`, firmware
detects SYSTEM or LARGE from exact image size. An explicit `LARGE` entry
requires the LARGE image size. All data-drive images must match the exact
size of their declared profile; firmware does not format images or validate
their guest directory contents at mount time.

The checked-in configuration is:

```text
A=/littlefs/cpm80/system.dsk,RO,SYSTEM
B=/retro/images/cpm80/work1.dsk,RW,LARGE
C=/retro/images/cpm80/work2.dsk,RW,BIG
D=/retro/images/cpm80/languages.dsk,RW,BIG
E=/retro/images/cpm80/Utility.dsk,RW,LARGE
```

The parser accepts entries for A:–F: in
`/microSD/retro/images/cpm80/drives.cfg`. Each non-comment entry has this
form:

```text
drive=/path/to/image,RO|RW,SYSTEM|LARGE|BIG
```

A: paths must be below `/littlefs/cpm80/`; B:–F: paths must use the
`/retro/images/cpm80/` SD-volume namespace, which is mapped to the
`/microSD` VFS mount. The parser accepts blank lines and lines beginning with
`#`, trims spaces and tabs around fields, and rejects duplicate drive
entries, unsupported profile/mode values, malformed or overlong lines,
control characters, and paths outside the allowed roots or containing
traversal, repeated separators, or backslashes. A: must be read-only and
cannot use BIG. Any malformed configuration invalidates the file and
restores the built-in A:-only configuration. A missing configuration also
leaves only the built-in A: configured.

On launch, the machine probes the configured A: image. A missing image is
reported as a missing resource; a wrong size, invalid `RETROCPM` header,
read failure, or close failure makes the resource invalid. During
initialization, each configured B:–F: image is independently opened only if
it exists, has the exact declared profile size, and can be opened with the
configured access mode. A missing or invalid optional drive remains
unavailable to CP/M. Disk images are accessed using bounded read-at-offset
and write-at-offset host APIs; the entire image is not loaded into RAM.

## 8. Console and terminal

The default terminal personality for CP/M-80 and the other guest operating
systems is VT100/ANSI, 80 columns by 24 rows. Minicom on macOS is the default
terminal program for the user's workflow and hardware acceptance tests.

The ESP32 USB Serial/JTAG console is a byte-stream transport. It does not
render VT100/ANSI or generate terminal control sequences on behalf of
CP/M-80. Guest applications send their own control bytes; the connected
terminal interprets them. Do not add a generic VT100/ANSI terminal emulator
to the common host console. Machine-specific video output may have a
separate, explicitly defined implementation.

CP/M console input maps LF to CR, maps CR+LF to one CR, and masks input to
7-bit ASCII. Console output is passed as bytes. Terminal acceptance covers
line endings, backspace/delete, control keys, escape-key handling, cursor
positioning, screen erase, and reverse video using the intended guest
application. Binary file-transfer data must not be modified by terminal
line-ending handling. Diagnostics must not corrupt guest terminal output.

## 9. CP/M-80 `HOST.COM`

`HOST.COM` is the CP/M-80 guest adapter for the common host exchange service.
It copies files between the current CP/M filesystem and the
`/retro/exchange/cpm80/` host directory. All guest filesystem access is
performed through CP/M BDOS calls; the host does not edit CP/M directory
entries or allocation blocks to implement individual transfers. WiFi is not
required.

The program is project-authored 8080 code:

- source: `guest/cpm80/host/HOST.ASM`;
- checked-in binary: `guest/cpm80/host/HOST.COM`;
- rebuild inside the emulated CP/M guest using `ASM HOST` and `LOAD HOST`.

Invocation without a command tail opens an interactive `HOST>` prompt.
Blank input and unknown commands print the command list and return to the
prompt. `QUIT` returns to CP/M. The existing one-shot command-tail form is
also supported.

```text
A>HOST
HOST> DIR
HOST> GET HELLO.C
HOST> PUT RESULT.TXT
HOST> GET *.C
HOST> PUT HELLO?.TXT
HOST> QUIT

A>HOST DIR
```

Command behavior:

- `DIR` lists files in the CP/M-80 exchange directory. It is available from
  every CP/M user area.
- `GET name[.ext] [O]` receives a host file and creates it on the current
  CP/M drive in USER 0.
- `PUT name[.ext] [O]` sends a file from the current CP/M drive and USER 0
  to the exchange directory.
- `QUIT` leaves the utility and returns to CP/M.
- GET and PUT accept CP/M `*` and `?` wildcard patterns. Wildcard transfers
  exclude `.HST` sidecars. PUT gathers up to 128 distinct matches before
  sending so BDOS directory searches are not disrupted.

GET and PUT require USER 0; the utility reports that requirement if run in
USER 1–15. Since A: is read-only, select a writable drive such as E: before
GET and run HOST from A: (for example, `E:` followed by `A:HOST GET FILE.TXT`).
GET writes to the current CP/M drive. PUT reads from the current drive.
HOST restores the drive from which it was started.

Without `O`, existing CP/M GET destinations and existing host PUT
destinations are not overwritten. With `O`, GET replaces an existing local
file and its `.HST` sidecar after the host accepts the GET; PUT requests the
host's explicit overwrite operation. A rejected individual wildcard match
is reported and processing continues. A stream or checksum failure aborts
the batch. HOST prints a progress dot per 16 CP/M records (2 KiB).
Insufficient guest media is reported as `disk full` or `directory full`;
partial GET files and sidecars created by that operation are removed.

CP/M files have record-oriented lengths. HOST never infers exact length by
stripping `$1A`, zero bytes, or other trailing content. A sidecar named
`NAME.HST` preserves the exact payload length for a file received with GET.
It contains a 16-byte header beginning with `HST1`, followed by the exact
payload length, CRC-32 of the padded CP/M records, and CRC-32 of the exact
payload; 32-bit values are little-endian. The sidecar occupies one CP/M
record. PUT uses its exact length only if the padded-record CRC still
matches; otherwise it sends all complete 128-byte records. Text newline and
EOF conversion is not performed. The sidecar is guest-visible and reserved
for HOST.

## 10. HOST exchange port protocol

The CP/M-80 adapter uses the common host exchange service at data port `$F8`
and abort port `$F9`. This section records the CP/M-80 transport mapping; the
machine-neutral exchange service and browser workflow are defined by
`projectPrompt.md`.

- Multi-byte integers are unsigned 32-bit little-endian values.
- `QUERY` is command 0. The response is status 0, protocol version 1, and
  capability bits `$07` (DIR, GET, PUT).
- `DIR` is command 1. The host returns status, a sequence of NUL-terminated
  printable names, an empty-string terminator, and final status.
- `GET` is command 2. The guest sends the 11-byte uppercase CP/M 8+3 FCB
  filename with space padding. The host returns status, exact byte length,
  payload bytes, CRC-32, and final status.
- `PUT` is command 3. The guest sends the filename, exact length, and
  expected CRC-32. The host returns an initial status; the guest then sends
  exactly the declared payload length and receives final status.
- `PUT` overwrite is command 4 and uses the same payload/checksum contract.
- CRC-32 uses reflected polynomial `$EDB88320`, initial value `$FFFFFFFF`,
  and final XOR `$FFFFFFFF`.
- Status values are 0 success, 1 invalid name, 2 unavailable/not found,
  3 target exists, 4 I/O or checksum error, and 5 busy.
- Host names are flat ASCII names restricted to letters, digits, `_`, and
  `-`; all access is confined to `/retro/exchange/cpm80/`.
- Transfers stream through the port and use bounded host buffers. No
  complete host file is buffered in guest or host RAM.
- The guest aborts an active transfer by writing zero to `$F9`. Interrupted
  sessions close the service and remove any unpublished temporary file.
- PUT writes to a private temporary file, verifies exact length and CRC,
  then publishes the target. Existing targets are never replaced without
  command 4; the old target remains until the new content is verified.
- The protocol's 32-bit transfer length limit is 4 GiB minus 2 bytes, in
  accordance with the common HTTP/content-length limit in `projectPrompt.md`.

CRC-32 detects accidental corruption; it is not a cryptographic identity
check.

## 11. CP/M-80 image and host tools

The CP/M-80 image utilities run on the development computer, not on the
ESP32. They prepare raw images and do not format the physical SD card.

### Image profiles and file operations

`tools/diskImage.py --os cpm80` dispatches to
`tools/include/diskImageCpm80.py`. The CP/M-80 profile module supplies the
SYSTEM, LARGE, and BIG geometries; the shared CP/M directory/extents and
allocation implementation is in `tools/include/diskImageCpm.py`. Its CLI
operations are:

- `create` makes an empty formatted image. It refuses to replace an existing
  image unless `--force` is supplied.
- `add` imports local files into an existing image, validates CP/M 8.3 names,
  extents, allocation, capacity, duplicate names, and directory space, and
  writes the result atomically. Wildcard entries that are invalid or empty
  are skipped with a warning; explicitly named invalid files fail.
- `list` validates and lists image directory/allocation data.
- `extract` extracts a named file from a specified user area.

Examples:

```sh
python3 tools/diskImage.py create --os cpm80 --profile LARGE work.dsk
python3 tools/diskImage.py add --os cpm80 work.dsk ~/cpm/*.COM
python3 tools/diskImage.py list --os cpm80 work.dsk
python3 tools/diskImage.py extract --os cpm80 work.dsk PROGRAM.COM \
  --output PROGRAM.COM
```

Images are raw CP/M sector images; the utility does not convert ImageDisk,
TeleDisk, or other archival floppy formats. Validate the source geometry
and ordering before importing a complete image.

### System-image generation

`tools/buildDiskImage.py --os cpm80` dispatches to
`tools/include/buildDiskImageCpm80.py` and composes a SYSTEM boot disk from
the checked-in CCP/BDOS binaries, utility files, `HOST.COM`, `HELLO.COM`, and
`WELCOME.TXT`. Its output is deterministic. The source modules establish the
CCP/BDOS sizes, image header, directory placement, and allocation limits.

`tools/createSystemDsk.py --os cpm80 --profile SMALL|LARGE` creates a
CP/M-80 boot disk with the selected system-image profile and optionally adds
the files from a supplied source directory. `SMALL` selects the SYSTEM
profile. SYSTEM/SMALL and LARGE boot images contain the same CP/M CCP/BDOS/
header arrangement; A: accepts the corresponding exact image sizes.

To create a blank data drive and prepare it for the configured SD card:

```sh
python3 tools/prepareSd.py /Volumes/SDCARD
python3 tools/diskImage.py create --os cpm80 --profile LARGE \
  ~/Desktop/work.dsk
python3 tools/diskImage.py add --os cpm80 ~/Desktop/work.dsk \
  ~/Downloads/PROGRAM.COM
python3 tools/diskImage.py list --os cpm80 ~/Desktop/work.dsk
```

Copy the image into `/retro/images/cpm80/` at the location configured in
`drives.cfg`, then safely eject the card before using it in the ESP32-S3.
Image generation does not change a user's existing drive configuration.

### CP/M-80 resource importer

`tools/fetchCpm80Software.py` can list links from an HTML index and import
explicitly selected files or explicitly selected ZIP members into a
CP/M-80 image. ZIP member paths are read as archive data; they are not
extracted onto the host filesystem. Downloads are limited to 64 MiB. The
tool preserves original downloads, records source URLs, timestamp, size,
SHA-256, selected members, and user-supplied rights evidence in
`cpm-resource-manifest.json`. It requires a rights reference for network
imports but does not determine whether the reference grants permission.
Known Microsoft archive entries are blocked. Its `disk` operation can copy a
complete raw CP/M image when its size and directory/allocation structure
validate; it does not prove sector ordering or convert ImageDisk, TeleDisk,
or other foreign floppy-image formats.

## 12. Bundled guest software

The A: image includes these standalone CP/M utility files:

`ASM.COM`, `DDT.COM`, `DUMP.COM`, `ED.COM`, `HELP.COM`, `HELP.HLP`,
`LIB.COM`, `LINK.COM`, `LOAD.COM`, `MAC.COM`, `PIP.COM`, `RMAC.COM`,
`STAT.COM`, `SUBMIT.COM`, `XREF.COM`, `XSUB.COM`, and `ZSID.COM`.

`DIR`, `ERA`, `REN`, `TYPE`, and `USER` are commands built into the CCP;
they are not separate COM files. `WELCOME.TXT` is a text fixture for `TYPE`.
`HOST.COM` is the project-authored guest exchange utility described above.
These files are installed in the system image by the image builder. The
source and license scope for the imported DRI utility set are documented in
`components/cpm80Core/os/utilities/README.md`.

The design does not require a particular compiler or BASIC implementation.
Any added application must fit the selected profile, work with the CP/M-80
BIOS and terminal contract, and have documented provenance and rights.

## 13. Verification and acceptance

Use the host test project under `tests/` for portable tests. The CMake host
test build enables AddressSanitizer and UndefinedBehaviorSanitizer where
supported. CP/M-80 tests in `tests/hostTests.c` exercise the CPU adapter,
genuine CCP/BDOS boot, console and BIOS services, drive configuration,
CP/M commands, disk profiles, transfers, and in-guest assembly of
`HOST.ASM` with the bundled `ASM.COM` and `LOAD.COM`.

Acceptance of a reproducible CP/M-80 implementation requires:

1. The pinned Z80 core and guest adapter pass CPU, memory-boundary,
   instruction, port-callback, and lifecycle tests.
2. The guest boots the genuine CCP/BDOS and reaches the CP/M prompt through
   the BIOS; `DIR`, `TYPE`, a transient program, and WBOOT operate.
3. BIOS calls obey the CP/M 2.2 jump-table ABI and all configured drives have
   the correct DPH/DPB, access mode, bounds, and independent directory state.
4. SYSTEM, LARGE, and BIG images are constructed and validated against their
   declared geometry. Test first/last records, directory and disk capacity,
   invalid images, unavailable drives, and read-only write rejection.
5. `HOST.COM` is reproducibly assembled from its source and its guest tests
   verify DIR, exact-byte GET/PUT, overwrite policy, wildcards, sidecar
   validation, USER restrictions, cancellation, and error cleanup.
6. The firmware builds for ESP32-S3. Hardware acceptance separately verifies
   CP/M boot, real SD drives and persistence, USB console behavior, and the
   VT100/ANSI 80×24 Minicom workflow on macOS; build verification does not
   require flashing.
7. Test editor/application compatibility and record actual hardware
   performance and recovery behavior before making claims about them.

A host test, generated disk image, or firmware build is not evidence of a
hardware result. Hardware records should identify firmware revision,
ESP-IDF version, board, terminal, image profile, exact commands, expected
and actual output, and retained evidence. Do not claim that every bundled
utility or arbitrary CP/M application is compatible solely because it is
present in the disk image.
