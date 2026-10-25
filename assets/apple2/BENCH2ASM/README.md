# BENCH2ASM

6502 machine-code version of the Apple II benchmark (ProDOS BIN, load address `$6000`).
Same workloads and report layout as `BENCH80`/`BENCH86` (INTEGER with AX=4ED8 BX=D678, SIEVE
with 1028 primes, `@DONE PASS`), a `.` every 5 s, and a `6502 MODEL ONLY` line for a
1.023 MHz Apple II. It asks `SCALE (1-9)?`; scale 1 takes about 5.4 s (INTEGER) and 3.8 s
(SIEVE).

```sh
python3 build.py        # needs ca65 and ld65 (cc65); writes BENCH2ASM.BIN
```

Upload `BENCH2ASM.BIN` through the web File Transfer page with Apple file type `06` and
aux type / load address `6000` (both hex), then run `BRUN BENCH2ASM.BIN`. Without the load
address the file loads at `$0000` and BRUN fails with `NO BUFFERS AVAILABLE`. Do not upload
`BENCH2ASM.S` (source only).
`reference.inc` holds the scale-1 model milliseconds, measured with the
cycle-exact 6502 core; the model for scale n is n times that value. Requires
`APPLE2_TIMER_C1` at `$C0A0`.
