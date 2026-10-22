# Disk Image Manager Design

## 1. Overview

The Disk Image Manager is the browser-based file-management service for disk images on the ESP32-S3 Retro Computer. It runs in the firmware's File Transfer mode and provides access to two different kinds of content:

- **Whole images** are ordinary files on the physical FAT32 microSD card.
- **Guest files** are files stored inside a disk image and interpreted according to that image's guest filesystem.

These are separate operations. Copying a whole image preserves its bytes and does not inspect its guest files. Guest-file operations read or modify the filesystem inside an image without replacing the image as a whole.

The manager is implemented as a browser application in `main/web.html`, HTTP handlers in `main/fileTransfer.c`, random-access image I/O in `main/imageFile.c`, and filesystem adapters in `components/guestFs/`. The browser communicates with firmware over HTTP; it cannot access the SD card or arbitrary files on the host computer directly.

## 2. Purpose

The manager lets a user maintain disk images and transfer guest files using a browser, without requiring a computer-side helper program, Python process, running emulator, or guest-side transfer utility for these operations.

It provides a single interface for:

- Selecting an operating-system profile and an image on the SD card.
- Managing complete image files on the SD card.
- Browsing, uploading, downloading, copying, renaming, and deleting guest files where a supported filesystem adapter permits the operation.
- Moving files between a guest image, another guest image, the SD image directory, and the user's computer, subject to the available workflows.
- Managing `drives.cfg` files stored with the SD images.

The manager does not create a host filesystem path or bypass browser permissions. Host files are accessed only through user-selected browser file pickers, authorized folder selection where supported, drag-and-drop, and browser download mechanisms.

## 3. Scope and terminology

| Term | Meaning |
| --- | --- |
| Host computer | The computer running the browser. |
| SD filesystem | The physical FAT32 microSD card mounted by the firmware at `/microSD`. |
| Image | A complete disk-image file stored on the SD card, normally under `/retro/images/<OS>/`. |
| Guest filesystem | The filesystem encoded inside an image, such as CP/M, ProDOS, Apple Pascal, or DOS 3.3. |
| Guest file | A file or directory entry stored inside a guest filesystem. |
| OS profile | The machine or operating-system profile selected by the user, such as CP/M-80, CP/M-86, UCSD Pascal, Apple II, or SWTPC 6800. |
| Mapping | A reference to an image in an SD `drives.cfg` file. Mapping an image does not make it a different physical file. |

The image path, guest filename, CP/M user number, guest volume, and configured emulator drive are distinct values. The manager must not infer one from another or silently change drive mappings during ordinary browsing or file operations.

The supported image layout is flat: image and `drives.cfg` files reside directly in `/retro/images/<OS>/`. The manager does not browse image subdirectories.

## 4. Operating constraints

### Device and lifecycle

- The target is native ESP-IDF firmware on the ESP32-S3 with the project's existing SD, Wi-Fi, HTTP-server, storage, and menu services.
- File Transfer mode runs while the emulator is stopped. The manager must not access an image concurrently with an active guest emulator.
- The SD card must be mounted as FAT32 and pass the existing retro SD-layout checks. The firmware does not format the card automatically.
- Transfers must use bounded buffers and streaming I/O. Whole images and guest files must not be loaded into RAM in their entirety.

### Storage boundaries

- All manager image and guest-file operations target the physical SD card.
- LittleFS resources, including firmware-provided system disks and configuration, are not guest-file-manager targets. The manager must not expose LittleFS write or delete operations.
- `drives.cfg` is an SD-side file and can be transferred by its dedicated workflow. `config.cfg` and manager recovery/journal files are protected from ordinary file operations.
- Existing image bytes, formats, geometry, sector order, guest metadata, and emulator mappings must be preserved unless the specific guest operation changes guest filesystem content.
- An emulator mapping marked `RO` does not make an SD image read-only to the manager. The emulator's `RO` setting remains unchanged and continues to apply when that emulator uses the image. Mapped-image changes can affect later emulator operation and must be made visible to the user.

### Browser and network

- The host computer is accessed only through browser-authorized file selection, folder selection where supported, drag-and-drop, and downloads.
- The current File Transfer service uses unauthenticated HTTP on the configured Wi-Fi network. Treat that network as trusted; anyone able to reach the service can access its exposed SD operations while File Transfer is active.
- Uploads require a valid content length and are bounded by the firmware's transfer limit and the FAT32 single-file limit.
- Browser previews are advisory. Firmware validates every request and filesystem mutation independently.

## 5. Capabilities and boundaries

### Whole-image and SD image-file operations

The manager supports selecting and transferring complete image files between the host and SD card, copying images between SD image directories, and managing image files on the SD card. It can list files, download, upload, copy, rename, and delete images; replacement and deletion require explicit user action. Preserved replacement backups can be restored through the recovery workflow.

