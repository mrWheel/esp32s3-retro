# Monotonic Millisecond Time Source

## Purpose and scope

This document defines a reusable host-side time source for emulator code and
records the first guest-facing implementation, CP/M-86's
`RETRO86_TIMER_B1` extension. The callback contract is generic and may be
injected into other CPU emulators. A guest-visible timer protocol is not
automatically shared: each emulator must explicitly integrate and document its
own guest API.

This is elapsed-time measurement, not a calendar clock. It does not provide a
date or time of day, and does not require network access or clock
synchronization.

## Host callback contract

The shared callback type is declared in
`components/hostCore/include/hostClock.h`:

```c
typedef uint32_t (*hostReadMilliseconds)(void *context);
```

An emulator that needs elapsed time accepts the optional callback and its
context in its configuration:

```c
hostReadMilliseconds readMilliseconds;
void *clockContext;
```

The emulator calls the callback when it needs a fresh sample. The callback
returns an unsigned 32-bit millisecond count. The context is owned by the
adapter or caller and must remain valid for as long as the emulator may invoke
the callback. A null callback means that no time source is available; each
emulator must define its safe behavior for that case.

This boundary keeps CPU emulation independent of ESP-IDF and lets desktop
tests inject a deterministic clock. It also permits other hosts to provide
their own monotonic source without changing the callback type.

## ESP32-S3 source

The CP/M-86 ESP32 adapter uses ESP-IDF ESP Timer:

```c
#include "esp_timer.h"

uint32_t milliseconds =
    (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL);
```

`esp_timer_get_time()` returns monotonic microseconds from ESP Timer
initialization. Conversion to milliseconds provides 1 ms units. The unsigned
32-bit value wraps to zero after 2^32 milliseconds, approximately 49.7 days.
Elapsed-time subtraction is valid modulo 2^32 for intervals shorter than one
full wrap, provided ESP Timer is not reset or reinitialized during the
measurement.

This count advances independently of guest instruction execution, including
while the emulator is yielding to the host or otherwise paused. A timer reset
or reinitialization invalidates measurements that span that event.

Keep the ESP-IDF include and call in the platform adapter. Do not add ESP-IDF
dependencies to a portable CPU core.

## CP/M-86 guest protocol: RETRO86_TIMER_B1

The initial guest-facing binding is implemented only by the CP/M-86 machine.
It uses byte-wide 8086 `IN` operations on ports F0h–F4h:

| Port | Direction | Meaning |
|---|---|---|
| F0h | IN byte | Capability: B1h when a time callback is configured; otherwise 00h. |
| F1h | IN byte | Read a fresh counter value into the emulator-instance snapshot; return bits 0–7. |
| F2h | IN byte | Return bits 8–15 of the F1h snapshot. |
| F3h | IN byte | Return bits 16–23 of the F1h snapshot. |
| F4h | IN byte | Return bits 24–31 of the F1h snapshot. |

F0h does not affect the snapshot. Before the first F1h read, and after emulator
reset, the snapshot is zero. Reset clears only this snapshot; it does not reset
ESP Timer. Reads of F2h–F4h do not call the callback. With no callback, F0h
reports unavailable and the four time bytes are zero.

The ports are read-only. Writes follow the core's existing invalid-I/O error
behavior. Sixteen-bit I/O is unsupported and must not perform any partial timer
operation. The snapshot belongs to the emulator instance.

B1h identifies this exact protocol; it is an emulator extension, not a CP/M or
BDOS feature. The capability check is safe only on firmware that implements
`RETRO86_TIMER_B1`: older firmware may reject unknown ports as an I/O error.
Firmware introducing the protocol documents this revision in
`docs/cpm86/virtualIo.md`.

### 8086 read routine

The following Intel/NASM-style routine uses original 8086 instructions:

```asm
read_milliseconds:
    push bx
    in   al, 0F1h
    mov  bl, al
    in   al, 0F2h
    mov  bh, al
    in   al, 0F3h
    mov  dl, al
    in   al, 0F4h
    mov  dh, al
    mov  ax, bx
    pop  bx
    ret
```

It returns the unsigned 32-bit value in `DX:AX`, preserves `BX`, and does not
preserve flags. Save a start value and subtract it from a later result using
`SUB AX, [start_low]` and `SBB DX, [start_high]`. The modular subtraction
handles one counter wrap for measurements shorter than 2^32 ms, unless the
underlying time source was reset.

## Integration guidance for other emulators

The callback may be reused by any emulator that needs a host-provided elapsed
time source. For each new integration:

1. Add the optional callback and context to that emulator's configuration and
   explicitly initialize them at every configuration construction site.
2. Keep platform-specific clock calls outside the CPU implementation.
3. Define what happens when no callback is supplied and how emulator reset
   affects emulator-owned snapshots.
4. If guest software needs access, select and document a guest protocol only
   after checking that its ports, registers, calls, or other interface do not
   conflict with existing devices and behavior.
5. Define protocol capability detection and unsupported-access behavior.
6. Add deterministic tests for conversion/byte order, snapshot consistency,
   callback counts, reset, unavailable source, wraparound and invalid access.
7. Exercise the guest interface through the CPU emulator where practical, and
   separately run that emulator's existing regression tests and platform
   build.

Do not expose the CP/M-86 F0h–F4h protocol in another emulator merely by
reusing the callback. Such a binding requires its own explicit integration and
compatibility decision.

## Verification record

### CP/M-86 timer implementation — 2026-10-08

- **Desktop tests:** `cmake --build build-host && ctest --test-dir build-host
  --output-on-failure` passed; CTest reported 1/1 test passed. The host test
  executable uses AddressSanitizer and UndefinedBehaviorSanitizer.
- **Target firmware build:** ESP-IDF 6.0.2 ESP32-S3 build passed. The generated
  `build/retroHost.bin` was 0xECBA0 bytes; the smallest app partition was
  0x300000 bytes, leaving 69% free.
- **Timer-specific checks:** covered capability with and without a callback,
  12345678h byte order, stable snapshot across F0h and callback-time changes,
  exactly one callback call per F1h read, subsequent fresh snapshots, reset
  without changing the external clock, absent-source zero reads, rejected
  writes and word I/O, and unsigned wraparound from FFFFFFF0h to 00000018h
  (40 ms).
- **Guest CPU execution:** the test executed the 8086 timer-read routine in the
  CPU emulator and verified the DX:AX value and BX preservation.
- **Hardware:** NOT RUN. No firmware was flashed. After flashing, read the
  counter through host-driven byte I/O, wait approximately 1,000 ms in the host
  layer, and read again. Accept an unsigned difference from 900 through
  1,100 ms. Do not use a guest instruction loop as the calibrated delay.

These are distinct results: desktop tests passed, the firmware build passed,
and physical hardware verification remains open.
