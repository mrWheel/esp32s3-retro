designDiskImageManager.md — Native SD Disk Image File Manager

Revision: 2026-10-21 (revision 4: SPA function selection, transfer matrix and manager access to RO-mapped SD images)
Status: implementation specification and autonomous coding assignment
Repository: https://github.com/mrWheel/esp32s3-retro

1. Purpose and required outcome

Implement a complete browser-based Disk Image File Manager in the ESP32-S3 firmware. It must directly manipulate guest files inside disk-image files stored on the physical FAT32 microSD card, and manage complete image files on that card. It replaces the /retro/exchange/<os>/ staging workflow, OS-specific exchange scripts for routine transfers, and guest HOST file-transfer programs. It must work without a Mac/PC-side helper, Python process, or running guest emulator.

Definition of done: From a browser, select an operating-system profile, select any compatible SD-card disk image (including unassigned images), inspect its guest directory, upload one or more guest files, choose whether to overwrite conflicts, download guest files, and delete guest files. Separately, upload, download, copy, rename, and delete whole image files. Support transfers between two SD-card images and between the SD card (whole images or guest files) and a browser-running host computer on macOS, Windows, or Linux, using native browser file-selection and save dialogs. Preserve all valid existing images and the ability to boot each emulator after edits.

Do not interpret ‘copy image’ as copying only files inside the image: both operations are required and distinct.

2. Non-negotiable project constraints

	1.	The File Transfer webserver and every emulator are mutually exclusive by design. The webserver is NEVER running while a guest emulator is active. Keep and verify this invariant in the machine/menu lifecycle; never introduce simultaneous image access.
	2.	Operate on the physical 32 GB FAT32 SD card, not on a computer’s mounted SD card and not via exchange staging. The existing card directory structure remains authoritative, except that /retro/exchange/ and its OS subdirectories must be removed from the supported layout and preparation workflow.
	3.	system.dsk images and other firmware-packed resources in LittleFS must not be edited by this manager. Existing Python tools for creating/updating LittleFS system disks remain supported. Never expose LittleFS write/delete API endpoints.
	4.	Preserve existing emulator disk formats, drive mappings, ROM resources, boot paths, disk geometry, guest-visible metadata, and read-only flags. Do not invent new layouts or silently convert images. The manager's permission to edit an SD image is independent of that image's emulator `RO` flag in `drives.cfg`; do not alter the config flag as a side effect.
	5.	Keep existing Python image creation/build tools for maintenance and offline system-image preparation. They are no longer required for normal browser guest-file transfers.
	6.	Follow .codingRules.md, projectPrompt.md, designCPM80.md, designCPM86.md, designApple2.md, designUCSD.md, and relevant current documentation. When these conflict with this specification, preserve working hardware behavior and document/resolve the conflict explicitly.
	7.	Target the repository’s actual ESP-IDF configuration (README currently says ESP-IDF 6.0.2, target ESP32-S3, LOLIN S3 Pro, 16 MB flash, 8 MB PSRAM). Use existing SD mount, HTTP server, Wi-Fi provisioner, storage abstractions, and UI patterns.
	8.	The browser host computer may run macOS, Windows or Linux. File selection must use standard browser/OS file pickers, never hard-coded Mac paths or a native desktop helper. Browser security prohibits arbitrary local file access: every host-to-SD transfer begins with user-selected files, and SD-to-host transfers use browser downloads or an explicitly user-authorized save picker where supported.
	9.	No placeholders, simulated directory contents, unimplemented buttons, guest-side transfer dependency, or success responses before durable writes.

3. Mandatory repository reconnaissance (before coding)

Inspect the entire local current checkout, not just this design or the README. In particular:

	●	main/, components/, current HTTP server and File Transfer GUI, Wi-Fi provisioning, menu/machine registry, SD mounting and disk I/O.
	●	sdcard/retro/, every actual drives.cfg, profile naming and mapping conventions (including Apple II slot/unit names, SmartPort and Pascal mappings).
	●	tools/prepareSd.py, tools/buildDiskImage.py, tools/createSystemDsk.py, all *exchange*, *Exchange*, *HOST*, image parsing/building utilities, and associated tests.
	●	guest/, bootDisks/, littlefs/, tests/, docs/verification.md, and build files.
	●	Existing disk image geometry/DPB logic, CP/M directory/extents and user areas, Apple DOS 3.3 and ProDOS implementations, UCSD/Apple Pascal filesystem code, and any existing host-side libraries that can be compiled for ESP-IDF.

Produce an internal inventory mapping actual filenames and functions to each new subsystem before making changes. This document deliberately does not assert nonexistent source filenames or unverified filesystem support. Reuse proven code rather than creating parallel, inconsistent disk-format implementations. If an existing Python tool has the only correct implementation, port its validated algorithms into portable C/C++ with fixtures and cross-checks.

4. Scope and terminology

	●	Host FAT32 filesystem: physical SD-card files/directories, including .dsk, .img, .po and other actual project-supported image extensions.
	●	Guest filesystem: files/directories encoded inside a disk-image file, such as CP/M, Apple DOS, ProDOS or Apple Pascal.
	●	OS profile: CP/M-80, CP/M-86, Apple II or UCSD Pascal, as selected in the current menu/configuration. Planned SWTPC must be shown as unsupported until its filesystem is implemented and verified.
	●	Image: one physical FAT32 file, possibly assigned to a guest drive or not.
	●	Guest file: a file stored inside a selected image; not a file in /retro/exchange/.

Do not confuse image filename, mounted drive identifier, guest volume name and guest filename. They must be separate fields throughout the implementation.

4.1 Transfer endpoints and directions (mandatory)

There are three distinct storage locations: (A) files on the user’s host computer, accessed only through the browser’s authorized file picker/download mechanisms; (B) whole image files on the physical FAT32 SD card; and (C) guest files inside those SD-card images. Support all meaningful directions:

|Source                            |Destination                         |Required operation                                                                                                              |
|----------------------------------|------------------------------------|--------------------------------------------------------------------------------------------------------------------------------|
|Host local file(s)                |Guest files in selected SD image    |Browser-selected multi-file upload, Skip/Overwrite, OS-specific metadata                                                        |
|Guest file(s) in SD image         |Host local filesystem               |Download single file or multi-file archive, preserving content/metadata                                                         |
|Host local image file             |SD FAT32 image directory            |Upload complete image, verify, optionally replace                                                                               |
|SD FAT32 whole image              |Host local filesystem               |Download complete image byte-for-byte                                                                                           |
|SD image A guest files            |SD image B guest files              |Direct device-side guest-to-guest copy (same or different OS where representable), Skip/Overwrite and metadata conversion policy|
|SD image A whole file             |SD image B whole file/path          |Device-side byte-for-byte image copy                                                                                            |
|Host local file(s)                |SD FAT32 regular permitted directory|Optional only if existing UI needs raw SD files; never confuse with guest import                                                |
|SD FAT32 regular permitted file(s)|Host local filesystem               |Optional raw file download where supported                                                                                      |

Do not force host transfers through /retro/exchange/, a server-side staging directory, Python scripts, a guest HOST program, WebDAV, SMB, or host-mounted SD card. Stream host uploads directly through the HTTP request into the selected guest-image operation or a safely committed SD image file. The browser host is the user’s Mac/Windows/Linux computer, not a filesystem mounted on the ESP32.

5. Supported operations

5.1 OS and image selection

	1.	List available OS profiles from the existing registry/configuration.
	2.	For a selected OS, show configured drives/images and discover compatible unassigned images from existing SD image locations. Display physical path, image size, detected filesystem/geometry, mapped drive(s), writable/read-only status and, where available, guest volume label.
	3.	Use existing drives.cfg as the authority for configured mappings; do not infer that selecting an image changes any drive mapping. Do not modify drives.cfg merely by browsing.
	4.	An image can be used by more than one profile. Avoid duplicates in physical-image management; identify images by canonical SD path.
	5.	Unknown or ambiguous filesystem: allow whole-image download/copy/rename/delete subject to policy, but disable guest writes and explain why. Do not guess format based only on extension.
	6.	Never include LittleFS system.dsk as an editable/selectable SD image.

5.2 Guest file manager

