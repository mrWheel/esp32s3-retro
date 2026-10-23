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

- Generated `littlefs/cpm80/system.dsk` with `tools/include/buildDiskImageCpm80.py`. Image size is 256,256 bytes.
- Independently inspected its CP/M directory/extents and reconstructed data blocks: 20 directory extents for 19 files, all required source files match their padded 128-byte records, 147 allocated blocks are within the DPB's valid blocks 2–242, and the image header/size are correct.
- Host command: `cmake -S tests -B /tmp/retro-host-tests && cmake --build /tmp/retro-host-tests && ctest --test-dir /tmp/retro-host-tests --output-on-failure`.
- Result: PASS under ASan/UBSan. The genuine guest lists the utilities, exercises resident `TYPE` and `USER`, performs `PIP E:=A:HELLO.COM`, then `REN` and `ERA` on E:. BIOS tests also verify E: record writes and A: write rejection. This is a host fixture, not a physical SD card.
- ESP-IDF 6.0.2 ESP32-S3 build: PASS; `retroHost.bin` is 0xDDE90 bytes, within the 3 MiB app partition.
- Source pin and compatibility notes are in `components/cpm80Core/os/utilities/README.md`. `HELP.COM` identifies as a CP/M 3.0 utility and has not been proven against this CP/M-80 BDOS. `SUBMIT.COM` writes `$$$.SUB` to read-only A: and is not usable until that assumption is addressed. No separate SDIR/SHOW binaries are included; resident `DIR` and `STAT` are the available directory/status commands.
- Hardware SD-media tests remain to be run. No device was flashed.

## Mac CP/M disk and archive tools — 2026-10-04

- `tools/diskImage.py --os cpm80` creates empty 256,256-byte raw images matching the BIOS's 77-track, 26-sector, 128-byte-record DPB and can add/list CP/M 8.3 files. Additions validate extent and block allocations and replace the disk file atomically only after all checks succeed.
- `tools/fetchCpm80Software.py` listed the user-specified RetroArchive language index successfully. It selects links from that index, requires per-import rights evidence, blocks identified Microsoft software, explicitly selects ZIP members without extracting paths, preserves original downloads, and records URLs, license-evidence text, timestamps and SHA-256 values in a JSON manifest. No copyrighted archive binary was downloaded during validation.
- Python test command: `PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_*.py' -v`.
- Result: PASS, 13 tests covering image creation, CP/M file extents, duplicate/disk-full preservation, CLI operation, archive link parsing, host restrictions, Microsoft-download blocking and provenance retention.
- SD card formatting/mounting, Finder copy, firmware mounting and hardware PIP execution remain untested. Raw disk images are size/structure-checked only; source documentation must establish geometry and sector ordering. IMD/TD0 conversion is unsupported.

## CP/M HOST transfer utility — 2026-10-04

- Reassembled `components/cpm80Core/os/host/HOST.ASM` with z80asm 1.8; a second assembly was byte-identical to the checked-in `HOST.COM` (3,267 bytes).
- Regenerated `littlefs/cpm80/system.dsk` with `tools/include/buildDiskImageCpm80.py`; its size is 256,256 bytes.
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

## Guest disk activity RGB indicator — 2026-10-07

- Added WS2812B control on the LOLIN S3 Pro RGB LED GPIO 38 using the ESP-IDF RMT driver. CP/M-80 and CP/M-86 disk-record reads signal green; record writes signal red. A dedicated task handles LED updates and turns the LED off 120 ms after the latest activity. Other host filesystem access is not indicated.
- ESP-IDF 6.0.2 ESP32-S3 firmware build: PASS; `retroHost.bin` is 0xEC660 bytes, within the 3 MiB app partition.
- Host regression command: `cmake --build build-host && ctest --test-dir build-host --output-on-failure`: PASS, 1/1 test.
- Physical RGB output: NOT RUN. Firmware was not flashed; LED color and board-level behavior remain to be verified on hardware.

## Disk-image manager partial implementation — 2026-10-21

