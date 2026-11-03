# Host tools

## Production use versus test use

All Python programs under `tools/` run on the development computer, not on the
ESP32. They prepare images/configuration, generate a source file used by the
firmware, or test the running device. The firmware itself is the C/C++ code in
`main/` and `components/`.

| Program/file | Purpose | Classification |
| ------ | ----------------------- | -------------- |
| `tools/diskManager.py` | The one disk-image command line: `--type SYSTEMDSK` builds the bootable `littlefs/<os>/system.dsk` (or `system.po`) from the OS boot resources plus `bootDisks/<os>/systemDsk/`; `--type WORKDSK` creates an empty work disk under `sdcard/retro/images/<os>/`. Every writing run shows a summary and asks `Are you sure (y/N)` (skip with `--yes`). `--type` also accepts unique abbreviations in any case (`S`, `SYS`, `WORK`, ...). | Regular host-side media preparation and production-image preparation |
| `tools/extractFiles.py` | List, add, extract and remove files inside existing images through the OS-specific adapters (`--os cpm80|cpm86|apple2|ucsd`). | Regular host-side media preparation |
| `tools/prepareSd.py` | Create the documented directory layout on an existing SD-card mount; does not format the card. | Regular host-side SD preparation |

| Helper Utility/file | Purpose | Classification |
| ------ | ----------------------- | -------------- |
| `tools/apple2/buildApple2DiskBootRom.py` | Regenerate `components/apple2Core/apple2DiskBootRom.h`, which is compiled into the Apple II firmware. Normally only run when intentionally regenerating that generated header. | Production build-input generator; not a test |
| `tools/ucsd/buildApple2PascalCardRom.py` | Regenerate `components/apple2Core/apple2PascalCardRom.h` for the Apple Pascal card. | Production build-input generator; not a test |
| `tools/apple2/extractApple2Files.py` | Interactively browse Apple II disk images and copy/remove files between disk images and host folders; also imported by the Apple II, CP/M, and UCSD adapters. | Optional host media utility |
| `tools/ucsd/diskImageApple2Pascal.py` | Create a blank Apple/UCSD Pascal disk image; also the volume builder used by `diskManager.py --type WORKDSK --os ucsd`. | Optional host media utility |
| `tools/apple2/buildApple2TestRom.py` | Generate the minimal diagnostic ROM fixture in `tests/fixtures/`. | Test-only |
| `tools/transferTest.py` | Exercise the HTTP file-transfer service on a running device. It creates and deletes uniquely named test files. | Test-only; run only when deliberately testing a device |
| `tools/include/*.py` (other files) | Shared disk-format helpers, the system-disk builders (`systemDisk.py`), the confirmation helpers and the OS registry imported by the host entrypoints. | Internal modules; not separate production applications |
| `tools/cpm86/diskdefs` | Optional disk geometry definitions for external CP/M image tools; not read by the firmware. | Host-side support data |
| `tools/README.md` | Usage and behavior documentation for these tools. | Documentation |

In short: use `diskManager.py`, `extractFiles.py` and `prepareSd.py` for
normal host-side preparation. The two explicitly test-only tools are not
needed for normal firmware operation. The Apple II and UCSD ROM generators are
usually run only by developers, but their generated headers are part of the
production firmware.

## OS-independent usage (`--os`)

`diskManager.py`, `extractFiles.py` and `prepareSd.py` select the target
system with `--os cpm80|cpm86|apple2|swtpc|ucsd`. SWTPC is registered but not
implemented. `-h` shows the general help; `-h` together with `--os <name>`
adds the OS-specific profiles and rules. WORKDSK images are written to
`sdcard/retro/images/<os>/` (override with `--sd-root`); a path instead of a
bare name is used as given. The Apple II and UCSD images use the `.po`
extension, which the tools enforce. The device's File Transfer feature can
copy images to the physical SD card.

