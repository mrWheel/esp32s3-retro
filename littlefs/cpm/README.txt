CP/M 2.2 LittleFS A: image.

system.dsk is a 256,256-byte raw image with 77 tracks, 26 128-byte sectors per
track, two reserved tracks and the read-only 8-inch SSSD DPB. Its SHA-256 is
73c56e9f49292f8c9c8ac1530cbcf5799300253f5803d55b8837787cfb8e1e28.
It contains the genuine CCP and BDOS, resident ERA/REN/TYPE/USER/DIR commands,
HELLO.COM, WELCOME.TXT and the CP/M utility set documented in
components/cpmCore/os/utilities/README.md. The 16-byte RETROCPM resource header
is in the reserved area; the BIOS is implemented in the guest adapter.

The machine loads CCP at C400 and BDOS at CC00. A: is immutable on LittleFS.
Use A>DIR to list the files, A>TYPE WELCOME.TXT to verify text display, and
A>HELLO to verify transient loading and warm boot. DIR is the resident
directory command; STAT supplies disk status in place of a separate SHOW.COM
or SDIR.COM. SUBMIT.COM is included, but its patched build writes $$$.SUB on
read-only A: and cannot submit jobs with the current access policy.

Optional SD images map to B:–E: at /retro/images/cpm/languages.dsk,
tools.dsk, archive.dsk and work.dsk. Images must be exactly 256,256 bytes and
match this profile. B:–D: are read-only; E: is read/write. The firmware does
not create the images. Prepare an empty work.dsk on a Mac with
python3 tools/cpmDiskImage.py create ~/Desktop/work.dsk, add licensed local
programs with python3 tools/cpmDiskImage.py add, then copy it to
/Volumes/SDCARD/retro/images/cpm/work.dsk and safely eject the card. See
tools/README.md for downloading rights-cleared programs and disk images.
Switch to E: before writing files with PIP, ERA or REN.

Regenerate the A: image with python tools/buildCpmDisk.py from the project
root. The composer uses the checked-in assembled images and utilities under
components/cpmCore/os. See designCPM.md, components/cpmCore/os/README.md and
SHA256SUMS.txt for source, license scope, geometry and checksum details.
Firmware verifies the exact image size, signature and SHA-256.