- Extended `main/fileTransfer.c` and `main/web.html` with mapped-image reporting and whole-image SD-to-SD copy, rename/move and explicit backup restoration. Copies use bounded buffers, flush a temporary file and compare it byte-for-byte with the source before committing. Replacements retain the old destination as `manager-replace-backup.tmp` in that destination directory until commit succeeds; no automatic recovery-file deletion is performed. The UI uses a browser-native file picker for uploads and normal attachment downloads instead of buffering downloaded image blobs in browser JavaScript.
- The browser explicitly reports that guest files inside disk images are not yet supported. CP/M-80, CP/M-86, Apple/ProDOS, and UCSD Pascal filesystem adapters, guest file operations, direct guest-image-to-image file copy, and the unified path/copy workflow have not been implemented. Existing `/retro/exchange/` and guest HOST paths remain required.
- `source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh" && idf.py build && idf.py size`: PASS; final `retroHost.bin` 0x100F90 bytes, within the 0x300000-byte app partition. The linker report listed 759,372 bytes flash code, 177,308 bytes flash data and 132,293 bytes DIRAM use. No device was flashed.
- Focused host test `ctest --test-dir /tmp/esp32s3-retro-build-host --output-on-failure -R '^hostCore$'`: PASS, 1/1.
- Full host CTest: 6/14 passed, 8/14 failed. Failures concern current Apple ROM/image fixture availability and a stale expected Pascal drive path; see `designDiskImageManager.md` checkpoint 2026-10-21 for command and failure detail. These tests do not cover the new HTTP operations.
- Full Python test suite: 77/79 passed; the two failures are the existing CP/M-80 image-equality expectation and an outdated Apple extraction-menu string. Details are recorded in `designDiskImageManager.md`.
- Hardware acceptance, browser compatibility, actual SD whole-image operations, power-loss recovery and mapped-image operation behavior on device: NOT RUN. LittleFS source resources were not modified.

## Disk-image manager GUI visibility — 2026-10-21

- The initial File Transfer page selected the legacy `/retro/exchange/` view by default, so the previously added whole-image controls were not visible on first load. `main/web.html` now selects `/retro/images/` by default and labels the implemented whole-image scope directly on the page. The legacy exchange view remains available as an alternate mode because current guest HOST integrations still depend on it.
- `main/CMakeLists.txt` embeds `web.html` in the application, and the page response sets `Cache-Control: no-store`. After the UI change, `source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh" && idf.py build && idf.py size`: PASS; `build/retroHost.bin` is 0x100F90 bytes. `strings build/retroHost.bin` confirmed the new heading, default-mode message and image-mode option are in the generated firmware.
- The coding agent did not flash the device and did not run a browser/device test. For the next manual flash, use the freshly generated `build/retroHost.bin`; expected visible text is “Retro SD Disk Image Manager” and the selected browse mode “Whole disk images (/retro/images/)”. If the device still shows the previous page, first verify the exact binary and programmed app partition rather than repeating source edits.
- Guest filesystems, guest-file operations, the unified Source Path / Copy From / Destination Path / Copy To interface and hardware acceptance remain unimplemented/unverified.

## Disk-image manager SPA — 2026-10-21

- Replaced the one-page legacy file listing as the default view with a client-side Single Page Application in `main/web.html`. The “Functions / Selection” screen lists the five requested transfer directions and related combinations; implemented whole-image and legacy exchange views are navigable without document reload. Guest-file functions are explicitly marked “Not implemented” because no guest filesystem adapter exists yet.
- Added a persistent `[Help]` popup whose content changes by function, plus a Help control in the SD-directory picker. Implemented flows: Computer → SD whole-image multi-file upload with overwrite confirmation; SD → Computer whole-image download; SD → SD byte-verified whole-image copy; SD image browsing with directory navigation, download, copy, rename/move, delete confirmation and backup recovery; and the existing legacy exchange browser.
- Removed `drives.cfg` mapping as a server-side lock for whole-image upload/replacement, delete, SD-to-SD copy, rename/move and backup recovery. Mapped status remains visible; the UI warns that rename/delete/replacement can affect emulator drive references. `drives.cfg` (now stored in LittleFS) remains protected from these generic file operations.
- Browser smoke test: opened `main/web.html` in the integrated browser. Confirmed Functions / Selection rendered, Computer → SD upload view rendered without page navigation, SD → SD displayed Source Path / Copy From / Destination Path / Copy To controls, and both home and SD → SD Help popups rendered. Because this was opened from `file://`, attempted SD listing API calls returned 403 (“File does not reside within a trusted folder”); no real SD/API transfer was tested.
- `source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh" && idf.py build && idf.py size`: PASS; final `build/retroHost.bin` is 0x106C20 bytes, within the 0x300000-byte app partition. `strings build/retroHost.bin` confirms SPA and RO/RW policy text are embedded. `git diff --check`: PASS.
- Device flashing and firmware-served browser/API acceptance were not performed. Guest filesystem adapters and guest-file transfer functions remain unimplemented; do not treat the SPA navigation shell as completion of the full manager.

## Computer upload picker and download save picker — Phase E update

