# One Step Better — CP/M-80 guest init error reporting

Status: DONE on host side — hardware checks pending (user-run)
Date started: 2026-10-31
Owner: trainee + Claude Code

## Problem

`cpm80GuestInitialize()` in `components/cpm80Core/cpm80Guest.c` returns a plain `bool`
and reports `false` for three genuinely different failures:

1. invalid arguments (NULL pointers, guest already initialized);
2. invalid drive profile from `drives.cfg` (an available drive reports a missing or
   unknown profile);
3. real guest-memory allocation failure in `cpm80CpuInitialize()`.

`cpm80MachineInitialize()` in `main/cpm80Machine.c` maps every one of them to
`ESP_ERR_NO_MEM` and logs a PSRAM/internal-heap dump. A typo in `drives.cfg` is
therefore reported as a memory problem. This is also called out in
`assets/cpm80/cpm80-crashes.md` (analysis + verification step 4).

## Goal

The machine layer must be able to tell the three causes apart and log/report a
matching message and error code. Keep the change minimal; do not restructure the
guest, the machine, or any other subsystem.

## Design

Add a result enum in `components/cpm80Core/include/cpm80Guest.h`:

```c
typedef enum
{
  cpm80GuestInitOk,
  cpm80GuestInitInvalidArg,
  cpm80GuestInitConfig,
  cpm80GuestInitNoMem
} cpm80GuestInitResult;
```

`cpm80GuestInitialize()` returns `cpm80GuestInitResult` instead of `bool` and returns
the cause-specific value. All existing rollback behaviour (`memset` of the guest) stays.

`cpm80MachineInitialize()` maps:

- `cpm80GuestInitConfig`      -> `ESP_LOGE` naming `/littlefs/cpm80/drives.cfg` + `ESP_ERR_INVALID_STATE`;
- `cpm80GuestInitInvalidArg`  -> `ESP_LOGE` programming error + `ESP_ERR_INVALID_ARG`;
- `cpm80GuestInitNoMem`       -> `ESP_LOGE` with the existing PSRAM/internal heap dump + `ESP_ERR_NO_MEM`;
- `cpm80GuestInitOk`          -> unchanged success path.

`closeDiskImages()` still runs on every failure path.

## To do

- [x] `cpm80Guest.h`: add `cpm80GuestInitResult`, change the `cpm80GuestInitialize()` prototype.
- [x] `cpm80Guest.c`: return the cause-specific result from each failure branch.
- [x] `main/cpm80Machine.c`: branch on the result; cause-specific log message and `esp_err_t`.
- [x] `tests/hostTests.c`: update the four `assert(cpm80GuestInitialize(...))` call sites to
      compare against `cpm80GuestInitOk` (plain `assert` would now be wrong because
      `cpm80GuestInitOk == 0`).
- [x] `tests/cpm80TimerTests.c`: update the two call sites the same way.
- [x] Add focused negative coverage in `testCpm80GuestBoot()`:
      invalid-argument result (NULL host ops / already-initialized guest) and
      config result (available drive with an out-of-range profile), without disturbing
      the existing boot test.
- [x] Run host tests (see below); record the result here.
- [x] Run the ESP-IDF firmware build (see below); record the result here.
- [ ] Hardware checks below (user-run).

## How to test

Host tests (ASan/UBSan, `-Wall -Wextra -Werror`):

```sh
cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && \
  ctest --test-dir /tmp/retro-host-tests --output-on-failure -R 'cpm80Timer|hostCore|cpm80Help'
```

Expect: PASS. The four/five updated call sites prove the API change; the new negative
assertions prove the `Config` and `InvalidArg` results. Pre-existing failures in other
ctest targets (Apple/UCSD fixture issues) are out of scope.

Note: the `hostTests` executable runs as the ctest names `hostCore` (default, runs the
full CP/M-80 suite including `testCpm80GuestBoot`) and `cpm80Help`; `cpm80TimerTests`
runs as `cpm80Timer`.

Firmware build (also proves `main/cpm80Machine.c` compiles — host tests do not build it):

```sh
source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh"
idf.py -B build-agent-check build
```

Expect: build succeeds with no new warnings. A separate build directory was used because
the checked-in `build/` directory is configured with a stale Python environment
(`idf.py build` there aborts asking for `fullclean`; see progress log).

Hardware checks (user-run, not executed by Claude Code, no flashing):

1. Normal start -> CP/M-80 reaches `A>`; the success heap-statistics log line is present.
2. Corrupt `drives.cfg` (e.g. an unknown profile word on an available drive) ->
   log states the drives.cfg problem, machine returns to menu, no PSRAM dump.
3. Normal repeated start/reset cycles still reach `A>` (unchanged behaviour).

## Progress log

- 2026-10-31: Document created. No code changed yet.
- 2026-10-31: Added `cpm80GuestInitResult` to `cpm80Guest.h`; `cpm80GuestInitialize()`
  now returns `cpm80GuestInitInvalidArg`, `cpm80GuestInitConfig` or `cpm80GuestInitNoMem`
  for the three causes and `cpm80GuestInitOk` on success. Rollback behaviour unchanged.
- 2026-10-31: `cpm80MachineInitialize()` now logs the `drives.cfg` path and returns
  `ESP_ERR_INVALID_STATE` for a config failure, logs a programming-error line and returns
  `ESP_ERR_INVALID_ARG` for invalid arguments, and keeps the heap dump plus
  `ESP_ERR_NO_MEM` only for the allocation failure. All failure paths still
  `closeDiskImages()`.
- 2026-10-31: Updated six `assert(cpm80GuestInitialize(...))` call sites (four in
  `tests/hostTests.c`, two in `tests/cpm80TimerTests.c`) to compare against
  `cpm80GuestInitOk`. Added negative coverage in `testCpm80GuestBoot()`: out-of-range
  profile on an available drive yields `cpm80GuestInitConfig`, NULL host yields
  `cpm80GuestInitInvalidArg`, and re-initializing an initialized guest yields
  `cpm80GuestInitInvalidArg`.
- 2026-10-31: Host tests: `ctest -R 'cpm80Timer|hostCore|cpm80Help'` — PASS 3/3 under
  ASan/UBSan (`cmake --build /tmp/retro-host-tests` clean under `-Wall -Wextra -Werror`).
- 2026-10-31: Firmware build: `idf.py -B build-agent-check build` (ESP-IDF 6.0.2) — PASS,
  `retroHost.bin` 0x145F20 bytes, 58% of the app partition free, no compiler warnings.
  The temporary build directory was removed afterwards. The pre-existing `build/`
  directory could not be reused: it was configured with
  `~/.espressif/python_env/idf6.0_py3.11_env` while the active venv is
  `~/.espressif/tools/python/v6.0.2/venv`; `idf.py fullclean` in `build/` would fix that
  but was not run because it discards the user's existing build tree.
- 2026-10-31: Hardware checks NOT RUN (user-run, nothing flashed).

## Result

Complete on the host side. `cpm80GuestInitialize()` reports the failure cause; the CP/M-80
machine no longer blames memory for `drives.cfg` problems or programming errors. Verified
by host tests (3/3 PASS) and a warning-free ESP-IDF build. Outstanding: the three hardware
checks above on the ESP32-S3, and the stale-venv `build/` directory (needs
`idf.py fullclean` — user decision).