For every supported, verified guest filesystem implement:

	●	List directory entries with native names, type, size, and OS-specific metadata. Support CP/M user areas 0–15 and applicable ProDOS subdirectories.
	●	Upload one or multiple files selected through the host OS file chooser (<input type="file" multiple> or equivalent) into the selected image; validate names, type/metadata, size, available blocks and filesystem rules.
	●	Conflict policy selectable: Skip existing (default) or Overwrite existing. Never silently overwrite. Display per-file results and error details.
	●	Download one or multiple guest files to the host computer through browser download handling (native save-location dialog when browser settings allow it). For multiple downloads, provide a streaming archive where feasible, or individually downloadable items without browser popup abuse. Preserve binary content exactly; when Apple formats require metadata, use a documented reversible representation or metadata sidecar.
	●	Delete selected guest files only after explicit confirmation; protect filesystem-reserved/system records as required by the actual format.
	●	Rename guest files if safe for the filesystem; otherwise disable with a clear explanation. Directory creation/navigation where supported, notably ProDOS.
	●	Refresh the listing and free-space display after every successful mutation.

Overwrite semantics: If replacing a file fails, the previous valid file must remain recoverable; do not delete the old directory entry before validating and reserving the replacement. Implement a filesystem-appropriate safe transaction or an image-level rollback strategy (see §8). Never claim atomicity unless it is demonstrated.

5.3 Guest-to-guest copy between SD images

Add a Copy Files Between Images workflow. Select source OS/profile, source image, user area/subdirectory and one or more guest files; independently select destination OS/profile, destination image, user area/subdirectory, and conflict policy. Support same-OS and cross-OS copies when a safe representation exists. Read and write using the respective filesystem adapters in bounded chunks; no /retro/exchange/ staging. Do not silently strip Apple metadata, Pascal types or CP/M record semantics: show a conversion/metadata choice, document round-trip limitations, and reject ambiguous lossy conversions unless the user explicitly confirms. For same source/destination image, prevent unsafe overlapping operations. A failure must not corrupt either image. Copy does not delete the source; if a Move action is provided, delete the source only after the destination has been committed and verified. Protect LittleFS images on both sides.

5.4 Complete image manager

Implement on the physical SD FAT32 volume:

	●	Upload a complete image chosen via the host OS native file picker to a selected permitted SD image directory; optionally overwrite with explicit confirmation and safe replacement.
	●	Download a complete image to the host computer, byte-for-byte, with correct Content-Length where known and streaming transfer.
	●	Copy a complete image to another SD filename/location, including across OS directories where permitted; preserve original unchanged. Show source, destination, size and free-space check.
	●	Rename/move an image safely within allowed SD locations, with collision protection.
	●	Delete an image only after explicit confirmation. Warn if it is referenced by any drives.cfg and identify the affected mappings; do not block a manager action solely because an image is mapped or its emulator drive is marked `RO`. A confirmed delete/replace can leave a drive mapping unresolved, so report that consequence explicitly and never silently edit drives.cfg.
	●	Report progress and errors for large transfers; support cancellation/cleanup where practical.
	●	Detect FAT32 file size limit (single file < 4 GiB) and available SD capacity; do not buffer complete images in RAM.

Image copy must use bounded chunks and must not interpret or modify guest filesystem contents.

5.5 Required parity with the Python disk-image management menus

Use the user’s supplied CP/M wrapper (which imports diskImageCpm, diskImageCpm80, diskImageCpm86, and extractApple2Files) as a concrete reference for the required capabilities, not as a runtime dependency. Its CpmImage, CpmVolume, openCpm and run implementations demonstrate listing, extraction, addition, deletion, persistence and format-profile selection. Inspect the actual repository version of extractApple2Files.py and its interactive menu, including options 7 and 10 that use Dest. Path. Inventory every menu operation and implement its meaningful equivalent for each supported OS, not just CP/M. Include copy/move between source and destination images, ordinary SD paths and host, rename where supported, multi-file selection, delete and overwrite. If an operation is inherently impossible for a specific filesystem, disable it with a precise explanation and tests. Do not copy the Python approach of loading the whole image into memory: use bounded sector/record I/O in firmware.

For CP/M specifically, preserve profile-by-size/geometry checks, directory extent handling, record padding (128-byte logical records), allocation block bookkeeping, CP/M user areas 0–15 (the supplied wrapper only shows user 0), and safe save/rollback behavior. Validate imported and exported byte lengths against CP/M record granularity; report any unavoidable padding. Do not treat a deleted extent as proof that its allocation blocks can be reused until the directory has been consistently updated.

6. Filesystem adapters and compatibility

Design a shared interface such as image_open, image_probe, guest_list, guest_read, guest_write, guest_delete, guest_rename, image_close, and image_flush. These are conceptual operations, not mandated exact symbols. Separate image/block I/O, filesystem format logic, and HTTP handlers. Return structured error codes.

CP/M-80 and CP/M-86

Reuse the repository’s actual DPB/geometry/sector mapping and directory extent logic. Correctly handle allocation blocks, directory entry slots, multi-extent files, records, file size conventions, user areas, deleted entries and free space. Support all actually configured SMALL/LARGE and other existing profiles. CP/M-86 must not be assumed byte-for-byte identical to CP/M-80 without examining its configured disk formats. For ambiguous trailing bytes in 128-byte records, preserve the project-defined transfer convention and document binary round-trip limitations explicitly; verify against Python exchange fixtures and guest programs.

Apple II

Support each filesystem that is actually present and validated in current images: DOS 3.3 and ProDOS as applicable. Respect 13/16-sector and nibble/sector ordering only where used, DOS catalog/T-S lists, ProDOS directory/block allocation and file metadata, boot/system blocks, and actual image .dsk/.po ordering. Do not conflate DOS 3.3 with ProDOS. Preserve Apple file type, auxiliary type, load address and resource metadata where relevant. Define explicit browser metadata choices/defaults and round-trip rules.

UCSD / Apple Pascal

Support the actual Apple Pascal 1.3 volumes and disk layouts in the checked-in profiles, including the 800K boot volume and Pascal work volumes if their filesystem is verified. Recognize the distinction between the shared Apple II machine emulator and the Pascal filesystem: reusing the CPU emulator does not mean using ProDOS parsing for Pascal volumes. Implement Pascal volume directory/file allocation rules, name/type handling and required contiguous allocation/compaction constraints from verified specifications and repository fixtures. Preserve bootability and native file types, especially .TEXT and .CODE. If an image is actually ProDOS-formatted, select the detected filesystem adapter rather than treating it as Pascal based on OS selection.

Unsupported/uncertain formats

Fail closed for guest writes. Whole-image operations remain available. No fabricated support claims. Add explicit test fixtures and enable write support only after round-trip verification.

6.1 Host file transfer and native picker behavior

Implement host-local interaction using standard HTML browser primitives and progressive enhancement:

	●	For uploads from macOS Finder, Windows File Explorer or Linux desktop file managers, invoke the OS-backed chooser using <input type="file"> (multiple for multiple guest files). The browser exposes only user-selected file handles/content; do not request or assume arbitrary host paths.
	●	For downloads to the host, return an HTTP attachment with sanitized Content-Disposition filename and correct MIME type; the browser will download to its configured location or display a Save As dialog depending on browser/user settings. A web application cannot guarantee a native Save As dialog on every browser; do not claim otherwise.
	●	Where supported (e.g. compatible Chromium browsers), optionally offer showSaveFilePicker() / File System Access API behind a feature check and user gesture, with graceful fallback to regular downloads. Safari/Firefox must work without that API. Never require browser extensions, native apps or Python.
	●	For multiple guest files, offer a ZIP download (streaming if feasible) with a manifest or sidecar metadata when needed; if streaming ZIP is impractical on ESP32, provide sequential individual downloads initiated by user action or another robust documented fallback, without relying on automatic popup permissions.
	●	Make the direction explicit in GUI labels: Computer → Disk Image, Disk Image → Computer, Disk Image → Disk Image, Computer → SD (whole image), SD → Computer (whole image), SD → SD (whole image).
	●	Show source/destination names, file count, sizes, progress, errors and confirmation for overwrite/delete. For uploaded host files, never pretend the browser can read their full original local path. Browser file names may need OS-specific normalization/guest-name mapping; display mapping and collisions before commit.
	●	Implement HTTP upload as chunked/bounded processing. Avoid buffering a whole selected host file, ZIP or image in RAM. Handle browser cancellation, disconnect, upload interruption and retries safely.
	●	Verify Chrome, Safari and Firefox where possible on macOS; Edge/Chrome on Windows; Firefox/Chrome on Linux. Test picker invocation and download behavior manually where automation cannot access OS dialogs. Browser-origin/CORS behavior must remain compatible with the existing File Transfer server.

6.2 Firmware language versus browser language