Whole-image operations treat the image as opaque bytes. The manager reports whether an image is referenced by a configured drive, but does not silently alter `drives.cfg`. Supported image filename extensions are `.dsk`, `.img`, `.po`, `.woz`, and `.2mg`; an extension does not establish that the guest filesystem inside an image is supported.

The `drives.cfg` workflow transfers the selected OS profile's configuration to or from the SD image directory. Upload is limited to a plain-text file with bounded size and line length. The firmware does not fully validate its configuration semantics before storing it.

### Guest-file operations

The guest-file interface supports listing and reading guest files, uploading files, deleting and renaming files, and copying files between supported guest images or between a guest image and an ordinary SD file. Guest operations use filesystem adapters and return explicit failures for unsupported or damaged filesystems.

Guest filesystem support in the code is:

- **CP/M-80 and CP/M-86:** separate geometry/profile handling, CP/M names, user areas, records, and extents.
- **ProDOS:** supported root-directory operations on supported Apple II image sizes; subdirectory contents are not navigable.
- **UCSD Pascal:** Pascal volume and file operations on supported Apple II image sizes.
- **DOS 3.3:** supported operations on 140K images.
- **SWTPC 6800:** no guest filesystem adapter; guest-file operations are unsupported.

Apple image sector ordering is interpreted using the same extension/content-probing rules as the firmware's Apple II image handling. An unknown or incompatible image must fail closed for guest operations rather than be treated as a supported format based only on its extension.

### Browser workflows

The application exposes host-to-SD image upload, host-to-guest upload, SD-to-host image download, guest-to-host download, guest-to-guest copy, whole-image SD-to-SD copy, image management, SD-file/guest-file copy, and guest-file rename/delete workflows.

Ordinary file copy between SD directories and ordinary-file transfer between the host and SD are not available. Unsupported workflows are identified as unavailable rather than presented as successful operations.

Drag-and-drop is available for the host-to-image and host-to-guest upload workflows. A dropped folder contributes only its top-level files; nested folders are not traversed. Dropping files only populates the pending selection. It does not start a transfer.

Upload previews are available for whole-image upload and host-to-guest upload. They show destination names and report conflicts or invalid selections before the user starts the operation. The firmware rechecks destinations, names, and available guest capacity at commit time.

## 6. Architecture

### Browser application

`main/web.html` implements a single-page interface with function selection, client-side navigation, per-screen help, file selection, drag-and-drop upload input, progress and operation results. It uses browser APIs for host files and sends requests to the firmware. It does not parse or write disk-image sectors.

### HTTP service

`main/fileTransfer.c` serves the embedded application, help fragments, SD listings, file transfers, image management, and guest-file operations. The handlers validate paths and operation parameters, serialize conflicting image operations, stream data, and report per-request errors. The server is started and stopped by the existing File Transfer lifecycle.

The current HTTP surface is:

| Method | Endpoint | Purpose |
| --- | --- | --- |
| `GET` | `/` | Serve the manager application. |
| `GET` | `/api/help?id=...` | Serve a help fragment from LittleFS. |
| `GET` | `/api/list` | List permitted SD image-directory entries and mapping state. |
| `GET` | `/api/file` | Download an SD file. |
| `PUT` | `/api/file` | Upload an SD file, subject to path and file-type protections. |
| `DELETE` | `/api/file` | Delete an eligible SD file. |
| `POST` | `/api/images/copy` | Copy an image to another permitted image path. |
| `POST` | `/api/images/rename` | Rename or move an image within the permitted flat layout. |
| `POST` | `/api/images/recover` | Restore a preserved replacement backup. |
| `GET` | `/api/guest/files` | Return guest filesystem information and file entries. |
| `GET` | `/api/guest/file` | Stream a guest file to the browser. |
| `POST` | `/api/guest/upload` | Write a host-supplied file into a guest image. |
| `POST` | `/api/guest/delete` | Delete a guest file. |
| `POST` | `/api/guest/rename` | Rename a guest file. |
| `POST` | `/api/guest/copy` | Copy between supported guest images and ordinary SD files. |

### Guest filesystem layer

`components/guestFs/` defines a common adapter interface for opening and probing volumes, listing and reading entries, writing files with optional metadata, deleting, renaming, and copying files. `main/guestFsImage.c` binds the adapter's bounded random-access I/O to image files. Filesystem-specific code remains separate from HTTP handling.

Guest writes use an undo journal and recovery checks. New data is written before directory metadata is committed. A damaged, inconsistent, or unrecoverable journal blocks affected guest operations rather than silently proceeding.

## 7. Operation safety and data handling