```sh
python3 tools/diskManager.py --type SYSTEMDSK --os cpm80 --profile CPM80_1440K
python3 tools/diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_1440K
python3 tools/diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS_640K
python3 tools/diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K
python3 tools/diskManager.py --type WORKDSK --os cpm80 --profile CPM80_512K work.dsk
python3 tools/diskManager.py --type WORKDSK --os cpm86 work86.dsk
python3 tools/diskManager.py --type WORKDSK --os apple2 data.po
python3 tools/diskManager.py --type WORKDSK --os ucsd work.po
python3 tools/diskManager.py --yes --type WORKDSK --os cpm80 work.dsk
python3 tools/prepareSd.py --os cpm86
python3 tools/diskManager.py --os cpm86 -h
```

  `--type` is mandatory: `SYSTEMDSK` builds the firmware's bootable system
disk (see below), `WORKDSK` creates an empty work disk. `--profile` selects
the geometry: for SYSTEMDSK the values are the drives.cfg size identifiers
(`CPM80_*`, `CPM86_*`) or `PRODOS*`/`PASCAL_800K`; for WORKDSK they are the
engine profiles (`CPM80_256K`…, `CPM86_160K`…, `140K/640K/800K`, `140K/640K`).
WORKDSK refuses to overwrite an existing image unless `--force` is given.
Apple II WORKDSK takes the ProDOS volume name from the image file name
(upper-cased; `DATA` when it is not a valid name); UCSD behaves the same with
`WORK` as fallback.

  Every writing run first prints a "Planned action" summary (OS, type, image,
profile/sources/output and the effect) and asks `Are you sure (y/N)`; anything
but `y`/`yes` aborts without touching any file. For scripts pass `--yes` (or
`-y`); without a terminal a writing run is refused unless `--yes` is given.

  Files go in and out of existing images with `extractFiles.py` (list, add,
extract, remove; `--os cpm80|cpm86|cpm|ucsd` take the image as argument,
`--os apple2` runs the interactive menu).

  `tools/` contains the host-side command-line entrypoints listed above.
OS-specific image modules and extraction adapters live in their matching
`tools/<os>/` directories; shared modules, including `osProfiles.py`,
`systemDisk.py`, `diskImageCpm.py` and `diskImageCommon.py`, live in
`tools/include/`. The `diskManager.py` entrypoint dispatches to the OS modules
using `--os`.

  A new OS needs `tools/<os>/diskImage<Os>.py`, the system-disk branch in
`tools/include/systemDisk.py`, and one entry in `tools/include/osProfiles.py`.
The entrypoints add the shared and OS-specific directories to Python's module
search path before importing these modules.
  `prepareSd.py` without a mount point prepares `sdcard/`, and without `--os`
prepares all systems.

  `prepareSd.py` (Python 3.9+) adds the documented layout to an already formatted/mounted FAT32 card. It does not format, delete, make disk images, create drive configurations (those live in `littlefs/<os>/drives.cfg`) or rewrite an incompatible layout marker. Run with the card mount root as its sole argument.

  `transferTest.py` exercises an actual running device using the session URL displayed on USB. It creates uniquely named files in exchange/common and removes only those files. Do not run it during manual transfers. Its 8 MiB payload intentionally exceeds normal ESP32-S3 internal RAM; the test PC can hold it in memory, while the device must stream.

  The CP/M-80 base of the A: image (`tools/include/systemDisk.py` with the constants of `tools/cpm80/buildDiskImageCpm80.py`) deterministically composes the read-only image from the checked-in CCP/BDOS outputs and pinned utility binaries. It validates input sizes, CP/M 8.3 names and allocation-block capacity against the DPB, and creates directory extents for larger files. It does not assemble the CCP/BDOS sources; the upstream Macro Assembler AS and `p2bin` are needed for that step. Utility provenance and non-commercial use scope are in `components/cpm80Core/os/utilities/README.md`.

### Create a SYSTEM.DSK on LittleFS

`diskManager.py --type SYSTEMDSK` builds `littlefs/<os>/system.dsk` from OS
resources and files in `bootDisks/<os>/systemDsk/` (it also accepts the
existing CP/M-80 spelling `systemDisk`). Apple II uses a ProDOS base image.
Use `--source-dir` to select another folder and `--output` to choose another
destination. The destination image is replaced when the build succeeds:

