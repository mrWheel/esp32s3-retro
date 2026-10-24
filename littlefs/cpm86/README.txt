RETRO86_V1 CP/M-86 system resources

cpm.sys
  CP/M-86 CMD-format system file (10,240 bytes). CCP/BDOS comes from
  tsupplis/cpm86-kernel at commit
  00927e17f43ea4241cc0809531cf68ce835e9c16. The IBM PC BIOS is not linked into
  this file; the project-owned RETRO86_V1 BIOS is supplied separately as
  retro86bios.h86 and loaded by the firmware.

retro86bios.h86
  ASM-86 1.1N H86 overlay assembled from
  components/cpm86Core/bios/retro86bios.a86. It supplies the RETRO86_V1 BIOS
  routines for console and A:–F: disk I/O, with a DPB for 160 KiB images and a
  second DPB (EXM=0, 2 KiB blocks, one 16 KiB extent per
  directory entry) for 516 KiB RETRO86_DATA_LARGE_V1 images, and a third
  DPB (16 KiB blocks, EXM=7, 512 directory entries, DSM=511) for 8 MiB
  RETRO86_DATA_BIG_V1 images.

system.dsk
  Raw, 160 KiB, 40 tracks, 8 physical 512-byte sectors per track. Track 0 is
  reserved; CP/M-86 uses 32 128-byte records per track and DPB OFF=1.
  Contains ASM86.CMD, ED.CMD, GENCMD.CMD, and PIP.CMD. CPM.SYS is
  deliberately kept outside the disk at /littlefs/cpm86/cpm.sys; the firmware
  loads that file directly. The system-disk builder verifies the external
  kernel and BIOS overlay and does not put either on A:.
  Rebuild this disk with `python3 tools/createSystemDsk.py --os cpm86 --profile SMALL`.
  The LARGE profile (`--profile LARGE`, 528,384 bytes, 516 KiB) also boots as A:.
  The firmware picks the DPB from the size of system.dsk (163,840 or 528,384
  bytes), so drives.cfg only needs A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM.
  Any other size is reported as an error and A: is not mounted.

Resource sizes, the on-disk directory, the BIOS overlay and the
RETRO86_V1 boot prompt plus DIR listing are checked by tests/hostTests.c. This
desktop test does not establish ESP32-S3 hardware boot or performance.

The upstream repository includes its rights-holder permission statement in
LICENSE.md. Preserve that provenance and review those terms before redistributing
these CP/M-86 binaries; no broader license is asserted here. The user-provided
assets/cmp86.img was not used or modified.

The system disk defaults to read-only, but may be configured read/write in
/littlefs/cpm86/drives.cfg. Writes persist in LittleFS until the firmware
rebuilds the generated system image. Use disposable images for writable disks.