The web GUI runs HTML/CSS/JavaScript in the browser. The ESP32-S3 webserver, SD-card block/file I/O, HTTP endpoints, and disk-image filesystem adapters run in the ESP-IDF firmware, which is normally C or C++. JavaScript cannot directly open the ESP32’s FAT32 filesystem or modify guest disk sectors; it sends HTTP requests to firmware endpoints. Existing Python scripts are reference/maintenance/test tools on the host, not required during normal web operation. Prefer repository coding conventions for C/C++ and JavaScript separately; do not propose running the Python scripts in the browser.

7. Web API and GUI

Replace the entire old File Transfer webserver GUI and obsolete exchange-specific HTTP endpoints; retain and reuse only useful underlying Wi-Fi, SD, HTTP-server infrastructure and filesystem logic. The old user-facing functionality is removed, not kept alongside the new manager. Keep the same Wi-Fi lifecycle and trust model; the README currently describes unauthenticated HTTP on a trusted LAN. Bind all endpoints to the existing File Transfer mode and restrict paths rigorously.

Suggested versioned endpoints (adapt naming to current routes if necessary):

|Method|Endpoint                                  |Purpose                                                                              |
|------|------------------------------------------|-------------------------------------------------------------------------------------|
|GET   |`/api/fs/list?path=...`                   |List ordinary SD files and directories for Source/Destination Path and None selection|
|GET   |`/api/fs/download?id=...`                 |Stream ordinary SD file to browser                                                   |
|POST  |`/api/fs/upload`                          |Upload browser-selected file(s) into permitted SD directory                          |
|POST  |`/api/fs/copy`                            |Copy/move ordinary SD files with explicit conflict policy                            |
|DELETE|`/api/fs/files`                           |Delete selected ordinary SD files after confirmation                                 |
|GET   |`/api/images/os`                          |OS profiles and capabilities                                                         |
|GET   |`/api/images?os=...`                      |Configured and unassigned SD images                                                  |
|GET   |`/api/images/inspect?id=...`              |Probe image, metadata and supported operations                                       |
|GET   |`/api/guest/files?id=...&dir=...&user=...`|Guest directory                                                                      |
|GET   |`/api/guest/file?id=...&path=...&user=...`|Download guest file                                                                  |
|POST  |`/api/guest/upload`                       |Upload guest files, overwrite policy and metadata                                    |
|DELETE|`/api/guest/file`                         |Delete selected guest file                                                           |
|POST  |`/api/guest/rename`                       |Rename guest file                                                                    |
|POST  |`/api/guest/copy`                         |Copy selected guest files between two SD images, including cross-OS conversion rules |
|GET   |`/api/images/download?id=...`             |Download complete image                                                              |
|POST  |`/api/images/upload`                      |Upload complete image                                                                |
|POST  |`/api/images/copy`                        |Copy image on SD                                                                     |
|POST  |`/api/images/rename`                      |Rename/move image                                                                    |
|DELETE|`/api/images`                             |Delete image                                                                         |

Do not expose arbitrary absolute paths through HTTP. Use opaque IDs or canonicalize and verify every supplied path against permitted SD roots. Reject traversal (..), symlinks if relevant, URL-encoded traversal, illegal FAT32 names, embedded NUL, overlong names and destination collisions. Validate OS/profile ownership independently of UI selections. Use proper status codes and JSON error messages. Support streaming and request size limits.

GUI requirements (mandatory interaction model; supersedes earlier separate-panel suggestions):

	0.	The browser UI is a Single Page Application (SPA): selecting a function and moving through its selection/operation screens updates the view without full-page navigation or reload. HTTP requests to firmware remain the only way to access SD files or image contents; the SPA must not imply browser JavaScript directly accesses device storage.
		- The first screen is titled “Functions / Selection” (or equivalent) and presents explicit choices for the transfer functions below. Selecting a function opens its relevant selectors and operation view inside the SPA.
		- Every screen/state, including “Functions / Selection”, has a visible `[Help]` control. It opens a popup/dialog explaining what the user must select or enter, what the operation will do, and applicable limits, conflict policy, metadata behavior, overwrite/delete consequences, or unsupported-format restrictions.
		- Help content follows the selected function and detected capabilities. It must not promise an unavailable operation.
		- The first screen offers at least:
			1. Copy disk images from Computer to SD.
			2. Copy single files from Computer to a disk image on SD.
			3. Copy disk images from SD to Computer.
			4. Copy single files from a disk image on SD to Computer.
			5. Copy single files from one SD disk image to another SD disk image.
			6. Copy ordinary files between permitted SD directories.
			7. Copy ordinary SD files into a guest disk image, or guest files to an ordinary SD directory.
			8. Copy selected files between Computer and permitted SD ordinary-file storage when useful; label this distinctly from importing files into a guest image.
			9. Rename/move or delete a whole SD image, ordinary SD file, or guest file where supported, with explicit confirmation for destructive operations.
			10. Include other valid source/destination combinations from this model rather than silently excluding them. Never confuse a whole image container with guest files inside it.
		- The shared selector order is Source Path, Copy From, Destination Path, Copy To. Depending on the selected function, an endpoint may be Computer (using only browser-authorized file selection/download) or a permitted SD directory. Copy From/Copy To lists actual images in the corresponding SD directory and includes `None`; choosing an image means its guest files, while whole-image functions operate on the container file. The UI must make the distinction explicit.
		- Show only fields relevant to the chosen operation. Do not fabricate directory/image entries or present inactive controls as functional.
	1.	Present Source Path first. This is a browsable directory on the physical SD card, scoped to permitted SD roots; allow the user to navigate and select the directory containing source disk-images or ordinary SD files. Do not confuse it with a host-computer absolute path (which browser JavaScript cannot discover). An OS filter/profile can be chosen separately to interpret guest images; it must not hide compatible images outside drives.cfg.
	2.	Immediately after Source Path, present a Copy From drop-down listing the disk-images actually present in that SD directory, plus an explicit None choice. Display drive mapping where available, but use real image paths as identity.
	3.	When Copy From = an image, show its real guest-directory contents and allow multi-selection of guest files; additionally allow a clearly distinct whole disk-image selection/action.
	4.	When Copy From = None, show the ordinary files actually present directly in Source Path in a multi-select file list (including complete disk-image files). Do not show a guest directory or fabricate a list. Directories may be navigated; define and test whether recursive selection is supported.
	5.	Present a symmetrical Destination Path followed by a Copy To drop-down of destination disk-images plus None. Copy To = image means insert guest files inside that image; Copy To = None means write ordinary files to the selected physical SD directory. Make the target of whole-image copies unambiguous: a whole image is a regular SD file and must not be inserted as a guest file merely because Copy To selects an image.
	6.	Provide a Host Computer source/destination choice in addition to the SD Source/Destination Path controls. Host-to-device selection must invoke the browser’s native OS file picker (<input type="file" multiple> or equivalent) from a user gesture. The browser cannot directly enumerate host directories or receive an unrestricted host path. Host downloads use the browser’s native download/save behavior; where available, the File System Access API may offer a user-initiated Save As picker with a safe fallback. Do not promise a Save As popup on every browser.
	7.	Use one consistent Copy/Move/Delete/Overwrite workflow for guest files, SD files and whole disk-images, with capability-aware controls and per-file status. Copy preserves source. Move deletes source only after verified destination commit, and is disallowed when rollback cannot be made safe. Overwrite must be explicitly selected or confirmed; default Skip. Delete uses multi-select and confirmation. No silent format conversion.
	8.	The source/destination combination determines the action. Support: SD ordinary file ↔ SD ordinary file; SD ordinary file ↔ guest file in SD image; guest file ↔ guest file (including cross-OS when valid); host file ↔ SD ordinary file; host file ↔ guest file; whole image SD ↔ host; whole image SD ↔ SD; host whole image → SD. Distinguish an image’s container file from its guest files in the UI.
	9.	The OS selection is for filesystem interpretation and conversion; the image drop-down is populated from the actual directory, not a hard-coded OS list. A detected incompatible/unknown image may be copied as a whole file but must not expose unsafe guest mutations.
	10.	Preserve metadata, filename rules, CP/M user areas, Apple file types, UCSD types, and guest filesystem constraints as described in §6. Show unsupported transformations instead of silently corrupting data.

