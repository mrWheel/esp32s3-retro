# ESP32-S3 Retro Computer — HOST-M1

Native ESP-IDF **6.0.2** host project. All five machines are placeholders. No CPU, ROM, guest OS, disk geometry or guest filesystem has been implemented or bundled.

`projectPrompt.md` contains the complete specification supplied in this conversation, with Markdown formatting. It is authoritative. The exact dependency constraint `==0.4.0` deliberately tightens the specification's caret example so the mandatory provisioner cannot upgrade silently.

## Hardware choices to check before flashing

The exact board and SD wiring were not supplied. This project uses an **explicit example configuration**, not a claim about your board:

- ESP32-S3 with at least **8 MB flash**. No PSRAM dependency.
- Native USB Serial/JTAG connector (USB D− GPIO19, D+ GPIO20); do not use those pins for SD.
- 3.3 V compatible microSD socket/module using **SPI2**: MOSI GPIO11, MISO GPIO13, CLK GPIO12, CS GPIO10, common ground. Verify your board schematic, pin availability and the SD module's required pull-ups/power circuitry.
- 16/32 GB microSDHC, FAT32. Do not hot-remove the card during I/O. No card-detect GPIO is assumed; detection means an actual mount/probe. A missing card and electrical failure cannot be distinguished without additional hardware.

Set the real pins in `idf.py menuconfig` → **Retro Host hardware**. Change flash size and the partition table together if your board differs. The table reserves 3 MiB for the app, 2 MiB for LittleFS, 24 KiB for NVS and 4 KiB for PHY. App headroom is provisional until the first full build size report; no OTA partition is reserved.

## VSCode setup and build

1. Extract the whole ZIP and open `esp32s3-retro` as the VSCode folder.
2. Install/configure the Espressif ESP-IDF extension with **ESP-IDF 6.0.2** and ESP32-S3 tools. No PlatformIO or Arduino.
3. Open an **ESP-IDF terminal** in VSCode. The generic tasks assume that environment is active; no personal tool paths are hardcoded.
4. Run:

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py size
idf.py -p YOUR_USB_PORT flash monitor
```

Replace `YOUR_USB_PORT` with the native USB serial port (for example a discovered `/dev/cu.usbmodem...`, `/dev/ttyACM...` or `COM...`). Exit IDF Monitor with Ctrl+]. Select serial flashing in the extension. Normal `flash` includes the generated LittleFS image; `app-flash` alone does not initialize LittleFS.

The first build needs internet for tools/dependency resolution and **littlefs-python==0.15.0**, installed by the unchanged LittleFS component into a build-local virtual environment. `managed_components/` contains the exact downloaded runtime component sources and their licenses; `dependencies.lock` records their registry hashes. Do not edit managed sources. They can be fetched again from the manifest/lock if necessary.

After adding legitimate resources below `littlefs/<machine>/`, run `idf.py build flash` again. Flashing `bootfs` replaces that filesystem with the packaged directory contents. Never put credentials or user data there.

## Prepare the SD card

Format an SDHC card as FAT32 using your operating system; this project never formats a card. Back up existing card data yourself before formatting. Then either copy the supplied `sdcard/retro` folder to the root of the card **including empty directories**, or use Python 3.9+:

```sh
python tools/prepareSd.py /path/to/mounted/card
```

On Windows use a drive root such as `E:\`. The tool is additive, does not create disk images, does not format and refuses a conflicting marker. It cannot confirm the host filesystem type; firmware does.

The exact marker is:

```text
ESP32-S3-RETRO
layout=1
```

CR, LF and CRLF line endings are accepted. Missing marker, wrong version, non-FAT32 and missing required directories disable File Transfer. Required directories are the five `images/<machine>` and `exchange/<machine>` folders plus `exchange/common` and `backup`. The menu works without SD. Selecting 6 reprobes the card, so insertion/replacement while idle is detected. Reboot to recover from an unmount failure.

## Menu and File Transfer

RESET always enters the main menu. Type **one digit then ENTER**. CRLF is treated as one ENTER, backspace is supported, overflow and invalid input are rejected. Choices 1–5 report `Not implemented yet.` and wait for ENTER. They never enter emulator code.

Choice **6** checks FAT32/layout before starting WiFi. The **only** connection/provisioning implementation is `michmich/esp-idf-wifi-provisioner` **0.4.0**. Configure its defaults under **Component config → WiFi Provisioner**. By default its setup AP is open; on a trusted network join **Retro-Setup** and open `http://192.168.4.1/` if the captive page does not appear. You may configure an AP password in menuconfig; the host does not print it. Credentials belong to the provisioner's NVS storage.

After station connection, the console prints `http://DEVICE_IP/`. The File Transfer server uses standard HTTP port 80 and has no password, key, or login step. Anyone on the connected WiFi network can browse, upload, download, and delete files under `/retro/exchange/` while File Transfer is active. Use it only on a trusted network.

