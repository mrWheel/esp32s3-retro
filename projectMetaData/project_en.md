# ESP32-S3 Retro Computer

The ESP32-S3 Retro Computer is a multi-system retro-computer emulator built around the Espressif ESP32-S3 microcontroller. Instead of recreating a single classic machine, it hosts several distinct retro computing worlds on one small board: CP/M-80, CP/M-86, UCSD Pascal, the Apple II and, as a future addition, the SWTPC 6800. After power-up the device shows a main menu on its USB console; the user selects a system and the matching emulator boots its own guest CPU, firmware and disk drives while sharing one common set of host services.

The reference board is a LOLIN S3 Pro (ESP32-S3-WROOM-1) with 16 MB of flash and 8 MB of octal PSRAM. The firmware is written in native ESP-IDF. The internal flash holds a LittleFS partition with the minimum boot resources for every system, so the esp32s3-Retro also works without a memory card. All bulk storage lives on a microSDHC card formatted as FAT32, organised under a /retro/ root with per-system image and backup folders; physical floppy drives are replaced by disk-image files.

Interaction is deliberately text-based. Any ANSI/VT100-capable USB terminal with an 80-column by 24-line window acts as the keyboard and screen, which makes the device ideal for serious, text-oriented software rather than graphics or games.

File Transfer is a browser-based Disk Image Manager served directly by the ESP32-S3 itself. After joining the Wi-Fi network the device shows its IP address in the terminal. The build-in webserver/GUI offers upload, download, copy, rename and delete of loose files and complete disk images — including managing the files inside supported disk images — without installing anything on your computer. Guest programs can use a monotonic millisecond timer for timing and benchmarks.

It is a hands-on way to relive the early days of personal computing and to experience programming the retro way.