Additional UI details:

	●	OS selector; image selector with assigned drive labels and unassigned images; format indicator and a clear distinction between emulator `RO` mapping and the manager's ability to edit an SD image while the emulator is stopped.
	●	Directory browser (including CP/M user-area selector and ProDOS navigation as applicable), file size/type, free space.
	●	Explicit Computer → Image upload with native OS file picker, multi-file selection, Skip/Overwrite control, per-file outcomes.
	●	Explicit Image → Computer download using browser save/download handling; clear destination behavior.
	●	Image → Image guest-file copy panel with separate source and destination OS/image selectors and conflict/metadata handling.
	●	Guest file download, delete with confirmation, optional supported rename.
	●	Separate Whole Image panel: Computer → SD, SD → Computer, SD → SD, rename, delete, progress and confirmation; native host file picker for upload.
	●	Clear warning for mapped/boot-critical images and unsupported guest filesystems.
	●	Usable from macOS, Windows and Linux desktop browsers and iPad Safari, with no Python, command-line or helper application. Host chooser/save UX follows browser/OS capabilities.
	●	Never present mock file listings or inactive controls as working features.

8. Integrity, safety and recovery

Because the emulator is stopped, no runtime locking with guest disk I/O is necessary. Nevertheless, filesystem corruption from failed writes or power loss is a real risk.

	1.	Validate the complete image header/geometry, filesystem directory and allocation structures before modifying.
	2.	An emulator `RO` flag in `drives.cfg` does not lock the Disk Image Manager: supported operations must work on SD images regardless of that mapping flag because the emulator is stopped while File Transfer is active. Never change the configured flag as a side effect. Preserve separate safeguards: do not modify LittleFS/firmware resources; validate image and guest filesystem before mutation; reject unsupported or intrinsically read-only formats; require explicit confirmation for destructive actions; explain that edits to the SD image will be visible when it is next mounted.
	3.	Validate available guest blocks and FAT32 free space before operations.
	4.	For risky in-image mutations, implement a bounded journal/undo record or copy-on-write image transaction in a temporary SD file; select strategy based on image size and free space. A full temporary image is acceptable only when sufficient space is verified; never require loading the image into RAM. Preserve the original until replacement validates, flushes and commits.
	5.	For whole-image upload/copy, write to a unique temporary destination, flush, verify byte count (and checksum when available), then rename/commit. Define safe overwrite backup/rollback behavior; FAT32 rename is not a power-loss-proof transaction.
	6.	On startup or File Transfer entry, detect incomplete temporary operations and offer safe recovery/cleanup. Never auto-delete the only good copy.
	7.	Stream in bounded buffers, use PSRAM only if appropriate, and avoid heap fragmentation. Ensure all file descriptors and temporary resources close on error, disconnect or cancellation.
	8.	Add explicit safeguards against altering LittleFS, ROMs and firmware resources.
	9.	Preserve original test images and never run destructive tests on user data.

8.1 Legacy GUI removal

Remove the old File Transfer GUI pages, navigation, handlers, routes and exchange-only code that no longer serves any purpose. The new Disk Image File Manager replaces them completely. Preserve essential shared HTTP server, provisioning, authentication/network assumptions, SD driver, disk emulation, and menu entry/exit lifecycle. Audit all references to old endpoints and exchange/<os> and update/remove them. No backwards-compatibility requirement for the old GUI or exchange endpoints. Never remove unrelated emulator functionality. Existing HOST guest utilities may remain in historical sources/test fixtures but must not be needed for transfers or presented as an active workflow.

9. Eliminate the exchange workflow

After native transfer is verified:

	●	Remove creation and dependency on /retro/exchange/<os>/ in SD preparation scripts, sample SD tree, File Transfer routes/UI, runtime paths and documentation.
	●	Remove emulator-side HOST exchange programs and associated exchange-only build/test paths only when they are confirmed to serve no other purpose. Keep any reusable parsing/fixture code that remains valuable for tests.
	●	Update projectPrompt.md, relevant design*.md, README and tools documentation to describe the new native manager.
	●	Keep Python scripts required for LittleFS system.dsk generation/maintenance and other legitimate offline image construction. Do not indiscriminately delete every Python tool.
	●	Provide a migration note: users should back up SD data; old exchange directories may be manually removed after confirming no files remain needed. Do not automatically delete user files from existing cards.

10. Implementation phases (execute all; do not stop at a proposal)

Phase A — Audit and design alignment. Inventory real source files, formats, Python exchange algorithms, configs and fixtures. Document capability matrix for each image/OS. Identify code reuse and missing formats. Establish test baseline.

Phase B — Portable image/filesystem core. Implement image probing, bounded block I/O and adapters. Add host-native tests using real or generated fixtures. Cross-compare file listings and bytes against existing Python tools and guest-visible directories.

Phase C — SD image operations. Implement listing, whole-image upload/download/copy/rename/delete, collision checks, safe temp files and progress. Report drive mappings and warn before destructive changes, but do not deny an operation solely because `drives.cfg` marks the image `RO` or maps it to a drive. Test large streamed files.

Phase D — Guest operations. Implement directory, computer-to-image upload with Skip/Overwrite, image-to-computer download, image-to-image guest-file copy, delete, rename where supported and metadata handling. Test every supported filesystem and geometry.

Phase E — HTTP/GUI integration. Integrate into existing File Transfer mode, add API and responsive GUI, native browser/OS upload pickers and download/save flows for macOS/Windows/Linux, enforce exclusive emulator/webserver lifecycle, validate paths and error handling.

Phase F — Remove exchange dependency. Remove exchange tree creation and obsolete guest HOST transfer path after equivalent functions pass tests. Keep LittleFS tools untouched.

Phase G — Verification and delivery. Run all existing and new host tests, build ESP-IDF firmware, inspect size/memory, run available hardware tests and update docs. Fix regressions. If hardware is unavailable, clearly distinguish host-verified from hardware-unverified results; never claim hardware tests were performed.

### Current checkout inventory and capability boundary

The following is the implementation evidence found in this checkout; a host-side Python utility is not itself a firmware adapter.

| Area | Existing implementation | Manager-ready capability |
| --- | --- | --- |
| SD image HTTP I/O | `main/fileTransfer.c`: `resolveRequest`, `listHandler`, `downloadHandler`, `uploadHandler`, `deleteHandler`, `imageCopyHandler`, `imageRenameHandler`, `imageRecoverHandler`. | Whole-image/file-in-SD operations under `/retro/images/`, bounded upload/download/copy, verified image copy and rename/delete/recovery. No guest filesystem API. |
| Random-access media | `main/imageFile.c` / `.h`: `imageOpen`, `imageReadAt`, `imageWriteAt`, `imageFlush`, `imageClose`. | Bounded byte access to local files. It does not interpret guest sectors or filesystem metadata. |
| Apple II media probing | `main/apple2DiskImage.c` / `.h`: geometry/order handling and `apple2DiskImageProbe` for ProDOS and Apple Pascal volume headers. | Firmware can inspect selected media geometry/order and identify these two volume types. It does not list/read/write guest files. DOS 3.3 is not a supported file adapter in this code. |
| CP/M-80 | `tools/include/diskImageCpm.py` and `diskImageCpm80.py`; profiles SYSTEM, LARGE and BIG; `inspect_image`, `list_files`, `extract_file`, `add_files`. | Python reference implementation and tests exist. No portable firmware filesystem adapter exists. Current configured SD images use LARGE/BIG (with current `drives.cfg` entries documented above); unassigned compatible images must still be probed by exact geometry. |
| CP/M-86 | `tools/include/diskImageCpm.py` and `diskImageCpm86.py`; profiles CPM86, LARGE and BIG. | Separate Python geometry profiles exist; do not assume CP/M-80 geometry. No portable firmware filesystem adapter exists. |
| ProDOS 8 | `tools/include/diskImageApple2Prodos.py` implements volume probing/format, directory listing, file read and file add; `extractApple2Files.py` supplies the interactive cross-image/host UI. | Host-side reference algorithms and tests exist. No firmware guest-file adapter exists. |
| Apple Pascal / UCSD | `main/apple2DiskImage.c` probes Apple Pascal volumes. `tools/include/extractApple2Files.py` contains the shared Pascal volume operations; `extractUcsd.py` converts UCSD TEXT and routes Pascal file operations. Current profile maps an 800K boot volume plus 640K work volumes. | Host-side workflows exist, but no firmware guest-file adapter exists. Pascal is distinct from ProDOS despite sharing the Apple II emulator. |
| DOS 3.3 | `tools/include/extractApple2.py` routes to the interactive Apple II browser; current checked-in files do not provide a firmware DOS 3.3 adapter. | No native firmware guest-file support verified. Fail closed for guest-file writes until a tested adapter exists. |
| Exchange migration | `components/hostCore/hostExchange.c`, machine integrations in `main/cpm80Machine.c`, `main/cpm86Machine.c`, `main/apple2Machine.c`, and exchange layout checks in `main/storage.c` / `tools/prepareSd.py`. | Existing guest integrations and card preparation still depend on `/retro/exchange/`; Phase F must remain gated until equivalent native transfers are verified. |

