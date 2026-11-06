# CP/M-80 system-disk files

Place files for the CP/M-80 system disk (drive A:) in this directory. Every
`diskManager.py --type SYSTEMDSK --os cpm80 --profile <DISKPROFILE>` run — and every `idf.py build`
(the top-level CMakeLists regenerates the disk from this folder with profile `CPM80_1440K`; keep it
equal to the A: identifier in `littlefs/cpm80/drives.cfg`) — stores every file of this folder, after
the OS-specific conversion below, in `littlefs/cpm80/system.dsk`.

The builder starts from the genuine CCP and BDOS of `components/cpm80Core/os/` (plus the RETROCPM
resource header in the reserved tracks) and then adds every file of this directory:

- names must be valid CP/M 8.3 names: 1-8 name characters plus an optional 1-3 character extension,
  `A-Z`, `0-9`, `_`, `$` and `-` (lower case is uppercased)
- `.COM` programs are installed first so they always fit; when one does not fit the run aborts
- the remaining files (`.HLP`, `.TXT`, sources, ...) follow; they are skipped with a warning when
  the disk or directory is full
- `README.md` is not copied to the disk

Profiles: `CPM80_256K`, `CPM80_512K` and `CPM80_1440K` (A: is never `CPM80_8M`), for example:

```sh
python3 tools/diskManager.py --type SYSTEMDSK --os cpm80 --profile CPM80_1440K
```

`idf.py flash` flashes the LittleFS partition to the ESP32-S3 Retro Computer, so the generated
`littlefs/cpm80/system.dsk` is the disk the machine mounts as `A=/littlefs/cpm80/system.dsk` — the
first (boot) disk at every CP/M-80 start. Files added or changed here therefore only reach the
computer after `idf.py build` AND a new flash.
