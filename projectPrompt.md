# ESP32-S3 Retro Computer — Project Prompt

## 1. Authority and scope

This is the authoritative project-wide specification for the ESP32-S3 Retro Computer host. It defines coding rules, ESP-IDF host architecture, USB console, boot/menu system, LittleFS/SD storage, machine discovery, File Transfer, WiFi, common testing, and the boundary between host and emulator code.

It does NOT define emulator internals. Read this file plus the complete machine-specific design before implementing a machine:

- CP/M-80     -> designCPM80.md
- CP/M-86     -> designCPM86.md
- UCSD Pascal  -> designUCSD.md
- Apple II     -> designApple2.md
- SWTPC 6800   -> designSWTPC.md


A separate designFileTransfer.md may later hold detailed protocol evolution, but until then this document is authoritative for File Transfer.

## 2. Project goal and target

### Goal

Build one ESP32-S3-based retro computer that can host several distinct retro-computer systems. The user selects a machine from a shared menu; each machine runs its own guest CPU/OS implementation and uses the common host services defined here. The intended systems are CP/M-80, CP/M-86, UCSD Pascal, Apple II and SWTPC 6800. This is a multi-system project, not a single CP/M appliance.

Keep the host services and machine-specific emulation separate so additional systems can be integrated without duplicating USB, storage, WiFi or file-transfer infrastructure. Each machine's design document defines its own CPU, guest firmware/OS, media and compatibility requirements.

### Target hardware and software

- ESP32-S3; the current configured board is the LOLIN S3 Pro with ESP32-S3-WROOM-1, 16 MB flash and 8 MB octal PSRAM. Verify the actual board and configuration before hardware acceptance.
- ESP-IDF 6.0.2, native ESP-IDF only; no Arduino framework or PlatformIO.
- USB Serial/JTAG console.
- LOLIN S3 Pro WS2812B RGB LED on GPIO 38, used as guest disk-I/O activity feedback.
- Internal flash with LittleFS for minimum boot resources.
- microSDHC formatted as FAT32; 32 GB recommended and 16 GB acceptable. Do not require exFAT.
- CP/M-86's current 640 KiB guest-memory profile requires external PSRAM on the configured board.

### Current implementation snapshot

This snapshot describes repository status, not a change to the machine-specific acceptance gates. Consult each design document and `docs/verification.md` for detailed evidence and outstanding work.