```sh
python3 tools/diskManager.py --type SYSTEMDSK --os cpm80 --profile CPM80_256K
python3 tools/diskManager.py --type SYSTEMDSK --os cpm80 --profile CPM80_512K
python3 tools/diskManager.py --type SYSTEMDSK --os cpm80 --profile CPM80_1440K
python3 tools/diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_160K
python3 tools/diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_1440K
python3 tools/diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_8196K
python3 tools/diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS
python3 tools/diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS --binary-load-address 0x800
python3 tools/diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K
```

Use the profile declared by the `A=` line of `littlefs/<os>/drives.cfg` (for
example `A=/littlefs/cpm80/system.dsk,RO,CPM80_1440K` means `--profile
CPM80_1440K`): the firmware refuses an A: image whose size does not match its
identifier.

Executable files (`.COM` for CP/M-80 and `.CMD`/`.SYS` for CP/M-86) are installed
before other files. If remaining non-executable files do not fit, the builder
keeps the executables, skips the files that do not fit, and prints
`Disk too small`. If an executable itself cannot fit, the build fails rather
than producing a disk missing a program.

The CP/M `--profile` values are the drives.cfg size identifiers themselves:
`CPM80_256K` (256,256 bytes), `CPM80_512K` (512,512 bytes) and
`CPM80_1440K` (1,474,560 bytes) for CP/M-80, and `CPM86_160K` (163,840 bytes),
`CPM86_516K` (528,384 bytes), `CPM86_1440K` (1,474,560 bytes) and
`CPM86_8196K` (8,392,704 bytes) for CP/M-86. All of them boot as A:: the firmware detects
the layout from the size of `littlefs/<os>/system.dsk` when drives.cfg is
missing, and the A: line's identifier must then match the file size. A size
that matches no system profile is reported as an error and A: is not mounted. CP/M-86 is built from an empty
image plus the source directory only (never from an older `system.dsk`). It requires
`littlefs/cpm86/cpm.sys` and `littlefs/cpm86/retro86bios.h86` to remain in place.
The builder validates these runtime files but does not put them on A::
firmware loads them directly from LittleFS. In particular, the duplicate
`CPM.SYS` is never placed on the generated CP/M-86 A: image.
The firmware build (`idf.py build`/`flash`) runs `diskManager.py --type SYSTEMDSK` at CMake configure time (see `CMakeLists.txt`) for Apple II, CP/M-80, CP/M-86 and UCSD, so the `system.dsk`/`system.po` files in `littlefs/` always match `bootDisks/<os>/systemDsk/`; changing a file there re-runs the configure step. The `APPLE2_SYSTEM_DISK_PROFILE` CMake option selects `PRODOS` (140K), `PRODOS_640K` (default), or `PRODOS_800K`; for example, use `-DAPPLE2_SYSTEM_DISK_PROFILE=PRODOS_800K` for an 800K bootable system volume in the `bootfs` LittleFS partition. The `CPM80_SYSTEM_DISK_PROFILE` and `CPM86_SYSTEM_DISK_PROFILE` CMake options select the CP/M system disk profiles (defaults `CPM80_1440K` and `CPM86_1440K`, kept equal to the A: identifiers in the drives.cfg files). `--os ucsd --profile PASCAL_800K` copies `assets/ucsd/ucsdDiskImages/Apple Pascal 1.3 3.5.po` and adds the files of `bootDisks/ucsd/systemDsk/` (default output `littlefs/ucsd/system.po`); `128K.APPLE`/`128K.PASCAL` are placed as `SYSTEM.APPLE`/`SYSTEM.PASCAL`, and a source file named like an existing base-volume file overwrites it. An Apple II source file named `NAME#ADDR.BIN` (ADDR is 1-4 hex digits) is stored as `NAME` (without extension) with load address ADDR; a `NAME.ADDR` file (hex, e.g. `6000`) next to `NAME.BIN` still gives that file's load address. Bench programs (BENCH2, BENCH2ASM, BENCH80, BENCH86, BENCHUCSD) are copied from `assets/` into these folders; `tests/test_bootDisksBench.py` checks that the copies are current.
The Apple II emulator uses ProDOS disks only; DOS 3.3 is not supported. For Apple II,
`diskManager.py --type SYSTEMDSK` copies the bootable base volume `bootDisks/apple2/prodosEmpty.po`
(140K, volume `/SYSTEM`: `PRODOS`, `BASIC.SYSTEM`, `QUIT.SYSTEM`, `BITSY.BOOT`), adds every
file from `bootDisks/apple2/systemDsk/` (a source file named like an existing base-volume
file overwrites it) and writes `littlefs/apple2/system.dsk`
by default. Use `--source-dir` to select another file folder, `--output` to
choose the destination and `--base-image` to use another bootable ProDOS volume.
The base image is never modified.

