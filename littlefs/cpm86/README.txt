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
  second DPB (EXM=0, 2 KiB blocks, one 16 KiB extent per
  directory entry) for 516 KiB RETRO86_DATA_LARGE_V1 images.
  SHA-256: 3c978c2e4a24ef271a9630aa9f5c8fb00af01f579f38e47c10fb5f835bfeaab6

system.dsk
  Raw, 160 KiB, 40 tracks, 8 physical 512-byte sectors per track. Track 0 is
  reserved; CP/M-86 uses 32 128-byte records per track and DPB OFF=1.
  Contains CPM.SYS, ASM86.CMD, ED.CMD, GENCMD.CMD, HOST.CMD, and PIP.CMD.
  SHA-256: 04050eacdf37310f9ffc4ed6ffb47b0e8b03b458b2b7609a1e82cc437d8bf464

HOST.CMD
  Project-authored native CP/M-86 HOST DIR/GET/PUT utility (3,584 bytes,
  including final CP/M record padding). Built from guest/cpm86/host/HOST.A86
  with the supplied ASM86.CMD and GENCMD.CMD.
  SHA-256: 5ec615603dfdfa3d2cd5ab72b65f1ee399732652e90c6f168ce690d0f04793ed

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

HOST.CMD is installed on system.dsk. The host CP/M-86 fixture assembles the
source in a guest and passes DIR/GET/PUT round-trip tests against this disk;
hardware transfer testing remains open. See docs/cpm86/host.md for the build
procedure and verification details.
