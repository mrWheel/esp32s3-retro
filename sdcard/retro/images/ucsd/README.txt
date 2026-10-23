Menu option 3 uses this profile and the same Apple II 6502 emulator as option 4.

The active profile boots the Apple Pascal 1.3 64K system from the 800K
`/retro/images/ucsd/Apple Pascal 1.3 3.5.po` image as a writable block-autostart
device on SD6.1. Copy that image from `assets/ucsd/ucsdDiskImages/` to this
directory on the SD card. The 140K LittleFS boot image remains available as an
alternate Disk II profile. The active profile mounts
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

The UCSD `drives.cfg` is `littlefs/ucsd/drives.cfg` (flashed with LittleFS, next to
the boot image). Menu option 3 requires that file and the 800K boot image. In File Transfer, select UCSD Pascal and
Disk image; uploads go directly into this machine's image directory.

The profile's `drives.cfg` is stored in LittleFS as `/littlefs/ucsd/drives.cfg`.
In File Transfer, select UCSD Pascal and Disk image; uploads
go directly into this machine's image directory. The Pascal editor read path
was host-tested in the earlier fixture; write/reopen, volume swaps and
hardware behaviour have not been qualified.

Disk-image paths in drives.cfg must remain below /retro/images/ucsd/. Menu
option 3 reads /littlefs/ucsd/drives.cfg; option 4 reads
/littlefs/apple2/drives.cfg.