| System | Current status |
| --- | --- |
| CP/M-80 | Firmware machine integration and host-side guest tests are present. The CP/M-80 design log records a user report of booting and running commands on an ESP32-S3; independent hardware, SD-drive and broader compatibility acceptance remains outstanding. |
| CP/M-86 | Firmware machine integration, genuine guest boot tests on the host and an ESP-IDF build are present. The firmware has not been flashed; hardware boot, performance and broad compatibility remain unverified. |
| UCSD Pascal | Placeholder; no guest machine implementation yet. |
| Apple II | Phase 1 motherboard implementation uses the 12 KiB Apple ][ system ROM in `assets/apple.rom`; host tests boot Integer BASIC and execute `PRINT 2+2`. Hardware acceptance and ROM redistribution rights remain unverified. |
| SWTPC 6800 | Placeholder; no guest machine implementation yet. |

In the current menu, CP/M-80 and CP/M-86 are available when their required LittleFS resources validate; Apple II uses the 12 KiB system ROM copied from `assets/apple.rom`; UCSD Pascal and SWTPC 6800 remain placeholders. The Apple II host test reaches the ROM's Integer BASIC prompt and executes `PRINT 2+2`; ROM redistribution rights and ESP32-S3 hardware behavior are not yet verified. A successful host test or firmware build is not evidence of successful execution on the ESP32-S3.

Mind you: idf.py command is in
```
source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh"
```

## 3. Coding rules

- Use native ESP-IDF only. No Arduino framework, compatibility layer, Arduino libraries, or Arduino WiFi code.
- All repository code, identifiers, comments, logs, menu text, web text, and documentation are English.
- Project code uses Allman style, lowerCamelCase, 2-space indentation, no tabs.
- Comments are always on their own line above the code and start exactly:
```
//— Comment text
```
- Never place comments after code. Preserve upstream formatting in untouched third-party code.

Do not invent undocumented hardware, CPU, ROM, filesystem, disk-geometry, or historical behaviour. Consult the relevant design document and primary/upstream references. Record uncertainty rather than guessing.

Do not change multiple unrelated subsystems while debugging one problem.

## 4. Responsibility boundary

The common host owns:

- USB console
- main menu
- machine registry and probing
- LittleFS
- SD/FatFS
- SD layout validation
- generic virtual-image file access
- shared guest disk-activity RGB indicator
- exchange filesystem
- WiFi lifecycle
- File Transfer HTTP server
- common timing/status/logging

Machine design files own:

- CPU core and integration
- guest memory map
- ROM/BIOS
- guest boot process
- guest disk geometry and size limits
- guest filesystem
- terminal details
- guest-side transfer utility
- historical compatibility
- machine-specific tests and issue log

Emulators must not take ownership of common USB, LittleFS, SD, WiFi, or File Transfer initialization without an explicit documented reason.

## 5. Boot sequence

Every physical RESET executes:

```text
app_main()
  |
  +-- initialize minimum host services
  +-- initialize USB Serial/JTAG
  +-- mount LittleFS
  +-- inspect LittleFS resources
  +-- detect SD card
  +-- mount FAT32 if present
  +-- validate Retro SD layout
  +-- probe machine availability
  +-- build menu state
  +-- display main menu
```

Do NOT start WiFi during normal boot. WiFi starts when File Transfer is selected.

Physical RESET always returns to the main menu. Do not auto-boot the previously selected machine.

## 6. USB console

Prefer ESP32-S3 USB Serial/JTAG, including CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y where appropriate. Do not add TinyUSB solely for serial if Serial/JTAG is sufficient.

Provide a common abstraction conceptually equivalent to:

```c
int hostConsoleGetChar(void);
bool hostConsoleCharAvailable(void);
void hostConsolePutChar(char value);
void hostConsoleWrite(const char *text);
```

The USB connection may serve flashing/monitor, menu, and emulator terminal I/O.

## 7. Main menu

Initial menu:

```text
ESP32-S3 Retro Computer
=======================

1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800
6. File Transfer

Select system [1-6]: _
```

ENTER is mandatory

A choice is accepted only after ENTER.

```text
1<ENTER>     valid
1           incomplete; keep waiting
```

Support CR, LF and CR+LF without executing twice. Reject empty, non-numeric, out-of-range, and disabled selections cleanly.

Unavailable machines remain visible where useful:

```text
4. Apple II       [not installed]
```

Selecting one prints a useful reason and waits for ENTER before returning to the menu.

## 8. Machine registry

Do not scatter machine-specific selection logic through main.c. Use a registry/table, conceptually:

```c
typedef struct
{
  const char *name;
  bool (*probe)(void);
  esp_err_t (*init)(void);
  void (*run)(void);
} retroMachine_t;
```

The exact API may evolve. It must support display name, availability/probe, initialization and execution.

Availability can depend on compiled implementation, required LittleFS resources, required SD resources, and basic resource validation. A probe is fast and never boots the machine.

Useful internal states include:

- AVAILABLE
- NOT_IMPLEMENTED
- MISSING_RESOURCE
- SD_REQUIRED
- RESOURCE_INVALID

## 9. Initial emulator placeholders

The first host milestone contains no CPU emulator implementation.

For example, 1<ENTER> prints:

```text
CP/M-80
========

Not implemented yet.

Press ENTER to return to the main menu.
```

Equivalent placeholders exist for UCSD, Apple II, MP/M and SWTPC.

Once an emulator is implemented, its design file defines runtime/exit behaviour; current designs normally use physical RESET.

## 10. LittleFS

LittleFS contains minimum resources that should remain available without SD:
```
/littlefs/cpm80/
/littlefs/cpm86/
/littlefs/ucsd/
/littlefs/apple2/
/littlefs/swtpc/
```
Actual boot/system filenames are defined by each machine design. The host mounts LittleFS, reports failures, supplies generic file/resource helpers, and permits probes to verify resources.

Prepare the directory/build mechanism during the first milestone, but never fabricate copyrighted ROM/OS binaries.

## 11. SD card

Recommended card:

- microSDHC
- 32 GB
- FAT32

At boot:

1. detect card;
2. mount FAT32;
3. validate expected layout/version;
4. retain status;
5. use status to enable/disable functions and machines.

The main menu must work without SD.

Missing-card example:

```text
WARNING: No SD card detected.
Emulators requiring SD resources may be unavailable.
File Transfer is unavailable.
```

Invalid-layout errors must state what is wrong rather than merely saying “SD error.”

## 12. SD layout

Use a dedicated root:
```
/retro/
```
Layout version 1:

```text
/retro/
  layout.txt
  images/
    cpm80/
    cpm86/
    ucsd/
    apple2/
    swtpc/
  exchange/
    common/
    cpm80/    
    cpm80/
    ucsd/
    apple2/
    swtpc/
  backup/
```

/retro/layout.txt contains:

```text
ESP32-S3-RETRO
layout=1
```

Accept normal CR/LF variations. An arbitrary FAT32 card without the marker is not a valid Retro card.

The host may later offer an explicit format/prepare function, but must never silently create fake emulator images.

## 13. Large virtual disk images

Historical floppy images remain supported, but the host must also support large image files on SD where the guest permits them. Never impose a 140/160/360 KB host limit.

The generic image layer supports open, close, size, bounded read-at-offset, write-at-offset, flush and read-only mode. Stream I/O; do not load an entire image into RAM.

The shared host disk-activity indicator uses the LOLIN S3 Pro WS2812B RGB LED on GPIO 38. The current CP/M-80 and CP/M-86 disk-record paths signal green for reads and red for writes; the LED turns off 120 ms after the most recent activity. This is guest disk-record activity feedback, not an indicator for every host filesystem access.

The machine design, not this file, determines guest sector size, geometry, block size, maximum capacity, filesystem limits and bootability.

## 14. Exchange filesystem

Common transfer storage is ordinary FAT32:
```
/retro/exchange/common/
/retro/exchange/cpm80/
/retro/exchange/cpm86/
/retro/exchange/ucsd/
/retro/exchange/apple2/
/retro/exchange/swtpc/
```
Architecture:

```text
Mac / PC / iPad
      |
      | browser
      v
File Transfer web GUI
      |
      v
/retro/exchange/<system>/
      |
      | guest-side transfer utility
      v
guest filesystem
```

The browser does not need to understand CP/M-80, CP/M-86, UCSD, ProDOS, etc.

The File Transfer GUI also manages files directly on the SD card. The user selects a machine and transfer type:

- Loose files use `/retro/exchange/<machine>/` and are visible to that machine's guest transfer utility. For CP/M-80, `HOST DIR` lists `/retro/exchange/cpm80/`.
- Disk images use `/retro/images/<machine>/`, for example `/retro/images/cpm80/`.

These are separate destinations on the same physical SD card; disk images are not staged through the exchange directory.

## 15. Guest transfer contract

Each emulator may have a small native guest utility. CP/M-80 could for example provide:

```text
HOST DIR
HOST GET HELLO.C
HOST PUT RESULT.TXT
```

Equivalent utilities/protocols are defined in each machine design.

The common host supplies an emulator-neutral exchange service. Prefer having the guest OS create/read its own guest files. For example, CP/M HOST.COM should receive/send bytes through a virtual host interface and use BDOS for its CP/M file operations. The host should not edit CP/M directory structures merely to transfer an individual file.

The common exchange operations are `QUERY`, `DIR`, `GET`, `PUT` and `ABORT`. QUERY negotiates a protocol version and capability bits before other operations. Transfers are streamed with a 32-bit exact byte length and CRC-32; implementations must not load a whole file into RAM. `GET` reports a final status after its data and checksum. `PUT` carries the expected checksum, writes to a temporary file, verifies length/checksum, and publishes the final file only on success. Existing targets are never overwritten implicitly. Names are validated by the machine adapter and may not escape that machine's exchange directory. Missing media, invalid names, existing targets, I/O/checksum failures and cancellation must be distinguishable to the guest. Each machine design defines how these operations are transported and how guest record-oriented file sizes preserve exact byte lengths.

## 16. File Transfer menu mode

Selecting:

6&lt;ENTER&gt;

performs:

```text
verify valid SD
  |
start WiFi connect/provision flow
  |
wait for usable connection
  |
display IP address
  |
start Retro File Transfer HTTP server
  |
serve browser GUI
```

Example:

```text
Retro File Transfer
===================

SD card: OK
WiFi: connected
IP address: 192.168.2.123

Open in a browser:
http://192.168.2.123/

Press ENTER to stop File Transfer and return to the main menu.
```

ENTER exits File Transfer. Stop the application HTTP server, safely finish/abort transfers, flush/close files, and return to the menu. WiFi should preferably also stop, but stopping WiFi is not a hard requirement.

Without a valid SD card, do not start WiFi:

```text
File Transfer is unavailable.
No valid Retro Computer SD card was found.
Press ENTER to return to the main menu.
```

## 17. Mandatory WiFi component

For WiFi connection/provisioning use ONLY:

- michmich/esp-idf-wifi-provisioner
- version 0.4.0

Pin the dependency:

- michmich/esp-idf-wifi-provisioner^0.4.0

Do not replace it with WiFiManager, custom captive-portal code, another provisioning component, Arduino provisioning, or duplicate credential storage.

Use its public version-0.4.0 lifecycle/API, including as appropriate:
```
wifi_prov_start(...)
wifi_prov_wait_for_connection(...)
wifi_prov_is_connected()
wifi_prov_get_ip_info(...)
wifi_prov_stop()
```
Expected behaviour:

stored credentials -> attempt STA

no credentials / failed connection -> SoftAP + captive portal

user configures network -> credentials stored -> STA connection

Do not silently upgrade the component. Any future version change is an explicit project decision and requires regression testing.

## 18. Provisioning server versus File Transfer server

The provisioner owns its captive provisioning HTTP/DNS services. Our File Transfer web server is a separate application service.

Correct lifecycle:

```text
wifi_prov_start()
  |
connect/provision
  |
network usable
  |
start Retro File Transfer HTTP server
```

Do not modify the provisioner to embed our file GUI. Verify actual provisioning-server shutdown/lifecycle and avoid HTTP port/resource conflicts rather than assuming them away.

## 19. File Transfer web GUI

The first GUI is deliberately simple and must provide:

- list current permitted directory;
- navigate subdirectories under both permitted SD roots;
- upload;
- download;
- delete with confirmation;
- filename and size;
- full `/microSD/retro/...` path next to each listed filename;
- useful success/error responses;
- basic byte/percentage progress for uploads and downloads;
- machine selection (`cpm80`, `cpm86`, `ucsd`, `apple2`, `swtpc`) and transfer type (loose file or disk image), selecting the matching exchange or image directory.

Optional later: mkdir, rename, multi-upload, checksums, free-space display, image backup/restore.

Do not delay the first working version for optional features.

### Scope and security

The browser file server exposes only `/retro/exchange/` and `/retro/images/` on the physical SD card. It must not access LittleFS or any other SD path. The GUI must let the user select a machine (`cpm80`, `cpm86`, `ucsd`, `apple2` or `swtpc`) and a transfer type. Loose files go under `/retro/exchange/<machine>/` so the selected guest can access them (CP/M `HOST DIR` reads `/retro/exchange/cpm80/`); disk images go under `/retro/images/<machine>/`. The GUI must show each item's full VFS path (for example `/microSD/retro/exchange/cpm80/FILE.TXT`) beside its name. File Transfer runs separately from emulators, so browser writes cannot modify an active guest disk image.

Normalize and validate every requested path. Reject ../, encoded traversal, absolute-path escape, and any resolved path outside the two permitted roots.

Never directly concatenate an untrusted URL path into a filesystem path.

For uploads, resolve the selected machine and transfer type to `/retro/exchange/<machine>/` or `/retro/images/<machine>/`. The default GUI selection is CP/M loose files, `/retro/exchange/cpm80/`. Show the full `/microSD/retro/...` destination in the GUI and report it after successful upload.

### Uploads

Stream uploads in bounded buffers. Prefer temporary-file -> verify close/write -> rename to final name. Avoid leaving a corrupt final file after interruption.

### Downloads

Stream downloads. Never allocate a complete-file-sized RAM buffer.

### Binary integrity

Transfers are byte-preserving. Never automatically change CR/LF, character sets, tabs, EOF markers, or high bits.

A binary upload/download round trip must preserve the exact bytes (byte-for-byte comparison).

The HTTP transfer size limit is 4 GiB minus 2 bytes. ESP-IDF's HTTP parser reserves the maximum 32-bit `Content-Length` value as a sentinel; reject that value rather than risk accepting an incomplete upload.

## 20. Filename rules

FAT32 exchange and image names may be more capable than guest filenames. The browser GUI does not silently rename files.

Guest-specific naming/type conversion belongs in its machine design/guest utility.

## 21. Concurrency and ownership

Initial architecture intentionally prevents dangerous concurrent modification:

  emulator mode      -> File Transfer web server not active
  File Transfer mode -> emulator not active

Never let the browser modify an active guest disk image.

Future live guest exchange access requires explicit ownership/locking rules.

## 22. Partition table and NVS

Use a custom flash partition table sized from measured requirements for application, NVS, LittleFS and required ESP-IDF partitions.

OTA is optional. Do not sacrifice large amounts of LittleFS merely for OTA unless OTA becomes an explicit requirement.

NVS may be used by the host and by the mandatory WiFi provisioner. Do not store the last emulator for automatic boot. Do not duplicate WiFi credential storage outside the provisioner.

## 23. Resource manifest

Avoid scattering filenames through menu code. Support a simple compiled resource description per machine containing, as needed:

- display name
- required LittleFS resources
- required SD resources
- optional SD resources
- probe function

Do not introduce JSON unless it provides a concrete benefit.

## 24. Logging and errors

Use ESP-IDF logging where practical. Avoid noisy release logs and never print WiFi passwords/credentials or uploaded file contents. Emulator sessions must not be corrupted by unrelated host logs.

Expected runtime problems are recoverable and should not use fatal ESP_ERROR_CHECK() indiscriminately:

- missing SD
- invalid SD layout
- missing emulator resource
- WiFi timeout/failure
- missing file
- upload/download failure
- out of space

Return to the main menu whenever safe.

Large I/O must not starve FreeRTOS/watchdogs. Do not busy-loop for console input, WiFi, network clients, or file I/O.

## 25. HOST-M1 — first coding milestone

Implement the complete common host WITHOUT implementing any emulator CPU.

HOST-M1 is done when:

1. ESP-IDF project builds for ESP32-S3.
2. USB Serial/JTAG console works.
3. LittleFS mounts and resource directories/build preparation exist.
4. SD detection and FAT32 mount work.
5. /retro/layout.txt is validated.
6. required SD directories are validated.
7. machine registry/probe framework exists.
8. menu works and every selection requires ENTER.
9. unavailable states can be displayed/handled.
10. at the HOST-M1 baseline, all five emulator choices show Not implemented yet and return on ENTER.
11. File Transfer checks SD before WiFi.
12. michmich/esp-idf-wifi-provisioner 0.4.0 is pinned/integrated.
13. stored credentials reconnect.
14. first-time/failed connection can use the component’s captive portal.
15. connected IP is displayed.
16. separate Retro File Transfer HTTP server starts.
17. /retro/exchange/ is browsable.
18. upload works.

19. download works.
20. delete works with confirmation.
21. traversal outside exchange is blocked.
22. files larger than RAM are streamed.
23. ENTER exits File Transfer.
24. active transfers are safely stopped and files flushed/closed.
25. application HTTP server stops.
26. WiFi may be stopped.
27. main menu works again without RESET.
28. no CPU emulator has been imported merely to complete HOST-M1.

HOST-M1 was the pre-emulator baseline. The current implementation supersedes item 10 for menu choices 1, 2 and 4: CP/M-80 and CP/M-86 are available when their required boot resources validate, and Apple II uses the 12 KiB system ROM copied from `assets/apple.rom`; UCSD Pascal and SWTPC 6800 remain placeholders. The diagnostic ROM is a host-test fixture only. This does not imply that ESP32-S3 hardware acceptance has been completed.

## 26. HOST-M1 tests

Console:

- RESET -> menu;
- 1 alone does not select;
- 1<ENTER> selects;
- CR, LF, CR+LF do not double-execute;
- invalid/disabled choices are safe.

LittleFS:

- mount success;
- corrupt/missing filesystem behaviour;
- resource paths inspectable;
- no fabricated copyrighted resources.

SD:

- physically absent;
- blank FAT32;
- missing /retro;
- wrong layout marker/version;
- correct layout;
- missing required directory;
- exchange read/write;
- full/nearly-full condition where practical.

File Transfer:

- first-time provisioning;
- stored-credential reconnect;
- page loads;
- text upload/download;
- binary SD-card disk-image upload/download + byte-for-byte equality;
- CP/M-86 loose-file upload to `/retro/exchange/cpm86/` is visible to `HOST DIR`;
- selected-machine loose files and disk images reach their respective `/retro/exchange/<machine>/` and `/retro/images/<machine>/` directories;
- every listed file shows its full `/microSD/retro/...` path;
- upload/download progress is displayed while bytes are transferred;
- zero-byte file;
- spaces in filename;
- delete confirmation;
- traversal attempts rejected;
- file larger than RAM;
- interrupted upload;
- exit to menu;
- re-enter File Transfer without RESET.

Recovery:

- WiFi unavailable;
- SD absent before File Transfer;
- HTTP client disconnect;
- write failure;
- recoverable failures do not crash the host.

## 27. Emulator boot-resource preparation

HOST-M1 prepares:

LittleFS:

  /littlefs/cpm80/
  /littlefs/cpm86/
  /littlefs/ucsd/
  /littlefs/apple2/
  /littlefs/swtpc/

SD:

  /retro/images/cpm80/
  /retro/images/cpm86/
  /retro/images/ucsd/
  /retro/images/apple2/
  /retro/images/swtpc/

Do not assume exact boot filenames in generic host code except through machine resource definitions. Provide generic existence/open/size/checksum helpers where useful.

## 28. Machine implementation order and integration process

The original implementation sequence after HOST-M1 was:

1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800

CP/M-80 and CP/M-86 implementation milestones are now integrated; consult the current implementation snapshot and their design documents for acceptance status. Continue with UCSD Pascal, Apple II and SWTPC 6800 in that order unless a documented project decision changes the sequence.

Before each machine:

1. read this document;
2. read its complete design document;
3. verify upstream CPU/emulator source and license;
4. establish reference tests;
5. integrate through host abstractions;
6. update the machine decision/problem log.

Later work must not destabilize the HOST-M1 menu or File Transfer.

## 29. Third-party and historical software

For every third-party component record upstream URL, exact tag/commit/version, license, selection reason, and local modifications.

For WiFi provisioning the required pinned component is specifically michmich/esp-idf-wifi-provisioner 0.4.0.

Treat emulator-source licensing and historical ROM/OS/application redistribution rights separately. Record provenance/checksums. Never fetch arbitrary ROM/OS images from unofficial sites during the build.

## 30. Target repository structure

```text
esp32s3-retro/
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  README.md
  projectPrompt.md
  designCPM80.md
  designCPM86.md
  designUCSD.md
  designApple2.md
  designSWTPC.md
  main/
    main.c
    systemMenu.c
    systemMenu.h
  components/
    retroHost/
    cpm80/
    cpm86/
    ucsd/
    apple2/
    swtpc/
    third_party/
  littlefs/
    cpm80/
    cpm86/
    ucsd/
    apple2/
    swtpc/
  tools/
    README.md
```

This is a responsibility guide; do not create meaningless empty files solely to match it.

## 31. Definition of done — common host

The host is ready for CP/M development only when:

- RESET always reaches the main menu;
- USB console is reliable;
- every choice requires ENTER;
- invalid input is safe;
- registry/probing works;
- unavailable machines are marked/disabled appropriately;
- LittleFS/resource preparation works;
- SD presence and FAT32 mount are tested;
- Retro layout/version is validated;
- bad/missing SD is clearly reported;
- menu works without SD;
- File Transfer is unavailable cleanly without valid SD;
- File Transfer starts WiFi only when selected;
- only the pinned michmich/esp-idf-wifi-provisioner 0.4.0 is used for WiFi provisioning/connection;
- captive provisioning and stored-credential reconnect work;
- application File Transfer server starts after network connection;
- only /retro/exchange/ and /retro/images/ on the physical SD card are exposed by the browser file server;
- loose files upload to /retro/exchange/<selected-machine>/;
- disk images upload to /retro/images/<selected-machine>/;
- each listed filename has its full /microSD/retro path shown alongside it;
- the GUI defaults to CP/M loose files at /retro/exchange/cpm80/;
- upload/download sizes above 4 GiB minus 2 bytes are rejected;
- upload/download/delete work;
- complete disk-image files upload to and download from the physical SD card with visible progress;
- the GUI defaults to CP/M-80 loose files at /retro/exchange/cpm80/;
- upload/download sizes above 4 GiB minus 2 bytes are rejected;
- binary integrity is proven;
- traversal is blocked;
- large files stream;
- File Transfer stops cleanly and returns to menu;
- files are closed/flushed;
- emulator boot-resource locations are prepared;
- emulator placeholders work;
- no emulator implementation was invented as part of HOST-M1.

CP/M-80 and CP/M-86 implementation milestones have started; their current acceptance state and remaining work are recorded in their design documents. Continue the implementation sequence with UCSD Pascal, then Apple II and SWTPC 6800, without treating host tests or firmware builds as hardware acceptance.
