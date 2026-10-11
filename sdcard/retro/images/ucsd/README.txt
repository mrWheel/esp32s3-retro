Menu option 3 uses this profile and the same Apple II 6502 emulator as option 4.

The current profile boots `/littlefs/ucsd/ApplePascal2-680-0005-00.dsk`
read-only as SD6.1 and mounts `/retro/images/ucsd/pascal640.po` read/write as
SD6.2. The latter is the second Disk II drive in the same slot, available to
the guest as its work volume. In File Transfer, select UCSD Pascal and Disk
image; uploads go directly into this machine's image directory.

The Pascal guest boot and write/reopen workflow have not yet been qualified.
Verify this system disk's release requirements, geometry, sector order and
work-volume compatibility before relying on guest filesystem writes.

Disk-image paths in drives.cfg must remain below /retro/images/ucsd/. Menu
option 3 selects /littlefs/ucsd/config.cfg; option 4 selects
/littlefs/apple2/config.cfg and continues to use the existing ../drives.cfg.
