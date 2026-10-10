# Host tools

## Production use versus test use

All Python programs under `tools/` run on the development computer, not on the
ESP32. They prepare images/configuration, generate a source file used by the
firmware, or test the running device. The firmware itself is the C/C++ code in
`main/` and `components/`.

| Program/file | Purpose | Classification |
| --- | --- | --- |
| `tools/diskImage.py` | Create, add, list, extract and validate CP/M images (`--os cpm80` or `cpm86`). | Regular host-side media preparation |
| `tools/buildDiskImage.py` | Build a complete OS data/system image using the selected OS builder. Apple II ProDOS data volumes are supported here too. | Regular host-side media preparation |
| `tools/createSystemDsk.py` | Compose `littlefs/<os>/system.dsk`. CMake runs it automatically for the Apple II ProDOS system image; run it manually for CP/M system images. | Production-image preparation |
| `tools/prepareSd.py` | Create the documented directory layout and default config on an existing SD-card mount; does not format the card or overwrite an existing config. | Regular host-side SD preparation |
| `tools/fetchCpm80Software.py` | Optional CP/M-80 archive/resource importer. Only fetches explicitly selected content; licensing still needs to be verified by the user. | Optional host maintenance; not needed to build/run firmware |
| `tools/buildApple2DiskBootRom.py` | Regenerate `components/apple2Core/apple2DiskBootRom.h`, which is compiled into the Apple II firmware. Normally only run when intentionally regenerating that generated header. | Production build-input generator; not a test |
| `tools/include/diskImageApple2Pascal.py` | Create a blank Apple/UCSD Pascal disk image; not part of the normal `diskImage.py` dispatcher. | Optional host media utility |
| `tools/buildApple2TestRom.py` | Generate the minimal diagnostic ROM fixture in `tests/fixtures/`. | Test-only |
| `tools/transferTest.py` | Exercise the HTTP file-transfer service on a running device. It creates and deletes uniquely named test files. | Test-only; run only when deliberately testing a device |
| `tools/include/*.py` (other files) | OS-specific builders, disk-format implementations, shared helpers and registry imported by the host entrypoints. | Internal modules; not separate production applications |
| `tools/cpm86/diskdefs` | Optional disk geometry definitions for external CP/M image tools; not read by the firmware. | Host-side support data |
| `tools/README.md` | Usage and behavior documentation for these tools. | Documentation |

In short: use the first four programs for normal host-side preparation. The
two explicitly test-only tools are not needed for normal firmware operation.
`buildApple2DiskBootRom.py` is different: although it is usually run only by
developers, its generated header is part of the production firmware.

## OS-independent usage (`--os`)

`diskImage.py`, `buildDiskImage.py` and `prepareSd.py` select the target system with `--os cpm80|cpm86|apple2|swtpc|ucsd`. CP/M-80 and CP/M-86 are supported by both image entrypoints; Apple II is supported by `buildDiskImage.py` for ProDOS volumes, but not by `diskImage.py`. SWTPC and UCSD are registered but not implemented. `-h` shows the general help (commands and OS overview); `-h` together with `--os <name>` adds the OS-specific profiles and rules. Images are written to `sdcard/retro/images/<os>/` (override with `--sd-root`); a path instead of a bare name is used as given. The device's File Transfer feature can copy images to the physical SD card.