Phase A is partially evidenced by this inventory and the focused test baseline in `docs/verification.md`; full Phase A requires reconciling all fixtures/configurations and resolving known unrelated test drift. Phase B has no portable guest-filesystem adapter yet. Phase C's whole-image SD operations and Phase E's whole-image SPA flows are implemented, but large-file/device/browser acceptance is incomplete. Phase D has no guest-file HTTP/GUI operations because Phase B is missing. Phase F is not started and is blocked by active emulator HOST/exchange integrations. Phase G has a passing ESP-IDF build and focused host/reference checks, with a known unrelated Python menu-string failure; hardware acceptance is outstanding. Do not report Phase B or Phase D complete based on the current Python utilities or header probes.

11. Mandatory automated test matrix

For every verified image format and geometry test:

	1.	Probe and list files; compare with known-good directory and Python/guest results.
	2.	Upload a new small text file and binary file; verify bytes and metadata after closing/reopening image.
	3.	Upload duplicate with Skip: original remains unchanged.
	4.	Upload duplicate with Overwrite: new bytes appear, no orphan blocks, correct free space.
	5.	Delete and verify directory and allocation reuse.
	6.	Fill image near capacity and reject oversized upload without corruption.
	7.	Multi-extent CP/M file, CP/M user area other than zero, and records not divisible by 128 where applicable.
	8.	ProDOS nested directories, DOS 3.3 file type/load metadata, Pascal .TEXT/.CODE and volume limits where applicable.
	9.	Power-loss/fault-injection simulations at meaningful commit stages; verify rollback/recovery.
	10.	Reopen changed image in existing emulator/host test and confirm directory, reads and bootability.
	11.	Read-only and unsupported-format rejection.
	12.	Path traversal, malformed image, corrupt directory, filename collision, interrupted HTTP upload, oversized request and resource cleanup.

For whole-image operations test byte-for-byte SD-to-SD copy, host-to-SD upload and SD-to-host download (hash comparison), rename, delete/replace confirmations and mapping warnings, successful manager operations on images mapped `RO` and `RW`, FAT32 capacity errors, temporary-file recovery, and files larger than available RAM. Mapping status must not be treated as a manager write lock.

Run existing commands (adjust to repository reality):

cmake -S tests -B build-host

cmake --build build-host

ctest --test-dir build-host --output-on-failure

idf.py set-target esp32s3

idf.py build

idf.py size

Do not overwrite local configuration or user modifications without inspecting them. Keep tests deterministic and independent of an actual SD card where possible; add physical SD acceptance steps separately.

11.1 Host transfer and image-to-image test matrix

	1.	On macOS, Windows and Linux browsers, select local files via the native OS-backed chooser and upload into a selected SD guest image; verify exact contents and conflict handling.
	2.	Download a guest file from an SD image to the host; verify its bytes and metadata, and that filename/download behavior matches the browser configuration.
	3.	Upload a whole .dsk/.img/.po image from the host to SD and download it back; verify full-image SHA-256 equality against the original.
	4.	Copy selected guest files between two SD images, including same OS, cross-OS representable content, duplicate names, insufficient space, and interrupted copy. Verify both guest filesystems remain valid and source unchanged.
	5.	Copy whole images between SD directories; verify byte-for-byte equality, collision policy, mapping warnings and successful operation on images mapped `RO`.
	6.	Verify Safari/Firefox fallback without File System Access API; never require a guaranteed Save As popup or access to arbitrary local paths.
	7.	Verify upload/download does not create or use /retro/exchange/, requires no host Python utility, and remains bounded in ESP32 RAM.

12. Acceptance criteria

Implementation is complete only when:

		File Transfer works with the emulator fully stopped and uses only physical SD image files.
		GUI is an SPA whose first screen is Functions / Selection, with an applicable `[Help]` popup on every screen.
		Functions / Selection offers the five requested PC/SD/image copy directions and the other valid SD ordinary-file, guest-file, image, move/rename and delete combinations.
		Each operation uses the applicable Source Path, Copy From, Destination Path and Copy To controls; image choices reflect actual SD contents and `None` selects ordinary-file mode.
		Browser can select OS and mapped or unassigned image.
		Guest directories are real, correct, and navigable where supported.
		Host-computer → guest-image multi-file upload (native macOS/Windows/Linux chooser), Skip, Overwrite, guest-image → host download, and Delete work and survive reboot.
		Guest files can be copied directly between two SD disk-images, with safe same-OS and supported cross-OS conversion.
		Copy/move/delete/overwrite and Python menu-equivalent operations are available for all supported OS adapters, with clear filesystem-specific restrictions.
		The legacy webserver GUI and exchange-specific routes are removed; the new GUI is the only File Transfer UI.
		Browser code is JavaScript/HTML/CSS; C/C++ is used only for ESP-IDF firmware-side HTTP, SD and disk-image operations.
		Host ↔ SD whole-image upload/download and SD → SD whole-image copy work, with byte-for-byte integrity checks.
		Browser-native save/download fallback works without requiring an OS Save As dialog or nonstandard API.
		Supported Apple and CP/M metadata/geometry are preserved; guest OS can still read and boot the images.
		Whole-image upload, download, copy, rename and delete work with large streamed files.
		Images mapped `RO` or `RW` remain eligible for manager operations; mapping information is shown and destructive changes require confirmation. Unsupported/malformed guest formats and LittleFS/firmware resources remain protected from unsafe writes.
		No /retro/exchange/<os>/ path is needed or created by normal operation or SD preparation.
		Guest HOST transfer utilities are not required for any supported routine transfer.
		LittleFS system.dsk remains unmodified and unexposed for editing.
		Host tests pass; ESP-IDF firmware builds; any unperformed hardware tests are listed clearly.
		README, design docs, API docs, migration instructions and verification record reflect reality.

If a current image format cannot safely support guest writes, do not declare the project finished by hiding it: implement and verify the missing adapter, or report the specific blocker with concrete source evidence and leave it read-only. No fake completion.

13. Instructions to the autonomous coding agent

You are authorized to inspect and modify this repository to implement the specification end-to-end. Do the implementation, not merely a plan. First read all required project documents and the real code. Work incrementally, run tests after each subsystem, and preserve unrelated functionality. Use the repository’s naming, formatting and architecture rules; do not create speculative replacement architectures when reusable implementations exist. Do not ask the user routine engineering questions that can be resolved by inspecting the code and tests. Make defensible technical choices, document them, and continue. Ask only when an irreversible choice or unavailable external resource genuinely blocks progress.

Deliver a final implementation report with: files added/changed/removed; filesystem capability matrix; host ↔ image and image ↔ image transfer matrix; OS-native picker/browser download behavior; API and GUI behavior; exact test commands and results; hardware verification status; migration steps; remaining limitations; and explicit confirmation that LittleFS system images were untouched. Do not report unsupported features as complete.

14. Provenance and current verification caveat

The public repository README inspected on 2026-10-21 describes a native ESP-IDF project with File Transfer under /retro/exchange/ and /retro/images/, OS profiles CP/M-80, CP/M-86, UCSD Pascal and Apple II, FAT32 microSD storage, LittleFS resources, drives.cfg mappings, and host tests. The README also explicitly notes that some physical ESP32-S3 behaviors remain unverified. The coding agent must verify details against the actual checkout, because this document was prepared from the public README and the user’s requirements, not from a complete source-code audit.

15. Implementation progress log

This section is the persistent execution record. Update it after each material phase or verification attempt, including exact failures and the changed condition before retrying. Do not rerun an unchanged failing command without a concrete reason. A partial implementation must remain explicitly partial; do not mark a phase complete because its code compiles.

### Current phase status — 2026-10-21