- Updated the Computer → SD whole-image screen with a Computer folder picker. Secure browsers supporting the File System Access API enumerate supported image files directly in the selected folder; other connections use a native directory/file-picker fallback. Individual-file selection remains available.
- The Computer → SD source-folder handle remains stored in browser IndexedDB per machine profile where supported; this is browser/origin scoped and does not expose or store an absolute host path in firmware LittleFS.
- Removed the separate SD → Computer “Computer destination directory” control. On browsers supporting `showSaveFilePicker()` over a secure origin, the native save picker is opened at the moment the user starts the download. On ordinary HTTP to ESP32 or unsupported browsers, a standard attachment download is triggered; users who want a picker in that fallback must enable the browser's “Ask where to save each file” setting.
- Browser validation: `main/web.html` opened in the integrated browser. The HTTP/unsupported-API fallback rendered with the upload directory picker enabled, no download destination-directory fields, and no JavaScript page errors. Selecting the repository's `assets/cpm/cpm80DiskImages/` folder through the directory-input test selected three supported images and ignored nested/non-image files. A mocked secure-origin `showSaveFilePicker()` test verified the suggested filename and streamed all three response bytes to the mocked file writer; a fallback test verified the normal attachment link is requested with the selected filename. The download Help popup describes the picker/fallback. `file://` SD listing returned the existing expected 403 and did not validate device/API transfers.
- ESP-IDF 6.0.2 `idf.py build && idf.py size`: PASS; final `build/retroHost.bin` is 0x109970 bytes, within the 0x300000-byte app partition (0x1F6690 bytes free).
- `ctest --test-dir /tmp/esp32s3-retro-build-host --output-on-failure -R '^hostCore$'`: PASS, 1/1.
- Focused Python image-reference tests: `test_diskImageCpm80.py` 8/8 PASS; `test_extractCpm.py` 4/4 PASS; `test_diskImageApple2Prodos.py` 13/13 PASS; `test_diskImageApple2Pascal.py` 6/6 PASS; `test_extractUcsd.py` 5/5 PASS. `test_extractApple2Files.py` had 12/13 pass; its pre-existing `test_main_menu_shows_paths_and_disks_as_empty` expects the old “full path” menu string, while current output adds the wildcard syntax. This test was not changed because the task did not modify that offline CLI.
- These Python tests validate existing host-side reference tools, not a firmware guest-image adapter. They do not complete Phase B or Phase D.
- `git diff --check`: PASS.
- No firmware was flashed. Native save-picker use, actual streamed downloads, ESP32 HTTP fallback and hardware transfer integrity remain unverified. Phase E remains partial; Phases B/D and the remaining transfer matrix are still open.

## Flat image layout / exchange removed from manager

- `main/web.html`: subdirectory fields, Browse buttons, directory picker and the legacy exchange screen removed; manager uses an OS selector. `main/fileTransfer.c`: exchange area removed; image paths limited to `<OS>/<file>`.
- Emulator-side HOST exchange and SD layout requirements intentionally unchanged (see design doc revision 5).

## Guest file access, CP/M-80/86 slice — 2026-10-22

Executed (host and build only):

- Host tests (`cmake -S tests -B /tmp/hostbuild -DCMAKE_C_FLAGS=-Wno-error=string-concatenation`, ASan/UBSan): `ctest -R guestFs` PASS 3/3 (`guestFsFixtures`, `guestFsCpm`, `guestFsPythonVerify`). Coverage: all six CP/M profiles (cpm80 SYSTEM/LARGE/BIG, cpm86 CPM86/LARGE/BIG) built by the Python tools and compared byte for byte with the C output (add of the same files); Python re-reads every image written by C and compares file lists/contents/free counts; the real sdcard CP/M images (cpm80 languages/Utility/work1/work2, cpm86 TurboP/Utility/work86) read identically to the Python extraction; user areas, rename, overwrite, delete, full disk, directory full, duplicate names, bad names, empty files; nine corrupt-image mutations plus wrong image size (fail closed, image untouched); garbage/unsealed/sealed journals; crash-recovery sweeps (I/O-budget and fault-hook stages for overwrite/delete/rename/add on cpm80-SYSTEM, cpm86-LARGE, cpm80-BIG): every crash point yields exactly the old or the new state and the recovered image stays writable; streaming copy/export/import; the `imageFile` binding.
- `idf.py build` + `idf.py size` (ESP-IDF 6.0.2): PASS; `retroHost.bin` 0x111D50 bytes, 64% of the 0x300000 app partition free; no new warnings under `-Wall -Wextra -Werror`.
- `main/web.html` script parsed without syntax errors (JavaScriptCore via `osascript`; `node` is not installed). It was not run in a browser.
- Remaining ctest failures are pre-existing and unrelated: 8 Apple/UCSD host tests abort on missing ROM/image fixtures; 2 Python tests (`test_build_cpm80_matches_checked_in_image`, `test_main_menu_shows_paths_and_disks_as_empty`).

