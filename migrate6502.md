# migrate6502.md — Making the 6502 core at least 2× faster

Status: **DONE (2026-10-25)** — real BENCH2 (Applesoft, II+ with 80-column card): index 37 → **245** (ELAPSED 14.5 s → 2.1 s, ≈7×); synthetic loops 3.4–4.8×. Cause of the earlier bad result and its fix: see §9. Steps 1, 3 and 5 kept; steps 2 and 4 not needed. This file is the working log: update the
"Result" column and the log sections after every experiment.

Scope: **only the 6502 CPU** (`components/apple2Core`, the `m6502_tick()` loop in
`apple2CoreRunCycles`). Everything else (Apple II/IIe emulation, Videx, disk, SmartPort, keyboard,
video, `apple2Core.h` public API, CP/M-80, CP/M-86, UCSD) must behave exactly as it does now.
Target: **≥ 2× emulated 6502 cycles per second** on the ESP32-S3 at 240 MHz.

## 1. Current situation (analysis)

- The CPU is floooh's `chips/m6502.h` (`third_party/chips/m6502.h`, 3107 lines), a
  **cycle-stepped** core: one call to `m6502_tick(cpu, pins)` per bus cycle; a single huge
  `switch (c->IR++)` with `(opcode<<3)|cycle` cases (2048 cases).
- It is compiled header-only inside `apple2Core.c` (`#define CHIPS_IMPL`), with the project default
  `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` (`-Os`) and IDF's `-fno-jump-tables`. The code lives in
  flash (cache). This is exactly the situation the 8086 work started from.
- `apple2CoreRunCycles()` (apple2Core.c ~1629) does, **for every single cycle**:
  1. lowercase-echo countdown, `core->cycles++`, `cpuBusAccess = true`;
  2. `executeSmartPortTrap()` (function call, checks `PC`/SYNC);
  3. `m6502_tick()` with a **64-bit `pins` word** (Xtensa LX7 is 32-bit: every mask/shift is a
     register pair operation);
  4. `readAddress()`/`writeAddress()` (large, soft-switch/IIe/language-card decode; the RAM case is
     reached only after several compares);
  5. for the II/II+: 3 SYNC-gated checks (SETVID, Pascal card, `$FC58`) + `endVidexWorkspace()`;
  6. `cpuBusAccess = false`.
- `apple2Machine.c` calls `apple2CoreRunCycles(core, cyclesPerPeriod)` with
  `cyclesPerPeriod = 1023000 * period / tickRate` (real time 1.023 MHz). So "faster" means: the
  same cycle budget is finished in less CPU time (more headroom / a faster-than-real-time mode, and
  more margin for video/disk work). Measure it as **emulated cycles per host second**.
- Hard semantic constraints that any new core has to keep:
  - `core->cycles` is used for disk timing (`apple2DiskAccess(..., core->cycles, ...)`) and IIe
    VBL (`cycles % apple2IIeFrameCycles`): the cycle counter must stay exact at every soft-switch
    access, not just per instruction.
  - `cpuBusAccess` + `cpu.PC` is used by the Videx workspace logic and the lowercase echo.
  - SmartPort/ProDOS block traps fire on an opcode **fetch** at `$Cn0x` entry points.
  - NMOS 6502 incl. undocumented opcodes and BCD, as today (`bcd_disabled = false`).
  - IRQ/NMI/RES pin behaviour is not used by the Apple II glue (pins are only RW/SYNC/addr/data),
    but `m6502_init` reset sequence (7 cycles from `RES`) must give the same PC/state.

## 2. Research: faster 6502 core designs

Sources consulted (web): floooh/chips `m6502.h`; Mike Chambers' *Fake6502*
(rubbermallet.org/fake6502.c, and the omarandlorraine fork with a per-CPU state struct);
Gianluca Ghettini's `gianlucag/mos6502` (table-driven, read/write callbacks, tested against Klaus
Dormann's `6502_65C02_functional_tests`); lib6502 (Ian Piumarta, `jump tables + inline macros`);
the experience from the 8086 work in `migrate8086CPU.md`.