```sh
python3 tools/diskImage.py create --os cpm80 --profile LARGE work.dsk
python3 tools/diskImage.py add --os cpm80 work.dsk ~/cpm/*.COM 'utils/*.HLP'
python3 tools/diskImage.py list --os cpm86 work.dsk
python3 tools/buildDiskImage.py --os cpm80
python3 tools/buildDiskImage.py --os cpm86 --source-dir ~/cpm86/files
python3 tools/buildDiskImage.py --os apple2 --profile 800K --name DATA
python3 tools/createSystemDsk.py --os cpm80 --profile SMALL
python3 tools/createSystemDsk.py --os cpm80 --profile LARGE
python3 tools/prepareSd.py --os cpm86
python3 tools/diskImage.py --os cpm86 -h
```

  `add` accepts wildcards (quoted or shell-expanded). Wildcard matches with an invalid 8.3 name or empty content are skipped with a warning; explicitly named files must be valid. 

  `buildDiskImage.py` now writes `system.dsk` to `sdcard/retro/images/<os>/` by default (Apple II: `data<size>.po`) and then reminds you to upload the image to the physical SD card with "File Transfer"; use `--output littlefs/cpm80/system.dsk` to refresh the firmware's LittleFS copy. 

  For CP/M-86 it needs `--source-dir` with `CPM.SYS` and the `.CMD` files (no sources are checked in). 

  `tools/` contains the host-side command-line entrypoints listed above. Reusable OS and format modules, including `osProfiles.py`, `diskImageCpm.py`, `diskImageCpm80.py`, `diskImageCpm86.py`, `diskImageApple2Prodos.py`, `apple2Basic.py` and the per-OS image builders, live in `tools/include/`. The `diskImage.py` and `buildDiskImage.py` entrypoints dispatch to those modules using `--os`; for example, create CP/M-80 images with `python3 tools/diskImage.py create --os cpm80 --profile LARGE work.dsk`.
  
  A new OS needs `tools/include/diskImage<Os>.py`, `tools/include/buildDiskImage<Os>.py` and one entry in `tools/include/osProfiles.py`. Main entrypoints add `tools/include/` to Python's module search path before importing these modules.
  `prepareSd.py` without a mount point prepares `sdcard/`, and without `--os` prepares all systems.

  `prepareSd.py` (Python 3.9+) adds the documented layout and a default CP/M `drives.cfg` to an already formatted/mounted FAT32 card. It does not format, delete, make disk images, overwrite an existing drive configuration or rewrite an incompatible layout marker. Run with the card mount root as its sole argument.

  `transferTest.py` exercises an actual running device using the session URL displayed on USB. It creates uniquely named files in exchange/common and removes only those files. Do not run it during manual transfers. Its 8 MiB payload intentionally exceeds normal ESP32-S3 internal RAM; the test PC can hold it in memory, while the device must stream.

  `tools/include/buildDiskImageCpm80.py` deterministically composes the read-only CP/M A: image from the checked-in CCP/BDOS outputs and pinned utility binaries. It validates input sizes, CP/M 8.3 names and allocation-block capacity against the DPB, and creates directory extents for larger files. It does not assemble the CCP/BDOS sources; the upstream Macro Assembler AS and `p2bin` are needed for that step. Utility provenance and non-commercial use scope are in `components/cpm80Core/os/utilities/README.md`. No SD work image is generated; E: requires a separately prepared matching CP/M image.

  `HOST.COM` is project-authored 8080 source at `guest/cpm80/host/HOST.ASM` (CR+LF line endings), written for the standard CP/M-80 `ASM.COM` and `LOAD.COM`. It is built inside the emulated CP/M-80, not with a host assembler: copy `HOST.ASM` to a writable drive (for example E:) with `diskImage.py add`, then run `A:ASM HOST` and `A:LOAD HOST` there and extract `HOST.COM` with `diskImage.py extract`. `CPM80_HOST_ASM_OUTPUT=<path> ./build-host/hostTests` does exactly this headless and stores the result at `<path>`; the normal `hostTests` run fails when `bootDisks/cpm80/systemDsk/HOST.COM` differs from a fresh build. Copy the result to `bootDisks/cpm80/systemDsk/HOST.COM` and `guest/cpm80/host/HOST.COM`, then rebuild the system image with `python3 tools/createSystemDsk.py --os cpm80 --profile LARGE`.

### Create a SYSTEM.DSK on LittleFS

`createSystemDsk.py` builds `littlefs/<os>/system.dsk` from OS resources and
files in `bootDisks/<os>/systemDisk/` (it also accepts the existing CP/M-80
spelling `systemDsk`). Apple II uses a ProDOS base image. Use
`--source-dir` to select another folder and `--output` to choose another
destination. The destination image is replaced when the build succeeds:

```sh
python3 tools/createSystemDsk.py --os cpm80 --profile SMALL
python3 tools/createSystemDsk.py --os cpm80 --profile LARGE
python3 tools/createSystemDsk.py --os cpm86 --profile SMALL
python3 tools/createSystemDsk.py --os apple2 --profile PRODOS
python3 tools/createSystemDsk.py --os apple2 --profile PRODOS --binary-load-address 0x800
```

Executable files (`.COM` for CP/M-80 and `.CMD` for CP/M-86) are installed
before other files. If remaining non-executable files do not fit, the builder
keeps the executables, skips the files that do not fit, and prints
`Disk too small`. If an executable itself cannot fit, the build fails rather
than producing a disk missing a program. The project HOST program is used in
preference to a different `HOST.COM`/`HOST.CMD` in the source folder.

