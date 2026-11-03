SYSVIEW - VT100/ANSI terminal edition for CP/M-80

This is the CP/M-80 companion of assets/cpm86/SYSVIEW. It is a native
8080 program written for the RETRO80 host; it is not a patch of the
CP/M-86 SYSVIEW and needs no original binary.

Copy SYSVIEW.COM and SYSVIEW.HLP to a CP/M-80 disk (the build copies
both into bootDisks/cpm80/systemDsk so the system disk contains them).

Start: SYSVIEW
Help:  HELP SYSVIEW
Keys:  R = refresh; Q or Esc = exit.
Terminal: VT100/ANSI, at least 80 columns and 24 rows.

The front end uses CP/M BDOS console I/O only (function 6 direct console
output/status, function 12 version, function 25 current drive, function
32 user area). It does not touch the BIOS directly.

Hardware information is guest-visible emulator information, not host Mac
information. The host part of the report (firmware version, CPU core,
guest RAM, host PSRAM, uptime, Z80 T-states, console and mounted virtual
disks) comes from the RETRO80-SYSINFO/1 interface: IN F8 returns A6h when
the firmware supports it, OUT F8,1 builds a fresh snapshot and IN F9
drains the byte stream (the same port layout as the RETRO86 SYSVIEW).
Missing data is shown explicitly; an unsupported or older firmware prints
"Emulator information interface is not available."

Rebuild (requires Python 3 and z80asm):
  python3 build.py
The result SYSVIEW.COM is a flat CP/M transient loaded at 0100h.

Verification: host test tests/cpm80SysviewTests.c runs the built SYSVIEW.COM
in the CP/M-80 guest core with a simulated BDOS, controlled ports and
scripted keys (redraw, keys, missing/invalid data, More paging, ANSI
cleanup), and the program was run on the ESP32-S3 hardware console.