Source files are named by suffix: `.BAS` (numbered ASCII Applesoft source, tokenized
by the builder, ProDOS type BAS), `.TXT` (text), `.BIN` (raw binary; a source file
named `NAME#ADDR.BIN` is stored as `NAME` with load address ADDR, other `.BIN` files
need a load address from `--binary-load-address`, decimal or `0x`-prefixed
hexadecimal, or a `NAME.ADDR` file next to them) and `.SYS`. ProDOS names allow only `A-Z`, `0-9` and
`.`, so `-` and `_` become `.`; `HELLO.BAS` becomes `STARTUP`. The disk builder
creates a host image only.

### ProDOS 8 on the Apple II

ProDOS 8 uses 140K, 640K or 800K images as one filesystem (800K, `APPLE2_800K`, is the
preferred size for data volumes). The
emulator therefore treats a slot as a ProDOS block device when one of its
images holds a ProDOS volume (a `.po` file, or a `.dsk` that holds a ProDOS
volume): the Disk II boot ROM cannot start ProDOS, so such a slot gets a
project-authored block-device ROM. The Autostart ROM boots it at power-on
(or type `PR#6`), and both drives of that slot are block devices, so keep Apple
Pascal (Disk II) images in a slot of their own. `SP<slot>.<unit>` selects the
same kind of device explicitly as a SmartPort interface; slots 4–7 accept up
to four configured units each (`SP5.1`/`SP5.2` with `PR#5` are one example).
Apple Pascal 1.3's tested ProDOS block path addresses only two units per slot,
so extra Pascal volumes must use another slot. This guest limit is distinct
from the emulator's four-unit SmartPort command interface.
The firmware passes 512-byte block reads and writes to the image backend,
preserving either DOS or ProDOS sector ordering and the configured `RO`/`RW`
access. The guest filesystem owns all ProDOS directory and bitmap updates.

**ProDOS boot disk (`SD6.1`).** `system.dsk` is part of the firmware. To boot
ProDOS from `SD6.1` (list it in `drives.cfg`; there is no default drive, and the
emulator reports an error and returns to the system menu when `SD6.1` is missing):

```sh
python3 tools/diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS
python3 tools/diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS_800K
idf.py build
```

The default volume is `/SYSTEM` (140K, DOS sector order like every ProDOS `.dsk`). Larger bootable 640K and 800K volumes can be selected with `PRODOS_640K` and `PRODOS_800K`:
`PRODOS`, `BASIC.SYSTEM`, `QUIT.SYSTEM`, `BITSY.BOOT` from
`bootDisks/apple2/prodosEmpty.po`, plus every file in
`bootDisks/apple2/systemDsk/`. ProDOS names allow only `A-Z`, `0-9` and `.`, so
`-` and `_` become `.` (`TEST-NONGR.BAS` becomes `TEST.NONGR`) and `HELLO.BAS`
becomes `STARTUP`, which `BASIC.SYSTEM` runs at boot. To use the new `system.dsk` the emulator must be
rebuilt and flashed again; `--type SYSTEMDSK` prints this reminder.

**ProDOS data volumes (SD card).** `diskManager.py --type WORKDSK --os apple2` writes a
ProDOS volume to `sdcard/retro/images/apple2/`:

```sh
python3 tools/diskManager.py --type WORKDSK --os apple2                     # data.po, volume /DATA
python3 tools/diskManager.py --type WORKDSK --os apple2 --profile 140K WORK.po
python3 tools/diskManager.py --type WORKDSK --os apple2 --profile 800K DATA800.po   # volume /DATA800
```

Options: `--profile 140K|640K|800K` (default 800K), `--force`. The volume name
follows the image file name; add files afterwards with
`python3 tools/extractFiles.py --os apple2` (interactive menu; `.BIN` files
need a load address: a `NAME.ADDR` file next to them).
The image is **not** on the SD card yet: upload it with "File Transfer" (emulator
menu option 6) to `/retro/images/apple2/` and add it to `drives.cfg`, for
example `SD6.2=/retro/images/apple2/DATA800.po,RW,APPLE2_800K` (in ProDOS `/DATA800`,
`CATALOG,S6,D2`).

The host guest tests boot ProDOS 2.4.2 through SmartPort, boot a generated
ProDOS `system.dsk` from `SD6.1` with autostart, catalog the 800K volume and
`SAVE` a BASIC file to it; hardware behavior remains unverified. Use
disposable images until guest writes have been qualified on the target hardware.

## Prepare CP/M disk images on macOS

### CP/M-86 drives

The CP/M-86 drive configuration is `littlefs/cpm86/drives.cfg` (flashed with LittleFS). The CP/M-86 BIOS supports drives A: through F:. A: is the
read-only LittleFS system image; B: through F: are optional SD images. Every
line ends with the size identifier of its image: `CPM86_160K` (163,840 bytes,
40 tracks, eight 512-byte sectors per track, 128-byte guest records, 1 KiB
allocation blocks and 64 directory entries), `CPM86_516K` (528,384 bytes,
129 tracks, 2 KiB blocks, 128 directory entries), `CPM86_1440K` (1,474,560
bytes, 360 tracks, 8 KiB blocks, 128 directory entries) or `CPM86_8196K`
(8,392,704 bytes, 2049 tracks, 16 KiB blocks, 512 directory entries).

The generated configuration maps E: to `work86.dsk` and sets it to read/write.
Create the image if needed, then copy it to that path on the card:

```sh
python3 tools/diskManager.py --type WORKDSK --os cpm86 --profile CPM86_160K work86.dsk
cp sdcard/retro/images/cpm86/work86.dsk /Volumes/SDCARD/retro/images/cpm86/work86.dsk
```

The configuration uses one line per drive (the A: identifier must match the
flashed system image size; keep `CPM86_SYSTEM_DISK_PROFILE` equal to it):

```text
A=/littlefs/cpm86/system.dsk,RO,CPM86_1440K
B=/retro/images/cpm86/languages.dsk,RO,CPM86_160K
C=/retro/images/cpm86/tools.dsk,RO,CPM86_160K
D=/retro/images/cpm86/utilities.dsk,RO,CPM86_160K
E=/retro/images/cpm86/work86.dsk,RW,CPM86_160K
F=/retro/images/cpm86/archive.dsk,RW,CPM86_160K
```

For a larger disk (516 KiB, 128 directory entries) create the image with
`--profile CPM86_516K` and give that drive's line the same identifier:

```sh
python3 tools/diskManager.py --type WORKDSK --os cpm86 --profile CPM86_516K work86.dsk
```

```text
E=/retro/images/cpm86/work86.dsk,RW,CPM86_516K
```

For a 1.44 MB disk (360 tracks, 8 KiB blocks, 128 directory entries) use
`--profile CPM86_1440K` and the identifier `CPM86_1440K`:

```sh
python3 tools/diskManager.py --type WORKDSK --os cpm86 --profile CPM86_1440K work86.dsk
```

```text
E=/retro/images/cpm86/work86.dsk,RW,CPM86_1440K
```

For an 8 MiB disk (16 KiB blocks, 512 directory entries) use
`--profile CPM86_8196K` and the identifier `CPM86_8196K`:

```sh
python3 tools/diskManager.py --type WORKDSK --os cpm86 --profile CPM86_8196K work86.dsk
```

