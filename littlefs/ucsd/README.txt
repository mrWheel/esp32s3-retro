ApplePascal1-680-0004-01.dsk is the configured read-only Apple Pascal system
disk for menu option 3. It contains SYSTEM.APPLE and boots to the Pascal
startup screen in the LinApple desktop reference. The project emulator boots
it to the Pascal command line in the host test harness; ESP32-S3 behaviour has
not been verified.

ApplePascal2-680-0005-00.dsk is a compiler/tools disk, not a system boot disk.
Its source image remains in assets/apple2Downloads/appleDisks/.

The system disk is mounted as SD6.1; pascal640.po remains the read/write work
disk mounted as SD6.2 from /retro/images/ucsd/.

The Pascal editor and writable filesystem compatibility are not yet verified.
