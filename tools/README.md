# Host tools

## OS-independent usage (`--os`)

`diskImage.py`, `buildDiskImage.py` and `prepareSd.py` select the target system with `--os cpm80|cpm86|apple2|swtpc|ucsd`. CP/M-80 and CP/M-86 work today; the other systems are registered in `osProfiles.py` and are refused as "not implemented yet" until a backend is added. `-h` shows the general help (commands and OS overview); `-h` together with `--os <name>` adds the OS-specific profiles and rules. Images are written to `sdcard/retro/images/<os>/` (override with `--sd-root`); a path instead of a bare name is used as given. The web server/GUI can later copy these images to the physical SD card.

```sh
python3 tools/diskImage.py create --os cpm80 --profile LARGE work.dsk
python3 tools/diskImage.py add --os cpm80 work.dsk ~/cpm/*.COM 'utils/*.HLP'
python3 tools/diskImage.py list --os cpm86 work.dsk
python3 tools/buildDiskImage.py --os cpm80
python3 tools/buildDiskImage.py --os cpm86 --source-dir ~/cpm86/files
python3 tools/createSystemDsk.py --os cpm80 --profile SMALL
python3 tools/createSystemDsk.py --os cpm80 --profile LARGE
python3 tools/prepareSd.py --os cpm86
python3 tools/diskImage.py --os cpm86 -h
```

  `add` accepts wildcards (quoted or shell-expanded). Wildcard matches with an invalid 8.3 name or empty content are skipped with a warning; explicitly named files must be valid. 

  `buildDiskImage.py` now writes `system.dsk` to `sdcard/retro/images/<os>/` by default; use `--output littlefs/cpm80/system.dsk` to refresh the firmware's LittleFS copy. 

  For CP/M-86 it needs `--source-dir` with `CPM.SYS` and the `.CMD` files (no sources are checked in). 

  The work is split per OS: `diskImage.py` and `buildDiskImage.py` dispatch (via `osProfiles.py`) to `diskImageCpm80.py`/`diskImageCpm86.py` and `buildDiskImageCpm80.py`/`buildDiskImageCpm86.py`, which share the CP/M engine `diskImageCpm.py`; the per-OS modules also run directly (e.g. `diskImageCpm80.py create ...` implies `--os cpm80`). 
  
  A new OS needs `diskImage<Os>.py`, `buildDiskImage<Os>.py` and one entry in `osProfiles.py`. 
  `prepareSd.py` without a mount point prepares `sdcard/`, and without `--os` prepares all systems.

  `prepareSd.py` (Python 3.9+) adds the documented layout and a default CP/M `drives.cfg` to an already formatted/mounted FAT32 card. It does not format, delete, make disk images, overwrite an existing drive configuration or rewrite an incompatible layout marker. Run with the card mount root as its sole argument.

  `transferTest.py` exercises an actual running device using the session URL displayed on USB. It creates uniquely named files in exchange/common and removes only those files. Do not run it during manual transfers. Its 8 MiB payload intentionally exceeds normal ESP32-S3 internal RAM; the test PC can hold it in memory, while the device must stream.

  `buildDiskImageCpm80.py` deterministically composes the read-only CP/M A: image from the checked-in CCP/BDOS outputs and pinned utility binaries. It validates input sizes, CP/M 8.3 names and allocation-block capacity against the DPB, and creates directory extents for larger files. It does not assemble the CCP/BDOS sources; the upstream Macro Assembler AS and `p2bin` are needed for that step. Utility provenance and non-commercial use scope are in `components/cpm80Core/os/utilities/README.md`. No SD work image is generated; E: requires a separately prepared matching CP/M image.

  `HOST.COM` is project-authored 8080 source at `guest/cpm80/host/HOST.ASM` (CR+LF line endings), written for the standard CP/M-80 `ASM.COM` and `LOAD.COM`. It is built inside the emulated CP/M-80, not with a host assembler: copy `HOST.ASM` to a writable drive (for example E:) with `diskImage.py add`, then run `A:ASM HOST` and `A:LOAD HOST` there and extract `HOST.COM` with `diskImage.py extract`. `CPM80_HOST_ASM_OUTPUT=<path> ./build-host/hostTests` does exactly this headless and stores the result at `<path>`; the normal `hostTests` run fails when `bootDisks/cpm80/systemDsk/HOST.COM` differs from a fresh build. Copy the result to `bootDisks/cpm80/systemDsk/HOST.COM` and `guest/cpm80/host/HOST.COM`, then rebuild the system image with `python3 tools/createSystemDsk.py --os cpm80 --profile LARGE`.

### Create a LittleFS system disk

`createSystemDsk.py` builds `littlefs/<os>/system.dsk` from OS resources and
files in `bootDisks/<os>/systemDisk/` (it also accepts the existing CP/M-80
spelling `systemDsk`). Apple II uses a supplied DOS base image. Use
`--source-dir` to select another folder and `--output` to choose another
destination. The destination image is replaced when the build succeeds:

