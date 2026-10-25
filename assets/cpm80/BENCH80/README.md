# BENCH80

CP/M-80 benchmark using `RETRO80_TIMER_B1` (Z80 `IN` ports F0h-F4h, same layout as
`RETRO86_TIMER_B1`). Its output is identical in layout to `BENCH86`:

```
BENCH80 v2 SCALE=1
@BEGIN INTEGER
@END INTEGER PASS AX=4ED8 BX=D678
  Elapsed: N ms
  Z80 MODEL ONLY (not measured): 1MHz=.. ms, 2MHz=.. ms, 4MHz=.. ms
  Index vs 1MHz MODEL: N (100=model, higher=faster; theoretical)
@BEGIN SIEVE
@END SIEVE PASS PRIMES=1028
...
@DONE PASS
```

`BENCH80 [1-9]` selects the scale. While running, one `.` is printed every 5 s on a single
line. The model values are T-state counts (`reference_model.py`, generates `reference.inc`)
converted to milliseconds for an original Z80 at 1, 2 and 4 MHz; an index of 100 means the
emulator keeps pace with a 1 MHz Z80. Without a timer the program prints `Timer unavailable`.

Rebuild with `python3 build.py` (requires `z80asm`); copy `BENCH80.COM` to a writable CP/M drive.
Verified deterministically by `tests/cpm80TimerTests.c`.
