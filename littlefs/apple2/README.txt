apple2.rom is the 12 KiB Apple system ROM copied from assets/apple.rom. The
matching source copy and mirror URL are recorded in assets/apple2_roms/.
SHA-256: 378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249
It boots the Apple ][ monitor and Integer BASIC in the host-side Apple II core
test. The ROM image's redistribution license has not been verified.

system.dsk is the bootable ProDOS 8 volume /SYSTEM (140K), generated from the base
bootDisks/apple2/prodosEmpty.po plus every file in bootDisks/apple2/systemDsk by the
firmware build (CMake configure time), or manually with:
  PYTHONPATH=tools python3 tools/createSystemDsk.py --os apple2 --profile PRODOS
HELLO.BAS becomes STARTUP (run by BASIC.SYSTEM at boot; the 80-column card is selected
automatically when the machine starts) and TEST-NONGR.BAS becomes TEST.NONGR (tokenized
Applesoft; LOAD TEST.NONGR, then RUN). The emulator must be rebuilt AND flashed again to
use a new system.dsk.

The Apple II uses ProDOS disks only. There is no default drive: drives.cfg on the SD card
must list /littlefs/apple2/system.dsk as SD6.1. When SD6.1 is not configured or its image
cannot be opened, the emulator prints an error at start-up and returns to the system menu.

The separate project-authored diagnostic test fixture is at
tests/fixtures/apple2-diagnostic.rom and can be regenerated with:
  python3 tools/buildApple2TestRom.py

More disks: /microSD/retro/images/apple2/drives.cfg (see sdcard/retro/images/apple2/drives.cfg)
maps images to slots 4..7, drives 1..2, for example the 800K read/write ProDOS data volumes
made with tools/buildDiskImage.py --os apple2 --name DATA1 data1.po:
  SD6.1=/littlefs/apple2/system.dsk,RO,APPLE2_140K
  SD6.2=/retro/images/apple2/data1.po,RW,APPLE2_800K
  SD5.1=/retro/images/apple2/data2.po,RW,APPLE2_800K
Upload the data images to /retro/images/apple2/ with "File Transfer". An Apple (UCSD)
Pascal 640K volume (tools/diskImageApple2Pascal.py) must have a slot of its own (Disk II).