NOT verified (needs the user's device/browser): the `/api/guest/*` handlers (they are thin and cannot be host-tested), the SPA screens, large uploads/downloads over Wi-Fi, SD-file export/import commit path, journal recovery after a real power cut on the SD card, behaviour of emulators on images edited by the manager, and OSes other than CP/M-80/86 (HTTP 501 by design). Nothing was flashed or uploaded by the coding agent.

Suggested hardware checks: list each real CP/M image from function 11; download a file from `cpm80/languages.dsk` (BIG) and compare with `tools/extractCpm.py` output; upload a multi-block file to `work1.dsk` and boot CP/M to `DIR`/run it; overwrite and delete it; copy between `work1.dsk` and `cpm86/work86.dsk`; export a guest file to `/retro/images/cpm80/` and import it back; pull power during an upload and check that the next File Transfer start leaves the image in the old or new state.

## Guest file access, ProDOS / UCSD Pascal / DOS 3.3 adapters — 2026-10-22

- `idf.py build` PASS (`retroHost.bin` 0x117110 bytes, 64% of the 3 MiB app partition free). `idf.py size` run. Nothing was flashed or uploaded by the coding agent.
- Host tests (`cmake -S tests -B /tmp/gfsBase -DCMAKE_C_FLAGS=-Wno-error=string-concatenation`, ASan/UBSan): `ctest -R guestFs` PASS 13/13: the CP/M triple, `guestFsProdos{Fixtures,,PythonVerify}`, `guestFsPascal{...}`, `guestFsDos33{...}` and `guestFsAppleOrder`. The full ctest run still has the same 8 aborting Apple/UCSD emulator tests that fail on missing ROM/image fixtures (apple2SystemRom, apple2Disk, apple2DiskStream, apple2MultiDisk, ucsdPascalBoot, apple2ProdosAutostart, apple2ProdosAutostart80Columns, apple2ProdosSmartPort); they are unrelated to this work.
- Coverage per adapter: ProDOS — 140K/640K/800K `.po` and 140K DOS-order images, seedling/sapling/tree/sparse files, byte-identical comparison with `diskImageApple2Prodos.py add_file`, Python re-read of every C-written image, full disk, 51-entry directory full, duplicate/bad names, empty file, corrupt images (bad header, chain loop, out-of-range, cross-linked, claimed-but-free), copy between volumes, crash-recovery sweeps (I/O budget and fault stages) for add/overwrite/delete/rename, and the real images `data1.po`, `data2.po` and `system.dsk`. UCSD Pascal — 140K/640K/800K images in both sector orders, C output compared byte for byte with `extractApple2Files.PascalVolume` (Python date patched to 0), fragmentation, directory full, corrupt directories, crash sweeps, and the real images under `sdcard/retro/images/ucsd` and `littlefs/ucsd`. DOS 3.3 — four fixture variants (`.do`, `.dsk`, `.po`, track-0-free) written by an independent Python formatter/writer and compared byte for byte, T/I/A/B/R/S types, files above 122 sectors, sparse files, corrupt VTOC/catalog/T-S lists, full disk, catalog full, crash sweeps, and the real `test1.dsk`.
- `guestFsAppleOrder`: the sector order chosen by the guestFs Apple layer matches `apple2DiskImage` (the emulator) for all nine real Apple/UCSD images in the tree.
- The SPA script was syntax-checked with JavaScriptCore only; its screens were not rendered.

NOT verified (needs the user's device/browser): `/api/guest/*` with Apple images, the changed SPA screens (type column, hidden user area, type/aux fields, name previews), real-device behaviour of the emulators on images edited by the manager (Apple II and UCSD Pascal boot and read edited images), power loss during an Apple image edit, large 640K/800K transfers over Wi-Fi, and DOS 3.3 on real disks other than `test1.dsk`. Hash comparisons were deliberately not recorded.

Suggested hardware checks: list `apple2/data1.po`, `apple2/test1.dsk` and `ucsd/pascalWork1.po` from function 11; upload a `.TXT` and a `.BIN` to a ProDOS image and read them from ProDOS (`CAT`, `TYPE`); upload a `.TEXT` file to `ucsd/pascalWork1.po` and open it from the Pascal filer; copy a text file from ProDOS to Pascal and back; upload a file to `test1.dsk` and `CATALOG`/`LOAD` it in DOS 3.3; rename and delete in each; pull power during an upload and check that the next File Transfer start leaves the image in the old or new state.

## drives.cfg transfer (2026-10-22)

- Firmware build with the new `<OS>/drives.cfg` upload exception and the drives.cfg entry in the SPA function 1/3 selectors: `idf.py build` PASS. Not exercised: upload/download of drives.cfg on a device or in a browser, the 413/422 rejection paths, replacement with a failed transfer.