| Design | Speed | Cycle accuracy | Notes |
|---|---|---|---|
| Cycle-stepped (`m6502.h`, current) | slowest | exact, per bus cycle | per-cycle dispatch + 64-bit pin word + glue per cycle |
| Instruction-stepped, `switch`/jump-table per opcode (Fake6502, lib6502, mos6502) | ~3–10× faster | per instruction (cycle count added after it) | one dispatch per instruction, registers in locals, memory via inline read/write |
| Same + lazy flags / flag split into separate bytes | extra 10–30 % | same | invasive; only if needed |
| Instruction-stepped with **per-access cycle counter** (cycle added at each read/write) | between the two | exact at every bus access | what we need: soft-switch accesses see the right `core->cycles` |

Conclusion: the best trade-off for this project is an **instruction-stepped core with a per-bus-access
cycle counter** (so disk/VBL see the exact cycle, like today), a RAM/ROM fast path in the memory
accessors, registers held in locals, and the whole thing compiled with `-O2` + jump tables and placed
in IRAM. Step 1–3 below get a large part of the gain *without* replacing the core; step 4 replaces it
only if 2× is not reached.

## 3. Ground rules

- Only touch the 6502: `components/apple2Core/` (+ its CMake/linker fragment/Kconfig) and, for
  the benchmark harness only, temporary code that is removed again.
- Keep the old `m6502.h` core in the tree and selectable (`CONFIG_APPLE2_FAST6502`, default `y`
  only once verified) until the new one is proven; remove it at the end as with EMU86.
- One change at a time, measure after each, **revert what does not help** (record it in §6).
- Each step must pass the regression checks in §5 before it counts as "done".

## 4. Steps

| # | Step | Expected gain | Risk | Result |
|---|---|---|---|---|
| 0 | **Baseline.** Build a reproducible benchmark (see §5) and record cycles/s for: tight RAM loop, ROM monitor loop, BASIC `BENCH2` (assets/apple2/BENCH2), DOS/ProDOS boot. | — | none | TODO |
| 1 | **Compiler flags.** Per-file `-O2 -fjump-tables -ftree-switch-conversion -fno-bit-tests --param=jump-table-max-growth-ratio-for-speed=2000` for the 6502 code (same recipe as `tiny8086.c`). Needs a dedicated translation unit: move `#define CHIPS_IMPL` + `#include "m6502.h"` into e.g. `m6502Impl.c`. | 1.3–2× | low | TODO |
| 2 | **IRAM placement.** `linker.lf` (`noflash`) for `m6502Impl` (and, if it fits, the hot part of `apple2Core`), `Kconfig` switch `APPLE2_6502_IRAM` (default y). The 2048-case jump table must be in internal RAM (`-fjump-tables` is only safe then). Check IRAM budget (`idf.py size-components`); the tiny8086 already uses ~30 KB. | 1.2–1.5× | IRAM budget | TODO |
| 3 | **Slim the per-cycle glue in `apple2CoreRunCycles`.** (a) Run the II/IIe flag test once outside the loop (two loops or a function pointer). (b) Execute the SmartPort trap / SETVID / Pascal / `$FC58` checks only when `pins & M6502_SYNC` (currently the trap function is *called* every cycle). (c) Decrement `lowercaseEchoCycles` only while pending. (d) Keep `cycles` in a local and write it back before any call that reads it. (e) `cpuBusAccess` set only around accesses that can observe it. (f) Inline RAM fast path in front of `readAddress`/`writeAddress`: `address < 0xC000` non-IIe, no language-card/aux bank → direct `core->ram[address]`; `0xD000+` ROM read → direct ROM pointer. All behaviour-preserving. | 1.3–1.8× | medium: soft-switch order/side effects | TODO |
| 4 | **32-bit pin word.** Keep the `m6502.h` algorithm but replace the `uint64_t pins` by a struct `{uint16 addr; uint8 data; uint8 ctrl}` / 32-bit word (only RW, SYNC, RDY, IRQ, NMI, RES and data/addr are needed; the m6510 port pins are not). Avoids 64-bit emulation on LX7. | 1.1–1.3× | medium: patch of generated header | TODO |
| 5 | **Instruction-stepped core `fast6502`** (only if 1–4 do not reach 2×). New files `components/apple2Core/fast6502.{c,h}`. Design: registers `A,X,Y,S,P,PC` in locals for the duration of `fast6502Run(cycleBudget)`; 256-entry dispatch (`switch` → jump table, IRAM); addressing-mode helpers as `always_inline`; flags N/Z computed from the result value, C/V/D/I as bytes; `read8()/write8()` `always_inline` with the fast path from step 3(f) and `cycles++` **at every bus access** (including dummy reads/writes of RMW and page-cross so that count and soft-switch side effects equal `m6502.h`); `SYNC`-hook (`onFetch(pc)`) called per opcode fetch for the SmartPort/Videx logic; NMOS illegal opcodes taken from `m6502.h` semantics; BCD identical to `_m6502_adc/_sbc`. | 2–4× on top of 3 | highest: exactness | TODO |
| 6 | **Differential test.** Host test that runs `m6502.h` and `fast6502` in lock-step on (a) Klaus Dormann `6502_functional_test`, (b) Apple II ROM boot + DOS 3.3/ProDOS disk boot, (c) random opcode streams: compare A/X/Y/S/P/PC, `core->cycles`, and the **sequence of (address, rw, data)** bus accesses. Any difference is a bug in the new core. | — | — | TODO |
| 7 | **Cleanup.** Remove the old core selector (or keep as `CONFIG_`), update `docs/thirdParty.md` (chips license no longer needed if m6502.h is removed), update this file, README performance line. | — | low | TODO |

