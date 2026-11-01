# WBOOT86

CP/M-86 warm-boot acceptance program for the firmware (CP/M-86 phase 86-09).

It prints `@BEGIN WBOOT` and then calls BDOS function 0 (warm boot). The
RETRO86 BIOS `WBOOT` entry (`mov byte ptr CURRENTDRIVE,0 / jmp CCP+6`, see
`components/cpm86Core/bios/retro86bios.a86`) must bring the CCP prompt back
WITHOUT the host menu (`Select system`) appearing: a warm boot is guest
state, not a host lifecycle event.

`tools/cpm86/cpm86HardwareTest.py` runs it automatically (`warmBootTest`):
it sets `USER 3` and the current drive to `D:` before the call, requires the
`A>` prompt to return with no `Select system` in between, and records the
`USER` value shown after the warm boot as survival evidence.

Rebuild with `python3 build.py` (requires NASM, CPU 8086 enforced; WBOOT86.ASM
uses NASM syntax, not native ASM86 - the native ASM86 workflow is covered
separately by the 86-08 editor test). The checked-in
`bootDisks/cpm86/systemDsk/WBOOT86.CMD` matches this build and is verified by
`tests/test_bootDisksBench.py`; hardware results are recorded in
`designCPM86.md`.