`SMALL` uses 256,256 bytes for CP/M-80 and 163,840 bytes for CP/M-86. `LARGE`
uses 512,512 bytes for CP/M-80 and 528,384 bytes for CP/M-86. Both profiles boot
as A:: the firmware detects the layout from the size of
`littlefs/<os>/system.dsk`, so `drives.cfg` needs only `SYSTEM` (CP/M-80) or
`RETRO86_SYSTEM` (CP/M-86) for A:. A size that matches no system profile is
reported as an error and A: is not mounted. CP/M-86 is built from an empty
image plus the source directory only (never from an older `system.dsk`), so
`HOST.CMD` must be in `bootDisks/cpm86/systemDsk`. It requires
`littlefs/cpm86/cpm.sys` and `littlefs/cpm86/retro86bios.h86` to remain in place.
The builder validates these runtime files but does not put them on A::
firmware loads them directly from LittleFS. In particular, the duplicate
`CPM.SYS` is never placed on the generated CP/M-86 A: image.
For Apple II, the firmware build (`idf.py build`/`flash`) runs `createSystemDsk.py` at CMake configure time (see `CMakeLists.txt`), so `littlefs/apple2/system.dsk` always matches `bootDisks/apple2/systemDsk/`. The `APPLE2_SYSTEM_DISK_PROFILE` CMake option selects `PRODOS` (140K), `PRODOS_640K`, or `PRODOS_800K`; for example, use `-DAPPLE2_SYSTEM_DISK_PROFILE=PRODOS_800K` for an 800K bootable system volume in the `bootfs` LittleFS partition. CP/M disks are not generated by the build; run the script for them manually.
The Apple II emulator uses ProDOS disks only; DOS 3.3 is not supported. For Apple II,
`createSystemDsk.py` copies the bootable base volume `bootDisks/apple2/prodosEmpty.po`
(140K, volume `/SYSTEM`: `PRODOS`, `BASIC.SYSTEM`, `QUIT.SYSTEM`, `BITSY.BOOT`), adds every
file from `bootDisks/apple2/systemDsk/` and writes `littlefs/apple2/system.dsk`
by default. Use `--source-dir` to select another file folder, `--output` to
choose the destination and `--base-image` to use another bootable ProDOS volume.
The base image is never modified.

Source files are named by suffix: `.BAS` (numbered ASCII Applesoft source, tokenized
by the builder, ProDOS type BAS), `.TXT` (text), `.BIN` (raw binary; requires
`--binary-load-address`, decimal or `0x`-prefixed hexadecimal, applied to every
binary in the source folder) and `.SYS`. ProDOS names allow only `A-Z`, `0-9` and
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
Pascal (Disk II) images in a slot of their own. `SP5.1`/`SP5.2` select the same
kind of device explicitly as a SmartPort interface in slot 5 (`PR#5`).
The firmware passes 512-byte block reads and writes to the image backend,
preserving either DOS or ProDOS sector ordering and the configured `RO`/`RW`
access. The guest filesystem owns all ProDOS directory and bitmap updates.

**ProDOS boot disk (`SD6.1`).** `system.dsk` is part of the firmware. To boot
ProDOS from `SD6.1` (list it in `drives.cfg`; there is no default drive, and the
emulator reports an error and returns to the system menu when `SD6.1` is missing):

```sh
python3 tools/createSystemDsk.py --os apple2 --profile PRODOS
python3 tools/createSystemDsk.py --os apple2 --profile PRODOS_800K
idf.py build
```

The default volume is `/SYSTEM` (140K, DOS sector order like every ProDOS `.dsk`). Larger bootable 640K and 800K volumes can be selected with `PRODOS_640K` and `PRODOS_800K`:
`PRODOS`, `BASIC.SYSTEM`, `QUIT.SYSTEM`, `BITSY.BOOT` from
`bootDisks/apple2/prodosEmpty.po`, plus every file in
`bootDisks/apple2/systemDsk/`. ProDOS names allow only `A-Z`, `0-9` and `.`, so
`-` and `_` become `.` (`TEST-NONGR.BAS` becomes `TEST.NONGR`) and `HELLO.BAS`
becomes `STARTUP`, which `BASIC.SYSTEM` runs at boot. To use the new `system.dsk` the emulator must be
rebuilt and flashed again; both programs print this reminder. Both
`createSystemDsk.py` and `buildDiskImage.py` end with an English message when
an image has to be rebuilt/flashed or uploaded.

