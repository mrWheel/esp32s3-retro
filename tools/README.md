# Host tools

`prepareSd.py` (Python 3.9+) adds the documented layout to an already formatted/mounted FAT32 card. It does not format, delete, make disk images or rewrite an incompatible layout marker. Run with the card mount root as its sole argument.

`transferTest.py` exercises an actual running device using the session URL displayed on USB. It creates uniquely named files in exchange/common and removes only those files. Do not run it during manual transfers. Its 8 MiB payload intentionally exceeds normal ESP32-S3 internal RAM; the test PC can hold it in memory, while the device must stream.

`buildCpmDisk.py` deterministically composes the read-only CP/M A: image from the checked-in CCP/BDOS outputs and pinned utility binaries. It validates input sizes, CP/M 8.3 names and allocation-block capacity against the DPB, and creates directory extents for larger files. It does not assemble the CCP/BDOS sources; the upstream Macro Assembler AS and `p2bin` are needed for that step. Utility provenance, non-commercial use scope and per-file hashes are in `components/cpmCore/os/utilities/README.md`; the image SHA-256 is in the project checksum inventory. No SD work image is generated; E: requires a separately prepared matching CP/M image.

`HOST.COM` is project-authored Z80 source at `components/cpmCore/os/host/HOST.ASM`. To rebuild it on macOS, install the Z80 assembler with `brew install z80asm`, then run `z80asm -o components/cpmCore/os/host/HOST.COM components/cpmCore/os/host/HOST.ASM`. Rebuild `system.dsk` with `python3 tools/buildCpmDisk.py` after assembling. The checked-in COM image and source are both covered by the SHA-256 inventory.

## Prepare an E: work disk on macOS

`cpmDiskImage.py` creates an empty raw CP/M 2.2 image using the firmware's exact geometry: 77 tracks, 26 128-byte records per track, two reserved tracks, 1 KiB blocks, 64 directory entries, and allocation blocks 0–242. It uses only the Python standard library.

```sh
python3 tools/prepareSd.py /Volumes/SDCARD
python3 tools/cpmDiskImage.py create ~/Desktop/work.dsk
python3 tools/cpmDiskImage.py add ~/Desktop/work.dsk ~/Downloads/MBASIC.COM
python3 tools/cpmDiskImage.py list ~/Desktop/work.dsk
cp ~/Desktop/work.dsk /Volumes/SDCARD/retro/images/cpm/work.dsk
```

The local-file `add` step accepts ordinary files with valid CP/M 8.3 names; use `--name MBASIC.COM` if the host filename needs changing. Create refuses to overwrite an existing image unless `--force` is supplied. `add` preserves the image if a file is invalid, duplicated, or does not fit. Copy the finished `work.dsk` to the shown SD path and safely eject the card before inserting it in the ESP32-S3. At the CP/M prompt, switch to `E:` and use `DIR`; a write-enabled CP/M image is required.

## Import resources from an HTML archive

`fetchCpmSoftware.py` can display an HTML index, fetch one selected link, select members from ZIP files, and add approved individual files to a work image. It also installs a raw disk image only if it is exactly 256,256 bytes and its directory/allocation structure validates. It does not convert ImageDisk `.IMD`, TeleDisk `.TD0`, or arbitrary foreign geometries.

```sh
python3 tools/fetchCpmSoftware.py list http://cpmarchives.classiccmp.org/cpm/mirrors/www.retroarchive.org/cpm/lang/lang.htm
python3 tools/fetchCpmSoftware.py add http://cpmarchives.classiccmp.org/cpm/mirrors/www.retroarchive.org/cpm/lang/lang.htm \
  --link SLR180.ZIP --member SLR180.COM --image ~/Desktop/work.dsk \
  --archive-dir ~/Desktop/cpm-archives \
  --rights-evidence "URL or citation for the applicable distribution terms"
```

ZIP members must be named explicitly; files are read from the archive without extracting paths onto the host. The tool saves each original download unchanged in `--archive-dir` and records its source URLs, timestamp, byte count, SHA-256, selected archive members and supplied rights evidence in `cpm-resource-manifest.json`. The tool requires rights evidence for each network import but cannot verify that evidence or decide whether software is licensed. For MBASIC 5.21, use the separately supplied copy for which you have permission, then add it with `cpmDiskImage.py add`; this avoids downloading that archive copy. To install a complete compatible raw image, use the `disk` subcommand with a new output path under `retro/images/cpm/`; `--force` is required to replace an existing disk.

The importer blocks archive entries identified as Microsoft software, including the linked MBASIC listing. Other links still require you to check and cite the terms for that specific resource; the archive's presence alone is not permission. For raw disk images, the tool checks file size and CP/M directory/allocation structure, but cannot infer sector ordering or prove the archive's stated geometry—verify those from the image documentation before mounting it.

Portable parser/path/image and CP/M guest boot tests are in `tests/`, built separately with CMake. They do not establish physical SD/USB/WiFi or ESP32 CP/M runtime behavior.
