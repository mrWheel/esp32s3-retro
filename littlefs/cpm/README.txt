CP/M 2.2 LittleFS A: image.

system.dsk is a 256,256-byte raw image with 77 tracks, 26 128-byte sectors per 
track, two reserved tracks, and a read-only 8-inch SSSD DPB. Its SHA-256 is 
a651470e4cf5b2c40ba1c736e8d534eeaa9679631dd2f33765d153d7b3e6bd8d. 
It contains the genuine 2.2 CCP and BDOS, an empty directory except for 
HELLO.COM, and a 16-byte RETROCPM resource header in the reserved area. 
The BIOS is implemented in the guest adapter; it is not a physical disk 
boot sector.

The machine loads CCP at C400 and BDOS at CC00 from the reserved image bytes. 
The disk image is immutable on LittleFS. Use `A>DIR` to list HELLO.COM and 
`A>HELLO` to verify transient loading and warm boot.

Regenerate the image with `python tools/buildCpmDisk.py` from the 
project root. The composer uses the checked-in assembled images under 
components/cpmCore/os. See designCPM.md, components/cpmCore/os/README.md 
and SHA256SUMS.txt for source, license, geometry and checksum details. 
Firmware verifies the exact image size, signature and SHA-256. 
The common host resource-manifest schema remains pending.