```sh
python3 tools/createSystemDsk.py --os cpm80 --profile SMALL
python3 tools/createSystemDsk.py --os cpm80 --profile LARGE
python3 tools/createSystemDsk.py --os cpm86 --profile SMALL
python3 tools/createSystemDsk.py --os apple2 --profile DOS33
python3 tools/createSystemDsk.py --os apple2 --profile DOS33 --binary-load-address 0x800
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
For Apple II, `createSystemDsk.py` copies the base image
`bootDisks/apple2/dos33Empty.dsk` (a 143,360-byte DOS 3.3-order image with the
DOS boot tracks and an empty catalog; Applesoft itself is in the ROM), adds every
file from `bootDisks/apple2/systemDsk/` and writes `littlefs/apple2/system.dsk`
by default. Use `--source-dir` to select another file folder, `--output` to
choose the destination and `--base-image` to use another bootable 143,360-byte
DOS 3.3 base image. The builder validates the geometry and catalog of the base
but cannot establish that its boot sectors are functional. The base image is
never modified.
Use repeatable `--remove-existing "FILENAME"` options to free space by removing
explicitly selected, unlocked files from a copied (non-empty) base image before
adding the source files; locked files are refused.
`bootDisks/apple2/dos33Empty.dsk` was derived once from `assets/Apple DOS 3.3P.dsk`
by deleting every catalog entry; only use a base image you are authorized to use.

Apple DOS filenames support `.TXT` (DOS text; line endings become carriage
returns and Applesoft listings can be entered with `EXEC NAME.TXT`, then
`RUN`), `.BAS` (tokenized Applesoft; numbered ASCII source is tokenized by the
builder and can be loaded with DOS `LOAD`, then started with `RUN`),
`.INT` (tokenized Integer BASIC), and `.BIN` (raw binary). Raw `.BIN` files
require `--binary-load-address` (decimal or `0x`-prefixed hexadecimal), which
is applied to every binary in the source folder. All files must fit; collisions
with files already on the base image are errors. For `.BAS` inputs, the suffix
is a host-side type marker and is omitted from the DOS catalog; for example,
`TEST-NONGR.BAS` is loaded with `LOAD TEST-NONGR`, then started with `RUN`. The
disk builder creates a host image only; the Apple II runtime reads it through the
read-only Disk II controller. Tokenized `.BAS` files get the 2-byte length prefix
that DOS 3.3 Applesoft files carry; the file type (A) comes from the `.BAS`
suffix, so no extra name marker is needed.

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

`diskImageCpm80.py` creates empty raw CP/M-80 images for either supported profile. `SYSTEM` uses 77 tracks × 26 128-byte records, two reserved tracks, 1 KiB blocks and 64 directory entries. `LARGE` uses 77 tracks × 52 128-byte records, two reserved tracks, 2 KiB blocks and 128 directory entries. Both profiles use allocation blocks 0–242. `BIG` is the CP/M 2.2 maximum of 8 MiB: 514 tracks × 128 128-byte records (8,421,376 bytes), two reserved tracks, 16 KiB blocks (DSM 511, EXM 7, 16-bit block pointers) and 512 directory entries. Create it with `python3 tools/diskImage.py create --os cpm80 --profile BIG big.dsk` and configure the drive as `E=/retro/images/cpm80/big.dsk,RW,BIG`. The guest DPB for `BIG` drives is generated by the firmware; the stock BDOS already supports DSM > 255. It uses only the Python standard library.

```sh
python3 tools/prepareSd.py /Volumes/SDCARD
python3 tools/diskImageCpm80.py create --profile LARGE ~/Desktop/work.dsk
python3 tools/diskImageCpm80.py add ~/Desktop/work.dsk ~/Downloads/MBASIC.COM
python3 tools/diskImageCpm80.py list ~/Desktop/work.dsk
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

ZIP members must be named explicitly; files are read from the archive without extracting paths onto the host. The tool saves each original download unchanged in `--archive-dir` and records its source URLs, timestamp, byte count, SHA-256, selected archive members and supplied rights evidence in `cpm-resource-manifest.json`. The tool requires rights evidence for each network import but cannot verify that evidence or decide whether software is licensed. For MBASIC 5.21, use the separately supplied copy for which you have permission, then add it with `diskImageCpm80.py add`; this avoids downloading that archive copy. To install a complete compatible raw image, use the `disk` subcommand with a new output path under `retro/images/cpm80/`; `--force` is required to replace an existing disk.

The importer blocks archive entries identified as Microsoft software, including the linked MBASIC listing. Other links still require you to check and cite the terms for that specific resource; the archive's presence alone is not permission. For raw disk images, the tool checks file size and CP/M directory/allocation structure, but cannot infer sector ordering or prove the archive's stated geometry—verify those from the image documentation before mounting it.

Portable parser/path/image and CP/M guest boot tests are in `tests/`, built separately with CMake. They do not establish physical SD/USB/WiFi or ESP32 CP/M runtime behavior.
