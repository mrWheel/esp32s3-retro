# Verification record

## CP/M boot implementation — 2026-10-04

- Host test command: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`.
- Result: PASS with AddressSanitizer and UndefinedBehaviorSanitizer. The test executed the genuine CCP and BDOS on the vendored Z80 through cold boot to `A>`, internal `DIR`, the transient `HELLO.COM`, and WBOOT back to CCP. It also checked page-zero jumps, DPH/DPB values, invalid-drive rejection and out-of-range DMA failure.
- Resource at this checkpoint: `littlefs/cpm80/system.dsk`, 256,256 bytes; size and header signature are checked at runtime. CCP/BDOS provenance is in `components/cpm80Core/os/README.md`.
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
4. Verify LittleFS mount, the CP/M image size/signature check and graceful behavior when bootfs is absent/corrupt.
5. Boot without SD; test blank FAT32, FAT16/exFAT, wrong marker/version, missing each required directory and correct prepared layout.
6. Verify no WiFi activity before choice 6, including choice 6 with invalid SD.
7. Test first provisioning, wrong credentials, stored credentials after reset, unreachable AP and leaving during setup.
8. Run `tools/transferTest.py` for HTTP round trips and hostile paths. Confirm byte-exact round trips on hardware, including files larger than available RAM.
9. Test browser delete confirmation (cancel and confirm), directory navigation and direct browser downloads.
10. Test full/nearly-full card, network disconnect, stalled/aborted HTTP client, and reset during upload. No partial final file should appear.
11. Press ENTER during upload and download; verify server closure, temporary cleanup and responsive menu; repeat entry/exit at least 20 times and inspect free heap.
12. Measure CP/M instruction throughput, watchdog health, terminal behavior and working editor/assembler/compiler workflow on the actual board.

## CP/M utility image and writable work drive — 2026-10-04

- Generated `littlefs/cpm80/system.dsk` with `tools/buildDiskImageCpm80.py`. Image size is 256,256 bytes.
- Independently inspected its CP/M directory/extents and reconstructed data blocks: 20 directory extents for 19 files, all required source files match their padded 128-byte records, 147 allocated blocks are within the DPB's valid blocks 2–242, and the image header/size are correct.
- Host command: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`.
- Result: PASS under ASan/UBSan. The genuine guest lists the utilities, exercises resident `TYPE` and `USER`, performs `PIP E:=A:HELLO.COM`, then `REN` and `ERA` on E:. BIOS tests also verify E: record writes and A: write rejection. This is a host fixture, not a physical SD card.
- ESP-IDF 6.0.2 ESP32-S3 build: PASS; `retroHost.bin` is 0xDDE90 bytes, within the 3 MiB app partition.
- Source pin and compatibility notes are in `components/cpm80Core/os/utilities/README.md`. `HELP.COM` identifies as a CP/M 3.0 utility and has not been proven against this CP/M-80 BDOS. `SUBMIT.COM` writes `$$$.SUB` to read-only A: and is not usable until that assumption is addressed. No separate SDIR/SHOW binaries are included; resident `DIR` and `STAT` are the available directory/status commands.
- Hardware SD-media tests remain to be run. No device was flashed.

## Mac CP/M disk and archive tools — 2026-10-04

- `tools/diskImageCpm80.py` creates empty 256,256-byte raw images matching the BIOS's 77-track, 26-sector, 128-byte-record DPB and can add/list CP/M 8.3 files. Additions validate extent and block allocations and replace the disk file atomically only after all checks succeed.
- `tools/fetchCpm80Software.py` listed the user-specified RetroArchive language index successfully. It selects links from that index, requires per-import rights evidence, blocks identified Microsoft software, explicitly selects ZIP members without extracting paths, preserves original downloads, and records URLs, license-evidence text, timestamps and SHA-256 values in a JSON manifest. No copyrighted archive binary was downloaded during validation.
- Python test command: `PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_*.py' -v`.
- Result: PASS, 13 tests covering image creation, CP/M file extents, duplicate/disk-full preservation, CLI operation, archive link parsing, host restrictions, Microsoft-download blocking and provenance retention.
- SD card formatting/mounting, Finder copy, firmware mounting and hardware PIP execution remain untested. Raw disk images are size/structure-checked only; source documentation must establish geometry and sector ordering. IMD/TD0 conversion is unsupported.

## CP/M HOST transfer utility — 2026-10-04

- Reassembled `components/cpm80Core/os/host/HOST.ASM` with z80asm 1.8; a second assembly was byte-identical to the checked-in `HOST.COM` (3,267 bytes).
- Regenerated `littlefs/cpm80/system.dsk` with `tools/buildDiskImageCpm80.py`; its size is 256,256 bytes.
- Host test command: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`.
- Result: PASS under AddressSanitizer and UndefinedBehaviorSanitizer. The genuine CCP/BDOS guest ran `HOST DIR`; imported and exported a 777-byte binary containing NUL, `0x1A`, high-bit bytes and CR/LF with exact-byte equality; refused a duplicate destination; and refused an existing `.HST` sidecar while the E: directory still showed that sidecar afterward.
- ESP-IDF 6.0.2 ESP32-S3 `build`: PASS. `retroHost.bin` is 0xDEA00 bytes, within the 3 MiB app partition.
- This verifies the host guest emulator path only. USER-area rejection after launching HOST in USER 1, cancellation/error cleanup, full E: media, physical SD and ESP32 transfer remain separate acceptance items. No device was flashed.

## CP/M-80 guest utility relocation — 2026-10-07

- Moved project-authored `HOST.ASM` and `HOST.COM` from `components/cpm80Core/os/host/` to `guest/cpm80/host/`; updated the system-image builder and documentation to use the guest-owned location.
- Reassembled `guest/cpm80/host/HOST.ASM` with z80asm and confirmed the output is byte-identical to `guest/cpm80/host/HOST.COM` (3,267 bytes).
- `PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_diskImage.py' -v`: PASS, 7 tests; CP/M-80 system-image output remains byte-identical to the checked-in image.
- `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`: PASS, including the host-side guest emulator test.
- ESP-IDF 6.0.2 ESP32-S3 `build`: PASS; `retroHost.bin` is 0xE8410 bytes (70% of the 3 MiB app partition remains free). No device was flashed.

## CP/M-80 HOST CP/M-86 parity — 2026-10-07

- Aligned CP/M-80 `HOST.COM` user-visible behaviour with the CP/M-86 utility for `DIR`, `GET`, and `PUT`: usage/error wording, wildcard result lines, reserved `.HST` handling, no-match/list-full reporting, completion text, and drive-preserving return behaviour. The CP/M-80-only local per-file reason remains `local CP/M-80 file I/O failed.`.
- The genuine CCP/BDOS host fixture imported multiple exchange files with `HOST GET *.*`, reported and continued after an existing local target, exercised `HOST GET INP?T.BIN`, rejected a pre-existing `.HST` sidecar, and verified the other file appears on E:. `HOST PUT *.BIN` continued after an existing host destination and round-tripped a 257-byte binary exactly; `HOST PUT INP?T.BIN` exercised `?`.
- `cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`: PASS under ASan/UBSan. `PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_diskImage.py' -v`: PASS, 7 tests. Reassembly of `guest/cpm80/host/HOST.ASM` matches checked-in `HOST.COM` (6,950 bytes). Rebuilt `littlefs/cpm80/system.dsk` is 256,256 bytes.
- ESP-IDF 6.0.2 ESP32-S3 `build`: PASS; `retroHost.bin` is 0xE8410 bytes, with 70% of the 3 MiB app partition free. No device was flashed.
