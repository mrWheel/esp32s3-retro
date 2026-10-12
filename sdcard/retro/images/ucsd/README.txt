Menu option 3 uses this profile and the same Apple II 6502 emulator as option 4.

The active profile boots the 128K Apple Pascal 1.3 system from
`/littlefs/ucsd/ApplePascal1-140K.dsk` read-only as SD6.1. It mounts
`ApplePascal2-640K.po` read-only as SP4.1, `pascalWork1.po` read/write as
SP5.1, and `pascalWork2.po` read/write as SP5.2. SmartPort block devices may
use slots 4 through
7, with up to four units per slot. Pascal's ProDOS block interface exposes
only two units per slot, so additional Pascal volumes must use another slot.
Use SmartPort for these block volumes: mounting them as Disk II drives makes
the Pascal editor fail to read `TEXT` files. The host harness verifies Filer
listed three volumes and opened `WORK1:SPIRODEMO.TEXT` in an earlier fixture.
The current worktree images fail the expected `DATA3:` assertion; verify the
actual SD media and reconcile the mapping before relying on that result.
ESP32-S3 behaviour and filesystem writes remain unverified.

Copy this directory's `drives.cfg` to `/retro/images/ucsd/drives.cfg` on the
SD card when updating the UCSD profile. Menu option 3 requires that file as
well as the LittleFS boot image. In File Transfer, select UCSD Pascal and Disk
image; uploads go directly into this machine's image directory.

The profile's `drives.cfg` must be copied to `/retro/images/ucsd/drives.cfg`
on the SD card. In File Transfer, select UCSD Pascal and Disk image; uploads
go directly into this machine's image directory. The Pascal editor read path
was host-tested in the earlier fixture; write/reopen, volume swaps and
hardware behaviour have not been qualified.

Disk-image paths in drives.cfg must remain below /retro/images/ucsd/. Menu
option 3 selects /littlefs/ucsd/config.cfg; option 4 selects
/littlefs/apple2/config.cfg and continues to use the existing ../drives.cfg.
