# CP/M-86 system-disk files

Place files for the CP/M-86 system disk (drive A:) in this directory. Every
`diskManager.py --type SYSTEMDSK --os cpm86 --profile <DISKPROFILE>` run — and every `idf.py build`
(the top-level CMakeLists regenerates the disk from this folder with profile `CPM86_1440K`; keep it
equal to the A: identifier in `littlefs/cpm86/drives.cfg`) — stores every file of this folder, after
the OS-specific conversion below, in `littlefs/cpm86/system.dsk`.

The builder formats a blank disk for the selected profile and then adds every file of this directory:

- names must be valid CP/M 8.3 names: 1-8 name characters plus an optional 1-3 character extension,
  `A-Z`, `0-9`, `_`, `$` and `-` (lower case is uppercased)
- `.CMD` and `.SYS` programs are installed first so they always fit; when one does not fit the run aborts
- the remaining files (`.HLP`, `.TXT`, `.A86`, ...) follow; they are skipped with a warning when
  the disk or directory is full
- `CPM.SYS` is refused here: the CP/M-86 kernel is loaded from `littlefs/cpm86/cpm.sys` and the BIOS
  overlay from `littlefs/cpm86/retro86bios.h86`, both kept outside A:
- `README.md` is not copied to the disk

Profiles: `CPM86_160K`, `CPM86_516K`, `CPM86_1440K` and `CPM86_8196K`, for example:

```sh
python3 tools/diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_1440K
```

`idf.py flash` flashes the LittleFS partition to the ESP32-S3 Retro Computer, so the generated
`littlefs/cpm86/system.dsk` is the disk the machine mounts as `A=/littlefs/cpm86/system.dsk` — the
first (boot) disk at every CP/M-86 start. Files added or changed here therefore only reach the
computer after `idf.py build` AND a new flash.
