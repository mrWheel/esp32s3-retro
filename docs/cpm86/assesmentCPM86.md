# CP/M-86 Emulator Status and Open Tests

Assessment date: 2026-10-24

This document records the implementation status, remaining work, and tests
needed to qualify the CP/M-86 emulator. It reflects the repository and
verification notes reviewed on the assessment date. A passing host test or
firmware build is not evidence of successful execution on the ESP32-S3.

## Overall status

The CP/M-86 machine is integrated at menu option 2 and is beyond the
placeholder/prototype stage. The desktop host tests boot the genuine CP/M-86
guest to the `A>` prompt and run `DIR`. The firmware has an ESP-IDF 6.0.2 build
recorded as passing.

The CPU implementation has recently changed: the project-owned
`tiny8086`-style interpreter replaced EMU86. User-reported ESP32-S3 benchmark
results exist for the new core, but the final firmware after the latest
diagnostic-code cleanup has not been reflashed or hardware-verified. Therefore
the current emulator is **integrated and host-tested, but not yet fully
hardware-qualified**.

This status does not establish complete 8086 conformance, broad CP/M-86
compatibility, reliable writable media, or a complete native development
workflow.

Local verification on 2026-10-24 confirmed that the CP/M-86 host tests pass
and that the current ESP-IDF firmware builds successfully. The full repository
test run also exposed failures in unrelated Apple II, UCSD Pascal, DOS 3.3 and
CP/M-80 tooling tests; details are recorded below.

## Implemented and evidenced

- CP/M-86 is registered at menu option 2 and has a dedicated
  `cpm86Machine` integration.
- The project-owned CPU core provides bounded memory access, instruction-budget
  execution, byte-port callbacks, and a timer extension. It enforces a single
  active core instance.
- The RETRO86_V1 BIOS provides console and disk services over the documented
  virtual byte-port interface.
- System resources include `cpm.sys`, the BIOS overlay, and a system disk.
  The firmware loads the guest system and BIOS, installs the BDOS vector, and
  starts the guest rather than printing a simulated prompt.
- The configured disk profiles include the 160 KiB system/data profile, the
  528,384-byte LARGE profile, and the 8,392,704-byte BIG profile.
- Host tests cover selected CPU instructions and flags, memory bounds and
  physical-address wrap, ports and timer behavior, drive-configuration
  validation, guest boot, `DIR`, additional drives, and reading files that
  span extents on LARGE and BIG images.
- The virtual I/O contract is documented in
  [`virtualIo.md`](./virtualIo.md).
- The machine-specific engineering history and verification log are in
  [`designCPM86.md`](../../designCPM86.md). The CPU migration and reported
  benchmark history are in [`migrate8086CPU.md`](../../migrate8086CPU.md).

## Verification performed (2026-10-24)

- `cmake --build build-host --target hostTests` passed with the configured
  warning-as-error and address/undefined-behavior sanitizer build.
- `ctest --test-dir build-host --output-on-failure` ran all 27 registered
  tests: 23 passed, 3 failed, and 1 was not run because its test dependency
  failed. The `hostCore` test passed; this includes the CP/M-86 CPU fixtures,
  timer-port tests, real guest boot to `A>`, `DIR`, configured-drive behavior,
  and large/BIG image guest tests. The failures were outside CP/M-86:
  `apple2DiskStream` failed opening its Apple II test image,
  `ucsdPascalBoot` failed opening its boot image, and `guestFsDos33` failed its
  `realCount > 0` check. Its dependent `guestFsDos33PythonVerify` test was not
  run. The expected UCSD boot image
  `littlefs/ucsd/ApplePascal1-680-0004-01.dsk` is absent in this worktree.
- `python3 -m unittest tests/test_createSystemDsk.py tests/test_diskImage.py tests/test_extractCpm.py`
  ran 27 tests: 26 passed and 1 failed. The failure was
  `test_build_cpm80_matches_checked_in_image`, which found a byte-level
  mismatch between a generated CP/M-80 image and the checked-in image; it is
  not a CP/M-86 test.