**ProDOS data volumes (SD card).** `buildDiskImage.py --os apple2` writes a
ProDOS volume to `sdcard/retro/images/apple2/`:

```sh
python3 tools/buildDiskImage.py --os apple2                     # data800.po, volume /DATA
python3 tools/buildDiskImage.py --os apple2 --profile 140K --source-dir ~/files WORK.po
python3 tools/buildDiskImage.py --os apple2 --bootable --name SYSTEM prodosBoot.po
```

Options: `--profile 140K|640K|800K` (default 800K), `--name` (volume name),
`--source-dir` (files get a ProDOS type from the `.BAS`, `.TXT`, `.BIN`, `.SYS`
suffix; `.BIN` needs `--binary-load-address`), `--bootable` or `--boot-from`
(boot blocks and system files from `prodosEmpty.po` or another ProDOS image).
The image is **not** on the SD card yet: upload it with "File Transfer" (emulator
menu option 6) to `/retro/images/apple2/` and add it to `drives.cfg`, for
example `SD6.2=/retro/images/apple2/data800.po,RW,APPLE2_800K` (in ProDOS `/DATA`,
`CATALOG,S6,D2`). Use the `buildDiskImage.py` entrypoint's `--bootable` or
`--boot-from` option to include boot files in a generated volume.

The host guest tests boot ProDOS 2.4.2 through SmartPort, boot a generated
ProDOS `system.dsk` from `SD6.1` with autostart, catalog the 800K volume and
`SAVE` a BASIC file to it; hardware behavior remains unverified. Use
disposable images until guest writes have been qualified on the target hardware.

## Prepare CP/M disk images on macOS

### CP/M-86 drives

`prepareSd.py --os cpm86` creates `/retro/images/cpm86/drives.cfg` if it does not
already exist. The CP/M-86 BIOS supports drives A: through F:. A: is the
read-only LittleFS system image; B: through F: are optional SD images. All six
drives use the same validated 160 KiB `CPM86` image geometry (40 tracks, eight
512-byte sectors per track, 128-byte guest records, 1 KiB allocation blocks and
64 directory entries).

The generated configuration maps E: to `work86.dsk` and sets it to read/write.
Create the image if needed, then copy it to that path on the card:

```sh
python3 tools/diskImage.py create --os cpm86 --profile CPM86 work86.dsk
cp sdcard/retro/images/cpm86/work86.dsk /Volumes/SDCARD/retro/images/cpm86/work86.dsk
```

The configuration uses one line per drive:

```text
A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM
B=/retro/images/cpm86/languages.dsk,RO,RETRO86_DATA_V1
C=/retro/images/cpm86/tools.dsk,RO,RETRO86_DATA_V1
D=/retro/images/cpm86/utilities.dsk,RO,RETRO86_DATA_V1
E=/retro/images/cpm86/work86.dsk,RW,RETRO86_DATA_V1
F=/retro/images/cpm86/archive.dsk,RW,RETRO86_DATA_V1
```

For a larger disk (516 KiB, 128 directory entries) create the image with
`--profile LARGE` and give that drive's line (B:–F: only) the profile
`RETRO86_DATA_LARGE_V1`:

```sh
python3 tools/diskImage.py create --os cpm86 --profile LARGE work86.dsk
```

```text
E=/retro/images/cpm86/work86.dsk,RW,RETRO86_DATA_LARGE_V1
```

For an 8 MiB disk (16 KiB blocks, 512 directory entries) use `--profile BIG`
and the profile `RETRO86_DATA_BIG_V1` (B:–F: only):

```sh
python3 tools/diskImage.py create --os cpm86 --profile BIG work86.dsk
```

```text
E=/retro/images/cpm86/work86.dsk,RW,RETRO86_DATA_BIG_V1
```

Use only the shown image roots and profile identifiers. Missing optional images
are left offline; malformed or duplicate configuration lines disable B:–F:
and retain the built-in A:. A drive is reported to CP/M-86 only when its image
exists, has exactly its profile's size (163,840 or 528,384 bytes) and can be opened with the configured access
mode. Do not switch an image to read/write unless its contents can be safely
modified; use disposable media for write testing.

