RETRO86_V1 CP/M-86 system resources

cpm.sys
  CP/M-86 CMD-format system file (10,240 bytes). CCP/BDOS comes from
  tsupplis/cpm86-kernel at commit
  00927e17f43ea4241cc0809531cf68ce835e9c16. The IBM PC BIOS is not linked into
  this file; the project-owned RETRO86_V1 BIOS is supplied separately as
  retro86bios.h86 and loaded by the firmware.
  SHA-256: 72ad573b5c2126d17cca6b1fdf5babea8f8a3403b21453dec9d401adc81dfd56

retro86bios.h86
  ASM-86 1.1N H86 overlay assembled from
  components/cpm86Core/bios/retro86bios.a86. It supplies the RETRO86_V1 BIOS
  routines for console and A:–F: disk I/O, with a DPB for 160 KiB images and a
  second DPB for 516 KiB RETRO86_DATA_LARGE_V1 images.
  SHA-256: 2e466eb5a0619337b3b7dd3a6c405dd5f252fe5cfc8083787d5c1ab5d776378e

system.dsk
  Raw, 160 KiB, 40 tracks, 8 physical 512-byte sectors per track. Track 0 is
  reserved; CP/M-86 uses 32 128-byte records per track and DPB OFF=1.
  Contains CPM.SYS, ASM86.CMD, ED.CMD, GENCMD.CMD, and PIP.CMD.
  SHA-256: c86e2db315b29e9ab270066fb4bf87ac498e58484c686bac09c6b0f64f0268b9

Resource sizes and hashes, the on-disk directory, the BIOS overlay and the
RETRO86_V1 boot prompt plus DIR listing are checked by tests/hostTests.c. This
desktop test does not establish ESP32-S3 hardware boot or performance.

The upstream repository includes its rights-holder permission statement in
LICENSE.md. Preserve that provenance and review those terms before redistributing
these CP/M-86 binaries; no broader license is asserted here. The user-provided
assets/cmp86.img was not used or modified.

The system disk is opened read-only by the firmware. Optional SD drives are
configured in /retro/images/cpm86/drives.cfg; use disposable images for drives
configured read/write.
