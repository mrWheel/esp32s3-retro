# bootDisks — system-disk sources

This folder holds the checked-in host-side sources of the bootable system disk of every supported
OS. It is an input of the firmware build: nothing in here runs on the ESP32-S3 directly.

## How it fits together

```text
bootDisks/<os>/systemDsk/  (+ OS boot resources in components/, 
        |                        assets/ and 
        |                        littlefs/cpm86/)
        |
        |  tools/diskManager.py --type SYSTEMDSK \
        |                       --os <os> \
        |                       --profile <DISKPROFILE>
        |  (run by hand, or automatically at CMake configure 
        |   time on every idf.py build)
        v
littlefs/<os>/system.dsk  (system.po for ucsd)  <- part of 
        |                                          the bootfs LittleFS 
        |                                          partition
        |
        |  idf.py flash
        v
ESP32-S3 Retro Computer: mounted as the first (boot) disk 
                            - A: for CP/M, 
                            - SD6.1 for Apple II/UCSD)
                         at every start of the OS
```

A file added or changed here therefore only reaches the computer after `idf.py build` AND a new
flash. Each `systemDsk/` folder has its own `README.md` describing exactly which files it accepts
and how they are converted onto the disk — this top-level file does not repeat those details.

## Layout

| Path | Purpose |
|---|---|
| `<os>/systemDsk/` | The files that are composed onto the system disk (accepted extensions and conversions: see `<os>/systemDsk/README.md`). Exists for `apple2`, `cpm80`, `cpm86` and `ucsd`; `swtpc` is still a placeholder with no system disk. |
| `<os>/utilities/` | Loose guest utilities kept with the OS (for example `FILER.SYS`, `TYPEL.COM`, `REBOOT.CMD`/`.DOC`, the `64K`/`128K` Pascal system files). The system-disk builder does not read this folder and its role is just to have a backup of these files. You can copy them to the `<os>/systemDsk/` folder if you need them on the os-bootDisk. |
| `apple2/prodosEmpty.po` | Bootable empty ProDOS base volume; the Apple II builder copies it and adds the `systemDsk/` files on top. Referenced directly by `CMakeLists.txt`. |

## Ground rules

- `systemDsk/` must stay a flat folder: the builder refuses nested directories and symbolic links,
  ignores `.DS_Store`, and never copies `README.md` onto a disk.
- The profiles `idf.py build` composes with are the CMake cache options
  `APPLE2_SYSTEM_DISK_PROFILE` (default `PRODOS_640K`), `CPM80_SYSTEM_DISK_PROFILE`
  (default `CPM80_1440K`) and `CPM86_SYSTEM_DISK_PROFILE` (default `CPM86_1440K`); UCSD always uses
  `PASCAL_800K`. Keep the CP/M profile equal to the A: identifier in `littlefs/<os>/drives.cfg`.
- The bench programs (`BENCH2`, `BENCH2ASM`, `BENCH80`, `BENCH86`, `BENCHUCSD`) are copies of
  freshly built programs from `assets/`; `tests/test_bootDisksBench.py` checks the copies are current.

Related documents: `tools/README.md` (diskManager usage) and `designCPM80.md`, `designCPM86.md`,
`designUCSD.md`, `designApple2.md` (per-machine boot disks).
