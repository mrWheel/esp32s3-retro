# Apple II ProDOS system-disk files

Place files for the Apple II system disk in this directory. `createSystemDsk.py --os apple2 --profile PRODOS`
(run by `idf.py build` as well) writes every file to a copy of the bootable ProDOS volume
`bootDisks/apple2/prodosEmpty.po` and stores the result as `littlefs/apple2/system.dsk` (volume `/SYSTEM`).

- `.BAS` numbered ASCII Applesoft source (tokenized by the builder, type BAS; `LOAD TEST.NONGR`, then `RUN`)
- `.TXT` plain text
- `.BIN` raw binary: a file named `NAME#ADDR.BIN` (ADDR is 1-4 hex digits) is stored as `NAME` (without extension) with load address ADDR (`BENCH2ASM#6000.BIN` -> `BENCH2ASM` at $6000); other `.BIN` files need `--binary-load-address` supplied to the builder, or a `NAME.ADDR` file (hex load address) next to them
- `.SYS` ProDOS system files

ProDOS names allow only `A-Z`, `0-9` and `.`: `-` and `_` become `.` (`TEST-NONGR.BAS` becomes `TEST.NONGR`) and
`HELLO.BAS` becomes `STARTUP`, which `BASIC.SYSTEM` runs at boot (the 80-column card is selected automatically at
machine start; no `PR#3` needed). The emulator must be rebuilt and flashed again to use a new system.dsk, and
`SD6.1=/littlefs/apple2/system.dsk,RO,APPLE2_140K` must be listed in drives.cfg.