| Phase | Status | Implemented / verified |
|---|---|---|
| A — Audit and design alignment | In progress | Inspected the current File Transfer HTTP/UI, storage/layout validation, menu lifecycle, common image-file I/O, host path validation, all checked-in `drives.cfg` files, the SD image tree, CP/M-80/86 Python image engines, the ProDOS Python engine, relevant project/machine design documents, and the existing host test configuration. A full checkout-wide format and utility audit is not complete. |
| B — Portable image/filesystem core | Not started | No guest filesystem adapter has been implemented in firmware. Existing Python tools remain offline references. |
| C — SD whole-image operations | In progress | Firmware builds with bounded, byte-verified SD-to-SD image copying; rename/move; explicit overwrite; mapping display; same-directory temporary and preserved-backup files with an explicit restore route. Mapped `RO`/`RW` images are no longer blocked in upload, delete, copy, rename or recovery handlers. Device-side operation tests remain pending. |
| D — Guest operations | In progress (CP/M-80/86, ProDOS, UCSD Pascal and DOS 3.3 adapters built; SWTPC 6800 and functions 8/10 open) | Native `components/guestFs` adapters with list/read/write/delete/rename, streaming copy and an undo journal; HTTP `/api/guest/*`; SPA functions 2, 4, 5, 9, 11. Host-tested; device and browser verification of the Apple adapters is pending. See “Revision 5 — guest file access, CP/M slice” and “Revision 5 — guest file access, Apple adapters”. |
| E — HTTP/GUI integration | In progress | A no-reload SPA now provides Functions / Selection, applicable Help popups, browser picker upload of whole images to SD, whole-image download, byte-verified SD-to-SD copy, image file browsing/download/delete/rename, and the existing legacy exchange browser. Browser-rendered screens were inspected. Guest-file functions are explicitly marked not implemented; real HTTP/device operations and browser matrix remain pending. |
| F — Remove exchange dependency | Blocked | Do not remove the exchange tree or guest HOST paths yet: current CP/M and Apple/Pascal machine integrations still initialize/use `hostExchange`. The new native guest-file adapters must first replace and pass those guest transfer paths. |
| G — Verification and delivery | In progress | ESP-IDF build and size checks pass; focused `hostCore` CTest passes. Full CTest, Python suite and browser/device verification remain incomplete or blocked as documented below. |

### Reconciliation inventory — actual checkout

| Subsystem | Actual file/function evidence | Reuse boundary |
|---|---|---|
| HTTP and current browser UI | `main/fileTransfer.c`: `listHandler`, `downloadHandler`, `uploadHandler`, `deleteHandler`, `fileTransferStart`, `fileTransferStop`; UI in `main/web.html` | Existing bounded HTTP I/O and route lifecycle are reused. New whole-image routes are in this same server. |
| Exclusive File Transfer lifecycle | `main/systemMenu.c`: `systemMenuRun`; `main/network.c` and `main/network.h` own Wi-Fi | Keep the existing menu-only server startup/stop sequence; no emulator or Wi-Fi lifecycle changes were made. |
| Physical SD validation and paths | `main/storage.c`: `storageRefresh`; `main/storagePath.c`: `storageResolveRetroPath`; `components/hostCore/hostCore.c`: `pathIsSafe`, `pathDecode`, `layoutIsValid` | Existing FAT32 mount/layout validation and path checks remain authoritative. |
| Bounded virtual-image I/O | `main/imageFile.c`: `resourceSize`, `imageOpen`, `imageReadAt`, `imageWriteAt`, `imageFlush`, `imageClose` | Generic sector/offset I/O exists, but it is not a guest-filesystem adapter. The whole-image endpoints stream regular SD files instead. |
| CP/M mappings and reference algorithms | `main/cpm80DriveConfig.c`: `cpm80DriveConfigLoad`, `cpm80DriveConfigSystemProfileFromSize`, `cpm80DriveConfigResolveSystemProfile`; corresponding `cpm86DriveConfig*` functions in `main/cpm86DriveConfig.c`. Offline algorithms: `tools/include/diskImageCpm.py` (`inspect_image`, `add_files`, `extract_file`, `list_files`) with separate profile tables in `diskImageCpm80.py` and `diskImageCpm86.py`. | CP/M-80 and CP/M-86 use different actual geometry tables. The Python parser is not linked into firmware; no native adapter was added. |
| Apple drive mappings and reference algorithms | `main/apple2DriveConfig.c`: `apple2DriveConfigResolve`, `apple2DriveConfigLoad`; offline ProDOS functions in `tools/include/diskImageApple2Prodos.py` (`load_volume`, `list_files`). `tools/extractApple2Files.py` has `openVolume`, `printListing`, `findEntry`, copy/remove helpers and a UCSD mode. | Firmware has Apple II block-device support, not a native ProDOS/Pascal directory editor. No DOS 3.3 guest-write claim is made. |
| SD mappings and offline preparation | `sdcard/retro/images/{cpm80,cpm86,ucsd,apple2}/drives.cfg`; `tools/prepareSd.py:prepareSd` currently prepares `exchange` roots as well as `images`. | Mappings are reported in the listing and used for UI warnings, but mapping/`RO` no longer blocks whole-image manager actions. The exchange-preparation workflow remains unchanged because guest HOST integrations still depend on it. |

### Checkpoint 2026-10-21 — first implementation pass

- Repository evidence located the active server/UI in `main/fileTransfer.c` and `main/web.html`; SD layout enforcement is in `main/storage.c`; image random-access I/O is in `main/imageFile.c`; machine lifecycle is in `main/systemMenu.c`.
- Active image mappings are defined by `sdcard/retro/images/{cpm80,cpm86,ucsd,apple2}/drives.cfg`. CP/M-80 and CP/M-86 have distinct geometries in `tools/include/diskImageCpm80.py` and `tools/include/diskImageCpm86.py`; ProDOS support is in `tools/include/diskImageApple2Prodos.py`. The checked-in Apple/Pascal work images use ProDOS block images, but that does not establish Pascal filesystem support.
- Initial host baseline command:
  `cmake -S tests -B /tmp/esp32s3-retro-build-host && cmake --build /tmp/esp32s3-retro-build-host && ctest --test-dir /tmp/esp32s3-retro-build-host --output-on-failure`
  stopped at compile time because the installed AppleClang treats an existing adjacent-string-literal diagnostic in `tests/hostTests.c:3005-3006` as an error (`-Wstring-concatenation`, promoted by `-Werror`). No test executed. Do not retry unchanged; any follow-up must either use a compiler/configuration that avoids this unrelated baseline diagnostic or report the same blocker.
- The first code pass adds mapped-image reporting and HTTP endpoints for whole-image copy, rename/move, and explicit backup restoration; the browser gets whole-image copy and rename/move controls and no longer offers deletion without confirmation. Image copies use bounded buffers and compare the staged file against the source before commit. Replacing a destination first renames it to `manager-replace-backup.tmp` beside that destination; the backup is not automatically discarded on a failed commit.
- Unresolved in this checkpoint: guest filesystem adapters and all guest-file operations; unified Source Path / Copy From / Destination Path / Copy To UX; host-to-guest / guest-to-host / guest-image-to-guest-image transfers; directory discovery outside the current per-machine UI; power-loss testing; free-space preflight; and browser/device acceptance.

### Checkpoint 2026-10-21 — firmware build and baseline test evidence

- `source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh" && idf.py build && idf.py size`: PASS for ESP32-S3. Final `retroHost.bin` is 0x100F90 bytes and fits the 0x300000-byte app partition. The linker report lists 759,372 bytes of flash code, 177,308 bytes of flash data, and 132,293 bytes of DIRAM use. No flashing or hardware operation was performed.
- `ctest --test-dir /tmp/esp32s3-retro-build-host --output-on-failure -R '^hostCore$'`: PASS, 1/1.
- Full CTest used `cmake -S tests -B /tmp/esp32s3-retro-build-host -DCMAKE_C_FLAGS=-Wno-error=string-concatenation && cmake --build /tmp/esp32s3-retro-build-host && ctest --test-dir /tmp/esp32s3-retro-build-host --output-on-failure`. It built with the existing warning downgraded, then reported 6/14 passed and 8/14 failed. The failures are in existing Apple ROM/media/configuration tests: unavailable or incompatible ROM/image fixtures and `apple2MultiDisk` expecting the older `Apple Pascal 1.3 3.5.po` path while the checked-in profile uses `ApplePascal1.3-800K.po`. The `hostCore` test itself passed. These tests do not exercise the new HTTP endpoints.
- `PYTHONPATH=tools python3 -m unittest discover -s tests -p 'test_*.py' -v`: 77/79 passed; two existing assertions failed: `test_build_cpm80_matches_checked_in_image` (generated image differs from checked-in `littlefs/cpm80/system.dsk`) and `test_main_menu_shows_paths_and_disks_as_empty` (expects an older menu string than the current tool emits). Neither test exercises the changed manager code.
- Browser script syntax automation was unavailable because `node` is not installed; browser loading, native picker behavior, downloads, SD copy, rename, mapped-image rejection, overwrite recovery, cancellation and power-loss behavior remain unverified.
- README now states the exact partial capability and outstanding guest-filesystem limitation. Existing exchange directories and guest HOST integrations remain untouched. No source under `littlefs/` was modified; the firmware build regenerated only ignored build outputs.
- Do not rerun the same failing broad suites unchanged. First resolve the specific fixture/configuration/test-baseline mismatches or use corrected fixtures, then add focused tests that invoke the new HTTP/image operations against a temporary FAT-like filesystem or hardware-backed SD.

