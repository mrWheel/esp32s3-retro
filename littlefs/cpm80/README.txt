CP/M-80 LittleFS A: image.

system.dsk is a 256,256-byte raw image with 77 tracks, 26 128-byte sectors per
track, two reserved tracks and the read-only 8-inch SSSD DPB..
It contains the genuine CCP and BDOS, resident ERA/REN/TYPE/USER/DIR commands,
HELLO.COM, WELCOME.TXT, the custom HOST.COM guest transfer utility and the CP/M utility set documented in
components/cpm80Core/os/utilities/README.md. The 16-byte RETROCPM resource header
is in the reserved area; the BIOS is implemented in the guest adapter.

The machine loads CCP at C400 and BDOS at CC00. A: is immutable on LittleFS.
Use A>DIR to list the files, A>TYPE WELCOME.TXT to verify text display, and
A>HELLO to verify transient loading and warm boot. DIR is the resident
directory command; STAT supplies disk status in place of a separate SHOW.COM
or SDIR.COM. SUBMIT.COM is included, but its patched build writes $$$.SUB on
read-only A: and cannot submit jobs with the current access policy.

Optional SD images map to B:–F: using /retro/images/cpm80/drives.cfg. The config
maps a drive letter to an image path, RO/RW access and SYSTEM/LARGE disk
profile. SYSTEM is 256,256 bytes (77 tracks × 26 128-byte sectors); LARGE is
512,512 bytes (77 tracks × 52 sectors, 2 KiB allocation blocks and 128
directory entries). Each of B:–F: may use either profile. Create a standard
256 KiB image with: python3 tools/diskImageCpm80.py create --profile SYSTEM x.dsk. The included prepareSd.py creates a sample drives.cfg
without replacing an existing one. Create a writable work image with
python3 tools/diskImageCpm80.py create --profile LARGE ~/Desktop/work.dsk, add
licensed local programs with python3 tools/diskImageCpm80.py add, copy it to the
path configured for E:, then safely eject the card. A: remains available from
LittleFS even when the SD card or drives.cfg is missing or invalid. See
tools/README.md for configuration, image creation and software rights details.

HOST DIR lists files in /retro/exchange/cpm. HOST GET NAME.EXT copies a host
file into the current CP/M drive; HOST PUT NAME.EXT copies it back. Transfer
works only in USER 0; HOST DIR works in all user areas. A: is read-only, so
from E: run the utility with A:HOST GET NAME.EXT or A:HOST PUT NAME.EXT. HOST
returns to the drive it was started from. HOST uses exact byte lengths and a visible NAME.HST
sidecar to preserve the length of imported files; the sidecar is reserved for
HOST and should not be edited or deleted separately. Modified files fall back
to full 128-byte CP/M records. No WiFi connection is required.
Add an O after a GET (for example A:HOST GET *.* O) to overwrite existing files
and their NAME.HST sidecars. Add an O after a PUT (A:HOST PUT NAME.EXT O) to
overwrite an existing exchange file. Dots show the progress of each transfer;
"disk full" means the CP/M drive is too small (A: has only about 486 KB free).

Build the A: image from the files in bootDisks/cpm80/systemDsk with
python3 tools/createSystemDsk.py --os cpm80 --profile SMALL. Executable files
are installed first; the project HOST.COM is used instead of any conflicting
HOST.COM in that source folder. The LARGE profile creates a 512,512-byte image
(2 KiB blocks, 128 directory entries) that boots as A: as well. The firmware
decides SMALL or LARGE from the size of littlefs/cpm80/system.dsk, so drives.cfg
only needs A=/littlefs/cpm80/system.dsk,RO,SYSTEM. A size that matches neither
profile is reported as an error and A: is not mounted.

The older curated image composer remains available as
python3 tools/buildDiskImage.py --os cpm80 --output littlefs/cpm80/system.dsk.
See designCPM80.md and components/cpm80Core/os/README.md for source, license
scope and geometry details. Firmware verifies the exact boot image size and
signature.