- Paths are restricted to the SD image area and the supported flat layout. Protected configuration and manager metadata cannot be deleted or treated as ordinary images.
- Existing destinations are not silently overwritten. The browser presents conflict handling and the firmware independently enforces the requested policy.
- Image replacements use a temporary file and a preserved backup path so that a failed replacement can leave the prior image available for recovery.
- Guest filesystem operations validate image geometry, names, directory structures, allocation references, and available space before committing metadata.
- Guest reads and writes are streamed in bounded chunks. Multi-file operations report failures per operation rather than claiming success for incomplete work.
- The manager's mapping warning is informational: it does not convert an emulator `RO` mapping into a manager lock and does not rewrite mappings.
- On cancellation, connection loss, or server shutdown, active operations must stop safely and temporary resources must be closed or left in a state that can be detected and handled explicitly.

## 8. Development phases

The design is organized into five phases. Each phase has an observable outcome and provides a base for the next phase.

1. **Storage and service foundation:** Establish the File Transfer lifecycle, physical SD/FAT32 boundary, flat image layout, path protections, bounded file I/O, shared HTTP error handling, and a guest filesystem adapter contract.
2. **Whole-image management:** Provide image discovery and mapping information, host upload/download, SD-to-SD copy, rename, delete, explicit replacement handling, recovery, and `drives.cfg` transfer.
3. **Guest filesystem adapters:** Implement safe probe/list/read/write/delete/rename behavior for each supported guest filesystem, preserving its geometry, naming rules, metadata, allocation semantics, and recovery requirements. Unsupported profiles must fail closed.
4. **End-to-end transfer workflows:** Connect browser-selected host files, guest images, ordinary SD files, and image-to-image operations where representable. Add conflict handling, upload previews, metadata policy, progress, and per-item outcomes without using whole-file RAM buffers.
5. **Acceptance and legacy transfer retirement:** Verify supported formats, interruption and recovery behavior, capacity limits, browser compatibility, and bootability on the target device. Retire guest HOST/exchange dependencies only after equivalent browser transfers are verified and the emulator SD-layout requirements are safely updated.

## 9. Known limitations

- The UI offers CP/M-80, CP/M-86, UCSD Pascal, Apple II, and SWTPC profiles, but SWTPC has no guest filesystem adapter. The unsupported guest operations must not be interpreted as support for SWTPC images.
- ProDOS subdirectories cannot be navigated, and extended/forked ProDOS files are not supported for normal file operations.
- UCSD Pascal allocation is contiguous; fragmentation can prevent a write even when the volume has enough free blocks in total.
- DOS 3.3 is limited to 140K images. Its catalog chain is not extended. Text-file reads trim trailing NUL bytes; raw record-oriented file types may be returned padded to a 256-byte boundary. Duplicate catalog names can make mutations ambiguous and are rejected.
- Empty guest files are rejected. Replacing a guest file requires enough free space for the replacement while the previous allocation remains available during commit.
- Some filesystem metadata cannot be preserved exactly across filesystem types. For example, conversion between ProDOS, Pascal, DOS 3.3, and CP/M has format-specific type, length, and padding semantics.
- The image browser is restricted to the flat `/retro/images/<OS>/` layout. General SD directory management is not provided.
- Upload previews are not yet a common review step for every multi-item copy, rename, move, and delete operation. They are advisory and do not reserve SD or guest capacity.
- Replacement backup and guest journal recovery provide explicit recovery mechanisms, but power-loss behavior and full crash durability across all SD/FAT32 failure modes are not guaranteed by the interface alone.
- The existing guest-side HOST/exchange functionality and `/retro/exchange/<OS>/` SD layout remain part of emulator operation until their replacement is verified. The manager does not remove or bypass those dependencies.
- File Transfer uses unauthenticated HTTP on the configured Wi-Fi network. It is not intended for exposure to an untrusted network.
- Browser file pickers and save behavior vary by browser and operating system. The application cannot choose arbitrary host paths or guarantee a save dialog for every download.
- Firmware build or host-side tests alone do not establish acceptance on a physical ESP32-S3, SD card, or browser.

## 10. Possible improvements

- Extend the common preview and review step to every multi-item operation, including SD-to-SD, guest-to-guest, rename, move, and delete; show full destinations, conflict decisions, space estimates, and per-item outcomes.
- Add SD free-space reporting and improve capacity preflight while retaining firmware-side commit-time checks.
- Add ProDOS subdirectory traversal and broader ProDOS file-format support, then extend verified guest filesystem support to additional profiles as required.
- Define explicit, user-visible metadata conversion policies for cross-filesystem copies, including lossy conversions and record padding.
- Improve power-loss guarantees for whole-image replacement and ordinary SD-file transfers, including startup handling for incomplete temporary files.
- Add optional access control or a pairing/confirmation mechanism before exposing the unauthenticated HTTP service to broader networks.
- Add device-level and cross-browser acceptance coverage for long transfers, cancellation, recovery, native file pickers, drag-and-drop, and download behavior.
- Add image inspection details such as detected format, geometry, guest volume label, and compatibility confidence without using filename extensions as proof of format.