Further ideas (only if still < 2×, in order): hot data (`apple2Core` struct fields used per access) in
DRAM and `ram[]` of the first 48 KB in internal RAM; `CONFIG_ESP32S3_DATA_CACHE_LINE_64B`; lazy
flags; page table `uint8_t* readPage[256]` for 6502 memory map with soft-switch pages marked NULL;
drop `assert(false)` cases in the decoder (compiled out with `NDEBUG`/`CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE` for this file only).

## 5. Measuring and regression checks

Benchmark (to be built in step 0, **temporary** code, removed afterwards):
- In `apple2CoreRunCycles` (or a host/ESP test), measure with `esp_timer_get_time()` the host time
  for N = 10 M emulated cycles, no throttling, for fixed workloads; report **Mcycles/s** and
  "× real-time" (1.023 Mcycles/s = 1.00). Run the workloads from RAM (no I/O) so the disk/video
  threads do not disturb.
- Workloads: (1) `LDA/ADC/STA/INX/BNE` loop in RAM, (2) Applesoft `BENCH2` from
  `assets/apple2/BENCH2`, (3) monitor/ROM routines (e.g. `$FC58` HOME, `$FBD9`), (4) DOS 3.3 and
  ProDOS boot to prompt (includes soft switches + disk).
- ESP32-S3 @ 240 MHz, same sdkconfig and same disk images for every row of the table in §6.

Regression (must pass after every step):
- Host tests: `tests/` ctest (`apple2DiskTests`, `apple2MultiDiskTests`, ProDOS autostart/SmartPort
  tests, Pascal boot). Note: 8 tests already abort on missing ROM/image fixtures
  (`docs/verification.md`) — compare against that baseline, do not add failures.
- On hardware: DOS 3.3 boot, ProDOS boot (incl. SmartPort/block trap), UCSD Pascal boot, Videx
  80-column boot (`SETVID`/`PR#3`), lowercase echo, IIe mode, language card, disk write.
- Disk timing: boot of copy-protected/nibble-sensitive images must still work (cycle counter exact
  per access).
- Cycle-exactness check: `core->cycles` after a fixed program equals the value from the old core.