The tool sets up directories and configuration only; it neither creates nor
overwrites images. Existing `drives.cfg` files are preserved.

### CP/M-80 drives

`diskImage.py --os cpm80` creates empty raw CP/M-80 images for either supported profile. `SYSTEM` uses 77 tracks × 26 128-byte records, two reserved tracks, 1 KiB blocks and 64 directory entries. `LARGE` uses 77 tracks × 52 128-byte records, two reserved tracks, 2 KiB blocks and 128 directory entries. Both profiles use allocation blocks 0–242. `BIG` is the CP/M 2.2 maximum of 8 MiB: 514 tracks × 128 128-byte records (8,421,376 bytes), two reserved tracks, 16 KiB blocks (DSM 511, EXM 7, 16-bit block pointers) and 512 directory entries. Create it with `python3 tools/diskImage.py create --os cpm80 --profile BIG big.dsk` and configure the drive as `E=/retro/images/cpm80/big.dsk,RW,BIG`. The guest DPB for `BIG` drives is generated by the firmware; the stock BDOS already supports DSM > 255. It uses only the Python standard library.

```sh
python3 tools/prepareSd.py /Volumes/SDCARD
python3 tools/diskImage.py create --os cpm80 --profile LARGE ~/Desktop/work.dsk
python3 tools/diskImage.py add --os cpm80 ~/Desktop/work.dsk ~/Downloads/MBASIC.COM
python3 tools/diskImage.py list --os cpm80 ~/Desktop/work.dsk
cp ~/Desktop/work.dsk /Volumes/SDCARD/retro/images/cpm80/work.dsk
```

The local-file `add` step accepts ordinary files with valid CP/M 8.3 names; use `--name MBASIC.COM` if the host filename needs changing. Create refuses to overwrite an existing image unless `--force` is supplied. `add` preserves the image if a file is invalid, duplicated, or does not fit. `prepareSd.py` writes `retro/images/cpm80/drives.cfg` only when it does not already exist; edit that file to choose the drive letter, image path, `RO`/`RW` access and `SYSTEM`/`LARGE` profile. Copy the finished `work.dsk` to the path named by that configuration and safely eject the card before inserting it in the ESP32-S3. At the CP/M prompt, switch to `E:` and use `DIR`; a write-enabled CP/M image is required.

## Import resources from an HTML archive

`fetchCpm80Software.py` can display an HTML index, fetch one selected link, select members from ZIP files, and add approved individual files to a work image. It also installs a raw disk image only if it matches one of the supported image sizes and its directory/allocation structure validates. It does not convert ImageDisk `.IMD`, TeleDisk `.TD0`, or arbitrary foreign geometries.

```sh
python3 tools/fetchCpm80Software.py list http://cpmarchives.classiccmp.org/cpm/mirrors/www.retroarchive.org/cpm/lang/lang.htm
python3 tools/fetchCpm80Software.py add http://cpmarchives.classiccmp.org/cpm/mirrors/www.retroarchive.org/cpm/lang/lang.htm \
  --link SLR180.ZIP --member SLR180.COM --image ~/Desktop/work.dsk \
  --archive-dir ~/Desktop/cpm-archives \
  --rights-evidence "URL or citation for the applicable distribution terms"
```

ZIP members must be named explicitly; files are read from the archive without extracting paths onto the host. The tool saves each original download unchanged in `--archive-dir` and records its source URLs, timestamp, byte count, SHA-256, selected archive members and supplied rights evidence in `cpm-resource-manifest.json`. The tool requires rights evidence for each network import but cannot verify that evidence or decide whether software is licensed. For MBASIC 5.21, use the separately supplied copy for which you have permission, then add it with `python3 tools/diskImage.py add --os cpm80`; this avoids downloading that archive copy. To install a complete compatible raw image, use the `disk` subcommand with a new output path under `retro/images/cpm80/`; `--force` is required to replace an existing disk.

The importer blocks archive entries identified as Microsoft software, including the linked MBASIC listing. Other links still require you to check and cite the terms for that specific resource; the archive's presence alone is not permission. For raw disk images, the tool checks file size and CP/M directory/allocation structure, but cannot infer sector ordering or prove the archive's stated geometry—verify those from the image documentation before mounting it.

Portable parser/path/image and CP/M guest boot tests are in `tests/`, built separately with CMake. They do not establish physical SD/USB/WiFi or ESP32 CP/M runtime behavior.