The GUI lists names/sizes, opens directories, uploads one file, downloads directly to the browser and confirms deletion. Only `/retro/exchange/` is reachable. There is no image access, directory deletion, mkdir or rename API. Create extra exchange subdirectories on the PC if desired.

Names: ASCII letter/digit first, then letters/digits/spaces/dots/underscores/hyphens, up to 64 characters per component and 255 per relative path; no trailing dot/space. Unsupported names already on the card are omitted from the GUI. Names are never silently converted for guests. Hidden `.upload-part` is reserved for a single upload and cannot be requested via the API.

Uploads/downloads use 4096-byte firmware buffers. Uploads use raw binary HTTP PUT, not multipart conversion. Existing files are rejected; delete explicitly first. A completed upload is synced and closed before same-volume rename. Cancellation removes its temporary file; a power-loss leftover is removed when the next File Transfer session starts. FAT32 cannot promise crash-atomic metadata under sudden power loss. Keep power stable and back up important files.

The initial supported per-file upload/image limit is **2 GiB minus one byte**, matching conservative signed offset handling in this build. This supports large images without claiming guest compatibility. No 140/160/360 KB limit exists. Download/list use bounded firmware memory; the GUI holds only directory metadata.

Press **ENTER** to stop the application server. The stop flag interrupts active transfers, files are closed, temporary uploads are discarded, and the menu resumes after the HTTP task stops. Socket receive/send timeout is 2 seconds; SD errors/timeouts can add delay. The HTTP task serializes file operations, so there are no concurrent uploads or image mutations.

**WiFi remains running after the first selection of 6.** This is explicitly allowed by the specification. The provisioner is started once per boot in a worker task, so its connection wait cannot block the USB menu. If you leave before provisioning finishes, its setup portal may remain available, but the application file server is stopped. The application server uses the ESP-IDF HTTP server defaults (port 80 and control port 32768). The provisioner's setup server also uses port 80, but it is stopped after provisioning completes before the application server starts.

On connection loss the application file server stops. Version 0.4.0 does not provide a reliable ongoing reconnect/cancel lifecycle: reset to retry if the connection does not recover. This project deliberately does not implement a competing WiFi connection manager or repeatedly tear down/start the provisioner. See `docs/thirdParty.md` for the source-review details. Physical RESET is also the way to stop all WiFi activity.

This is unauthenticated plain HTTP for a **trusted local network**, not an internet-facing service; file contents are not transport-encrypted. The mandatory upstream provisioning portal has its own security behavior and is separate from the File Transfer server.

## Code map and extension boundary

- `main/main.c`: minimum startup, LittleFS inspection, SD validation, menu.
- `main/hostConsole.*`: common USB Serial/JTAG abstraction. No TinyUSB.
- `main/systemMenu.*`: line-confirmed state machine and File Transfer lifecycle.
- `main/machineRegistry.*`: five compiled registry entries, state/probe/init/run hooks, required-resource descriptions. Empty resource lists mean unknown design requirements, not fabricated boot files.
- `main/storage.*`: SDSPI, strict FAT32/layout checks, read-only LittleFS preparation (no auto-format).
- `main/imageFile.*`: generic trusted-host file open/size/bounded read/write/flush/close, no guest semantics. Zero-initialize `imageFile`, close before reuse, use one owner. Read-only writes fail; ranges outside existing files fail. Paths must come from trusted machine definitions; never pass raw HTTP input to this API.
- `components/hostCore`: pure C path validation/resolution, marker parser and ENTER parser. `pathResolve` is the common exchange path boundary for future guest adapters as well as HTTP.
- `main/network.*`: public provisioner API wrapper, event observation only; no duplicated credentials or custom WiFi connection code.
- `main/fileTransfer.*`, `main/web.html`: unauthenticated exchange-only HTTP and GUI.
- `littlefs/`: resource directories with plain README files only.
- `design*.md`: explicitly marked missing-design placeholders. Obtain the complete authoritative design before implementing that machine.

The only project snake_case entry point is ESP-IDF's required `app_main`; ESP-IDF types, fields, symbols, linker names and configuration keys keep their mandated upstream spelling. All project-owned identifiers use lowerCamelCase. Source formatting is Allman/2 spaces; upstream sources remain untouched. Project code comments, where present, must use standalone `//—` lines; tooling/configuration syntax follows its language requirements.

## Verification

See `docs/verification.md` for actual results and pending hardware acceptance. A complete build is **not claimed**. Build completion was deferred to VSCode at the user's request.

Portable tests (optional, require a host C compiler and CMake):

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

On hardware, while File Transfer is active:

```sh
python tools/transferTest.py 'http://DEVICE_IP/'
```

This test creates unique files in `exchange/common`, verifies binary SHA-256 round trips for zero-byte, space-containing and 8 MiB files, verifies unauthenticated access, rejects traversal and overwrite attempts, interrupts an upload, and deletes only its own test files. It is an acceptance test supplied for later execution, not a claimed result.