### Checkpoint 2026-10-21 — make the implemented image controls visible

- The user reported that after flashing, the GUI appeared unchanged. The source page was still the legacy layout: its initial mode was `/retro/exchange/`, while the new whole-image copy/rename/recovery actions only appear in image mode. This explains why the new functionality was not prominent in the initial view; it does not establish which exact firmware binary was flashed.
- The page is embedded in the application by `main/CMakeLists.txt` (`EMBED_TXTFILES "web.html"`), and the HTTP page handler sends the embedded bytes with `Cache-Control: no-store`. The source has now been changed so the whole-image view is selected in both the HTML control and JavaScript initial state, and the heading/introduction explicitly identify the SD disk-image manager and its current whole-image scope. The legacy HOST exchange browser remains available as an explicit alternate mode; it was not removed.
- Verification after this UI change: ESP-IDF 6.0.2 `idf.py build && idf.py size` PASS; `build/retroHost.bin` is 0x100F90 bytes, within the 0x300000-byte app partition. Searching the resulting binary with `strings build/retroHost.bin` confirms the new heading, default-mode message and selected image-mode option are embedded. `git diff --check` PASS.
- No device was flashed by the coding agent and no browser/hardware test was performed. The next user flash should use the freshly generated `build/retroHost.bin`; the expected visible check is the “Retro SD Disk Image Manager” heading and “Whole disk images (/retro/images/)” as the selected Browse mode. If those are absent, the running device is not serving this build; do not repeat the same source edit without first identifying which binary/partition was programmed.
- This checkpoint addresses visibility only. It does not implement the guest filesystem adapters or the unified Source Path / Copy From / Destination Path / Copy To workflow. Phase B remains not started; Phase D and Phase F remain blocked on it and successful guest-file transfer verification.

### Checkpoint 2026-10-21 — SPA function selection and RO-mapped whole-image operations

- `main/web.html` is now a single-page interface with a Functions / Selection start screen and client-side history navigation. The function menu labels all five requested copy directions and other SD/Computer/guest-file variants. Only already implemented whole-image directions, image browsing/management and the existing exchange file browser are actionable; unsupported guest-file operations are plainly marked “Not implemented” rather than presented as working controls.
- Every SPA screen has a `[Help]` control with screen/function-specific instructions. The SD directory picker also exposes `[Help]`. The implemented browser flows include Computer → SD whole-image upload through the native multi-file picker, SD → Computer whole-image download, and SD → SD whole-image copy with Source Path, Copy From, Destination Path and a clearly labelled new whole-image Copy To filename. Image browsing includes actual server-listed directories/files, downloads, image copy, rename/move, delete confirmation and preserved-backup recovery.
- RO mapping behavior was reconciled with revision 4 in `main/fileTransfer.c`: mapped status continues to be displayed, but upload/replacement, delete, SD-to-SD copy, rename/move and backup recovery are no longer denied solely due to mapping. `drives.cfg`/`config.cfg` protections remain. The UI warns that modifying/deleting/renaming a mapped image can affect or break subsequent emulator access; mapping flags are never rewritten.
- Browser validation: opened `main/web.html` in the integrated browser; the Functions / Selection screen rendered, selecting the Computer → SD whole-image workflow rendered its picker/overwrite fields without a page navigation, and selecting SD → SD rendered the path/image selectors and its Help popup. API requests from a `file://` browser preview are expectedly rejected by the local trust policy (403), so SD listing and HTTP operations were not validated by this preview.
- ESP-IDF 6.0.2 `idf.py build && idf.py size`: PASS; final `retroHost.bin` is 0x106C20 bytes, within the 0x300000-byte app partition. `strings build/retroHost.bin` confirms the SPA Functions / Selection and RO/RW explanatory text are embedded. `git diff --check`: PASS.
- No firmware was flashed and no device/browser-to-firmware test was performed. Guest filesystem adapters, guest file I/O, ordinary SD-file copy and the full required transfer matrix remain unimplemented. Phase E is partially implemented; Phase B/D remain open, Phase F remains blocked, and Phase G is not complete.

### Phase E update — Computer directory selection and persistence

- The whole-image Computer → SD flow now offers a Computer-folder picker where the browser supports the File System Access API, plus the existing individual-file picker. On HTTP connections or browsers without that API, folder upload falls back to the browser's native directory/file picker; only supported disk images directly at the selected folder level are used.
- The upload source-folder handle is stored in IndexedDB by selected machine profile. It is scoped to this browser/origin, is not an absolute path, and may require permission to be granted again. LittleFS is on the ESP32 and cannot store or resolve a path on the connected Computer; no host path is written to LittleFS.
- Per user clarification, there is no separate Computer destination-directory control. A whole-image SD → Computer download invokes the browser's File System Access `showSaveFilePicker()` at the point the user clicks Download where available; otherwise the browser's ordinary attachment download flow is used. To choose a location in the fallback, the user enables the browser's “Ask where to save each file” preference. OS-specific paths cannot be discovered or forced by this HTTP app.
- This is a partial Phase E enhancement, not completion of Phase E: guest-file transfers and picker/save flows for every transfer-matrix entry remain outstanding. Browser/UI and firmware-build results are recorded in `docs/verification.md`; firmware-served and hardware transfers remain unverified.

### Phase E correction — choose download destination at download time

- User direction: remove the separate “Computer destination directory” option. The whole-image download screen now contains no host-directory selector.
- On a secure origin with `showSaveFilePicker()`, the browser opens its native save picker in response to the Download action and suggests the selected image filename. The file is streamed to that user-selected file. On the ESP32's ordinary HTTP origin, the page triggers the standard attachment download; users can enable the browser's “Ask where to save each file” setting to choose a destination for each download.
- The upload source-folder selector is unchanged. Its remembered handle is only for Computer → SD uploads; download destination selection is no longer persisted or stored.
- Phase B guest filesystem core, Phase D guest operations, full Phase E integration across the transfer matrix, and Phase F removal of exchange remain incomplete. Phase F is intentionally gated on equivalent guest transfers passing tests; existing machine integrations still depend on exchange.

### Specification update 2026-10-21 — SPA function selection and RO-mapped images

- Per user direction, revision 4 defines the first SPA screen as “Functions / Selection”. It lists the requested five PC/SD/image transfer directions plus other valid source/destination combinations, including ordinary SD files, guest files, whole-image operations, and supported rename/move/delete actions.
- Every SPA screen must expose a `[Help]` popup with screen-specific input instructions, operation behavior, and relevant limitations/consequences. Navigation between the function-selection, selection and operation views must not require a full-page reload.
- The shared selector model is Source Path → Copy From → Destination Path → Copy To, with Computer endpoints available only through browser-authorized pickers/downloads, actual SD paths/images, and `None` for ordinary-file mode. Whole-image container operations must remain distinct from guest-file operations.
- User clarification: manager actions must be available for SD images whether `drives.cfg` marks the mapped emulator drive `RO` or `RW`. The `RO` flag is preserved and continues to govern guest/emulator access; it is not a manager lock while the emulator is stopped. Mapping may be displayed as a warning for destructive operations, but must not by itself deny the requested operation or silently rewrite configuration. LittleFS, firmware resources, unsupported formats and unsafe guest mutations retain their independent protections.
- At the time this specification checkpoint was recorded, the SPA and RO-mapped operation behavior were not yet implemented. See the subsequent “SPA function selection and RO-mapped whole-image operations” checkpoint for the implementation and verification performed afterward.

## Revision 5 — flat image layout and exchange removal from the manager

- Images always live in `[SD]/retro/images/<OS>/<file>`; there are no sub-directories. The SPA no longer has subdirectory inputs, Browse buttons or the SD directory picker. Upload, download, copy and manage screens only select an OS and a filename. `fileTransfer.c` rejects image paths with more than `<OS>/<file>` (HTTP 400; `isFlatImagePath`).
- The legacy exchange function (old function 8), its `area=exchange` HTTP area and `.upload-part` cleanup are removed from the browser and `fileTransfer.c`. SPA functions were renumbered: 8 SD copy, 9 file↔guest, 10 Computer↔SD files, 11 guest rename/delete (guest-file-level functions are now 2, 4, 5, 9, 11).
- NOT removed (still used by the emulators): the guest-side HOST device (`components/hostCore/hostExchange.c`, `cpm80Machine.c`, `cpm86Machine.c`, `apple2Machine.c`), the `/retro/exchange/<os>` directories required by `storage.c` and created by `tools/prepareSd.py`. Removing these is Phase F and requires the guest-file adapters first (otherwise the emulators fail to start on an SD card without those directories).

