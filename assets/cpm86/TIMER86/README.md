# TIMER86

CP/M-86 millisecond timer acceptance program for the firmware
`RETRO86_TIMER_B1` byte `IN` ports F0h-F4h (F0h reports capability B1h,
F1h samples one unsigned 32-bit millisecond value and returns its low byte,
F2h-F4h return the remaining little-endian snapshot bytes without
re-sampling; see `docs/cpm86/virtualIo.md`).

```
@BEGIN TIMER
DELTA=N MS
@DONE PASS
```

`tools/cpm86/cpm86HardwareTest.py` runs it automatically: it waits for
`@BEGIN TIMER`, sleeps 1 s, sends one carriage return, and requires
`DELTA=` between 900 and 1100 ms plus `@DONE PASS`. The wait uses BDOS
function 1 (console input); the RETRO86 BIOS `CONIN` busy-polls port E0h,
so the program really blocks until the keystroke arrives. Without a timer
the program prints `Timer unavailable. Update firmware.`

Rebuild with `python3 build.py` (requires NASM, CPU 8086 enforced;
TIMER86.ASM uses NASM syntax, not native ASM86). The checked-in
`bootDisks/cpm86/systemDsk/TIMER86.CMD` matches this build and is verified
by `tests/test_bootDisksBench.py`; hardware results are recorded in
`designCPM86.md`.
