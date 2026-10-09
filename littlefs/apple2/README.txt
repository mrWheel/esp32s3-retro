apple2.rom is the 12 KiB Apple system ROM copied from assets/apple.rom. The
matching source copy and mirror URL are recorded in assets/apple2_roms/.
SHA-256: 378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249
It boots the Apple ][ monitor and Integer BASIC in the host-side Apple II core
test. The ROM image's redistribution license has not been verified.

system.dsk is generated from assets/Apple DOS 3.3P.dsk with:
  PYTHONPATH=tools python3 tools/createSystemDsk.py --os apple2 --profile DOS33 --base-image "assets/Apple DOS 3.3P.dsk" --remove-all-existing

The generated DOS 3.3 image keeps only the bootable DOS system tracks from the
base (Applesoft itself is in the ROM) and removes every base file, locked ones
included. The only catalog entry is TEST-NONGR, a tokenized Applesoft file (DOS
type A). In DOS, use LOAD TEST-NONGR, then RUN. The source listing is
bootDisks/apple2/systemDsk/TEST-NONGR.BAS; empty that directory (README.md
excepted) for a completely empty disk. The builder does not modify the supplied
base image. Because the disk has no HELLO file, DOS prints FILE NOT FOUND once
at boot and then shows the ] prompt.

The Apple II runtime has a read-only, project-authored Disk II controller
(slot 6, drive 1, 16-sector DOS 3.3) that reads this file sector by sector. In
the host-side emulator this image boots Apple DOS 3.3P, `LOAD TEST-NONGR`
succeeds and `RUN` reports "ALL SYSTEM TESTS OK". No drives.cfg is needed for
this single fixed drive. Nothing has been tested on the ESP32-S3 hardware.

The separate project-authored diagnostic test fixture is at
tests/fixtures/apple2-diagnostic.rom and can be regenerated with:
  python3 tools/buildApple2TestRom.py

Phase 1 does not include the Language Card, expansion video card or graphics
renderer. The Disk II is read-only: no writes, no second drive, no 13-sector
media. Next step: flash the firmware and the regenerated system.dsk and check
boot, `LOAD TEST-NONGR` and `RUN` on the board (APPLE-VERIFY-021 is host-only).
