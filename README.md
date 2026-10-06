# ESP32-S3 Retro Computer

An ESP32-S3-based retro-computer platform for running several different retro-computer systems on one device. Choose a machine from the shared menu and use its guest operating system through the USB console. The project is built with native ESP-IDF, not Arduino or PlatformIO.

The goal is to bring together the character of several classic systems in a compact, modern, self-contained computer. Each system has its own guest CPU, operating system, memory model and media design, while sharing one host for boot, storage, the menu and file transfer.

## Systems and current status

| System | Status |
| --- | --- |
| CP/M-80 | Integrated. Guest boot and command behavior have host-test coverage. The CP/M-80 design log records a user report of running it on an ESP32-S3; repeatable hardware and SD-drive acceptance is still outstanding. |
| CP/M-86 | Integrated. Guest boot has host-test coverage and the ESP-IDF firmware build succeeds. It has not been flashed; hardware boot, performance and broader compatibility remain unverified. |
| UCSD Pascal | Planned; currently a menu placeholder. |
| Apple II | Planned; currently a menu placeholder. |
| SWTPC 6800 | Planned; currently a menu placeholder. |

The current menu can start CP/M-80 and CP/M-86 when their required LittleFS resources validate. A host test or successful firmware build does not, by itself, prove operation on the physical board. See the machine-specific design records and [verification log](docs/verification.md) for exact evidence and remaining work.

## Shared host features

- USB Serial/JTAG console and a reset-to-menu startup flow.
- A machine registry that checks implementation and required boot resources.
- LittleFS for compact system resources and microSD storage for larger or writable images.
- An SD-card layout rooted at `/retro/`, with FAT32 validation.
- Optional browser-based file transfer for `/retro/exchange/` and `/retro/images/`, organized by machine.

File Transfer starts only when selected from the menu and a valid Retro SD card is available. It uses plain, unauthenticated HTTP; use it only on a trusted local network. Browser file access is confined to the documented SD image and exchange directories.

## Hardware

The configured target is the **LOLIN S3 Pro** with an ESP32-S3-WROOM-1, 16 MB flash and 8 MB octal PSRAM. CP/M-86's current 640 KiB guest-memory profile requires external PSRAM.

The project uses the native USB Serial/JTAG connector and SPI2 for the microSD interface. The current reference wiring is:

| Signal | GPIO |
| --- | ---: |
| MOSI | 11 |
| MISO | 13 |
| SCK | 12 |
| CS | 10 |
| USB D− / D+ | 19 / 20 |

Check the exact board schematic, module circuitry, pin availability, flash and PSRAM settings before connecting hardware or changing configuration. A 16 or 32 GB microSDHC card formatted as FAT32 is recommended; exFAT is not required.

## Build

Use the Espressif VS Code extension or an ESP-IDF terminal configured for **ESP-IDF 6.0.2** and the ESP32-S3 target. The project rejects other ESP-IDF versions and targets.

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py size
```

To flash and monitor a connected board, select the correct native USB serial port:

```sh
idf.py -p YOUR_USB_PORT flash monitor
```

Flashing is not required to run the host tests. These instructions do not claim hardware acceptance; see [Verification](#verification).

## Prepare storage and disk images

Format an SD card as FAT32 with your operating system, then prepare its layout:

```sh
python3 tools/prepareSd.py /path/to/mounted/card
```

This tool prepares the Retro directory layout; it does not format the card or create guest disk images. To explore the OS-specific disk-image tools and options:

```sh
python3 tools/buildDiskImage.py --help
python3 tools/buildDiskImage.py --os cpm80 --help
```

For complete instructions, image profiles and resource-rights guidance, see [Host tools](tools/README.md). Keep original user data backed up and safely eject the card before removing it.

## Project map

- [`projectPrompt.md`](projectPrompt.md) — shared host architecture, behavior and implementation rules.
- [`designCPM80.md`](designCPM80.md), [`designCPM86.md`](designCPM86.md) — implemented guest-system designs, constraints and verification records.
- [`designUCSD.md`](designUCSD.md), [`designAppleII.md`](designAppleII.md), [`designSWTPC.md`](designSWTPC.md) — designs for the remaining systems.
- [`main/`](main/) — firmware startup, machine registry, storage, console and file-transfer host.
- [`components/`](components/) — guest CPU/system cores and shared host components.
- [`littlefs/`](littlefs/) — firmware-packaged boot resources.
- [`sdcard/`](sdcard/) — sample Retro SD-card directory layout.
- [`tools/`](tools/) — disk-image, SD preparation and transfer utilities.

## Verification

Portable C tests build separately from the ESP-IDF firmware:

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The test suite includes host-side guest boot coverage. Hardware checks still require a correctly configured ESP32-S3 board, a prepared FAT32 card and a physical run. The outstanding hardware acceptance work is recorded in [`docs/verification.md`](docs/verification.md).
