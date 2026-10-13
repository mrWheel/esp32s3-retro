ApplePascal1-140K.dsk is the configured read-only Apple Pascal 1.3 128K system
disk for menu option 3. The project emulator boots it on the Apple IIe model
to the Pascal command line. It also holds HOST.CODE (19 blocks), the native
file-exchange utility: set the Filer prefix to a writable volume (P, WORK1:),
then X(ecute) APPLE1:HOST. Host-emulator tested only; see ucsdHostLog.md.
ESP32-S3 behaviour has not been verified.

ApplePascal2-680-0005-00.dsk is a compiler/tools disk, not a system boot disk.
Its source image remains in assets/apple2Downloads/appleDisks/.

The system disk is mounted as SD6.1. Four 640K SmartPort units are supported
per slot, with SP block controllers configurable on slots 4 through 7. Pascal's
ProDOS block interface exposes only two units per slot, so additional Pascal
volumes must use a different slot. The checked-in SD-card profile maps
ApplePascal2-640K.po read-only on SP4.1, pascalWork1.po read/write on SP5.1,
and pascalWork2.po read/write on SP5.2. The configuration parser does not
validate image labels or contents.
Use SmartPort for these block volumes: the Disk II mapping cannot open their
TEXT files in Pascal 1.3.

A prior host fixture verified Filer listing DATA3, WORK1 and WORK and opening
WORK1:SPIRODEMO.TEXT. The current worktree images fail that test's expected
DATA3: assertion; check the actual SD images and reconcile drives.cfg before
relying on the previous result. Writable filesystem compatibility and
ESP32-S3 behaviour are not yet verified.
