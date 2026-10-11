Menu option 3 uses this profile and the same Apple II 6502 emulator as option 4.

The profile configures `/littlefs/ucsd/ApplePascal1-680-0004-01.dsk`
read-only as SD6.1 and mounts `/retro/images/ucsd/pascal640.po` read/write as
SD6.2. The latter is the second Disk II drive in the same slot, available to
the guest as its work volume. The Pascal 1 image contains SYSTEM.APPLE and
boots in the LinApple desktop reference and, in the host test harness, in the
project emulator up to the Pascal command line. ESP32-S3 behaviour is not yet
verified.

Copy this directory's `drives.cfg` to `/retro/images/ucsd/drives.cfg` on the
SD card when updating the UCSD profile. Menu option 3 requires that file as
well as the LittleFS boot image. In File Transfer, select UCSD Pascal and Disk
image; uploads go directly into this machine's image directory.

The Pascal editor and write/reopen workflow have not yet been qualified.
Verify the system disk's release requirements, geometry, sector order and
work-volume compatibility before relying on guest filesystem writes.

Disk-image paths in drives.cfg must remain below /retro/images/ucsd/. Menu
option 3 selects /littlefs/ucsd/config.cfg; option 4 selects
/littlefs/apple2/config.cfg and continues to use the existing ../drives.cfg.