- `source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh" && idf.py build`
  passed using ESP-IDF 6.0.2. It generated `build/retroHost.bin` (0x123650
  bytes), with 62% of the app partition free. The build emitted non-fatal
  warnings for the missing `ota_manager_tools` Python module and several
  ESP-IDF Kconfig boolean defaults.
- No firmware was flashed by the assessor. The firmware already on the board
  (menu banner `v0.4.11`; build identity vs. the current `build/` not
  verified) was exercised with `python3 tools/cpm86/hardwareTest.py` over the
  USB console on 2026-10-24: **20 passed, 0 failed, 4 manual**. Passed on
  hardware: menu, cold boot to `A>`, `DIR`/`STAT` on A:, drive selection and
  `DIR` on B:, C:, D:, clean rejection of E:/F: (`E:?`), `PIP` copy A:->D:
  and D:->B:, file persistence after Ctrl+] stop and relaunch, `ERA`, three
  repeated stop/launch cycles without reset, a write to read-only A: refused
  (`Bdos Err On A: Bad Sector`, recoverable with Ctrl-C), and `DIR` in 0.4 s.
  The first run reported 4 failures that were script defects (wrong expected
  prompts, no Ctrl-C after a Bdos error); the script was corrected and rerun.
  The tests wrote and erased a temporary file on D: (`work86.dsk`).
- Boot log supplied by the user (second run): ESP32-S3 chip revision v0.2,
  16 MB flash, partition table as expected. The six app segment lengths in
  the boot log (0x4298c, 0x5b38, 0x7b24, 0xbd080, 0x16558, 0x24) are identical
  to those of the current `build/retroHost.bin` (checked with
  `esptool image-info`), so the board most likely runs the current build. This
  is circumstantial; the `bootloader compile time` line (Oct 23) belongs to the
  bootloader, which is not rebuilt with the app. The scripted run was repeated
  on this firmware: again 20 passed, 0 failed. That log showed no PSRAM line; see
  the PSRAM item below for the later result.
- User-reported on hardware (2026-10-24, not captured in a script log):
  the millisecond timer, the editor, assembler and compiler workflow, terminal
  behavior, disk-full, and reset during writes all work. No transcripts or
  measured values (e.g. timer delta) were provided, so these are reported
  results, not reproducible evidence.
- PSRAM verified on hardware: after adding a PSRAM line to the main menu
  (`systemMenu.c`; a matching log line was added in `cpm86Machine.c`) and
  reflashing, the user-supplied menu shows `PSRAM: 8192 KiB, free 8184 KiB`,
  matching the expected 8 MB octal PSRAM. The 640 KiB CP/M-86 guest RAM fits.
  This firmware build (`retroHost.bin` 0x1236b0 bytes) was flashed by the user.

## Remaining work

### CPU and guest compatibility

- Establish 8086 instruction and flag behavior with an independent
  conformance suite; current evidence is based on selected fixtures and guest
  workloads.
- Test segment and offset boundaries, 20-bit wrapping, near/far control flow,
  `INT`/`IRET`, and `REP` string operations against independent expectations.
- Document and test unsupported behavior. In particular, external interrupts,
  HLT wake-up, and trap-flag single-step are not implemented.
- Complete the primary-manual audit for the selected CP/M-86 release,
  including BIOS calling conventions, memory reporting, segment-aware DMA,
  and pointer representation.
- Test DMA ranges at populated-memory and segment boundaries and confirm that
  invalid or wrapping guest buffers fail according to the intended profile.

### Disk and storage reliability

- Independently validate the geometry, DPBs, filesystem capacity, allocation,
  directory limits, and extent behavior for every supported profile.
- Resolve or explicitly document the known mismatch between the LARGE profile
  and the external `cpmtools` definition before treating that tool as a
  validator for these images.
