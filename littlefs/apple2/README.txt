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
validated by host-side tests. This does not establish that the base image is
bootable. The Apple II runtime does not yet implement Disk II controller or
media reads, so the emulator cannot currently boot this disk or LOAD the
program from it. No drives.cfg is needed for this single fixed drive.

The separate project-authored diagnostic test fixture is at
tests/fixtures/apple2-diagnostic.rom and can be regenerated with:
  python3 tools/buildApple2TestRom.py

Phase 1 does not include the Language Card, expansion video card, disk
controller or graphics renderer. The DOS 3.3 image builder is a host-side
tool and does not change that runtime limitation. Next disk milestone: select
and implement the documented read-only 16-sector Disk II controller path,
mount littlefs/apple2/system.dsk without loading the whole image into RAM, and
verify sector reads before attempting DOS boot.
