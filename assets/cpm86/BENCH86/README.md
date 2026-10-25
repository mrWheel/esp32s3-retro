# BENCH86

CP/M-86 benchmark for original 8086 code. Version 2 reads monotonic
milliseconds from the firmware `RETRO86_TIMER_B1` (byte `IN` ports F0h-F4h:
F0h reports capability B1h, F1h samples one unsigned 32-bit millisecond value
and returns its low byte, F2h-F4h return the remaining little-endian snapshot
bytes without re-sampling) and prints the elapsed time plus an explicitly
**theoretical** 8086 timing-model comparison after each successful test.
`BENCH86 M` (or `BENCH86 2 M`) skips all timer ports, for manual or
original-hardware timing.

```
BENCH86 v2 SCALE=1
@BEGIN INTEGER
@END INTEGER PASS AX=4ED8 BX=D678
  Elapsed: N ms
  8086 MODEL ONLY (not measured): 5MHz=.. ms, 8MHz=.. ms, 10MHz=.. ms
  Index vs 5MHz MODEL: N (100=model, higher=faster; theoretical)
@BEGIN SIEVE
@END SIEVE PASS PRIMES=1028
...
@DONE PASS
```

`BENCH86 [1-9]` selects the scale. While a section runs in timer mode, one
`.` is printed every 5 s on a single line. Each timed interval excludes
printing the results. The timer has 1 ms resolution; increase the scale for
very short runs. Without a timer the program prints `Timer unavailable.
Update firmware or use BENCH86 M.`

Workloads: INTEGER is 32 x scale blocks of 32,768 add/XOR/rotate/LOOP
iterations, each block verifying AX=4ED8h/BX=D678h. SIEVE is 20 x scale
complete sieves of the integers 2 through 8191, verifying 1028 primes after
every round. No disk, floating-point or terminal-throughput benchmark is
included; the checks do not certify full emulator instruction compatibility.

The timing model sums kernel-path instruction clocks from the 8086/8088 tables
of the Intel ASM86 Language Reference Manual (order 121703-003, November 1983,
chapter 6): 31,458,356 clocks for INTEGER and 22,462,328 for SIEVE at scale 1,
excluding timer wrappers, output and setup before the timed kernels. For
16-bit MUL the model conservatively charges the table maximum of 133 clocks.
`reference_model.py` reproduces the numbers and regenerates `reference.inc`.
The model figures are estimates, not measurements of an original machine;
real hardware may differ materially due to prefetch, RAM wait states, bus
contention and interrupts.

Rebuild with `python3 build.py` (requires NASM, CPU 8086 enforced;
BENCH86.ASM uses NASM syntax, not native ASM86). The checked-in
`bootDisks/cpm86/systemDsk/BENCH86.CMD` matches this build and is verified by
`tests/test_bootDisksBench.py`; hardware results are recorded in
`designCPM86.md` (CPM86-VERIFY-043).
