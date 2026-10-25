# BENCH2

`BENCH2.SRC` is the numbered Applesoft BASIC source. `BENCH2.BAS` is the
tokenized Applesoft program for a ProDOS BASIC file (file type `$FC`, load
address `$0801`). Rebuild it with:

```sh
python3 build.py
```

Add `BENCH2.BAS` to a ProDOS volume using the Apple II disk-image tools, then
run it from Applesoft with `RUN BENCH2`. It needs firmware implementing
`APPLE2_TIMER_C1` (capability checked at `$C0A0`). Enter a scale from 1 to 9.

The output follows the BENCH86 layout (`@BEGIN/@END` INTEGER and SIEVE sections,
`ELAPSED`, a `6502 MODEL ONLY (NOT MEASURED)` line for a 1.023 MHz Apple II, an
`INDEX VS 1.023MHZ MODEL` line and `@DONE PASS`), in upper case because the Apple II+
has no lower case. A `.` is printed every 5 s on one line while it runs.

Applesoft is slow: scale 1 takes about 5 s per section. The model values (a table in the
DATA statements) were taken from the cycle-exact 6502 core running the real ROM, so an
index near 100 means the emulator runs at real Apple II speed.

The timer uses the Apple II slot-2 memory-mapped virtual device. Reading
`$C0A1` captures the millisecond counter; `$C0A2`–`$C0A4` return the same
snapshot.

See also `../BENCH2ASM/` for the 6502 machine-code version.