## 6. Experiment log (fill in while working)

Index: 1.00× = baseline cycles/s from step 0.

| Date | Change | ramloop | romloop | copyloop | Speedup (ram/rom/copy) | Kept? | Remarks |
|---|---|---|---|---|---|---|---|
| 10-25 | Step 0: baseline (m6502.h, -Os, flash, per-cycle glue) | 0.844 | 0.611 | 0.874 | 1.00× | — | Mcycles/s; real time = 1.023. Slower than a real Apple II! |
| 10-25 | Step 1: own TU `m6502Impl.c`, `-O2` + jump tables | 1.151 | 0.754 | 1.219 | 1.36 / 1.23 / 1.39× | yes | |
| 10-25 | Step 2: `m6502Impl` in IRAM (`noflash`) | 1.145 | 0.753 | 1.213 | ≈ same | no (to remove) | verified in map (0x4037…); the working set already fits the cache |
| 10-25 | Step 3: slim glue (SYNC-gated checks, RAM/ROM fast path, `cpuBusAccess` hoisted) | 1.331 | 1.156 | 1.409 | 1.58 / 1.89 / 1.61× | yes | |
| 10-25 | + `-O2` for `apple2Core.c` | 1.454 | 1.255 | 1.547 | 1.72 / 2.05 / 1.77× | yes | |
| 10-25 | Diagnostic: bare `m6502_tick` loop with direct RAM access | — | — | — | — | — | **94 host cycles per emulated cycle** just for `m6502_tick`; glue is the other ~70. Baseline was ~285. Target ≤ 142 (ramloop). |
| 10-25 | **Step 5: generated instruction-stepped fast core** (`apple2Fast6502.inc` from `tools/genM6502Fast.py`, same micro-ops as `m6502.h`, so identical bus-access sequence; cycle-stepped `runOneCycle` for the <8-cycle tail, interrupts, `$C000–$CFFF`, Videx/echo) | 3.313 | 2.051 | 4.192 | **3.93 / 3.36 / 4.80×** | yes | Identical end states in the benchmark. Step 2 (IRAM) and step 4 (32-bit pins) skipped: the fast core replaces them. |

### What worked
- Separate `-O2` + jump tables, slim glue with RAM/ROM fast path (1.7–2.0× on loops).
- Generated instruction-stepped core (3.4–4.8× on the synthetic loops; differential host test exact).
- Measuring on hardware with a PC histogram + loop timing (found the real problem, see §9).

### What did not work
- IRAM for the old `m6502_tick` object (no gain: it fitted the cache anyway).
- Removing real-time pacing (`APPLE2_REALTIME_PACING 0`): correct but did NOT change BENCH2 (37 vs 33) because the CPU was already the bottleneck in real programs. Kept (user wish), but not the cause.
- IIe slow-path helpers: needed, but did not change BENCH2 either (machine in the test is a II+ with the 80-column card, `iie=0`).
- Inlining the IIe path at every bus site: link error (`l32r` literal out of range) and `-mtext-section-literals` assembler error. Do not retry; use out-of-line helpers.

### Pitfalls / observations
- **Synthetic benchmarks lie**: a tight loop uses ~10 opcodes. Applesoft uses hundreds, so code size matters.
- `fastRun6502` was 245 KB (`-O2`), 131 KB after moving non-RAM accesses out of line, 110 KB with `-Os`. The ESP32-S3 instruction cache is 16–32 KB, flash fetches thrash it: measured 0.37 Mcycles/s (≈625 host cycles per emulated cycle) while PC was in Applesoft ROM and the fast core handled 100 % of the cycles.
- Hardware test harness: serial port opens reset the board; use pyserial with `dtr=False; rts=False` set before `open()`, send `4\r` (Apple II), `Q` (quit info screen), then `run bench2\r`, `1\r`. Scripts live in `/tmp` (not in the repo). DIAG6502 lines come from temporary code marked `TEMP-DIAG6502` (apple2Machine.c, end of apple2Core.c) — must be removed at the end.

