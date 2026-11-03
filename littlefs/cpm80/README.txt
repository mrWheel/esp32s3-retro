CP/M-80 LittleFS A: image.

system.dsk is a 512,512-byte raw image with 77 tracks, 52 128-byte sectors per
track, two reserved tracks, 2 KiB allocation blocks, 128 directory entries and
the double-density DPB.
It contains the genuine CCP and BDOS, resident ERA/REN/TYPE/USER/DIR commands,
HELLO.COM, WELCOME.TXT and the CP/M utility set documented in
components/cpm80Core/os/utilities/README.md. The 16-byte RETROCPM resource header
is in the reserved area; the BIOS is implemented in the guest adapter.

The machine loads CCP at C400 and BDOS at CC00. A: defaults to read-only but
may be configured read/write in drives.cfg. Writes persist in LittleFS until
the firmware rebuilds the generated system image.
Use A>DIR to list the files, A>TYPE WELCOME.TXT to verify text display, and
A>HELLO to verify transient loading and warm boot. DIR is the resident
directory command; STAT supplies disk status in place of a separate SHOW.COM
or SDIR.COM. SUBMIT.COM is included; it needs writable A: because it writes
$$$.SUB.

Optional SD images map to B:–F: using /littlefs/cpm80/drives.cfg. The config
maps a drive letter to an image path, RO/RW access and the image's size
identifier. CPM80_256K is 256,256 bytes (77 tracks × 26 128-byte sectors,
1 KiB blocks, 64 directory entries); CPM80_512K is 512,512 bytes (77 tracks ×
52 sectors, 2 KiB allocation blocks and 128 directory entries); CPM80_1440K is
1,474,560 bytes (360 tracks × 32 sectors, 8 KiB blocks, 128 directory
entries); CPM80_8M is 8,421,376 bytes (514 tracks × 128 sectors, 16 KiB
blocks, 512 directory entries). Each of B:–F: may use any of the four
profiles. Create a 1.44 MB image with: python3 tools/diskImage.py create
--os cpm80 --profile CPM80_1440K x.dsk. The included prepareSd.py creates a sample drives.cfg
without replacing an existing one. Create a writable work image with
python3 tools/diskImage.py create --os cpm80 --profile CPM80_1440K ~/Desktop/work.dsk, add
licensed local programs with python3 tools/diskImage.py add --os cpm80, copy it to the
path configured for E:, then safely eject the card. A: remains available from
LittleFS even when the SD card or drives.cfg is missing or invalid. See
tools/README.md for configuration, image creation and software rights details.

Build the A: image from the files in bootDisks/cpm80/systemDsk with
python3 tools/createSystemDsk.py --os cpm80 --profile CPM80_512K (the profile
the firmware build always uses). Executable files are installed first. The
CPM80_256K and CPM80_1440K profiles create 256,256-byte (1 KiB blocks, 64
directory entries) and 1,474,560-byte (8 KiB blocks, 128 directory entries)
images that boot as A: as well; CPM80_8M never boots as A:. The firmware
selects the A: profile from the size of littlefs/cpm80/system.dsk (256,256,
512,512 or 1,474,560 bytes), so drives.cfg only needs
A=/littlefs/cpm80/system.dsk,RO,CPM80_512K. A size that matches none of these
profiles is reported as an error and A: is not mounted.

The older curated image composer remains available as
python3 tools/buildDiskImage.py --os cpm80 --output littlefs/cpm80/system.dsk.
See designCPM80.md and components/cpm80Core/os/README.md for source, license
scope and geometry details. Firmware verifies the exact boot image size and
signature.
