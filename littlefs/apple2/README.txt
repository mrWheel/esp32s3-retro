apple2.rom is the 12 KiB Apple system ROM copied from assets/apple.rom. The
matching source copy and mirror URL are recorded in assets/apple2_roms/.
SHA-256: 378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249
It boots the Apple ][ monitor and Integer BASIC in the host-side Apple II core
test. The ROM image's redistribution license has not been verified.

system.dsk is generated from assets/Apple DOS 3.3P.dsk with:
  PYTHONPATH=tools .venv-1/bin/python tools/createSystemDsk.py --os apple2 --profile DOS33 --base-image "assets/Apple DOS 3.3P.dsk" --remove-existing "LOCKSMITH 4.1"

The generated DOS 3.3 image omits only LOCKSMITH 4.1 from its copy of the
base, preserves the other base catalog entries, and contains TEST-NONGR as a
tokenized Applesoft file (DOS type A). In DOS, use LOAD TEST-NONGR, then RUN.
The source listing is bootDisks/apple2/systemDsk/TEST-NONGR.BAS. The builder
does not modify the supplied base image.

The image's VTOC and catalog and the stored tokenized program have been
validated by host-side tests. The Apple II runtime now has a read-only,
project-authored Disk II controller (slot 6, drive 1, 16-sector DOS 3.3) that
reads this file sector by sector. In the host-side emulator this image boots
Apple DOS 3.3P to the HELLO banner and `LOAD TEST-NONGR` succeeds. No
drives.cfg is needed for this single fixed drive. Nothing has been tested on
the ESP32-S3 hardware.

Known blocker: `RUN` of TEST-NONGR does not work. The image builder stores the
tokenized program without the 2-byte length prefix that Applesoft DOS files
need and its token table skips `&` ($AF), so tokens from $AF upward are one
too low (APPLE-ISSUE-014 in designApple2.md). This is not a Disk II fault.

The separate project-authored diagnostic test fixture is at
tests/fixtures/apple2-diagnostic.rom and can be regenerated with:
  python3 tools/buildApple2TestRom.py

Phase 1 does not include the Language Card, expansion video card or graphics
renderer. The Disk II is read-only: no writes, no second drive, no 13-sector
media. Next step: fix the builder (length prefix and the `&` token) in
tools/diskImageApple2Dos33.py and tools/apple2Basic.py, update
tests/test_createSystemDsk.py, regenerate system.dsk with the command above,
then check `LOAD TEST-NONGR` and `RUN` in DOS on the host and on the board.
