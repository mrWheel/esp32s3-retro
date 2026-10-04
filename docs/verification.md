# Verification record

## CP/M boot implementation — 2026-10-04

- Host test command: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`.
- Result: PASS with AddressSanitizer and UndefinedBehaviorSanitizer. The test executed the genuine CCP and BDOS on the vendored Z80 through cold boot to `A>`, internal `DIR`, the transient `HELLO.COM`, and WBOOT back to CCP. It also checked page-zero jumps, DPH/DPB values, invalid-drive rejection and out-of-range DMA failure.
- Resource: `littlefs/cpm/system.dsk`, 256,256 bytes; SHA-256 `a651470e4cf5b2c40ba1c736e8d534eeaa9679631dd2f33765d153d7b3e6bd8d` is checked at runtime. CCP/BDOS provenance and hashes are in `components/cpmCore/os/README.md`.
- Firmware build: ESP-IDF 6.0.2 `build` succeeded for ESP32-S3 and generated `retroHost.bin` at 0xDD9E0 bytes. The integrated LittleFS partition image includes the CP/M resource.
- Board, physical terminal and hardware runtime: NOT RUN. No firmware or partition was flashed.

## Executed before delivery

- Confirmed local ESP-IDF checkout tag `v6.0.2` and ESP32-S3 Xtensa toolchain availability.
- The ESP-IDF component manager resolved the exact manifest dependencies and produced `dependencies.lock`: provisioner 0.4.0 and LittleFS 1.20.3.
- ESP32-S3 CMake configuration/generation succeeded on an offline dependency path after correcting the version check to use IDF's major/minor/patch variables.
- Portable C tests passed with AddressSanitizer and UndefinedBehaviorSanitizer: traversal and malformed encoding rejection, 50,000 generated path cases, layout line endings/version rejection, CR/LF/CRLF/overflow/backspace input, 64 MiB image range checks, read-only rejection and byte-preserving image I/O.
- A firmware build attempt stopped while LittleFS tried to install `littlefs-python==0.15.0`: sandbox DNS/network access was unavailable. The full application did not finish compiling/linking and no firmware binary is delivered.
- At that earlier checkpoint, no further builds were run; the user reserved build completion to VSCode. The CP/M implementation above was validated in a later build.

## Not executed / required before hardware acceptance

No device was flashed or exercised. Do not interpret implemented functionality or portable tests as completed hardware acceptance.

1. Confirm board flash size, native USB connection and actual SPI GPIO wiring.
2. After verifying board configuration, have the user flash the firmware and LittleFS partition and monitor boot output.
3. RESET repeatedly; verify CP/M option 1 reaches the genuine `A>` prompt and accepts `DIR` and `HELLO`, while options 2–5 remain placeholders and input edge cases remain safe.
4. Verify LittleFS mount, the CP/M image checksum and graceful behavior when bootfs is absent/corrupt.
5. Boot without SD; test blank FAT32, FAT16/exFAT, wrong marker/version, missing each required directory and correct prepared layout.
6. Verify no WiFi activity before choice 6, including choice 6 with invalid SD.
7. Test first provisioning, wrong credentials, stored credentials after reset, unreachable AP and leaving during setup.
8. Run `tools/transferTest.py` for HTTP round trips and hostile paths. Confirm SHA-256 on hardware, including files larger than available RAM.
9. Test browser delete confirmation (cancel and confirm), directory navigation and direct browser downloads.
10. Test full/nearly-full card, network disconnect, stalled/aborted HTTP client, and reset during upload. No partial final file should appear.
11. Press ENTER during upload and download; verify server closure, temporary cleanup and responsive menu; repeat entry/exit at least 20 times and inspect free heap.
12. Measure CP/M instruction throughput, watchdog health, terminal behavior and working editor/assembler/compiler workflow on the actual board.
