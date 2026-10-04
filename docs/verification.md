# Verification record

## Executed before delivery

- Confirmed local ESP-IDF checkout tag `v6.0.2` and ESP32-S3 Xtensa toolchain availability.
- The ESP-IDF component manager resolved the exact manifest dependencies and produced `dependencies.lock`: provisioner 0.4.0 and LittleFS 1.20.3.
- ESP32-S3 CMake configuration/generation succeeded on an offline dependency path after correcting the version check to use IDF's major/minor/patch variables.
- Portable C tests passed with AddressSanitizer and UndefinedBehaviorSanitizer: traversal and malformed encoding rejection, 50,000 generated path cases, layout line endings/version rejection, CR/LF/CRLF/overflow/backspace input, 64 MiB image range checks, read-only rejection and byte-preserving image I/O.
- A firmware build attempt stopped while LittleFS tried to install `littlefs-python==0.15.0`: sandbox DNS/network access was unavailable. The full application did not finish compiling/linking and no firmware binary is delivered.
- At the user's request, no further builds were run; build completion belongs in VSCode. Subsequent documentation and source review changes have not been compiled.

## Not executed / required before HOST-M1 hardware acceptance

No device was flashed or exercised. Do not interpret implemented functionality or portable tests as completed hardware acceptance.

1. Confirm board flash size, native USB connection and actual SPI GPIO wiring.
2. Finish `idf.py build`, inspect `idf.py size`, flash all partitions and monitor.
3. RESET repeatedly, test all five placeholder/ENTER returns and input edge cases.
4. Verify LittleFS mount and five directories; verify graceful behavior when bootfs is absent/corrupt.
5. Boot without SD; test blank FAT32, FAT16/exFAT, wrong marker/version, missing each required directory and correct prepared layout.
6. Verify no WiFi activity before choice 6, including choice 6 with invalid SD.
7. Test first provisioning, wrong credentials, stored credentials after reset, unreachable AP and leaving during setup.
8. Run `tools/transferTest.py` for HTTP round trips and hostile paths. Confirm SHA-256 on hardware, including files larger than available RAM.
9. Test browser delete confirmation (cancel and confirm), directory navigation and direct browser downloads.
10. Test full/nearly-full card, network disconnect, stalled/aborted HTTP client, and reset during upload. No partial final file should appear.
11. Press ENTER during upload and download; verify server closure, temporary cleanup and responsive menu; repeat entry/exit at least 20 times and inspect free heap.
12. Verify read-only/range constraints of generic images against real SD files before a guest uses them.

The project is ready for the requested VSCode build/setup workflow. The specification's final hardware Definition of Done remains pending these tests.