- Exercise all configured drive letters and first/last-record boundaries.
- Test read-only write failures, writable record persistence after close and
  reopen, short I/O, flush failures, missing or malformed images, and
  disk-full/directory-full behavior.
- Test recovery after media errors and resets during guest metadata writes.
  Multi-record guest filesystem updates are not guaranteed to be
  crash-atomic.

### Current firmware on hardware

- Flash and boot the exact current firmware build on the target ESP32-S3.
- Verify PSRAM initialization and the configured guest-memory allocation.
- Verify the guest boot, console input/output, disk activity, timer, and
  responsiveness on the board.
- Verify Ctrl+] exits the guest, closes media, returns to the host prompt, and
  permits repeated emulator launches.
- Repeat the hardware benchmark with the final build. Earlier measurements
  are useful performance evidence but do not validate the post-cleanup
  firmware image.

### Native software workflow and provenance

- Demonstrate a native CP/M-86 transient program, a file-copy workflow, an
  editor, an assembler, and at least one compile/run workflow on the target.
- Qualify the terminal behavior required by those programs, including
  newline, backspace/delete, Ctrl-C, flow control, escape sequences, cursor
  movement, erase, and reverse video.
- Finish recording the exact OS/resource provenance, applicable rights
  evidence, and primary-manual references for the materials actually used.

## Tests still needed

| Test area | Required evidence |
| --- | --- |
| CPU conformance | Independent 8086 instruction, flags, segment, wrap, interrupt, string-operation, and invalid-opcode tests. |
| BIOS and guest integration | Cold boot, warm boot, native transient program, memory reporting, segment-aware DMA, and invalid-DMA boundary cases. |
| Disk profile matrix | All supported profiles and drives; first/last records; extents; directory-full and disk-full; read-only errors; writable readback. |
| Host lifecycle | Clean stop, media close, fault recovery, repeated launches, and return to the menu without reset. |
| Current ESP32-S3 firmware | Boot and run the exact post-cleanup build; verify PSRAM, console, timer, SD access, and guest responsiveness. |
| SD persistence | On disposable images, write and read back after close/reopen; exercise flush failures, missing media, and reset recovery. |
| Timer and scheduling | On hardware, wait about one second outside guest execution and verify a counter delta of 900–1100 ms; test long instruction and `REP` workloads for scheduler/watchdog behavior. |
| Native development workflow | Run the editor and assembler or compiler workflow on hardware, including terminal compatibility and workload completion. |

## Current drive configuration note

At the time of assessment, the checked-in
[`drives.cfg`](../../littlefs/cpm86/drives.cfg) was modified in the worktree.
The inspected configuration sets A: to read-only and B:, C:, and D: to
read/write; E: and F: are not configured. The optional drives only become
usable if the corresponding SD images exist, have the exact expected sizes,
and contain filesystems matching their declared profiles. Verify the actual
target-card contents before using these writable entries. This assessment
records the observed configuration and does not change it.

## Acceptance summary

| Area | Status |
| --- | --- |
| Host integration and menu registration | Implemented |
| Desktop guest boot, `DIR`, timer, and configured-drive tests | Passed in local host test run |
| Current ESP-IDF 6.0.2 firmware build | Passed locally; hardware not verified |
| Boot, drives, SD write/readback, Ctrl+] relaunch on ESP32-S3 | Passed (20/20 scripted checks); current build most likely (segment sizes match) |
| PSRAM (8192 KiB seen in menu) on ESP32-S3 | Verified |
| Timer, editor/assembler/compiler, terminal, disk-full, reset during write | Reported working by user; no transcripts or measured values |
| Broad 8086 conformance | Open |
| BIOS ABI and DMA boundary audit | Open |
| Writable SD-media reliability | Scripted write/readback passed; further cases reported working by user |
| Full native editor/assembler/compile workflow | Reported working by user |
| Complete provenance and rights evidence | Open |