```text
E=/retro/images/cpm86/work86.dsk,RW,CPM86_8196K
```

Use only the shown image roots and size identifiers. Missing optional images
are left offline; malformed or duplicate configuration lines disable B:–F:
and retain the built-in A:. A drive is reported to CP/M-86 only when its image
exists, has exactly the size named by its identifier (163,840, 528,384,
1,474,560 or 8,392,704 bytes) and can be opened with the configured access
mode. Do not switch an image to read/write unless its contents can be safely
modified; use disposable media for write testing.

The tool sets up directories and configuration only; it neither creates nor
overwrites images. Existing `drives.cfg` files are preserved.

### CP/M-80 drives

`diskManager.py --type WORKDSK --os cpm80` creates empty raw CP/M-80 images for any of the four supported sizes. `CPM80_256K` uses 77 tracks × 26 128-byte records, two reserved tracks, 1 KiB blocks and 64 directory entries. `CPM80_512K` uses 77 tracks × 52 128-byte records, two reserved tracks, 2 KiB blocks and 128 directory entries. Both use allocation blocks 0–242. `CPM80_1440K` uses 360 tracks × 32 128-byte records (1,474,560 bytes), two reserved tracks, 8 KiB blocks (DSM 178) and 128 directory entries. Create it with `python3 tools/diskManager.py --type WORKDSK --os cpm80 --profile CPM80_1440K work1440.dsk` and configure the drive as `E=/retro/images/cpm80/work1440.dsk,RW,CPM80_1440K`. `CPM80_8M` is the CP/M 2.2 maximum of 8 MiB: 514 tracks × 128 128-byte records (8,421,376 bytes), two reserved tracks, 16 KiB blocks (DSM 511, EXM 7, 16-bit block pointers) and 512 directory entries. Create it with `python3 tools/diskManager.py --type WORKDSK --os cpm80 --profile CPM80_8M big.dsk` and configure the drive as `E=/retro/images/cpm80/big.dsk,RW,CPM80_8M`. The guest DPB for `CPM80_8M` drives is generated by the firmware; the stock BDOS already supports DSM > 255. It uses only the Python standard library.

```sh
python3 tools/prepareSd.py /Volumes/SDCARD
python3 tools/diskManager.py --type WORKDSK --os cpm80 --profile CPM80_512K ~/Desktop/work.dsk
python3 tools/extractFiles.py --os cpm80 ~/Desktop/work.dsk --add ~/Downloads/MBASIC.COM
cp ~/Desktop/work.dsk /Volumes/SDCARD/retro/images/cpm80/work.dsk
```

The `--add` step accepts ordinary files with valid CP/M 8.3 names; use
`--name MBASIC.COM` if the host filename needs changing. WORKDSK
refuses to overwrite an existing image unless `--force` is supplied.
`extractFiles.py` preserves the image if a file is invalid, duplicated, or
does not fit. The drive configuration is `littlefs/cpm80/drives.cfg` (flashed
with LittleFS); edit that file to choose the drive letter, image path,
`RO`/`RW` access and size identifier (`CPM80_256K`, `CPM80_512K`, `CPM80_1440K`
or `CPM80_8M`). Copy the finished `work.dsk` to the path named by that
configuration and safely eject the card before inserting it in the ESP32-S3.
At the CP/M prompt, switch to `E:` and use `DIR`; a write-enabled CP/M image
is required.

## Import CP/M software

The optional `tools/fetchCpm80Software.py` archive importer was removed from the
repository. Download the desired files or raw disk images yourself and check the
distribution terms for each resource (an archive's mere presence is not permission).
Add files to a work image with `python3 tools/extractFiles.py --os cpm80 <image>
--add <file>` (`--name NAME` to rename); for MBASIC 5.21 use the separately supplied
copy for which you have permission. Install a complete compatible raw image by placing
it under `retro/images/cpm80/` on the SD card; `diskManager.py --type WORKDSK` creates
empty images with the supported geometries.

Portable parser/path/image and CP/M guest boot tests are in `tests/`, built separately with CMake. They do not establish physical SD/USB/WiFi or ESP32 CP/M runtime behavior.
