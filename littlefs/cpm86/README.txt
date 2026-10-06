RETRO86_V1 CP/M-86 system resources

cpm.sys
  CP/M-86 CMD-format system file (10,240 bytes). CCP/BDOS comes from
  tsupplis/cpm86-kernel at commit
  00927e17f43ea4241cc0809531cf68ce835e9c16. The IBM PC BIOS was replaced by
  the project-owned RETRO86_V1 BIOS in components/cpm86Core/bios/retro86bios.a86.
  Built with ASM-86 and GENCMD 1.1N for load segment 0051h.
  SHA-256: 72ad573b5c2126d17cca6b1fdf5babea8f8a3403b21453dec9d401adc81dfd56

system.dsk
  Raw, 160 KiB, 40 tracks, 8 physical 512-byte sectors per track. Track 0 is
  reserved; CP/M-86 uses 32 128-byte records per track and DPB OFF=1.
  Contains CPM.SYS, ASM86.CMD, ED.CMD, GENCMD.CMD, and PIP.CMD.
  SHA-256: c86e2db315b29e9ab270066fb4bf87ac498e58484c686bac09c6b0f64f0268b9

Both resource sizes, hashes, the on-disk directory and the RETRO86_V1 boot
prompt plus DIR listing are checked by tests/hostTests.c. This desktop test does
not establish ESP32-S3 hardware boot or performance.

The upstream repository includes its rights-holder permission statement in
LICENSE.md. Preserve that provenance and review those terms before redistributing
these CP/M-86 binaries; no broader license is asserted here. The user-provided
assets/cmp86.img was not used or modified.

The system disk is opened writable by the firmware. Back it up before use if
guest programs or future CP/M commands may write to it.