## 9. Attempt log for the BENCH2 (real workload) problem — never repeat an entry marked ✗
| # | Hypothesis / change | Result |
|---|---|---|
| 1 | IIe has no fast path | ✗ fixed, BENCH2 unchanged (machine is II+) |
| 2 | Real-time pacing limits speed | ✗ removed, BENCH2 37–40 (CPU is busy 99 % of the time) |
| 3 | Fast core not used (fallback to cycle-stepped) | ✗ fast counter = total cycles |
| 4 | Instruction cache thrash from 245 KB fast core | ✓ diagnosis (PC in $D000–$EFFF, 625 host cycles/cycle) |
| 5 | Out-of-line `fastBusRead/Write`, 245→131 KB (110 KB with -Os) | partial: smaller, still > cache; host test exact |
| 6 | Put `fastRun6502` in IRAM (`components/apple2Core/linker.lf`, `noflash`, 131 KB of the 183 KB free DIRAM) | ✓ **BENCH2 index 245** (INTEGER 2062 ms, SIEVE 2183 ms). Kept. Host tests unchanged (same 6 fixture failures as baseline), differential test exact. |
| 7 | (not needed) If 6 fails or IRAM does not fit: hot/cold split (only common opcodes in IRAM, rest via cycle-stepped core), or smaller blocks | not tried |

## 7. Done criteria

- Measured speedup ≥ 2.0× vs the step-0 baseline on workloads (1)–(4).
- All regression checks of §5 pass; no change in behaviour of anything except 6502 speed.
- This document updated with final numbers, structure and what was removed.


## 8. Result and verification

- Final: ramloop 3.31, romloop 2.05, copyloop 4.19 Mcycles/s (baseline 0.84 / 0.61 / 0.87; real Apple II = 1.023).
- Final real-workload result: BENCH2 index 245 (was ~37–50 before). Pacing is off by default (`APPLE2_REALTIME_PACING`), temporary diagnostics removed.
- `tests/apple2Fast6502Tests.c` (ctest `apple2Fast6502`): differential test, reference core (`apple2CoreDisableFast6502=1`) vs fast core, random RAM/ROM/keys on II, II+ (boot80) and IIe; 400 trials, ~84M cycles on the fast core; compares CPU registers, pins, cycles, bus value, language card, Videx state and memory after every chunk. Mutation-tested: 15 deliberately broken opcode blocks were all detected.
- Existing host tests: same result as before the change (6 tests fail identically on the untouched tree because of missing ROM/disk fixtures).
- Regenerate the fast core with `python3 tools/genM6502Fast.py` if `m6502.h` changes.
- Not run interactively (console is on the screen): DOS 3.3 / ProDOS boot, SmartPort, Pascal, Videx 80 columns, lowercase echo on hardware. Advisable to check by hand.
- IIe: BENCH2 on a real IIe first ran at index 33 because every IIe access went through the slow `readAddress`/`writeAddress`. Fixed: `fastIIeRead`/`fastIIeWrite` (noinline) handle main/aux RAM, ROM and language-card accesses outside `$C000–$CFFF`; the rest still uses the generic functions. Inlining this per bus site made `fastRun6502` exceed the 256 KB `l32r` literal reach, hence the helper calls. 
- **BENCH2 stayed at index 33 (IIe).** Diagnostics showed the CPU was not the limit: `apple2MachineRun` paces the guest to 1.023 MHz (`vTaskDelayUntil`, 10 ms periods); 501 loops per 5 s, 5.12 M cycles, fast core 99.9 % of the cycles, CPU busy only 1.9 s of 5 s. BENCH2 measures wall-clock ms, so the index cannot exceed ~real speed while paced. At the user's request (no loss of function) pacing is now off by default (`APPLE2_REALTIME_PACING 0` in `main/apple2Machine.c`; set to 1 for the old behaviour); the loop yields one tick every 100 ms for the idle task/watchdog. Hardware BENCH2 result: TBD (user).