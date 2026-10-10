apple2.rom is the 12 KiB Apple system ROM copied from assets/apple.rom. The
matching source copy and mirror URL are recorded in assets/apple2_roms/.
SHA-256: 378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249
It boots the Apple ][ monitor and Integer BASIC in the host-side Apple II core
test. The ROM image's redistribution license has not been verified.

system.dsk is generated from the empty DOS 3.3P base bootDisks/apple2/dos33Empty.dsk
(boot tracks only, empty catalog; Applesoft itself is in the ROM) with:
  PYTHONPATH=tools python3 tools/createSystemDsk.py --os apple2 --profile DOS33

Everything in bootDisks/apple2/systemDsk is written to the disk: HELLO (the
greeting program; the 80-column card is selected automatically when the machine
starts, so HELLO needs no PR#3) and TEST-NONGR, a tokenized Applesoft
file (DOS type A). In DOS, use LOAD TEST-NONGR, then RUN. The builder does not
modify the base image; --base-image selects another base.

The Apple II runtime has a read-only, project-authored Disk II controller
(16-sector media, slots 4..7, two drives per slot) that reads this file sector by sector.
By default only slot 6, drive 1 is used, with this file. In
the host-side emulator this image boots Apple DOS 3.3P, `LOAD TEST-NONGR`
succeeds and `RUN` reports "ALL SYSTEM TESTS OK". No drives.cfg is needed for
this default drive. Nothing has been tested on the ESP32-S3 hardware.

The separate project-authored diagnostic test fixture is at
tests/fixtures/apple2-diagnostic.rom and can be regenerated with:
  python3 tools/buildApple2TestRom.py

Phase 1 does not include the Language Card, expansion video card or graphics
renderer. The Disk II is read-only: no writes, no 13-sector media. Next step: flash the firmware and the regenerated system.dsk and check
boot, `LOAD TEST-NONGR` and `RUN` on the board (APPLE-VERIFY-021 is host-only).

More disks: /microSD/retro/images/apple2/drives.cfg (see the commented example
in sdcard/retro/images/apple2/drives.cfg) maps images to slots 4..7, drives 1..2:
  PR6.2=/retro/images/apple2/pascal640.po,RO,APPLE2_640K
A 640K image is 655360 bytes = 160 tracks x 16 sectors x 256 bytes = 1280 Apple
(UCSD) Pascal blocks of 512 bytes, stored in ProDOS order (block n at byte n*512,
.po). tools/diskImageApple2Pascal.py builds a blank formatted Pascal volume:
  PYTHONPATH=tools python3 tools/diskImageApple2Pascal.py --name WORK pascal640.po
The guest sees these as Disk II drives; stock DOS 3.3 and Pascal handling of 160
tracks has not been verified (see designApple2.md, APPLE-ISSUE-016).
