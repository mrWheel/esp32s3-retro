Menu option 3 uses this profile and the same Apple II 6502 emulator as option 4.

Before option 3 can start, place an original Apple Pascal system disk under
/retro/images/apple2/ and add an explicit SD6.1 entry to this drives.cfg using
its verified image profile and read/write policy. For browser transfer, select
the Apple II image category. Do not use the blank pascal640.po volume as a
system disk. The matching Apple Pascal release, boot-media provenance, disk
order, geometry, and writable work-volume setup have not been qualified.

Disk-image paths in drives.cfg must remain below /retro/images/apple2/. Menu
option 3 selects /littlefs/ucsd/config.cfg; option 4 selects
/littlefs/apple2/config.cfg and continues to use the existing ../drives.cfg.
