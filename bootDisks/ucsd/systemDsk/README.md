# UCSD Pascal system-disk files

Place files for the UCSD Pascal system disk in this directory. Every
`diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K` run — and every `idf.py build`
(the top-level CMakeLists regenerates the disk from this folder; `PASCAL_800K` is the only profile) —
stores every file of this folder, after the OS-specific conversion below, in `littlefs/ucsd/system.po`.

The builder copies the clean Apple Pascal 1.3 boot volume
`assets/ucsd/ucsdDiskImages/Apple Pascal 1.3 3.5.po` and then adds every file of this directory;
a source file replaces an image file with the same name:

- `128K.APPLE` and `128K.PASCAL` are renamed to `SYSTEM.APPLE` and `SYSTEM.PASCAL`, the names the
  Pascal boot loader looks for
- `.CODE` and `.TEXT` files keep their name
- `README.md` is not copied to the disk

```sh
python3 tools/diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K
```

`idf.py flash` flashes the LittleFS partition to the ESP32-S3 Retro Computer, so the generated
`littlefs/ucsd/system.po` is the disk mounted as `SD6.1=/littlefs/ucsd/system.po,RO,APPLE2_800K` in
`drives.cfg` — the first (boot) disk at every UCSD Pascal start (SD6.1 is the boot drive). Files added
or changed here therefore only reach the computer after `idf.py build` AND a new flash.
