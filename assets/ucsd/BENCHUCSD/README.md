# BENCHUCSD

`BENCHUCSD.TEXT` is Apple Pascal 1.3 source (guest-compiled) using the shared Apple II timer
`APPLE2_TIMER_C1` at `$C0A0–$C0A4`. Apple Pascal has no `MEM[]`, so the timer is read through
a variant-record pointer (`$C0A0` = -16224). Output follows the BENCH86 layout: `@BEGIN/@END`
INTEGER and SIEVE sections, `Elapsed`, a `P-CODE MODEL ONLY (not measured)` line for a
1.023 MHz Apple II, an `Index vs 1.023MHz MODEL` line and `@DONE PASS`. A `.` is printed
every 5 s (checked once per block). Scale 1 takes about 5 s per section.

The model values (`INTMODEL`, `SIEVEMODEL`, scale 1) were measured by compiling and running
the program in the guest on the cycle-exact 6502 core; the model for scale n is n times that.

`BENCHUCSD.TEXT` is part of `bootDisks/ucsd/systemDsk/`, so a system disk generated with
`python3 tools/createSystemDsk.py --os ucsd --profile PASCAL_800K` already contains it. To add it to another volume:

```sh
python3 tools/extractFiles.py --os ucsd /path/to/work.po \
  --add assets/ucsd/BENCHUCSD/BENCHUCSD.TEXT --type text
```

Boot that volume, press `C`, compile `BENCHUCSD` (answer the code file prompt with
`WORK1:BENCHUC` (or any name) and press return at the listing prompt), then `X`ecute the code file.
Verified in the emulated guest: compiles without errors, scales 1 and 3 give PASS.