## Revision 5 — guest file access, CP/M slice (2026-10-22)

Built (source of truth: `components/guestFs/`, `main/guestFsImage.[ch]`, `main/fileTransfer.c`, `main/web.html`):

- **Component `guestFs`** (native C, no Arduino): one vtable (`guestFsOps`: open/close/info/list/stat/readAt/write/remove/rename) selected per OS (`supportsOs`); `guestFsCpm.c` is the only adapter so far. Block I/O is a callback struct (`guestFsIo`); `main/guestFsImage.c` binds it to `imageFile`. No whole-image or whole-file RAM buffers: reads/writes stream in chunks (4096-byte HTTP chunks, bounded copy chunk in `guestFsCopy`/`ExportToFile`/`ImportFromFile`).
- **CP/M engine** ported from `tools/include/diskImageCpm.py`: profile tables for CP/M-80 (SYSTEM, LARGE, BIG) and CP/M-86 (CPM86, LARGE, BIG), 128-byte records, extents with `exm`, 16-bit block pointers for BIG, user areas 0-15, 0xE5 empty entries, 0x1A padding, lowest-free-block/slot allocation (byte-identical to Python `add_files`). Names are normalised to uppercase 8.3. Fail-closed: unknown image size, bad user/name, record count, block range, duplicate block or duplicate extent => `guestFsErrCorrupt` and no mutation. Files with extent gaps/duplicates are listed as `damaged` and can be deleted/renamed but not read or copied.
- **Crash safety**: data blocks go to unreferenced free blocks first and are flushed; only then the directory change is applied through an undo journal (`manager-guest-journal.tmp` beside the images of that OS; header + old-byte records, fsync, seal, apply, flush, done footer, delete). Recovery runs at File Transfer start and before every guest operation: unsealed/garbage journal is discarded; sealed-but-not-done is rolled back; committed is removed; a mismatching or corrupt journal is kept and every guest operation on that OS is refused (HTTP 500) until resolved. The journal name is protected in `isProtectedImageMetadata`.
- **HTTP** (all under the existing `imageOperationActive` lock => 409 when busy; the `stopping` flag aborts streams): `GET /api/guest/files?image=<OS>/<file>` (JSON info + file list), `GET /api/guest/file?image&user&name` (chunked stream, `X-Guest-Size` header), `POST /api/guest/upload?image&user&name&overwrite` (raw body, `Content-Length` required), `POST /api/guest/delete`, `POST /api/guest/rename?...&to=`, `POST /api/guest/copy` with `fromImage|fromFile` and `toImage|toFile` (image->image incl. same image/cross OS, image->SD file, SD file->image; SD->SD not allowed here). SD files are `<OS>/<file>` directly in `/retro/images/<OS>/`, never disk images or manager metadata; the SD side is written to `.guest-part` and committed with the same backup-rename scheme as whole-image replacement. Status mapping: 400 bad name/empty file, 404, 409 exists/busy, 422 unsupported/corrupt/damaged, 501 OS without adapter, 507 no space/directory full. `drives.cfg` RO/RW flags do not block anything; `drives.cfg`/`config.cfg` stay protected. `max_uri_handlers` raised to 16.
- **SPA**: functions 2, 4, 5, 9 and 11 are live with a shared picker (OS, image, user area, file table, free-space line), a `[Help]` entry per screen, a client-side 8.3 name preview with collision/duplicate/empty detection, and confirmation before overwrite/delete/rename. Only CP/M-80 and CP/M-86 are selectable in the guest picker.

Known limitations: overwrite deliberately needs free room for both copies (old blocks are not reused); empty files are rejected; one journal per OS directory (one guest operation at a time under the lock); the emulators and the manager remain mutually exclusive; the SD-file copy path is not power-loss tested beyond the existing backup-rename scheme.

Remaining after the CP/M slice: ProDOS, UCSD Pascal and DOS 3.3 (now built, see the next section); functions 8 and 10 (ordinary SD files); Phase F stays blocked until the HOST transfers are replaced. Hardware/browser verification is the user's (see `docs/verification.md`).

## Revision 5 — guest file access, Apple adapters (2026-10-22)

Built (source of truth: `components/guestFs/guestFsApple.c`, `guestFsProdos.c`, `guestFsPascal.c`, `guestFsDos33.c`, `main/fileTransfer.c`, `main/web.html`):

- **Shared Apple layer** (`guestFsApple.c`): accepts only 140K (143,360), 640K (655,360) and 800K (819,200) byte images, maps 512-byte ProDOS/Pascal blocks and 256-byte DOS 3.3 sectors onto the file in either sector order (ProDOS order or DOS order, for every size), and selects the order exactly as the emulator does (extension `.po` = ProDOS, `.do` = DOS, otherwise content probe, otherwise DOS). The adapters are tried in the order CP/M, ProDOS, UCSD Pascal, DOS 3.3 and each answers `guestFsErrUnsupported` when the volume is not its own. The undo-journal commit (`guestFsRangeCommit`) is shared with the same stages as CP/M (dataWritten, beforeSeal, sealed, slotWritten, applied, committed).
- **Interface changes**: `guestFsEntry` gained `directory`, `typeLabel`, `hasAttributes`, `attributes`; `guestFsInfo` gained `nameRule` ("cpm", "prodos", "pascal", "dos33"); the vtable `write` takes optional `guestFsAttributes` (ProDOS file type/aux, Pascal kind, lock) and `guestFsWriteEx` was added. `guestFsNameCapacity` is now 32 (DOS 3.3 names have up to 30 characters). Attributes travel with `guestFsCopy`, so ProDOS <-> Pascal copies map type/kind (CODE $0B <-> kind 2, TEXT $0C <-> kind 3, DATA $0D <-> kind 5) and DOS 3.3 files are exported without their length/address header and rebuilt on write.
- **ProDOS**: root volume directory (blocks 2-5, max 51 entries), seedling/sapling/tree read (sparse blocks read as zeros) and write (allocation byte-identical to `diskImageApple2Prodos.py add_file`). Subdirectories are listed but not accessible; extended (forked) files are listed as damaged. Locks are reported but not enforced. Dates are written as 0. Mutations first validate every block claim against the bitmap (cross-links, out-of-range pointers, claimed-but-free blocks => Corrupt, nothing written).
- **UCSD Pascal**: header block 2, 26-byte directory entries, first-fit contiguous allocation, directory rewritten sorted by first block (as `extractApple2Files.PascalVolume`); fragmentation can refuse a file although enough blocks are free in total. Dates 0. Unknown kinds are listed as damaged (readable names, no read).
- **DOS 3.3** (140K only): VTOC/catalog/track-sector lists; T/I/A/B/R/S types with the same export semantics as `extractApple2Files.py`; writing stores type T (raw) unless attributes say otherwise (ProDOS $04 T, $FA I, $FC A, $FE R, $06 B with the aux type as load address; other types are stored as B at address 0). Track 0 sector 0 is never allocated to file data. Allocation order: tracks 1-34 ascending skipping track 17, then track 0. The catalog chain is never extended; headered types over 65,535 bytes are refused. Trailing NULs of text files are trimmed on read.
- **HTTP**: list responses add `nameRule`, per-file `directory`, `type`, `prodosType`, `auxType`, `pascalKind`. Upload accepts optional `ptype` (hex), `aux` (hex) and `kind` (decimal). `apple2` and `ucsd` are now supported OS ids; SWTPC still answers 501. Status mapping is unchanged.
- **SPA**: apple2 and UCSD Pascal are selectable in the guest picker; the user-area selector is hidden for file systems without user areas; a Type column is shown; the name preview follows the destination `nameRule`; the upload form has optional Apple file type and aux fields; Help texts and the home notice were updated.

Known limitations: overwrite needs free room for both copies; empty files are rejected; no ProDOS subdirectory navigation; no GS/OS forked files; DOS 3.3 catalogs are not extended; DOS 3.3 text files lose trailing NULs and raw R/S/other types read back padded to a multiple of 256 bytes; a duplicate name inside a DOS 3.3 catalog makes changes to that name fail with Corrupt; non-printable characters in DOS 3.3 names are shown as `?`.
