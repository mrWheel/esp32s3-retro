# SYSVIEW

`SYSVIEW.TEXT` is Apple Pascal 1.3 source (guest-compiled), the UCSD Pascal companion of
`assets/cpm80/SYSVIEW` and `assets/cpm86/SYSVIEW`. It shows the emulator information reported
by the RETRO80/RETRO86-style host interface for the shared Apple II machine: firmware version,
machine profile, CPU core, guest RAM, host PSRAM, uptime, executed 6502 cycles, console and the
mounted virtual volumes.

Apple Pascal has no `MEM[]`, so the interface registers are read through the same
variant-record pointer idiom as `assets/ucsd/BENCHUCSD` (`$C090` = -16240).

The interface lives in slot-1 I/O space (slot 1 is unused by the disk controllers, the slot-2
timer and the video card):

- a read of `$C090` returns `A6h` when the firmware supports the interface;
- a write of `1` to `$C090` builds a fresh snapshot;
- reads of `$C091` drain the snapshot byte by byte (zero after the last byte).

The stream starts with the `RETROAPPLE-SYSINFO/1` signature; the program rejects a foreign
responder and reports an unsupported interface explicitly instead of guessing.

Keys: `R` = refresh; `Q` = exit. The output pages after 22 lines ("any key" continues, `Q`
exits). Output goes through the normal Pascal console, so it appears on the 80-column card
(UCSD profile) or the 40-column screen.

The host side is `main/apple2SystemInfo.c` plus the slot-1 attach in `main/apple2Machine.c`.
It is present in both Apple-based menu profiles; the report labels the running profile.

`SYSVIEW.TEXT` is part of `bootDisks/ucsd/systemDsk/`, so a system disk generated with
`python3 tools/diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K` already
contains it. To add it to another volume:

```sh
python3 tools/extractFiles.py --os ucsd /path/to/work.po \
  --add assets/ucsd/SYSVIEW/SYSVIEW.TEXT --type text
```

Boot that volume, press `C`, compile `SYSVIEW` (answer the source prompt with `PASCAL:SYSVIEW.TEXT`
or the volume you copied it to, the code file prompt with `WORK1:SYSVIEW` and press return at the
listing prompt), then `X`ecute `WORK1:SYSVIEW`. The host test
`tests/apple2SysviewTests.c` verifies the stream builder, the slot-1 register protocol (plain and
IIe paths) and an executed 6502 drain of the interface.
