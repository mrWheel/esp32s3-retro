# designUCSD.md — Native Apple II Pascal engineering memory

Revision: 2026-10-12. Status: shared Apple II launch/configuration integration and Pascal 1.3 128K host boot are verified. A prior host fixture verified three-volume Filer enumeration and opening a native `.TEXT` file, but the current worktree's Pascal test fails its expected `DATA3:` assertion after disk-image/configuration changes. Reconcile the current media with the active `drives.cfg` before treating that earlier guest result as current. Compiler, filesystem writes, ESP32-S3 operation and end-to-end acceptance remain unverified.

## Provenance and purpose

This design replaces the independent Z80 p-System proposal with **native Apple Pascal/UCSD Pascal running on the existing Apple II NMOS 6502 emulator**. Menu options **3 + [Enter]** and **4 + [Enter]** select different `config.cfg` files and then start exactly the same Apple II emulator. The `drives.cfg` associated with the selected configuration alone defines the mounted disks; the boot disk determines whether Applesoft/ProDOS or Apple Pascal runs. The menu labels describe the intended configurations, not an OS enforced by the emulator. Both use the same Apple II CPU, memory bus, Language Card, slot controllers, video, keyboard and host lifecycle. There is no separate UCSD machine emulator, CP/M core, Z80 interpreter, CCP, BDOS or CP/M BIOS in the Pascal execution path.

The guest's Apple II 6502 p-code interpreter executes its matching Pascal system and compiled p-code. The ESP32 does not implement a replacement p-code VM or simulate the Pascal command environment. Boot original, matching Apple Pascal system images through the existing Apple II slot-controller interface and their own on-disk loaders. Add only the disk-format, boot and terminal support that reproducible evidence shows is missing.

The original design rebaseline did not inspect the repository or run tests. The subsequent implementation inspected the current project contract and Apple II launch, drive-config and registry code; it built the ESP-IDF firmware and ran Apple II host regressions. Pascal 1.3 128K booted in the host IIe harness. A prior fixture also listed and opened files from three block volumes, but the current focused test does not pass with the current worktree media. No desktop reference run or ESP32 execution was performed. The owner's report that Apple II and large ProDOS disks work is a user report; it does not independently prove native Pascal compatibility.

### Authority and conflict resolution

1. The current user request defines the native Apple II Pascal direction.
2. `projectPrompt.md` remains the project-wide authority and was inspected during implementation.
3. Later, explicit implementation decisions in the supplied `designApple2.md` take precedence over its earlier design prose. Preserve their evidence limits.
4. This document specifies the smallest Pascal extension. Historical records in the appendices are immutable evidence, not current implementation instructions.

The old `6202` target wording, Z80 II.0 architecture, menu-2 bootstrap and “only then begin Apple II” sequence are superseded. An exact Apple Pascal release has not yet been selected; do not carry the old Z80 II.0 version constraint into the new design. Likewise, the Apple document's “menu option 4, started by 3” and APPLE-DEC-001 “Menu 3” conflict with the fixed menu: this document resolves them as Pascal=3 and Apple II=4.

### Current baseline and evidence labels

| Classification | Meaning in this document |
|---|---|
| DOCUMENTED IMPLEMENTATION | Reported by the supplied Apple engineering record; not freshly inspected here |
| HOST/BUILD EVIDENCE | Previously recorded host execution or build, limited to the named test and revision |
| USER REPORT | Owner observation without independent reproduction in this rewrite |
| DESIGN DECISION | Required future behavior; not a claim that code exists |
| ASSUMPTION | Working hypothesis to be confirmed before depending on it |
| OPEN / NOT RUN | Missing source, execution or hardware evidence |

| Area | Controlling record and current interpretation |
|---|---|
| CPU | Existing `floooh/chips` NMOS 6502; recorded revision `ee88c35ad6427341aa6999c3b07233e1f8bd2396`; reuse, do not replace |
| Memory | APPLE-DEC-030 / VERIFY-028: 48 KiB motherboard + 16 KiB Language Card implemented and host-tested; Pascal use remains unverified |
| Apple II storage policy | APPLE-DEC-033: ProDOS-only production baseline; DOS 3.3 tooling/defaults were removed; no implicit boot drive |
| Configuration | APPLE-DEC-034 plus this 2026-10-12 extension: `SD4.1` through `SD7.2`, `SP4.1` through `SP7.4`; a slot cannot mix Disk II and SmartPort |
| Video | APPLE-DEC-016/023/035: project-authored Videx-compatible slot-3 profile, boot-80 behavior and ProDOS fixes; full Pascal editor unverified |
| Keyboard | APPLE-DEC-036 dated 2026-10-11: terminal cursor/control translation exists; Pascal bindings unverified |
| Exchange | APPLE-DEC-036 dated 2026-10-10: common version-1 exchange bridge and ProDOS `HOST.SYS`; no native Pascal utility proven |
| Pascal images | Host evidence includes Pascal 1.3 128K boot and a prior three-volume/editor fixture; current guest media/configuration regression fails at the expected DATA3 volume assertion. Guest writes and hardware remain unverified |

The Apple source contains two different APPLE-DEC-036 records. Cite them by ID **and date/topic**, without silently renumbering the source. The archived logs retain this collision.

## Project-wide contract

`projectPrompt.md` governs shared host architecture. Companion designs are `designCPM80.md`, `designCPM86.md`, `designApple2.md`, `designSWTPC.md` and this document; the Apple source also references `timeSource.md`. KIM-1 remains removed.

```text
ESP32-S3 Retro Computer
1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800

6. File Transfer

Select system [1-6]:
```

A selection requires Enter. Normalize CR/LF/CRLF to one submitted line; a digit alone does nothing. Distinguish “not implemented” from missing or invalid resources and return a useful error to the menu. The two Apple-based menu entries differ only in the selected `config.cfg`. Neither entry selects an OS-specific machine, boot routine, terminal path or hidden media override.

HOST-M1 owns the registry, USB console, menu, LittleFS, SD detection, versioned `/retro/layout.txt`, image storage, exchange service and browser File Transfer. Preserve the existing host contract and dependency lock. Earlier HOST-M1/CP/M acceptance gates remain project acceptance bookkeeping; they do not require a new CP/M implementation or CP/M runtime before extending the already implemented Apple II.

Paths under `/retro/` denote the SD volume independently of its VFS mount prefix. Use the implemented LittleFS mount and manifest schema. Manifests record size, checksum, format/order, origin, license, machine profile and RO/RW policy. Minimal immutable resources may reside in LittleFS; writable and larger images belong on SD. Never format an unknown card, overwrite user images implicitly, or load an entire disk into guest RAM. Use bounded buffers, checked offsets, end-of-image checks and explicit ownership.

Only option 6 starts WiFi and browser File Transfer, using the existing `michmich/esp-idf-wifi-provisioner` dependency (`^0.4.0` requirement in the supplied contracts; resolve against the project's actual lock). Its scope remains `/retro/exchange/` and `/retro/images/` on SD, with bounded streaming, temporary uploads, traversal protection, deliberate overwrite, byte-identical binary transfer and progress display. The documented host transfer limit remains 4 GiB minus 2 bytes; it is a host-contract statement, not a Pascal file-size limit. Enter exits File Transfer. Emulator execution and browser transfer remain mutually exclusive.

The UCSD image root `/retro/images/ucsd/` is supported by the Apple drive-config parser and File Transfer's machine selector. The LittleFS boot image and profile are in `/littlefs/ucsd/`. The `/retro/exchange/ucsd/` directory is the requested native Pascal HOST utility's exchange root; the native `HOST` utility is implemented and guest-tested in the host emulator (UCSD-DEC-021); ESP32-S3 operation is unverified.

Physical ESP32 reset returns to the host menu; guest reset is separate. Clean exit flushes/closes media, releases devices and restores the terminal. Ordinary guest Enter is never the host exit command. Sudden power loss cannot guarantee guest filesystem consistency; qualify recovery on disposable media.

## Implementation discipline

Use native ESP-IDF/CMake and the VSCode ESP-IDF workflow, no Arduino. Target the existing ESP32-S3 / LOLIN S3 Pro configuration and verify actual board, PSRAM, flash and USB settings when executing tests. Project code, names, messages and documentation are English. Use Allman braces, two-space indentation, lowerCamelCase and standalone `//— comment` lines; retain upstream style and licenses in vendored components.

Reuse the pinned 6502 core and Apple machine modules. Keep CPU, bus, controllers, image backend and terminal transport separable. Do not substitute host filesystem calls for guest Pascal operations, synthesize a Pascal prompt, bulk-load an arbitrary interpreter into RAM, or patch guest binaries until a prompt appears. If an authentic loader requires a missing controller-ROM entry, implement and document that compatibility behavior only after tracing it.

Share neutral HOST-M1 services with other machines; do not import CP/M CPU, BDOS, BIOS, DPB or drive-letter semantics. Bound allocations/queues, check failures and keep diagnostics outside the guest screen. Preserve CPU/bus semantics while yielding to ESP-IDF and servicing watchdogs. Measure performance on the board.

## Engineering-memory rules

Read decisions and unresolved issues before changing the emulator. Never erase failed experiments or silently rewrite conclusions. Supersede with a new numbered record; repeat an experiment only with new evidence or a changed variable. Keep design approval, source review, host tests, builds, user reports and hardware verification distinct.

Every verification entry records date, firmware commit or dirty-worktree state, upstream revision, ESP-IDF version, board, terminal/settings, exact input/command, image/ROM hashes, expected and actual results, evidence paths and limitations. Only observed execution may be PASS. Use this issue chain:

**Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat.**

Unknown causes stay unknown. A workaround is not a root cause. Preserve the first divergent CPU/bus/disk event and small reproducible fixtures. Guest media and ROM rights are separate from emulator licensing. The source's broad firmware-permission statement does not resolve its explicit open ROM-provenance issue.

The appendices preserve the original UCSD logs and the Apple implementation/decision/issue/verification record verbatim. Their old status labels are historical snapshots. The active disposition tables below determine what applies now.

## Layered architecture

```text
3 + Enter → select its config.cfg ─┐
                                 ├→ same Apple II emulator and initialization
4 + Enter → select its config.cfg ─┘
  → resolve the associated drives.cfg through the common configuration mechanism
  → attach exactly the configured disks using existing slot controllers
  → same 6502 + motherboard + Language Card + ROM + video/keyboard
  → same reset/autostart and boot from explicitly configured SD6.1
  → boot disk's own loader determines the guest environment:
      Applesoft / ProDOS, or native Apple Pascal / its 6502 p-code interpreter
```

Here, “configuration profile” means a selected `config.cfg` and its associated `drives.cfg`, not an OS mode. Do not introduce `isPascal`, a menu-dependent controller override or an OS selector that competes with the disks. Controller behavior follows the configured devices and validated media through the same dispatch logic for either entry. A fresh launch resets CPU/device state and owns its images exclusively. Guest reset retains or clears memory according to the existing machine contract, including Language Card reset retention; it is not equivalent to destroying and recreating a machine.

### Machine profile and scope

| Element | Pascal requirement |
|---|---|
| Machine | Default: expanded original Apple II. Optional `MACHINE=IIE`: real Apple IIe (needed by the 128K Pascal 1.3 system). Never a Z80 system |
| CPU | Existing NMOS 6502 and bus-cycle behavior |
| RAM | Apple II: 48 KiB motherboard plus bank-switched 16 KiB Language Card (64 KiB), optionally a Saturn 128K slot-0 card. Apple IIe: 64 KiB main + 64 KiB auxiliary (128 KiB) |
| Firmware | Existing ROM/autostart and slot firmware, checksum-pinned; identity/provenance still to qualify |
| Storage | Original compatible Apple Pascal system images, guest-controlled boot and filesystem access |
| Display | Existing 40×24 motherboard text and independent slot-3 80×24 card |
| Input | Existing keyboard latch/strobe and case-preserving USB character translation |
| Additions | Only demonstrated disk-format/order, boot/controller and Pascal terminal compatibility gaps |
| Exclusions | New CPU/machine/p-code emulator, CP/M runtime, restored DOS 3.3 product path, IIe auxiliary-memory substitution on the Apple II model (the IIe is a separate, explicit machine model), physical drives |

### Mandatory desktop reference gate

Select one complete matching **Apple II Apple Pascal** release and disk set. Record release, filenames, hashes, volume labels, required RAM/ROM, disk order, controller requirements and terminal/card configuration. The old z80pack II.0 review is historical background only. A non-Apple UCSD distribution is not automatically an Apple II boot disk.

Use a desktop reference configured for original Apple II plus Language Card and the required disk interface. Record emulator revision and configuration; a successful IIe boot alone does not prove original-machine or card behavior. First show boot, editor entry, a small Pascal program compiled and executed, and native file save/reopen. Match the release before comparing failures.

Trace reset/autostart, controller entry, boot sector, loader reads, Language Card transitions and entry into the guest interpreter. A filename such as `SYSTEM.PASCAL` does not specify the bootstrap. Preserve a successful reference trace before ESP32-specific diagnosis.

### Direct startup from the menu

1. `3` or `4` + Enter selects the corresponding `config.cfg`; this is the only menu-dependent difference.
2. Use the common configuration resolver to obtain its associated `drives.cfg`, validate resources and attach exactly its configured media. No inherited CP/M or previous guest state is allowed.
3. Initialize and reset the same Apple II machine and boot the explicitly configured `SD6.1` through autostart and slot firmware.
4. Execute that disk's loader. An Apple Pascal system disk starts native Pascal; the existing ProDOS disk starts its Applesoft/ProDOS environment. Neither menu number asserts which OS must appear.
5. Handle guest-requested media changes using the existing ownership/flush model; define a bounded host swap control only if the selected disk set needs it.
6. Guest restart repeats the common guest boot behavior; physical reset returns to the host menu.

**DECIDED behavior, OPEN integration detail:** menu entries select different `config.cfg` files. The supplied documents do not establish their exact paths or the existing key/path convention by which each resolves `drives.cfg`; inspect and reuse that mechanism instead of inventing syntax here. Selection must not rewrite either file or overwrite `/littlefs/apple2/system.dsk`. Preserve the shipped ProDOS configuration as a regression fixture, while allowing either entry to boot any supported disk set its associated `drives.cfg` explicitly selects.

Missing/invalid configuration or missing/unopenable `SD6.1` is an error returning to the menu, consistent with APPLE-DEC-033. There is no automatic fallback disk and no silent substitution of the ProDOS system image.

### CPU, ROM and reset

Reuse the supplied Apple record's pinned 6502. The documented runtime ROM is the 12 KiB `assets/apple2_roms/apple.rom`, deployed as `/littlefs/apple2/apple2.rom`, with recorded SHA-256 `378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249`. This is inherited metadata, not a fresh checksum of repository assets.

The design calls for Integer BASIC/Autostart, while recorded host execution also exercises Applesoft. Resolve the exact ROM identity and required Pascal compatibility rather than inferring it from `APPLE ][`, a prompt or a filename. Preserve the project-authored diagnostic ROM as a fixture, not a substitute for Pascal-capable firmware. Rights remain open.

### Memory and I/O — Language Card: 48 KiB + 16 KiB

Reuse `$0000–$BFFF` motherboard RAM and `$C000–$CFFF` I/O/slot space. The Language Card overlays `$D000–$FFFF` with two selectable 4 KiB banks at `$D000–$DFFF` and shared 8 KiB at `$E000–$FFFF`; this is not 64 KiB of simultaneously linear RAM and not IIe auxiliary memory.

APPLE-DEC-030 records `$C080–$C08F` switching: A3 chooses bank 2/1; A1=A0 selects RAM reads; two odd-switch reads enable writes; odd writes do not unlock; even accesses disable writes. Writes can target card RAM while ROM is selected for reads. Reset selects bank 2, ROM reads and disabled writes while retaining card RAM. The inherited implementation was compared with AppleWin and host-tested; primary-reference and Pascal execution verification remain open.

Trace the selected guest's actual bank use. Check loader writes, both bank boundaries, shared RAM, stack/heap behavior and ROM visibility. Any block transfer into banked memory must use the existing memory mapping, not bypass write protection. Audit ROM-address hooks against bank selection: an address in `$D000–$FFFF` can be Pascal RAM code.

## Storage and volume semantics

### Current ProDOS policy and the Pascal extension

APPLE-DEC-033 supersedes the earlier DOS 3.3 default and missing-drive fallback. Keep the existing ProDOS-only Apple II deployment, builders and acceptance tests. Do not restore removed DOS filesystem tools or make DOS 3.3 a prerequisite.

The current request authorizes **native Pascal media in the shared Apple II disk path**, selected solely by `drives.cfg`, as a narrowly scoped extension of that policy. This support is not restricted by menu number. It does not make Pascal media ProDOS volumes. Keep filesystem content, sector order and guest-visible controller protocol separate. A Pascal `.po` may use the documented ProDOS/Pascal sector layout without containing ProDOS or supporting SmartPort.

APPLE-DEC-032 attaches ProDOS content as a block-device slot, with both drives in that slot sharing the controller type. Pascal boot through Disk II needs its compatible sector/controller path. Do not identify a ProDOS filesystem from a `.po` suffix alone or pair Disk II and SmartPort media in one slot. A prior host test proves Pascal 1.3 can use the ProDOS block entry for Pascal volumes when the test images carry the expected volume contents; current configured media must be reconciled before relying on that result. This is separate from the custom ATTACH-style protocol tracked by APPLE-ISSUE-017.

### Configuration and resource locations

Use the existing `drives.cfg` line grammar:

```text
SD<slot>.<drive>=<image>,<RO-or-RW>,<profile>
```

Accepted baseline: Disk II slots 4–7 have drives 1–2; SmartPort block devices can be configured as `SP<slot>.<unit>` on slots 4–7, units 1–4. Pascal 1.3's ProDOS block interface exposes only units 1 and 2 per slot, so Pascal volumes beyond the first two must be assigned to another free slot. Slot 0 is Language Card, slot 2 is the exchange bridge, slot 3 is video. `PR6.1`, lettered CP/M mappings and Disk II drives 3–99 are not supported configuration extensions. Guest `PR#n` is a separate command, unchanged.

The checked-in UCSD `drives.cfg` maps the boot disk to Disk II and distributes
three block images across slots 4 and 5. Its current entries are:

```text
MACHINE=IIE
SD6.1=/littlefs/ucsd/ApplePascal1-140K.dsk,RO,APPLE2_140K
SP4.1=/retro/images/ucsd/ApplePascal2-640K.po,RO,APPLE2_640K
SP5.1=/retro/images/ucsd/pascalWork1.po,RW,APPLE2_640K
SP5.2=/retro/images/ucsd/pascalWork2.po,RW,APPLE2_640K
```

The code validates paths, image profiles, RO/RW modes and mutually exclusive
controller types per slot; it does not validate a Pascal volume's label or
contents. The workspace copy of
`sdcard/retro/images/ucsd/ApplePascal2-640K.po` was inspected on 2026-10-12:
it is the `SYSTEM:` volume and contains `SYSTEM.COMPILER`,
`SYSTEM.ASSMBLER`, `SYSTEM.LINKER` and the Pascal 1.3 system files, with
approximately 645 free blocks. The checked-in `drives.cfg` mounts it read-only
on SP4.1. This confirms the compiler tools are present in that workspace image,
not that the physical SD card contains the same bytes or that the compiler has
been run by the current test harness. The current `ucsdPascalIIeTextOpen` test
fails because its expected `DATA3:` volume is not present in the tested current
image set. A prior fixture verified Filer listing DATA3, WORK1 and WORK and
opening `WORK1:SPIRODEMO.TEXT` from slot 5 unit 2. The editor cannot read the
same TEXT file from a Disk II mapping. The preserved
ProDOS `drives.cfg` baseline, normally associated with option 4, explicitly lists:

```text
SD6.1=/littlefs/apple2/system.dsk,RO,APPLE2_140K
```

Each effective `drives.cfg` has one explicit SD6.1 mapping; do not combine these two alternatives into duplicate boot-drive entries. There is no additional Pascal/ProDOS switch in `config.cfg`: it selects configuration resources using the existing schema, while `drives.cfg` remains the sole disk inventory. If both menu entries resolve the same effective configuration, their guest behavior must be identical. Swapping the disk configurations must swap the resulting environments without a code change.

| Location | Intended use / current constraint |
|---|---|
| `/littlefs/apple2/` | Existing ROM and ProDOS master |
| `/littlefs/ucsd/` | UCSD profile and read-only Pascal system disk |
| `/retro/images/apple2/` | Apple II media root |
| `/retro/exchange/apple2/` | Existing slot-2 adapter root and browser staging |
| `/retro/images/ucsd/` | UCSD writable images; selected directly by File Transfer |
| `/retro/exchange/ucsd/` | Native Pascal HOST utility exchange root |

### Disk controllers, image ordering and native units

Reuse the existing slot controllers and image callbacks. The documented 16-sector Disk II path implements softswitch access, motor/stepper state, GCR read streams, decoded writes, per-drive heads and order. A host block read alone is not proof of controller compatibility. The project-authored boot ROM can differ from original firmware entry points; the ProDOS investigation already demonstrated why a loader trace matters. Do not assume the Pascal loader has the same problem or use ProDOS's workaround without evidence.

The documented image profiles are:

| Profile | Exact bytes | Backend representation | Pascal acceptance |
|---|---:|---|---|
| `APPLE2_140K` | 143,360 | 35 × 16 × 256; 280 blocks of 512 bytes | First qualification target; boot still NOT RUN |
| `APPLE2_640K` | 655,360 | 160 × 16 × 256; 1,280 blocks | Host sector/layout evidence only |
| `APPLE2_800K` | 819,200 | 200 × 16 × 256; 1,600 blocks | ProDOS large-volume baseline; Pascal support unknown |

Do not describe 640K/800K as universally supported original Apple Pascal floppy geometries. Thirteen-sector, nibble, protected and other image formats are not automatically accepted. Add one only if required by the selected original media, with a separate test and recorded format contract.

Record a volume table for the selected disk set: native Pascal unit, volume label, host SD handle, controller protocol, image hash/path, mandatory/optional state, RO/RW, profile, order, block count and reserved regions. Do not derive Pascal unit numbers mechanically from SD handles or use CP/M A:/B: naming.

Preserve raw image bytes and record physical-sector → image-sector → Pascal-block mapping. The Apple record describes Pascal/ProDOS block `n` as two logical sectors `2*(n%8)` and `2*(n%8)+1` on track `n/8`; qualify that mapping against the selected media and controller. Its Pascal probe examines directory block 2; a recognized volume header proves neither bootability nor a complete system disk. Unknown/ambiguous order needs explicit diagnosis and recorded resolution; do not treat a warning or suffix as proof.

Test first/last block, cross-track reads, malformed images, partial I/O, RO refusal, error propagation and flush/reopen. Never enlarge an image on an invalid guest request. Host tools may inspect or prepare unmounted copies; the running guest owns mounted filesystem structures.

### Larger volumes

First complete the workflow on original-size media. Retain APPLE-DEC-031's 800K preference for ProDOS and its 640K compatibility profile. Neither choice establishes a Pascal capacity limit or guest driver.

Larger Pascal volumes are a later qualification task. Audit the selected release's block representation, allocation/directory fields, formatter, file-size limits and device driver together. A 16-bit field alone is not a capacity proof. The existing blank Pascal-volume builder and block-1279 controller read do not prove that Pascal can boot, enumerate or write that volume.

Record a capacity equation, formatter recipe and guest driver before accepting a larger profile. Test allocation near the final block, directory saturation, file growth, delete/reuse and fragmentation restrictions. If unsupported, document original-size/multiple-volume operation and the limitation. Do not build a new controller or guest driver merely to complete the initial Pascal boot goal.

## Terminal and development workflow

### Keyboard, lowercase text and ROM behavior

Reuse `$C000` keyboard data/ready and `$C010` strobe clearing. USB transports characters, not physical modifier states. `$C063` must not synthesize Shift from letter case; the current setup has no physical Shift wire. Preserve the existing lowercase mapping and distinguish screen decoding, keyboard input, echo, stored source and string output.

APPLE-VERIFY-017 records a parked Applesoft case-loss problem. It does not establish the same Pascal defect. Check Pascal source and saved files independently; do not broaden an Applesoft echo workaround into a guest-independent text rewrite.

The 2026-10-11 keyboard decision maps cursor Left/Right/Up/Down to `$08/$15/$0B/$0A`, decodes normal/application ANSI cursor forms, drops other CSI/SS3 keys such as Home/Delete, delivers lone Escape after 50 ms, preserves other control characters, normalizes CR/LF and maps DEL to backspace. These are existing mappings, not proven Pascal editor commands. Record the guest editor's actual required bytes and make only necessary, tested common-device or guest-terminal adaptations. Never select key mappings from menu number or a hardcoded OS mode.

### 40-column and 80-column display

Reuse motherboard text memory and the independent slot-3 Videx-compatible card with banked VRAM, CRTC and project-authored firmware. Render guest memory/card state to the host ANSI/VT100 terminal. On the Apple II model no IIe main/auxiliary interleaving or new host-side Pascal screen engine is allowed; the IIe model (`MACHINE=IIE`) uses the IIe 80COL/ALTCHARSET softswitches with even columns in auxiliary and odd columns in main memory.

Existing boot-80 selection, CSW handling and ROM hooks were developed for BASIC/DOS/ProDOS. Pascal may use different output paths or overwrite vectors. Inspect whether its driver uses card registers, firmware calls or private routines; verify selection and rendering from real guest state. Keep the APPLE-DEC-035 protection that ROM HOME interception only applies while monitor ROM is visible. A Pascal RAM instruction at the same address must execute normally.

A minimal `PR#3` output driver and visible banner are insufficient proof of Pascal editor compatibility. Begin with an authentic 40-column reference boot if needed for diagnosis, then qualify 80×24 editing through the existing card. A required Pascal terminal/card configuration is part of the matched release profile, not a new emulator.

### End-to-end workflow

Demonstrate edit → compile → run → save → clean restart → reopen, first on the reference and then the shared emulator and ESP32. The sample must perform console input/output and native file write/read; retain source, compiled code and data. Verify lowercase inside strings and saved source, cursor motion, insert/delete, backspace, Escape/control commands, scrolling, error positioning, paste pacing and clean return to the guest menu. Keep host logs out of the screen. Record terminal settings and any terminal application interception separately from emulator input.

## Guest exchange utility

Whole-image transfer through option 6 is the first supported transfer workflow. Existing ProDOS `HOST.SYS` is a ProDOS SYS program; it is not runnable as a native Pascal utility and its extension-based metadata policy does not define Pascal files.

Retain the implemented common version-1 service, slot-2 `$C0A0` data / `$C0A1` abort interface, bounded streaming, length and CRC-32 behavior. It is documented as confined to `/retro/exchange/apple2/`; a Pascal-specific root needs deliberate host selection, not a new Z80 bridge or guessed addresses. Source/build evidence does not prove guest/hardware execution.

A native Pascal `HOSTXFER`, if subsequently needed, is deferred until boot/editor/storage acceptance. It would use the selected release's Pascal/6502 binding, guest filesystem services and existing bridge capabilities. Specify calling convention, bank safety, clobbers, filename rules, native types/logical lengths, collision policy, cancellation and failure cleanup. Do not start WiFi inside the guest or write mounted Pascal structures directly from the host.

Separate byte-preserving native transfer from explicit host-text conversion. Qualify actual Apple Pascal text layout before importing `.PAS` as compilable text; never strip padding heuristically. Additional guest utility work is not a prerequisite for this minimal native-Pascal extension.

### HOST text transfer: options `T` and `W` (UCSD-DEC-022)

Without options `GET` and `PUT` stay byte-preserving. UCSD Pascal stores text in blocks (a Pascal `.TEXT` file), not as a stream of lines, so two further options select an explicit conversion between OS text (plain ANSI/ASCII, the form used on Windows/Unix) and the Pascal TEXT block structure. The syntax is `GET <file> [T] [W] [O]` and `PUT <file> [T] [W] [O]`; the options may appear in any order, each at most once.

| Option | Meaning |
|---|---|
| `O` | Unchanged: replace an existing destination. |
| `T` | Text mode. `GET` converts an OS text file to a Pascal TEXT file; `PUT` converts a Pascal TEXT file to an OS text file. |
| `W` | Only together with `T` (otherwise the command is refused). Selects the line ending written by `PUT`: with `W` CR LF (Windows), without `W` LF (Unix). `GET` always accepts LF, CR LF and a lone CR as line end, so `W` has no effect there. |

Pascal TEXT layout written by `GET ... T` (the layout used by the repository's compiler-tested `.TEXT` staging code in `tests/apple2DiskTests.c`, and read back by the Editor and Compiler in the host-emulator guest tests):

- A 1024-byte header page (two blocks, zero-filled), followed by 1024-byte pages (two blocks each). The file is always a whole number of blocks (last-byte count 512).
- Each line is stored as DLE (`0x10`), `32 + number of leading spaces`, the remaining text, CR (`0x0D`). A line never crosses a page; the rest of the page is NUL padding and every page keeps at least one trailing NUL.
- A TAB expands to spaces up to the next multiple of 8 columns. Control characters other than the line end and all bytes above `~` (including UTF-8 multi-byte characters) become `?` and the number of replaced characters is reported. A last line without line end is kept; an empty source gives a header-only file.
- A line longer than 200 characters (after TAB expansion) aborts the `GET`; nothing is stored. Whether the Editor handles lines wider than 80 columns is not established.

`PUT ... T` reads the same layout: it skips the header page, expands DLE counts to spaces, writes each CR as the selected line ending and ignores NUL padding to the end of the page. It requires the Pascal file kind TEXT and refuses other files ("option T needs a Pascal TEXT file"). The CRC and the length sent to the exchange service cover the converted OS bytes; the converted length is found in a first pass over the file, the data are sent in a second pass.

File kind follows the Pascal name suffix, not the option: a `GET ... T` name must end in `.TEXT` (e.g. `SPIRO.TEXT`) to give a TEXT file the Editor and Compiler accept. The same name is used in the exchange directory and on the Pascal volume, so the PC-side file must also be named `*.TEXT` (rename on the PC). `GET` prints a note when the name does not end in `.TEXT`. A bare `<Return>` at the `HOST>` prompt (and any unknown command) prints the usage text; the program is left only with `QUIT` or `Q`.

## Reference and debugging strategy

Diagnose one first divergence: PC/registers, opcode/SYNC, cycles, visible ROM, Language Card bank/read/write state, slot access, controller mode, track/sector, image offset, keyboard strobe and video-card state. Pin ROM/media/reference hashes. Compare the same disk set before attributing a fault to the CPU or hardware.

Separate image-layout tests, executed synthetic controller tests, authentic guest boot, guest file persistence, firmware build and board execution. Keep current ProDOS autostart/SmartPort/80-column and missing-drive behavior as regressions; Pascal support must not reverse DEC-033/034 or reopen removed DOS tooling. Shared-component changes also retain applicable CP/M host-service regressions without importing CP/M runtime code.

## Milestones and acceptance gates

UCSD-M1–M10 retain their identifiers but are rebaselined by UCSD-DEC-015. They are **PLANNED / NOT RUN for native Pascal**; inherited component tests are prerequisites, not completed Pascal milestones.

| ID | Deliverable and exit evidence |
|---|---|
| UCSD-M1 | Confirm shared Apple II baseline, exact Apple Pascal release/media/ROM and provenance; inspect option-3 resource selection and current parser/controller policy |
| UCSD-M2 | Matched desktop reference boots, edits, compiles/runs and performs native file I/O; hashes, transcripts and configuration saved |
| UCSD-M3 | Document authentic boot/controller/order/unit mapping and Language Card/video behavior; identify only demonstrated compatibility gaps |
| UCSD-M4 | Both menu entries differ only in config.cfg selection; configured Pascal and ProDOS disks boot via the same machine path; swap/same-configuration and missing-SD6.1 regressions succeed |
| UCSD-M5 | Same boot on ESP32 using qualified resources; missing/invalid/RO/media-change cases observed; no silent fallback |
| UCSD-M6 | Guest Pascal work-volume writes, clean reopen, unit mapping, boundary/fault behavior and media ownership verified |
| UCSD-M7 | Full 80×24 edit/compile/run/save/restart/reopen on ESP32, control keys and source/string case checked |
| UCSD-M8 | Record larger-volume qualification or explicit supported-size limitation; no unsupported 640K/800K claims; enhancement may remain deferred |
| UCSD-M9 | Whole-image File Transfer round trip verified; native Pascal HOST utility implements `DIR`, `GET` and `PUT` through the common exchange protocol and passes guest tests (HOST part, including the `T`/`W` text options of UCSD-DEC-022: PASSED in the host emulator 2026-10-13, ctest `ucsdHostGuest`; whole-image round trip and ESP32 not verified) |
| UCSD-M10 | Repeated profile switching/reset, cleanup, recovery, memory/performance, deployment instructions and evidence logs complete |

Definition of Done for the core extension: a config.cfg/drives.cfg combination boots a qualified original Apple Pascal disk set on the shared Apple II 6502 machine from either menu entry; Language Card and existing display/input support a complete 80×24 development and persistent native-file workflow on ESP32; the ProDOS configuration and current SD policy regress successfully, including identical behavior for identical configurations from either entry; supported media and limitations are documented; no unexplained corruption or guest crash remains. A new machine emulator, restored DOS 3.3 stack, large-volume guest driver and native exchange utility are not required to meet this scope.

## Implementation record — documentation rebaseline, 2026-10-11

The 2026-10-11 design-only rebaseline reused the Apple record's motherboard, Language Card, Videx, multi-drive, writable-sector, ProDOS block-device, keyboard and exchange work. That record did not claim code changes, media deployment, successful Pascal boot, a selected release, new test execution or newly verified ROM rights.

The shared menu/profile routing, config.cfg resolution and current drives.cfg policy have now been inspected and integrated. Remaining work begins with selecting and hashing a complete original Apple Pascal release and desktop reference, then tracing its authentic boot and adding only demonstrated controller, ROM, Language Card or terminal compatibility fixes. The old “implement Apple II afterwards” sequence is retired. The inherited implementation and failed experiments are reproduced in Appendix B.

## Implementation record — shared profile routing, 2026-10-11

Menu option 3 selects `/littlefs/ucsd/config.cfg`; option 4 selects `/littlefs/apple2/config.cfg`. Both invoke the same Apple II machine initialization, run loop and Apple II core. A profile `config.cfg` accepts one `drives=/retro/images/<machine>/.../drives.cfg` line for Apple II or UCSD; comments and blank lines are allowed. The resolver rejects duplicate/unknown keys, paths outside those two image roots and paths that do not end in `drives.cfg`. Each referenced `drives.cfg` uses the SD4..SD7/SP4..SP7 grammar and validates image paths against the selected machine's roots.

The profile selectors are bundled in LittleFS, so deploying them does not require changing an existing SD card. The option-4 profile references the existing SD `drives.cfg` without changing its drive entries. The Pascal profile selects `/littlefs/ucsd/ApplePascal1-140K.dsk` read-only as SD6.1, `pascalWork2.po` read-only as SP5.1, `pascalWork1.po` read/write as SP5.2 and the empty `ApplePascal2-640K.po` read/write as SP4.1. The core and configuration parser support SmartPort block devices on slots 4 through 7, units 1 through 4; Pascal's ProDOS block interface exposes only two units per slot. The SD card must contain the matching `/retro/images/ucsd/drives.cfg`; a missing boot image or drive-config file makes menu option 3 report a missing resource. The drive-config parser permits safe read-only images below `/littlefs/ucsd/`, without changing the existing `/littlefs/apple2/system.dsk` restriction or Apple II SD image rules. File Transfer already maps its UCSD selector to `/retro/images/ucsd/`, so no GUI/server routing change is required. Host verification boots the Pascal 1.3 128K system, confirms Filer lists all three volumes, and opens `WORK1:SPIRODEMO.TEXT` from slot 5 unit 2; compiler workflow, filesystem writes and ESP32-S3 operation remain unverified.

At the time of this 2026-10-11 record, the ESP-IDF build succeeded and eleven selected Apple II and ProDOS host regressions passed, including assertions for both Pascal drive mappings, LittleFS write-protection and path traversal rejection. The native Pascal boot/editor, write/reopen and physical-board gates had not yet been run; the 2026-10-12 follow-up below records the later host boot/editor results.

In that same 2026-10-11 session, the ESP-IDF build succeeded and `apple2MultiDisk` passed, including the new profile-config parser cases. In the 12-test Apple II host subset, 11 passed; `apple2DiskStream` repeated the known image asset-open failure recorded under APPLE-VERIFY-033. Pascal boot/editor tests were not part of that run. The known image-asset failure does not establish a regression from the profile-routing changes.

## Implementation record — Pascal TEXT file open, 2026-10-12

The `minicom.txt` report's `ERROR: Reading file` was reproduced in the host
IIe harness while opening `WORK1:SPIRODEMO.TEXT` from a Pascal work volume.
Disk II sector reads had no callback failures, but the guest did not read the
file payload through that controller. Attaching the same 640K Pascal image as
slot-5 SmartPort instead opened the file and displayed `PROGRAM SPIRODEMO;`.
This isolates the issue to the configured controller interface, not a missing
TEXT file, Pascal 1.3 boot selection or `.po` sector-order decoding.

The core supports four SmartPort units per slot, and the configuration parser
accepts SmartPort block devices on slots 4 through 7. Pascal's tested ProDOS
block path exposes only units 1 and 2 per slot: a three-device profile all on
slot 5 did not enumerate unit 2; distributing the third device to slot 4 made
Filer list all three in the prior fixture. That fixture used DATA3 on SP5.1,
WORK1 on SP5.2 and WORK on SP4.1. The current source configuration uses
different image paths and RO/RW settings (see the configuration section
above), and the current Filer test fails its DATA3 assertion. The available
evidence does not yet establish the current configuration's guest volume
labels, compiler availability or writable-volume behavior. `apple2MultiDisk`
also currently fails because its assertions still expect the previous active
profile mapping. Compiler workflow, writes/reopen and ESP32-S3 remain
unverified; validate media contents and update the configuration/tests
together before hardware retesting.
`pascalWork2.po` named DATA3 with SYSTEM.PASCAL and SYSTEM.COMPILER,
`pascalWork1.po` named WORK1 with SPIRODEMO.TEXT, and
`ApplePascal2-640K.po` named WORK with no files. The Pascal regression now
lists all three volumes and opens WORK1 from slot 5 unit 2, using disposable
copies of both writable test images. `apple2MultiDisk` covers parser acceptance
through unit 4 and rejection of unit 5. Compiler workflow, writes/reopen and
ESP32-S3 remain unverified; copy the updated `drives.cfg` to the SD card before
retesting the hardware.

## Decision log

### Disposition of existing UCSD decisions

| ID | Current disposition | Reason / replacement |
|---|---|---|
| UCSD-DEC-001 | SUPERSEDED | Z80 II.0/menu 2/non-Apple target replaced by UCSD-DEC-007/008 |
| UCSD-DEC-002 | SUPERSEDED | Shared CP/M Z80 core replaced by existing Apple II 6502, UCSD-DEC-007 |
| UCSD-DEC-003 | RETAINED, reference retargeted | Desktop success still precedes ESP32 diagnosis; use matching Apple II Pascal, UCSD-DEC-011 |
| UCSD-DEC-004 | SUPERSEDED | No CP/M build/bootstrap/runtime dependency is required for this target, UCSD-DEC-007/009 |
| UCSD-DEC-005 | RETAINED IN PRINCIPLE, clarified | Immutable resources, SD work images and shared exchange retained; existing Apple roots first, UCSD-DEC-010/014 |
| UCSD-DEC-006 | RETAINED, release retargeted | Capacity derives from selected Apple Pascal stack; UCSD-DEC-013 |
| UCSD-DEC-019 | SUPERSEDED by UCSD-DEC-020 | Pascal exposes only two ProDOS block units per slot; distribute additional Pascal volumes across slots |

Original rows and old verification wording remain unchanged in Appendix A.

### New decisions — 2026-10-11

All following rows are design decisions from this request/document comparison, not implementation evidence.

| ID | Decision, alternatives, evidence and consequence |
|---|---|
| UCSD-DEC-007 | Run native Apple Pascal on the existing Apple II NMOS 6502. Reject a CP/M/Z80 backend, new machine emulator and host p-code VM. Evidence: current request and supplied Apple implementation record. Supersedes UCSD-DEC-001/002/004 and the old architecture/development sequence. |
| UCSD-DEC-008 | Menu 3 and menu 4 select different config.cfg files and invoke the same machine path. Their associated drives.cfg files alone define disks; boot media determines the OS. Reject menu-dependent OS modes and menu-2 startup. Evidence: explicit user clarification, 2026-10-11. Routing remains unimplemented/unverified. Supersedes conflicting old menu statements; see UCSD-DEC-016. |
| UCSD-DEC-009 | Boot original matching Apple media through existing reset/autostart/slot and on-disk loader. Reject COM-file loading, CP/M BIOS emulation and guessed PC/RAM bootstrap. Evidence: native Apple requirement and controller history. Qualify missing ROM entries before changing boot behavior. |
| UCSD-DEC-010 | Preserve APPLE-DEC-033/034: ProDOS baseline, explicit SD6.1, SD4.1..SD7.2, optional SP5.1..SP5.2, no PR aliases/default fallback. Add native Pascal media support to the same configuration-driven path for both menu entries. Reject restoring DOS 3.3 or assuming .po means ProDOS. This extends the earlier ProDOS-only media restriction while preserving ProDOS support, shipped configuration, SD grammar and no-default policy; neither menu entry enforces an OS. |
| UCSD-DEC-011 | Select a complete Apple Pascal release and original-Apple-II desktop reference before boot acceptance. Reject carrying Z80 II.0/z80pack acceptance into Apple Pascal. Evidence: no chosen Apple release or guest boot in supplied logs. Retargets UCSD-DEC-003. |
| UCSD-DEC-012 | Reuse 64 KiB physical memory/Language Card and existing slot-3 video/input. Add only traced Pascal compatibility gaps. Reject IIe auxiliary memory on the Apple II model (superseded for the optional IIe model by UCSD-DEC-017), synthetic Shift sensing and blanket ROM-address hooks. Evidence: APPLE-DEC-030/035/036 keyboard and open Pascal editor tests. |
| UCSD-DEC-013 | Qualify original-size media first; keep 640K/800K claims limited to evidence. Reject host-size-to-guest-capacity inference and a mandatory new large-disk driver. Evidence: APPLE-ISSUE-016/017 and VERIFY-024. Clarifies UCSD-DEC-006. |
| UCSD-DEC-014 | SUPERSEDED 2026-10-12 by UCSD-DEC-021. Original decision: use whole-image File Transfer first; defer optional native Pascal/6502 exchange work. |
| UCSD-DEC-015 | Preserve original logs verbatim and add explicit supersession/disposition; rebaseline M1–M10 without PASS promotion. Reject deletion of failed experiments and carrying old evidence into new acceptance. Evidence: both engineering-memory contracts. |
| UCSD-DEC-017 | 2026-10-12, implemented. Apple Pascal 1.3 128K (`128K.APPLE`/`128K.PASCAL`) requires IIe auxiliary memory, so the project gains an optional real Apple IIe model (`MACHINE=IIE`) instead of faking 128K on the Apple II. Alternatives rejected: running the 128K system on an Apple II + Saturn card (the system does not use Saturn banks), patching the system files, and replacing the Apple II model (existing Pascal 1.1 boot and tests must stay valid). Consequence: UCSD-DEC-012's rejection of IIe auxiliary memory applies to the Apple II model only. See APPLE-DEC-037/038. |
| UCSD-DEC-018 | 2026-10-12, implemented. Mount 640K Pascal work volumes as slot-5 SmartPort block devices (SP5.1/SP5.2), not Disk II sector drives (SD5.1/SD5.2). A host IIe test reproduces `ERROR: Reading file` for `WORK1:SPIRODEMO.TEXT` through Disk II and opens and displays its source through SmartPort. Keep the 140K Pascal boot disk on SD6.1. This does not establish ESP32-S3 compatibility or guest write support. |
| UCSD-DEC-019 | SUPERSEDED 2026-10-12 by UCSD-DEC-020. Original decision: extend slot-5 SmartPort to four 640K units and keep the active Pascal volumes on units 1–3. |
| UCSD-DEC-020 | 2026-10-12, implemented. Pascal 1.3's ProDOS block interface uses unit identifiers 1 and 2 per slot; more than two volumes on one slot are not enumerated. Keep DATA3 and WORK1 on SP5.1/SP5.2, and mount additional Pascal volumes on another free slot (SP4.1 in the tested profile). The emulator and config parser still support four SmartPort units per slot for software using the SmartPort command interface. Host Filer enumeration and file-open tests pass; firmware/hardware remains unverified. |
| UCSD-DEC-021 | 2026-10-12, implemented and guest-tested on the host emulator. Native UCSD p-System `HOST` program with the Apple host utility's `HOST>` interaction (`DIR`, `GET name [O]`, `PUT name [O]`) at `guest/ucsd/host/` (`HOST.PAS`, `HOSTIO.ASM`), using the existing Apple II emulator and slot-2 exchange protocol and `/retro/exchange/ucsd/`. Compiled, assembled and linked with the real Apple Pascal tools in the in-repository harness; the linked `HOST.CODE` is on `littlefs/ucsd/ApplePascal1-140K.dsk`. GET writes blocks with `BLOCKWRITE` and re-applies exact directory last-byte counts (the runtime directory cache would otherwise undo them). ctest `ucsdHostGuest` proves GET/PUT of empty, text and binary files, overwrite rules, errors and a disk-full abort. ESP32-S3 hardware is not tested. No separate p-code VM was added. See `ucsdHostLog.md`. |
| UCSD-DEC-022 | 2026-10-13, implemented and guest-tested on the host emulator. Extend the native `HOST` program with the text options `T` and `W` (`GET`/`PUT <file> [T] [W] [O]`): `T` converts between OS text and the Pascal TEXT block layout, `W` selects CR LF (default LF) for `PUT`; `GET` accepts any line ending. Alternatives rejected: automatic conversion by file name (would silently change binary files), a separate rename argument (not requested), heuristics on padding. Also: a bare `<Return>` now shows the usage text instead of leaving HOST (only `QUIT`/`Q` leaves). Layout, limits and file-kind rules: see "HOST text transfer" above. Evidence: ctest `ucsdHostGuest` (LF, CR LF, tab, non-ASCII, empty, multi-page, long-line and option-error cases; converted source compiled and run with the real Apple Pascal compiler; Editor opens a converted file; `PUT T` output compared byte for byte). Not tested: ESP32-S3 hardware, Editor save of a converted file, header-only files in the Editor. The new `HOST.CODE` (15 blocks) has replaced the old one on the boot image `littlefs/ucsd/ApplePascal1-140K.dsk` and was verified there: boot, `X APPLE1:HOST`, empty-Return usage, `GET T` and `PUT T W O`. |

### 128 KiB memory: Apple IIe model and Saturn 128K card (2026-10-12)

Apple Pascal 1.3 ships two system variants. The 64K `SYSTEM.APPLE`/`SYSTEM.PASCAL` runs on the original Apple II + Language Card (banner "Pascal system size is 64K"). `128K.APPLE`/`128K.PASCAL` on `ApplePascal1-140K.dsk` print "128K memory required" on an Apple II/II+, because the 128K system is the **Apple IIe** variant: its disassembly uses the IIe softswitches `$C001`, `$C002–$C005`, `$C006/7`, `$C008/9`, `$C00F` and reads the status registers `$C011–$C01C`. It does not use Saturn banks. Consequences:

- The Pascal profile selects the Apple IIe machine with `MACHINE=IIE` in its `drives.cfg` (see [designApple2.md](designApple2.md) "Apple IIe model (MACHINE=IIE)"). Booting `ApplePascal1-140K.dsk` as SD6.1 then prints "Welcome APPLE1, to Apple II Pascal 1.3", "Based on UCSD Pascal II.1" and **"Pascal system size is 128K"**.
- The IIe 80-column firmware at `$C300` is the Pascal console; the Pascal 1.1 card ROM/Videx path is not used in this model.
- `MACHINE=II` (default) keeps the original-machine behaviour: Pascal 1.1 (`ApplePascal1-680-0004-01.dsk`) with the 64K system, optionally with `RAMCARD=SATURN128`.
- Saturn 128K (slot 0, `RAMCARD=SATURN128`) is an optional original-II extension; Apple/UCSD Pascal 1.x does not use extra banks by itself, so it only helps software or drivers that know the Saturn protocol. Details and register map: [designApple2.md](designApple2.md) "128 KiB RAM options".
- Verification: see UCSD-VERIFY-015. Real hardware and editor/compiler use of the 128K system remain unverified.

### Configuration-only clarification — UCSD-DEC-016, 2026-10-11

**Decision:** only the selected `config.cfg` differs between menu options 3 and 4. The associated `drives.cfg` alone selects the disks; their boot contents determine Applesoft/ProDOS versus Apple Pascal. **Evidence:** explicit user clarification during this rewrite. **Alternative rejected:** a Pascal launch mode that changes CPU, controller selection, boot flow or terminal behavior based on the menu number. **Consequence:** one shared configuration-driven machine path, with same-configuration and swapped-disk regression tests. This clarifies UCSD-DEC-008/010 and supersedes any interpretation that menu 3 is restricted to Pascal or menu 4 to ProDOS. Exact configuration paths/resolution remain to be checked in source; no new config key is asserted here.

## Issue log

Each issue uses the engineering-memory chain. At this rewrite, Pascal experiments are **not run**, results are unknown, and no root cause or fix is asserted.

| ID | State and next evidence / do not repeat |
|---|---|
| UCSD-ISSUE-001 | RETARGETED OPEN: select matching Apple Pascal release/system/compiler disks and hashes; do not mix releases or reuse the old Z80 II.0 constraint |
| UCSD-ISSUE-002 | OLD LOADER QUESTION SUPERSEDED: PASCAL.COM/CP/M removal is no longer relevant; native slot boot is tracked in ISSUE-007 |
| UCSD-ISSUE-003 | OPEN: qualify actual Pascal editor/card/key behavior; a banner or BASIC PR#3 test is insufficient |
| UCSD-ISSUE-004 | RETARGETED OPEN: Apple Pascal block limits, native text and file metadata; no guessed capacity/encoding |
| UCSD-ISSUE-005 | OLD Z80 BINDING SUPERSEDED: optional Pascal/6502 exchange work deferred under ISSUE-012 |
| UCSD-ISSUE-006 | OPEN: inspect common launch lifecycle and implement config.cfg selection and common drives.cfg resolution without rewriting either configuration; test missing SD6.1 and failed-open cleanup |
| UCSD-ISSUE-007 | PARTIAL 2026-10-11: Pascal 1 boots to its startup/version screen in LinApple and now also in the host harness (`ucsdPascalBoot`: `Command: E(dit, R(un, F(ile, C(omp, L(ink...`, `Welcome APPLE1, to Apple II Pascal 1.1`, zero sector-read failures). Root causes in the project-authored slot ROM: (1) the requested sector (`$3D`) was not advanced after each read; (2) the 6-and-2 translate table was built at `$026A+diskByte`, while the Pascal boot code (blocks loaded at `$0800/$0900/$0A00`, disassembled from RAM) decodes directory sectors with `EOR $02D6,Y`, i.e. a table at `$036C..$03D5`. With the table elsewhere every sector decoded as zeros with a matching checksum, so `SYSTEM.APPLE` was never found (`NO FILE SYSTEM.APPLE`, ten retries of the directory read). Fix: `TABLE_BASE=$02D6` in `tools/buildApple2DiskBootRom.py`, regenerated `apple2DiskBootRom.h`; `testGuestBootSector` checks the table at `$02D6+diskByte`. Image order is DOS (.dsk, probe-confirmed); no image-order or Disk II stream defect was found. Still unverified: editor, compiler, file writes on SD6.2, Language Card use, ESP32-S3 behaviour (hardware not run). |
| UCSD-ISSUE-008 | OPEN: original-machine ROM identity/checksums/rights and primary Language Card reference; trace authentic Pascal bank use (APPLE-ISSUE-001/002/007) |
| UCSD-ISSUE-009 | PARTIAL 2026-10-11: Pascal 1.1 used its built-in 40-column driver because the slot-3 ROM carried no Pascal firmware-protocol identification. Evidence (disassembly of the running interpreter startup): Pascal scans slots $C7..$C1, checksums each ROM page and accepts a card only with `$Cn05=$38`, `$Cn07=$18` (type 4) and `$Cn0B=$01` (type 6, Pascal 1.1 protocol); the slot type is stored at `$BE67+$Cn` (slot 3: `$BF2A`). Fix: `tools/buildApple2PascalCardRom.py` generates `apple2PascalCardRom.h` (project-authored slot-3 page with those bytes and the init/read/write/status offsets `$0D..$10`, plus the entry routines at `$CA00`: control codes CR, LF with scroll, BS, FS, US, EM, FF, VT, GS, RS GOTOXY, ESC). A SYNC hook at the init entry `$C320` sets `pascalCardActive`, which switches off the Apple II-specific hacks that would corrupt Pascal data (CH/CV sync at `$24/$25`, CSW-based screen selection, boot-80 `$36/$37` conversion, lowercase-echo fix, `$FC58` HOME hook). Host result: `ucsdPascalBoot` now boots in 80x24 mode (`Command: E(dit, R(un, F(ile, C(omp, L(ink, X(ecute, A(ssem, D(ebug,? [1.1]` on row 0 of the card, mixed-case text, `$BF2A=$06`), and a `?` key through the card shows the second command line. Still unverified: editor/compiler on the card, all editor keys, ESP32-S3 hardware, real terminal key mapping. |
| UCSD-ISSUE-010 | OPEN: guest write persistence, RO behavior, volume swaps and interrupted-write recovery; host GCR write tests are insufficient |
| UCSD-ISSUE-011 | OPEN: documented SD/UCSD path routing and validated image order; use existing Apple roots until explicit host support exists |
| UCSD-ISSUE-012 | SUPERSEDED 2026-10-12 by UCSD-DEC-021: native Pascal exchange utility is now requested; implement and verify its binding, byte-preserving file behavior and UCSD exchange-root selection |
| UCSD-ISSUE-013 | OPEN: ESP32 latency/memory/throughput, repeated option 3↔4 launches and terminal restoration |
| UCSD-ISSUE-015 | PARTIAL 2026-10-12: Pascal 1.3 128K boots on the host IIe model. A prior fixture listed three block volumes and opened/displays `WORK1:SPIRODEMO.TEXT`; the current guest test fails its DATA3 volume assertion with the present worktree media. See UCSD-VERIFY-017. Not run: compiler, file writes, full editor workflow, ESP32-S3; IIe ROM redistribution rights remain unresolved. |
| UCSD-ISSUE-016 | RESOLVED for the host emulator 2026-10-12: `HOST.PAS`/`HOSTIO.ASM` compile, assemble and link with the mounted Apple Pascal tools; guest `DIR`/`GET`/`PUT`, CRC-32, overwrite and binary byte preservation pass (ctest `ucsdHostGuest`). Still OPEN: ESP32-S3 hardware, and a guest test with a corrupted checksum. |
| UCSD-ISSUE-014 | OPEN DOCUMENTATION DISCREPANCY: duplicate APPLE-DEC-036 IDs and stale menu/DOS/default prose; preserve source IDs, cite date/topic, follow explicit latest decisions |

Inherited Apple issues retain their original IDs in Appendix B. DOS-image procurement/removed-tool issues are historical for this scope, not reasons to reinstate DOS. Large Pascal controller support, firmware identity and hardware tests remain open; the hardware-confirmed RGB LED fix is retained as its specific recorded result, not proof of Pascal compatibility.

## Verification log

### Disposition of previous UCSD verification

| ID | Current meaning |
|---|---|
| UCSD-VERIFY-001 | Historical Z80 source-review record only; not Apple Pascal evidence; preserved verbatim |
| UCSD-VERIFY-002 | NOT RUN for selected Apple Pascal desktop reference and workflow |
| UCSD-VERIFY-003 | Old direct-Z80-bootstrap test superseded; new native boot and no CP/M runtime check under VERIFY-010 |
| UCSD-VERIFY-004 | NOT RUN: ESP32 Pascal boot/80×24 editor |
| UCSD-VERIFY-005 | NOT RUN: guest Pascal persistence, capacity and faults |
| UCSD-VERIFY-006 | NOT RUN / optional utility deferred: whole-image transfer remains required; native file/text conversion only if implemented |
| UCSD-VERIFY-007 | NOT RUN: repeated launch/reset, memory and performance |

### New verification records

| ID | Status | Evidence / remaining action |
|---|---|---|
| UCSD-VERIFY-008 | DOCUMENT REVIEW, 2026-10-11 | Both supplied Markdown files read in full; architecture and later Apple decisions reconciled. This is not source-code review or execution. Source fingerprints below; no firmware commit, board or terminal applies. |
| UCSD-VERIFY-009 | NOT RUN | Matched desktop reference: boot, editor, compile/run, file write/read, saved artifacts and hashes |
| UCSD-VERIFY-010 | PARTIAL 2026-10-11 (host harness only) | `ctest -R ucsdPascalBoot`: Pascal 1 (`ApplePascal1-680-0004-01.dsk`, SD6.1 slot-6 Disk II, DOS order) boots through the project slot ROM to the Pascal command line with zero sector-read failures. Loader/Language Card trace and ESP32-S3 boot NOT RUN. |
| UCSD-VERIFY-011 | NOT RUN | ProDOS and Pascal configurations through either entry, identical-config equivalence, swapped-config behavior, SD grammar, no-default errors and shared controller dispatch |
| UCSD-VERIFY-012 | PARTIAL HOST 2026-10-11 | `ucsdPascalBoot` host test: 80x24 card is the Pascal console (Pascal 1.1 firmware protocol), one key (`?`) round trip passes; not run: Pascal source case, all editor keys, bank-aware hooks, ESP32-S3 |
| UCSD-VERIFY-013 | NOT RUN | ESP32 guest save/reopen, media swaps, RO/errors, image bounds and power-loss recovery |
| UCSD-VERIFY-015 | PARTIAL HOST 2026-10-12 | `apple2DiskTests --pascal-iie-boot littlefs/ucsd/ApplePascal1-140K.dsk littlefs/apple2/apple2e.rom` boots the 128K system on the IIe model to "Command: F(ile, E(dit, R(un, C(omp, L(ink, X(ecute, A(ssem, ?  [1.3]" and "Pascal system size is 128K"; `hostTests` `testApple2IIe` and `testApple2Saturn128` pass. Not run: ESP32-S3, editor/compiler, 128K-specific behaviour. |
| UCSD-VERIFY-016 | PARTIAL HOST 2026-10-12 | Earlier `apple2DiskTests --pascal-iie-editor-open` runs proved the editor opens `WORK1:SPIRODEMO.TEXT` from a SmartPort block device; the same image failed with `ERROR: Reading file` through SD5.1 Disk II. This is host evidence only. |
| UCSD-VERIFY-017 | PARTIAL HOST; REGRESSION IN CURRENT WORKTREE | Earlier on 2026-10-12, `apple2DiskTests --pascal-iie-editor-open` booted Pascal 1.3 with DATA3 on SP5.1, WORK1 on SP5.2 and WORK on SP4.1; Filer listed all three and the editor displayed `PROGRAM SPIRODEMO;` after opening `WORK1:SPIRODEMO.TEXT`. Three devices on slot 5 did not enumerate unit 2; distributing the third to another slot fixed that fixture. Re-run against the present modified images now fails the `DATA3:` screen assertion. The `apple2MultiDisk` active-profile assertions also fail after `drives.cfg` changed. Confirm actual image headers/content and bring tests/configuration into agreement before using this historical pass as current evidence. Writes and ESP32-S3/hardware behaviour remain unverified. |
| UCSD-VERIFY-018 | PARTIAL HOST 2026-10-13 | `ctest -R ucsdHostGuest`: after the build stage, `GET ... T` converts LF, CR LF, tab, UTF-8, empty, 150-line multi-page and unterminated-last-line sources; the Pascal compiler compiles and the guest runs a converted program (`TEXT OK`); the Editor displays a converted file; `PUT ... T [W]` output equals the expected LF/CR LF text byte for byte; option errors, over-long line, non-`.TEXT` note and bare `<Return>` usage verified. ESP32-S3 hardware and Editor save/reopen are not verified. |
| UCSD-VERIFY-014 | NOT RUN | Whole-image File Transfer byte/hash round trip; optional native utility qualified separately |

Inherited host evidence: APPLE-VERIFY-024/025 covers image/controller subsets; VERIFY-028 covers Language Card; VERIFY-030/031 covers ProDOS guest boot/save in the host harness; VERIFY-032 records the ProDOS-only transition; VERIFY-033 covers keyboard translation and explicitly notes an `apple2DiskStream` asset-open failure. These runs occurred at different historical states. Do not combine them into a claim that the current suite is clean or Pascal works. Preserve unrelated recorded CP/M test failures as evidence, without assigning them to Pascal changes.

## Next implementation session

Inspect the current repository and governing project contract, map options 3 and 4 to the same Apple II launch path with only config.cfg selection differing, and select/hash original Apple Pascal media. Establish the desktop reference before changing boot behavior. Inspect current controller selection and path rules, run the authentic loader with trace capture, and fix only the first demonstrated compatibility gap. Then qualify Language Card use, native writes and 80×24 editing on host and ESP32 while retaining ProDOS/configuration regressions. Append findings and evidence; do not restart the retired Z80 design.

## Source fingerprints and archival policy

The following SHA-256 values identify the supplied files, not a repository revision:

- `designUCSD.md`: `464a0537a2f2c219661a2f75473ce500b96651609e90a196a09b0223a3c91d45` (187 lines).
- `designApple2.md`: `fc333ebfe2d0c798e26516a09c2d9d9f52c6dbe31720be9526118ee34b30bf19` (437 lines).

Appendix A preserves the original UCSD decision, issue and verification logs, including obsolete conclusions. Appendix B preserves the complete Apple record from “Phase 1 implementation record” through its final “Next implementation session”, including implementation follow-ups, decision details, failed experiments, all issue rows, all verification rows and extended evidence. Text inside each archival block is unchanged. Earlier design prose is reconciled above; neither appendix overrides the current architecture or disposition tables.

## Appendix A — Original UCSD logs (historical, non-normative)

<details>
<summary>Original UCSD decision, issue and verification records</summary>

````markdown
## Decision log

| ID | Status | Decision |
|---|---|---|
| UCSD-DEC-001 | Preserved | Z80 p-System II.0, menu 2; not Turbo Pascal or Apple Pascal |
| UCSD-DEC-002 | Preserved | Shared proven Z80 core and historical p-code interpreter |
| UCSD-DEC-003 | Preserved | Desktop reference success precedes ESP32 debugging |
| UCSD-DEC-004 | Preserved | CP/M only for historical build/bootstrap/reference, not runtime |
| UCSD-DEC-005 | Updated | Minimal LittleFS boot, larger/writable native volumes on SD, common exchange |
| UCSD-DEC-006 | Required | Volume maximum derives from this release's complete stack, not host size |

## Issue log

| ID | State | Problem / next evidence / do not repeat |
|---|---|---|
| UCSD-ISSUE-001 | OPEN | Select exact matching II.0 interpreter and volumes; never mix release artifacts |
| UCSD-ISSUE-002 | OPEN | Trace PASCAL.COM loader and direct boot contract; do not assume BIOS implies resident CP/M |
| UCSD-ISSUE-003 | OPEN | Verify terminal configuration with editor; do not accept a banner as full-screen compatibility |
| UCSD-ISSUE-004 | OPEN | Derive volume/block limits and native text format from chosen release |
| UCSD-ISSUE-005 | OPEN | Freeze Pascal/Z80 exchange calling convention and metadata handling |

Append Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat to each issue as work occurs.

## Verification log

| ID | Status | Evidence required |
|---|---|---|
| UCSD-VERIFY-001 | SOURCE-REVIEW | z80pack repository and maintainer's II.0 availability reviewed 2026-10-04; no runtime claim |
| UCSD-VERIFY-002 | NOT RUN | Desktop edit/compile/run and exact resources |
| UCSD-VERIFY-003 | NOT RUN | Direct bootstrap, absence of runtime CP/M dependency |
| UCSD-VERIFY-004 | NOT RUN | ESP32 system boot and 80×24 editor |
| UCSD-VERIFY-005 | NOT RUN | Native volume persistence, large-volume limits and fault behavior |
| UCSD-VERIFY-006 | NOT RUN | Raw binary round trip, text conversion and interrupted transfer |
| UCSD-VERIFY-007 | NOT RUN | Repeated launch/reset, memory and performance |
````

</details>

## Appendix B — Apple II engineering memory (historical snapshots, non-normative)

<details>
<summary>Original Apple II implementation, decisions, issues and verification evidence</summary>

````markdown
## Phase 1 implementation record — 2026-10-09

Implemented the motherboard-only subset requested for phase 1. The firmware now integrates the pinned `floooh/chips` NMOS 6502 core, 48 KiB RAM, a 12 KiB ROM window at `$D000–$FFFF`, the keyboard latch/strobe at `$C000/$C010`, motherboard text/video softswitches `$C050–$C057`, and a 40×24 text-memory renderer. The core is clock-paced near 1.023 MHz. Host tests use a synthetic test ROM and do not establish authentic ROM behavior.

Terminal refresh follow-up — 2026-10-09: the Apple II terminal renderer compares each 40-character row with its last successful output and writes only changed rows, instead of continuously redrawing all 24 lines. USB writes handle short writes and retry boundedly when the transmit queue temporarily has no space; a real stalled write is logged once per failed call, and the affected row remains eligible for redraw.

This phase intentionally excludes the Language Card, expansion 80-column video card, disk controllers/drives and graphics rendering. It also does not implement the remembered lowercase character ROM or Shift/PB2 modification. Unmodeled `$Cxxx` I/O uses the last data-bus value; accurate floating-bus timing is not implemented.

The machine requires `/littlefs/apple2/apple2.rom`, exactly 12 KiB with a reset vector in `$D000–$FFFF`. The selected system ROM is `assets/apple2_roms/apple.rom`; host-side execution reaches the `APPLE ][` prompt, accepts `PRINT 2+2`, and displays `4`. Its 12 KiB size and reset vector match the implemented ROM window. Redistribution rights have not been verified. A separate project-authored diagnostic fixture at `tests/fixtures/apple2-diagnostic.rom` can be regenerated with `python3 tools/buildApple2TestRom.py`. Phase 1 still does not establish DOS, Applesoft, Pascal, physical firmware behavior or ESP32-S3 performance.

The core source and its unmodified zlib license are pinned in `docs/thirdParty.md`. The implementation does not mark APPLE-M1 or the later execution/compatibility milestones as accepted.

This scoped coding work was explicitly requested before the design's HOST-M1, CP/M-80 and CP/M-86 acceptance prerequisites were closed. It does not waive those gates or establish that the integrated host has completed acceptance.

## Language Card implementation follow-up — 2026-10-10

Added 16 KiB Language Card RAM to the core, with two separately selected `$D000–$DFFF` banks and shared `$E000–$FFFF` RAM. The `$C080–$C08F` switch decode controls bank selection and ROM/RAM read mapping; odd switch reads implement the two-access write enable, odd switch writes do not unlock it, and even switch accesses disable writes. Reset restores the ROM/read-only mapping while retaining card contents. `tests/hostTests.c --apple2-language-card` checks both banks, shared-memory boundaries, ROM visibility, write protection, the two-read sequence, reset behavior, and an executing 6502 program. Host test evidence does not establish primary-manual accuracy, UCSD/ProDOS compatibility, or ESP32-S3 hardware behavior.

## Videx-compatible 80-column card — 2026-10-09

Added a slot-3 Videx-compatible device profile to the Apple II core. It models the `$C0B0–$C0BF` CRTC/bank-select I/O, 2 KiB of card VRAM in four 512-byte banks, the `$C300` slot ROM entry and `$C800` expansion-ROM window. The project-authored slot ROM supports `PR#3`, installs an output vector, configures an 80×24 CRTC text mode, and writes printable characters, carriage returns and backspaces into card VRAM. The host terminal switches to 80 columns only while that card output vector is selected; the 40-column motherboard renderer remains available.

PR#3 input follow-up — 2026-10-09: standard BASIC sets the output vector to the slot entry at `$C300`; it does not call the previous `$C303` initialization entry. The slot entry now initializes the card on its first output and then chains to the installed character routine while preserving that first character. The system-ROM host test types `PR#3` and then `PRINT 2+2`, verifying keyboard input, command echo and result on card VRAM.

Screen follow-up — 2026-10-09: when the project COUT driver advances beyond row 23, the card screen scrolls its 24×80 character buffer up one row and clears the new bottom row. Rendering clears the host terminal on machine entry and whenever BASIC changes the active output between `PR#0` and `PR#3`, redraws the selected guest screen, and positions the terminal cursor at the next COUT cell. The Videx host test drives output through a scroll and verifies retained lines, the new bottom row and cursor position; the system-ROM host test switches `PR#3` → `PR#0` → `PR#3` and checks output selection.

Applesoft output follow-up — 2026-10-09: the project COUT routine now preserves the incoming accumulator, including its high bit, around its screen write; Applesoft uses the returned value while expanding tokens for `LIST`, so masking it permanently truncated keywords to their first letter. Screen clearing now detects changes to either byte of the output vector, since `PR#0` changes the low byte before the high byte. The system-ROM regression confirms full `PRINT` output in both 40- and 80-column modes, clean screen contents after each transition, a visible prompt and an in-bounds cursor.

Scheduler follow-up — 2026-10-09: when a CPU batch exceeded its 1 ms pacing interval, `vTaskDelayUntil` could remain behind schedule and return immediately on subsequent iterations. That catch-up loop could starve CPU 0's idle task and trigger the task watchdog during long-running BASIC programs. The machine loop now yields for one RTOS tick when late and resets its pacing baseline instead of trying to catch up unboundedly. Hardware timing/performance still needs verification.

Host regression evidence: the synthetic 6502 test ROM executes the card's `PR#3` entry, writes 80 distinct characters, verifies carriage return output, exercises VRAM banks 2 and 3, scrolls multiple lines and checks the cursor, and switches the host-visible output state away from the card. The system-ROM test exercises full Applesoft `LIST` output in both display modes and clean mode transitions. These are not Videx-ROM or raster-accurate tests. The minimal driver uses private emulated state for Apple II zero-page bytes `$06–$0A` and does not implement character-generator fonts, CRTC cursor effects, or graphics. This is not evidence for behavior on the physical ESP32-S3 or the historical TU Delft card.

## Decision log

|ID           |Status                   |Decision / reason                                                                                                 |
|-------------|-------------------------|------------------------------------------------------------------------------------------------------------------|
|APPLE-DEC-001|Retained                 |Menu 3, native ESP-IDF, shared HOST-M1, KIM-1 removed                                                             |
|APPLE-DEC-002|**SUPERSEDED 2026-10-08**|Previous unenhanced IIe/auxiliary-RAM baseline replaced by expanded **original Apple II**                         |
|APPLE-DEC-003|Candidate                |External proven NMOS 6502 core, `floooh/chips` candidate; validate and pin                                        |
|APPLE-DEC-004|Revised                  |Language Card bank switching, **not IIe auxiliary video RAM**, supplies extra 16 KiB                              |
|APPLE-DEC-005|Retained                 |Terminal renderer reflects guest video memory/card state; not ROM print interception alone                        |
|APPLE-DEC-006|Retained                 |LittleFS minimal immutable resources, SD larger/writable images                                                   |
|APPLE-DEC-007|**SUPERSEDED 2026-10-08**|ProDOS-specific large-disk requirement removed; custom virtual controllers are independent of ProDOS              |
|APPLE-DEC-008|Revised                  |Shared hostExchange; guest file adapters for DOS 3.3 and/or UCSD Pascal, not ProDOS                               |
|APPLE-DEC-009|Revised                  |Versioned Apple DOS/Pascal metadata policy, exact encoding TBD                                                    |
|APPLE-DEC-010|Required                 |Autostart Monitor with Integer BASIC ROM; Applesoft may auto-load **from suitable system disk** into Language Card|
|APPLE-DEC-011|Required                 |80×24 via independent slot card; historical TU Delft card unidentified; Videx only a candidate                    |
|APPLE-DEC-012|Required                 |Lowercase character-ROM plus separate keyboard Shift/PB2 modification                                             |
|APPLE-DEC-013|Required                 |Virtual 5¼-inch and configurable larger/8-inch image profiles; no physical drives                                 |
|APPLE-DEC-014|Required                 |No Z80 and no ProDOS requirement                                                                                  |
|APPLE-DEC-015|Implemented subset       |Phase 1 is motherboard-only: 6502, 48 KiB RAM, ROM window, keyboard and 40-column text; no Language Card, expansion video or disk controller |
|APPLE-DEC-016|Selected 2026-10-09      |Videx-compatible slot-3 profile with project-authored minimal `PR#3` firmware; not a claim about the TU Delft card |
|APPLE-DEC-017|Implemented 2026-10-09   |Preserve the Applesoft COUT accumulator contract and clear screens on either output-vector byte changing         |
|APPLE-DEC-018|Implemented 2026-10-09   |Apple DOS 3.3 image builder copies a supplied DOS-order base image and inserts supported files; generated media is not claimed bootable |
|APPLE-DEC-020|Implemented 2026-10-09   |Project-authored read-only Disk II (slot 6, drive 1, 16-sector DOS 3.3) with bounded sector reads from `/littlefs/apple2/system.dsk`; host-emulated only |
|APPLE-DEC-021|Implemented 2026-10-09   |Superseded the DEC-019 disk content: `littlefs/apple2/system.dsk` is built from the emptied base `bootDisks/apple2/dos33Empty.dsk` (DOS 3.3P boot tracks, empty catalog) plus everything in `bootDisks/apple2/systemDsk` (`HELLO.BAS`, `TEST-NONGR.BAS`). The `--remove-all-existing` option and `allow_locked` were removed as too complicated. `HELLO` runs `PR#3`, so the 80-column card is active at boot and `FILE NOT FOUND` is gone. Evidence: APPLE-VERIFY-021, APPLE-VERIFY-022 |
|APPLE-DEC-022|Implemented 2026-10-09   |Videx 80-column card under Apple DOS 3.3: DOS swaps the output vector (CSW `$36/$37`) for its hook `$9EBD` on every key read and keeps the real vector at `$AA53/$AA54`. Output selection is now tracked in `videxOutputSelected`, tolerant of this hook swap and of half-written vectors, so the card no longer shows a blank screen under DOS. The card follows `HTAB`/`VTAB`/`POS` through CH/CV (`$24/$25`) and the monitor `HOME` (`$FC58`) clears the 80-column screen; going back to 40 columns resets CH/CV/BASL/BASH. Evidence: APPLE-VERIFY-022 |
|APPLE-DEC-023|Implemented 2026-10-09   |The 80-column card is selected automatically at machine start, like the Disk II boots, instead of by `PR#3` in `HELLO`. `apple2CoreSetBootIn80Columns(core, true)` (called by `apple2Machine.c` before `apple2CoreLoadRom`) makes every reset program the card's CRTC registers, set CSW `$36/$37` to `$C8B3`, select the card and clear its screen, and makes the monitor SETVID routine (`$FE93`–`$FEAF`, entered by reset and by the DOS boot code, not by `PR#n`) store the card vector instead of `$FDF0`. `HELLO.BAS` no longer runs `PR#3`. Alternatives rejected: a host-injected `PR#3` keystroke sequence and a hook at the slot scan (`$FAA9`), which is skipped on a warm reset and does not survive the later DOS SETVID calls. Evidence: APPLE-VERIFY-023 |
|APPLE-DEC-024|Implemented 2026-10-10   |Several Disk II controllers: slots 4..7 (boot ROM `$Cn00`, I/O `$C080+n*16`), two drives per controller, each with its own head position, medium, sector order and track count. A controller answers only when at least one of its drives has a medium. `apple2DiskBootRom.h` holds one project-authored boot ROM per slot, generated by `tools/buildApple2DiskBootRom.py` from the same verified code with only the slot number changed. Evidence: APPLE-VERIFY-024 (host only) |
|APPLE-DEC-025|Implemented 2026-10-10   |640K medium = 655,360 bytes = 160 tracks x 16 sectors x 256 bytes = 1280 Apple Pascal blocks of 512 bytes, presented as a virtual Disk II with 160 tracks (half-tracks 0..319, flattened 80 cylinders x 2 sides). The track count is chosen by the image profile `APPLE2_140K`/`APPLE2_640K`; the image size must match it exactly. Evidence: APPLE-VERIFY-024 (host only) |
|APPLE-DEC-026|Implemented 2026-10-10   |Sector order is explicit per drive. DOS order (`.do`/`.dsk`) and ProDOS/Pascal order (`.po`) are both supported; this uses the ProDOS block layout only as a sector layout and adds no ProDOS software (DEC-014 stands). Order comes from `.po`/`.do`, else from a content probe (DOS 3.3 VTOC plus two catalog sectors, or an Apple Pascal volume header in directory block 2), else DOS. A disagreement between the chosen order and the probe is logged as a warning, not an error. `drives.cfg` accepts RO/RW profiles for slots 4..7 and drives 1..2 below `/littlefs/apple2/` or `/retro/images/apple2/`; the built-in `/littlefs/apple2/system.dsk` must remain RO. `SD6.1` defaults to the system image; any error restores the defaults. RW was initially refused and is superseded by DEC-027. Evidence: APPLE-VERIFY-024/025 (host only) |
|APPLE-DEC-027|Implemented 2026-10-10   |Non-system Apple II images can be RW. Disk II write-mode bytes are decoded from a 6-and-2 data field, mapped back through the drive's DOS or ProDOS/Pascal sector order, written in place without changing image size, and flushed before success. The built-in `system.dsk` remains RO; failed/protected writes are counted and logged. Host tests verify persistence after reopen, sector-order mapping, RO enforcement and malformed-write refusal. Guest-software and hardware writes remain unverified. Evidence: APPLE-VERIFY-025|
|APPLE-DEC-028|Optional host + firmware implementation 2026-10-10|After the user reported that DOS 3.3 returned `DISK FULL` on a 640K unit and selected ProDOS 8/SmartPort, add an optional slot-5 SmartPort block interface while retaining the DOS 3.3 system disk/default. `SP5.1`/`SP5.2` map 512-byte callbacks to configured bounded images, including ProDOS-order block mapping, RO/RW and unit-status responses; slot 5 cannot host both Disk II and SmartPort. A host ProDOS 8 blank-volume builder is provided. Alternatives were to keep large media limited to custom DOS/Pascal software or require no extra guest OS; both leave the reported 640K ProDOS use case inaccessible. This is not a requirement or a ProDOS filesystem emulation. No prior decision is superseded; DEC-014 remains the default-policy constraint. Host API, image and build evidence is in APPLE-VERIFY-026; ProDOS boot, catalog/save and hardware verification are deferred by APPLE-DEC-029.|
|APPLE-DEC-029|Selected 2026-10-10      |Do not resume end-to-end ProDOS 8/SmartPort boot or filesystem testing until the Apple II's 16 KiB Language Card is implemented. The current emulated machine has 48 KiB motherboard RAM and no Language Card; the user clarified that ProDOS requires the additional 16 KiB. Alternatives were to continue diagnosing the boot image/controller now (deferred because the required memory hardware is absent) or to treat host SmartPort tests as proof of guest usability (rejected). Evidence: APPLE-VERIFY-026/027 and user clarification 2026-10-10. Consequence: host-side SmartPort and image-I/O tests remain valid, but boot/catalog/save claims are blocked until Language Card implementation and subsequent guest verification. This does not make ProDOS a default or required system.|
|APPLE-DEC-030|Implemented 2026-10-10|Add the slot-0 Language Card memory map and `$C080–$C08F` access semantics. The switch decode follows AppleWin's `LanguageCard.cpp` slot-0 model at revision `d9b2bf6787bc853be15652098dbf8666acedc6e6`: A3 selects bank 2/1, A1=A0 selects Language Card RAM for reads, two odd-switch reads enable writes, and an even access disables writes. Host tests verify this mapping and an executing NMOS 6502 sequence. This source audit is not a primary hardware reference; APPLE-ISSUE-002 remains partial pending primary-reference and hardware confirmation. This unblocks further ProDOS/SmartPort boot diagnosis but does not establish guest compatibility. APPLE-DEC-029 is superseded only as a blocker to resuming that experiment.|
|APPLE-DEC-031|Implemented 2026-10-10|Boot ProDOS 8 from the SmartPort slot and use 800K as the large-volume size. (a) Slot 5 gets project-authored boot code at `$C56B` (reached from the `$Cn08` boot entry) that reads block 0 of unit `$50` to `$0800`, sets X=`$50` and jumps to `$0801`; the Applesoft autostart only boots slots with `$Cn07=$3C`, so the guest starts it with `PR#5`. (b) The SmartPort trap now fires only on an opcode fetch (SYNC) at `$C50A`/`$C50D`, answers unit 0 / code 0 with an 8-byte controller status, and block transfers go through the Language Card mapping for buffers at `$D000-$FFFF` (writes honour the write-enable switch; `$C000-$CFFF` is rejected). (c) The Disk II boot ROMs now load X=slot*16 (`LDX $2B`) before jumping to the boot sector, as ProDOS expects. (d) The user asked to base large volumes on 800K rather than 640K: the new profile `APPLE2_800K` (819,200 bytes = 200 tracks x 16 sectors x 256 = 1600 512-byte blocks) is added, `apple2DiskMaxTracks` is raised from 160 to 200, and the ProDOS volume builder defaults to 800K. The 640K profile is retained for existing images (APPLE-DEC-025). Alternatives rejected: emulating the original P5A ROM (copyright/provenance, APPLE-ISSUE-002 style risk) and keeping 640K as the ProDOS default. DOS 3.3 and Pascal cannot use more than 140K/their own limits (APPLE-ISSUE-016); 800K is not guaranteed to work with them.|
|APPLE-DEC-032|Implemented 2026-10-10|ProDOS images become block devices in any controller slot and ProDOS can boot from `SD6.1`. After the user reported an unusable DOS-only system (DOS `DISK FULL` on the 800K unit, ProDOS only through `PR#5`), a slot whose images include ProDOS content (probe: ProDOS volume header, `.po` or DOS-order `.dsk`) is attached as a ProDOS block device instead of a Disk II controller (`SP5.x` stays the explicit SmartPort form). The block-device slot ROM keeps the signature bytes, ProDOS entry `$Cn0A`, SmartPort entry `$Cn0D` and the project boot code; with `$Cn07=$3C` the Autostart ROM boots it at reset and ProDOS then treats it as a generic block device. Both drives of a ProDOS slot are block devices, so DOS 3.3 and ProDOS images must not share a slot. The firmware `system.dsk` can be a ProDOS volume: `createSystemDsk.py --os apple2 --profile PRODOS` (base `bootDisks/apple2/prodosEmpty.po`, files mapped to ProDOS names, `HELLO.BAS` becomes `STARTUP`) and the CMake cache variable `APPLE2_SYSTEM_DISK_PROFILE` (default `DOS33`, so the existing default is unchanged). `buildDiskImage.py --os apple2` builds ProDOS 140K/640K/800K volumes (`--bootable`, `--source-dir`) and both tools print an English reminder to rebuild and flash (system.dsk) or to upload via "File Transfer" (SD images). Alternatives rejected: emulating the original P5A Disk II boot ROM (provenance, see APPLE-ISSUE-002 risk) and keeping `PR#5` as the only ProDOS boot path. Supersedes APPLE-DEC-031 (a) only where it says the guest must start ProDOS with `PR#5`. Evidence APPLE-VERIFY-031.|
|APPLE-DEC-033|Implemented 2026-10-10|The Apple II uses ProDOS disks only and has no default drive. All DOS 3.3 code was removed (`tools/diskImageApple2Dos33.py`, `bootDisks/apple2/dos33Empty.dsk`, the DOS VTOC/catalog probe, the `DOS33` profile, `--remove-existing`, the DOS tests and the CMake cache variable `APPLE2_SYSTEM_DISK_PROFILE`); `createSystemDsk.py --os apple2` always writes the ProDOS `/SYSTEM` volume. `assets/Apple DOS 3.3P.dsk` stays in the repository but is unused. `apple2DriveConfig` leaves every drive empty when drives.cfg has no line for it, so SD6.1 must be listed explicitly (`SD6.1=/littlefs/apple2/system.dsk,RO,APPLE2_140K`). If SD6.1 is not configured (also when drives.cfg is missing or invalid) or its image cannot be opened, `apple2MachineInitialize` prints an error and returns `ESP_ERR_NOT_FOUND`, so the system menu is shown again. The shipped drives.cfg lists `/SYSTEM` (SD6.1, RO), two 800K read/write data volumes `/DATA1` (SD6.2) and `/DATA2` (SD5.1) and a Pascal 640K image in slot 4. Supersedes the `DOS33` default of APPLE-DEC-032 and every earlier statement that SD6.1 defaults to `system.dsk`.|
|APPLE-DEC-034|Implemented 2026-10-10|The `drives.cfg` controller prefix `PR` is renamed to `SD`: `SD6.1`, `SD5.2` etc. Reason: the guest `CATALOG` command refers to `Sn`/`Dn`, so the host handle `SD<slot>.<drive>` matches `S<slot>,D<drive>`. `PR<slot>.<drive>` is no longer accepted (error: `controller must be SD4.1..SD7.2 or SP5.1..SP5.2`); `SP5.<unit>` is unchanged and the guest command `PR#n` is unrelated and unchanged. Existing `drives.cfg` files on the SD card must be edited. Alternatives rejected: accepting both spellings. Supersedes the `PR` spelling in every earlier entry (which are rewritten to `SD`). |
|APPLE-DEC-035|Implemented 2026-10-10|The 80-column card now works under ProDOS BASIC.SYSTEM. Causes: (1) BASIC.SYSTEM resets CSW from `$BE10` to COUT1 (`$FDF0`) at start-up, which deselected the card; in boot-80 mode a CSW write of `$FDF0` by code outside the monitor ROM is now stored as `$C8B3`, while PR#0 (monitor OUTPORT) is still honoured; (2) BASIC.SYSTEM swaps its own hook (here `$B85B`) into CSW on every key read and keeps the real vector at `$BE30/$BE31`, so output selection treats a hook in `$9000..$BEFF` as the card when `$BE30/$BE31` is `$C8B3`; this stops the card screen and workspace (cursor) being cleared on every input line; (3) the HOME trap at `$FC58` fired on ProDOS code running in the Language Card RAM and wiped the card screen during CATALOG; it now only applies while the monitor ROM is visible. Evidence: `apple2ProdosAutostart80Columns` (host only, not on hardware). |
|APPLE-DEC-036|Implemented 2026-10-11|Terminal cursor keys reach the guest. `main/apple2Keyboard.c` decodes `ESC [ A–D` and `ESC O A–D` (normal and application cursor mode; parameters such as `ESC [ 1;5C` ignored) to Left `$08`, Right `$15`, Up `$0B`, Down `$0A`; Left/Right match the original Apple II keys, Up/Down follow the IIe convention because the original keyboard has none. Other `ESC [`/`ESC O` sequences (Delete, Home, ...) are dropped. `ESC` followed by any other byte is delivered as two keys so Apple ESC editing (`ESC I/J/K/M`) still works; a lone `ESC` is delivered after 50 ms. All other control characters `$01–$1F` pass unchanged; CR/LF pair gives one CR, DEL becomes `$08`. UCSD Pascal editor key assignments are not yet checked.|
|APPLE-DEC-036|Implemented 2026-10-10|Implement the requested ProDOS 8 `HOST` transfer utility, not the earlier DOS 3.3/UCSD direction. Keep the common protocol and expose it on Apple II slot 2 at `$C0A0`/`$C0A1`; provide `DIR`, wildcard `GET`/`PUT`, explicit `O` overwrite, bounded streaming, exact lengths and CRC-32, and safe temporary-file handling. Store cc65 source and the runnable SYS binary in `bootDisks/apple2/utilities`; `HOST.SYS` (load address `$2000`) reaches the system disk only by being placed in `bootDisks/apple2/systemDsk`; the builder injects nothing. Alternatives rejected: keep the ProDOS utility only on removable exchange media or retain the earlier DOS 3.3 design, neither of which satisfies the user's ProDOS and feature-parity requirements. Supersedes APPLE-DEC-008/009/014 only for Apple II guest transfer utility and ProDOS use; it does not change disk-controller defaults or claim metadata fidelity. Host test/build evidence is recorded in the task result; guest and hardware execution remain unverified.|

New decisions must include date, alternatives, evidence, consequences and superseded IDs. Historical owner recollection is evidence for requirements, not proof of a particular controller/ROM implementation.

APPLE-DEC-015 details: dated 2026-10-09; the alternatives were to keep Apple II as a placeholder until all acceptance prerequisites were closed, or implement the requested motherboard subset while retaining those acceptance gates. This implementation follows the explicit phase-1 request. Evidence is the host-side synthetic-ROM test and ESP-IDF build; neither authentic ROM execution nor hardware behavior has been observed. The generated phase-1 test ROM is a separate original diagnostic, not the historical ROM image. It supersedes no historical machine-profile decision.

APPLE-DEC-016 details: dated 2026-10-09; alternatives were to require a user-provided Videx ROM dump or implement a project-authored minimal slot driver. The selected profile follows the explicit user choice. The ROM source is project-authored; no Videx firmware dump is bundled. It supports the standard Videx memory/register layout and `PR#3` terminal output. Host tests validate the CPU-executed slot ROM, scrolling and 80-column VRAM, not original card firmware, raster output, the historical TU Delft card, or ESP32-S3 hardware. This supersedes the "Videx only a candidate" portion of APPLE-DEC-011 for software-profile selection; the historical card identity remains unknown.

APPLE-DEC-017 details: dated 2026-10-09; investigation reproduced two independent `PR#3` failures with the authentic system ROM: Applesoft `LIST` rendered tokenized BASIC words as their first letters, and switching output modes left stale screen contents. The first cause was that the project COUT routine stripped the high bit from the incoming accumulator and returned the altered value; Applesoft relies on that value while expanding tokens. The second cause was that transition detection watched only the high byte of the output vector, while `PR#0` changes its low byte first. The chosen fix preserves and restores the complete accumulator around the Videx write, isolates the card routine's `$06–$0A` workspace from motherboard RAM, and detects selection changes on writes to either output-vector byte. Alternatives were to special-case or post-process `LIST` output, or clear only the host terminal; these would not preserve the guest ROM's output contract or prevent stale guest memory from being redrawn. Host system-ROM regression evidence now confirms full `PRINT` text on both screens, clean contents after `PR#0`/`PR#3` switches, a visible prompt, and in-bounds cursors. This does not establish compatibility with unrelated third-party slot firmware or hardware.

APPLE-DEC-018 details: dated 2026-10-09; alternatives were to synthesize DOS 3.3 boot/system software, download an unverified disk image, or require a supplied 16-sector DOS 3.3-order base and add guest files to it. The third option was selected to avoid claiming a synthetic or unverified image is bootable or fetching additional Apple DOS software. `assets/apple2_roms/APPLE_IIe_ROM_KRK.zip` contains a 143,360-byte `romkrk.dsk`; the DOS 3.3 VTOC/catalog check recognizes it and lists three ROM-patch files, but the archive notes identify it as Apple IIe ROM-hacking media and do not establish bootability. The user authorized reusing this candidate for the generated image; that authorization is not evidence of bootability. `tools/createSystemDsk.py --os apple2 --profile DOS33 --base-image <image>` accepts a supplied base and inserts `.TXT`, tokenized `.BAS`/`.INT`, and `.BIN` files; `.TXT` line endings are normalized to DOS carriage returns, and raw binaries require a load address. The generated `littlefs/apple2/system.dsk` adds `TEST.TXT` to the candidate and retains its ROM-patch files. Tests and catalog inspection establish image structure and file insertion, not startup. Runtime Disk II reads and a verified boot remain unimplemented/unverified; APPLE-M7 stays open.

|APPLE-DEC-019|Selected 2026-10-09|Keep the full DOS 3.3P base except for the user-approved unlocked `LOCKSMITH 4.1`; catalog the test Applesoft program as `TEST-NONGR`|

APPLE-DEC-019 details: dated 2026-10-09; `assets/Apple DOS 3.3P.dsk` has a valid DOS 3.3 catalog but zero free sectors. The user approved removing only unlocked `LOCKSMITH 4.1` from the generated copy and requested `TEST-NONGR` as the guest filename. Alternatives were to omit the test program or use another disk; the selected approach retains all other base files. The builder now removes only explicitly named, unlocked files, frees and validates their VTOC sectors, and preserves the supplied base image. The `.BAS` host suffix remains a source type marker but is omitted from the DOS catalog; its file type remains Applesoft, for `LOAD TEST-NONGR` followed by `RUN`. The generated disk has 21 catalog entries including `TEST-NONGR`, 20 free sectors, and no `LOCKSMITH 4.1`. This proves image/catalog structure and tokenized program insertion only; the base's bootability and runtime Disk II support remain unverified.

System disk handoff — 2026-10-09: APPLE-VERIFY-019 is the current generated-image state and supersedes the earlier `TEST.TXT`/`romkrk.dsk` output description in APPLE-DEC-018; retain DEC-018 as historical context. `littlefs/apple2/system.dsk` is generated from `assets/Apple DOS 3.3P.dsk`, excluding only the user-approved unlocked `LOCKSMITH 4.1`, and adds `TEST-NONGR` from `bootDisks/apple2/systemDsk/TEST-NONGR.BAS`. Regenerate using the command in `littlefs/apple2/README.txt`. The Applesoft program is intended to be invoked on a functioning guest disk setup as `LOAD TEST-NONGR` then `RUN`. Do not add `drives.cfg` to the disk: this design's first milestone is one fixed read-only drive. [SUPERSEDED 2026-10-09 by APPLE-DEC-020: the host-emulated read-only Disk II and image mount now exist; the statement that follows is retained as history.] The current firmware has no Disk II controller or image mount, so do not claim disk boot/read is working. Continue with APPLE-ISSUE-005: implement and test the project-authored read-only 16-sector controller from published hardware behavior, then integrate bounded LittleFS sector reads and test sectors before boot.

APPLE-DEC-020 details: dated 2026-10-09; scope was one fixed read-only drive (drive 1), slot 6, 35 tracks x 16 sectors x 256 bytes, image `/littlefs/apple2/system.dsk`. Alternatives were a sector-API shortcut in the ROM/monitor (rejected: guest software must use the real softswitches), or a copied controller ROM (rejected: the boot ROM is project-authored, see below). Implementation: `components/apple2Core/apple2Disk.[ch]` (controller, no file I/O; it asks the host through the `apple2DiskReadSectorFunction` callback for one 256-byte sector at a time and GCR-encodes it into a 416-nibble buffer, so the image is never loaded into RAM); `main/apple2DiskImage.[ch]` (size check, bounds check, `imageFile` read-only access); `main/apple2Machine.c` (`attachDisk`/`releaseDisk`: the image is attached at the end of machine initialisation and released before the core is destroyed and when the run loop stops; a missing or invalid image logs ESP_LOGW "no Disk II media: <path>: <reason>" and the machine continues without a drive); `apple2Core.c` (softswitches `$C0E0–$C0EF`, slot-6 ROM at `$C600–$C6FF` gated on an attached medium). The slot-6 boot ROM is generated by `tools/buildApple2DiskBootRom.py` into `components/apple2Core/apple2DiskBootRom.h` (251 of 256 bytes), is project-authored (autostart signature bytes, head recalibration, track-0 sector reader, jump to `$0801`) and is not derived from any existing controller ROM. Never edit the generated header by hand.

Sources consulted this session (the archives were read, not the original paper books): the Big Mess o' Wires article "The Amazing Disk II Controller Card" (softswitch map, Q6/Q7 modes, 32-cycle nibble, write-protect sense); Apple's `Software_control_of_IWM.pdf` on bitsavers (only for the IWM motor-off delay of about 1 s); the Wikipedia article "Disk II"; web search results quoting Beneath Apple DOS and Sather "Understanding the Apple II" for the address/data field layout and the DOS 3.3 sector skew table; a GitHub issue discussing Disk II soft switches. The controller behaviour was then checked against the real guest software: `assets/Apple DOS 3.3P.dsk` boots through the emulated interface and its own RWTS reads 229 sectors without error. That is behavioural evidence for the subset DOS uses, not a proof of hardware equivalence.

Timing approach: the core cycle counter drives the disk. One nibble takes 32 CPU cycles, a track has 6656 nibbles (416 per sector x 16), and the angular position is `(cycles / 32) % 6656`, never reset. A read in a new 32-cycle window loads the latch with the next nibble (bit 7 set); repeated reads in the same window return `latch & 0x7F`. Sectors rotate in ascending physical order; the on-disk order is converted with the DOS-order skew table `{0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15}`. CPU timing and instruction semantics are unchanged (the only core change is a `cycles++` per tick). Motor coast-down: after the motor-off switch the medium keeps delivering nibbles for 1,000,000 cycles (about 1 s). Reason: DOS 3.3 RWTS decides whether the motor is already running by comparing successive `$C08C,X` reads; without the coast-down every call saw a constant value and waited about 1 s, which made the boot take over 30 M cycles and left a keypress pending. With the coast-down the boot takes about 4.7 M cycles. The 1,000,000-cycle value is inferred from that observed RWTS behaviour; only the IWM 1 s delay was confirmed by a source, and the RWTS latch check was recalled from memory, not verified in a primary source.

Behaviour: phase on-events move the head one half-track toward the net pull of the energised magnets (half-track 0..69, clamped; odd half-tracks and tracks >= 35 read as `$FF` nibbles). Motor `$E8/$E9`, drive select `$EA/$EB`, Q6 `$EC/$ED`, Q7 `$EE/$EF`. Data read (Q6 low, Q7 low, offset `$C`) returns the latch; motor stopped (after coast-down) or drive 2 selected returns `$00`; Q7 high returns `$00`; Q6 high reports `$80` for RO media and `$00` for RW media. With Q7 high/Q6 low, writes to offset `$C` are scanned for the 6-and-2 data prologue, decoded and checksum-validated, then mapped from the rotating physical sector to the configured image order and flushed through the host callback. `writeAttempts` and `writeFailures` track complete data-field attempts and rejected/failed writes.

Still uncertain (not verified against a primary source or hardware): the Q6/Q7 write-protect-sense semantics and the latch validity window; the simplified stepper model including the head clamp during recalibration; whether real RWTS write timing tolerances match this fixed 32-cycle model; the motor coast-down value. Host tests verify the sector-write path and persistence, but DOS/ProDOS/Pascal guest writes and hardware behavior have not been tested. Unsupported: 13-sector media, half-track and quarter-track data, spiral/protected disks, Language Card interactions with writes.

System-disk finding (APPLE-ISSUE-014): `LOAD TEST-NONGR` completes (program end `$1032`, 2097 bytes), but `RUN` does not work on the shipped `system.dsk` because the generated file lacks the 2-byte length prefix that Applesoft DOS files carry and because the token table skips `&` ($AF). This is a builder defect, not a Disk II defect. A scratch image built with in-memory patches for both defects ran the program in DOS through the emulated Disk II and printed the TEST 1 result "OK"; TEST 2 then printed an error that is not explained (a direct `PRINT SIN(3.14159265/2);" ";COS(0);" ";TAN(0)` gives `.999999999 1 0`, so the interpreter maths is fine; possibly another tokenizer issue). [RESOLVED 2026-10-09, see APPLE-ISSUE-014 and APPLE-VERIFY-021: builder fixed, system.dsk regenerated, RUN completes with `ALL SYSTEM TESTS OK` in the host emulator.]

Resource follow-up — 2026-10-09: the missing menu resource was `/littlefs/apple2/apple2.rom` (12 KiB). Added that path as a generated project-authored phase-1 diagnostic image so the implemented CPU, text screen and keyboard path can be exercised without copyrighted firmware. Its deterministic generator is `tools/buildApple2TestRom.py`. A host integration regression boots this exact image, checks its screen text and verifies a keyboard echo. This removes the missing-resource gate for phase-1 testing; it does not satisfy APPLE-M1 ROM provenance or later guest-software milestones.

System ROM follow-up — 2026-10-09: replaced the bundled runtime diagnostic image with the 12 KiB `assets/apple2_roms/apple.rom` image (SHA-256 `378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249`), which matches the machine ROM window and boots the `APPLE ][` prompt in the NMOS 6502 core. The host regression types `PRINT 2+2`, verifies the echoed command, result `4`, and return to the BASIC prompt. The former diagnostic image remains under `tests/fixtures/`, and its generator now defaults there so it cannot overwrite the system ROM. This is host-core evidence only; no ESP32-S3 flash or hardware test was run, and redistribution rights for the source ROM remain unresolved.

APPLE-DEC-024..026 details: dated 2026-10-10; request: read more disks, start with 640K, respect the Apple (UCSD) Pascal volume layout. Alternatives for the slot number: one boot ROM that reads its own slot (rejected: would change the code verified in slot 6) or a ROM per slot from one generator (selected). Alternatives for 640K: an 800K-style 3.5-inch or SmartPort/ProDOS block device (rejected: DEC-014, and unmodified guests would not use it) or 160 virtual tracks behind the existing Disk II softswitches (selected: needs no new guest-visible interface, but see APPLE-ISSUE-016). The core asks the host for (track, image-order sector); only the core's physical-to-image table depends on the order, so `apple2DiskImage.c` reads and writes file offset `(track*16+sector)*256` for either order. A ProDOS/Pascal block n is the two sectors `2*(n%8)` and `2*(n%8)+1` (ProDOS logical order) of track `n/8`. Apple Pascal volume header: directory block 2, little-endian words first block 0, next block 6, kind 0, name length 1..7 plus name at +6, block count at `$0E`, file count at `$10`. `tools/diskImageApple2Pascal.py` builds a blank volume of either size. Consequences: the machine log reports per attached drive the slot, drive, image, order, detected content, Pascal name and block count, and a suspect-order warning. DOS 3.3 order probing needs a second catalog sector because sector 15 maps to the same file sector in both orders; a disk whose catalog has only two sectors, or an image with neither layout, reports the order as unknown and falls back to DOS order. `drives.cnf` in `sdcard/retro/images/apple2/` is an empty tracked file that looks like a typo; it was left untouched and `drives.cfg` was added next to it. Configured non-system images now support RW; `system.dsk` remains RO (DEC-027).

## Issue log

|ID             |State|Issue / next experiment                                                                              |
|---------------|-----|-----------------------------------------------------------------------------------------------------|
|APPLE-ISSUE-001|OPEN |Obtain exact original Apple II Autostart/Integer BASIC ROM images and checksums/licensing            |
|APPLE-ISSUE-002|PARTIAL 2026-10-10|Language Card switch model and standalone host regression implemented; confirm `$C080–$C08F` truth table against a primary hardware reference and physical behavior|
|APPLE-ISSUE-003|PARTIAL|Project lowercase text/input mapping selected and host-tested; identify the historical character ROM. PB2 Shift sensing is not connected in this project setup|
|APPLE-ISSUE-004|PARTIAL|Videx-compatible slot-3 profile selected; qualify full editor/scroll/mode behavior and actual TU Delft card remains unknown|
|APPLE-ISSUE-005|PARTIAL|16-sector Disk II read/write path implemented and host-tested (APPLE-DEC-020/027, APPLE-VERIFY-025). Still open: qualify guest writes against software/hardware or a reference emulator, the uncertainties listed in DEC-020, and 13-sector media|
|APPLE-ISSUE-006|OPEN |Identify DOS 3.3 System Master image that disk-loads Applesoft into Language Card; trace startup     |
|APPLE-ISSUE-007|OPEN |Obtain bootable UCSD Pascal image and verify Language Card use                                       |
|APPLE-ISSUE-008|OPEN |Define 8-inch/custom large virtual controller contract and image geometries without assuming 640 KiB |
|APPLE-ISSUE-009|OPEN |Align proposed `drives.cfg` profiles/mappings with actual host parser and layout manifest            |
|APPLE-ISSUE-010|OPEN |Define DOS/Pascal exchange metadata and optional guest transfer utility ABI                          |
|APPLE-ISSUE-011|OPEN |Measure ESP32-S3 terminal throughput, keyboard latency and storage performance                       |
|APPLE-ISSUE-012|PARTIAL 2026-10-11|Ctrl-C reaches the guest and breaks Applesoft (host test). In minicom the command key is Ctrl-A (or Option/Meta with `-m`, which sends `ESC`+key): the minicom 2.11.1 man page lists no Ctrl-C command, so the earlier local interception is unexplained (not reproduced here; check the terminal app and `stty`). With `-m`, `Option+<key>` (= `ESC <key>`) is consumed by minicom, so Apple `ESC I/J/K/M` must be typed as Esc, pause, key. Hardware check outstanding|
|APPLE-ISSUE-013|OPEN |Obtain a legally sourced bootable DOS 3.3 16-sector-order image; qualify file insertion and boot once Disk II support exists |
|APPLE-ISSUE-014|FIXED 2026-10-09|`tools/diskImageApple2Dos33.py` / `tools/apple2Basic.py` produce an unrunnable `TEST-NONGR`: no 2-byte length prefix on tokenized Applesoft files, and the token table omits `&` ($AF) so every token $AF or above is one too low. Fixed: the builder now prepends the length, the token table follows the ROM token order ($80 END to $EA MID$, including `&`, DEL, ROT=, FLASH, STORE) and the REM check uses the shifted REM token; `tests/test_createSystemDsk.py` pins the corrected payload and checks every token value. Two further defects were then found in the test program itself (not in the builder or Disk II): `ABS`-less `Y<>1` fails because the ROM's `COS(0)` is `.99999999977` (exponent $80, mantissa `7F FF FF FF`), and the error handler at line 500 sat in the program flow between TEST 5 and TEST 6, so execution ended there. Both were fixed in `TEST-NONGR.BAS` (handler moved to line 9000, `ABS(Y-1) > 0.00001`)|
|APPLE-ISSUE-015|CLOSED (hardware)|User reports the RGB LED (GPIO 38) blinks red while the Apple II reads the read-only `system.dsk`, while CP/M shows green. The code path to the LED is identical (`diskActivityRead()` queues green, GRB byte order), so the root cause is NOT identified. Mitigations made 2026-10-09 (not verified on hardware): `diskActivity.c` now transmits only on a colour change (the Apple Disk II loads a sector every ~12.5 ms, so the LED used to be re-sent ~80x/s) and uses a 300 us reset gap instead of 80 us; `apple2Machine.c` calls `diskActivityWrite()` only when `apple2CoreGetDiskState().writeAttempts` increases, so only a refused write attempt can turn it red. Check on hardware: does it still turn red on pure reads? If yes, suspect WS2812 timing glitches while the emulator runs and report back. Added 2026-10-09: temporary logging under `DISK_ACTIVITY_DEBUG_LOG` in `diskActivity.h` (set to 0 to silence): `LED request <colour>` when a colour is requested, `LED transmit <colour>: bytes G R B` when the RMT frame is sent, and `Disk II write attempt` in `apple2Machine.c`. In the host emulator boot, `CATALOG`, `LOAD` and `RUN` give 0 write attempts, so a red LED on pure reads would not come from emulated writes: if the log shows `request GREEN`/`transmit GREEN` while the LED is red, suspect the hardware/RMT/GRB path; if it shows `request RED`, a write attempt was logged. Finding 2026-10-10 (user observation on hardware): the start-up self-test sent red, green, blue as GRB bytes and the LED showed green, red, blue, i.e. this LED takes RGB byte order, not GRB; the log showed only `GREEN (read)` requests with `G=32` and no write attempts. `diskActivity.c` now sends RGB order (fix applied, awaiting hardware confirmation for both Apple II and CP/M). Hardware confirmation 2026-10-10: Apple II and CP/M both show green on read and red on write after the RGB-order change; `DISK_ACTIVITY_DEBUG_LOG` set to 0 (self-test and logging off) |
|APPLE-ISSUE-016|OPEN |Stock DOS 3.3 and Apple Pascal are not known to address 160 tracks or a 640K Disk II; verify with a suitable guest driver or reference emulator, or define the guest-side driver. Until then the 640K drive is only proven at the nibble/sector level in the host test|
|APPLE-ISSUE-017|OPEN |Pascal block-device access (ATTACH-style driver for the character/block firmware protocol) is future guest-side work and cannot be verified without that software|
|APPLE-ISSUE-018|FIXED 2026-10-10|Non-system multi-drive Disk II image writes, persistence and RO/RW configuration are implemented and host-tested (APPLE-DEC-027, APPLE-VERIFY-025). Guest-software writes, hardware behavior and 13-sector media remain unverified (APPLE-ISSUE-005)|
|APPLE-ISSUE-019|OPEN |Boot ROMs for slots 4, 5 and 7 are generated and checked as bytes only in the host test (slot 6 is the only one executed by a guest); no guest software has booted from them. Hardware behaviour unverified|
|APPLE-ISSUE-020|PARTIAL 2026-10-10|ProDOS 8 boot, catalog and BASIC `SAVE` through SmartPort now pass in the host harness (APPLE-DEC-031, APPLE-VERIFY-030). Root causes of the earlier failure: (1) the project-authored Disk II boot ROM left X=`$54`, so ProDOS booted from slot 5; (2) ProDOS block 0 re-enters the original P5A Disk II ROM (`$Cn5C`, bytes `$Cn63..$CnEA`), which this project-authored ROM cannot provide, so ProDOS cannot boot from a Disk II image (not supported; use `PR#5` from SP5.1); (3) the SmartPort trap fired on operand fetches at `$C50A` and had no unit-0 controller STATUS; (4) block buffers in Language Card space (`$D000-$FFFF`) were rejected. Still open: hardware verification; the STATUS code 3 device-information-block layout was not audited against a primary reference; other ProDOS versions and DOS 3.3-compatible use are untested. Update: ProDOS now also boots from `SD6.1` through the autostart block-device slot (APPLE-DEC-032, APPLE-VERIFY-031); the Disk II boot path itself remains unable to start ProDOS.|

Use: Reference behaviour → Hypothesis → Experiment → Result → Conclusion → Root cause → Fix → Regression verification → Do not repeat.

## Verification log

|ID              |Status                |Evidence / remaining action                                                                                                                                     |
|----------------|----------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
|APPLE-VERIFY-001|HISTORICAL REQUIREMENT|2026-10-08 owner clarified original Apple II, Integer BASIC, auto-boot, 16 KiB Language Card, disk-loaded Applesoft, Shift/lowercase and TU Delft 80-column card|
|APPLE-VERIFY-002|HISTORICAL REQUIREMENT|2026-10-08 owner clarified double-sided/double-density 8-inch dual-drive setup; controller/geometry unknown; no physical drives planned                         |
|APPLE-VERIFY-003|DESIGN REVIEW         |2026-10-08 prior IIe/ProDOS design conflicts identified and superseded; no emulator tests performed                                                             |
|APPLE-VERIFY-004|HOST PASS (subset)   |`cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-system-rom`: ROM reset reaches `APPLE ][`; keyboard input runs `PRINT 2+2` and displays `4`. Hardware/Autostart acceptance remains open.|
|APPLE-VERIFY-005|PARTIAL HOST PASS     |The Language Card standalone regression now passes; hardware acceptance, primary-reference comparison and UCSD/ProDOS use remain outstanding. Host tests also cover lowercase glyph decoding, digit preservation and case-preserving keyboard input.|
|APPLE-VERIFY-006|HOST PASS (subset)   |System-ROM host test verifies complete Applesoft `LIST` output, screen clearing across `PR#0`/`PR#3`, prompt visibility and in-bounds cursors; detailed reproducibility and environment are recorded under APPLE-VERIFY-010; hardware acceptance remains open.|
|APPLE-VERIFY-007|PARTIAL HOST (DOS boot only)|DOS 3.3P boot and `LOAD` observed in the host emulator, see APPLE-VERIFY-020; disk-loaded Applesoft into a Language Card and UCSD Pascal NOT RUN|
|APPLE-VERIFY-008|NOT RUN               |Virtual disks, exchange, durability, ESP32 hardware and recovery                                                                                                |
|APPLE-VERIFY-009|HOST PASS (subset)   |2026-10-09: `cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-videx-card`; synthetic 6502 ROM executes project `PR#3` firmware, verifies 80 distinct VRAM cells, CR, banked VRAM, scrolling, cursor and output-vector mode. Hardware, authentic Videx firmware/raster behavior and historical TU Delft card remain unverified.|
|APPLE-VERIFY-010|HOST + BUILD PASS    |2026-10-09: full host suite passed 4/4 and ESP-IDF build succeeded after the Applesoft/output-vector fixes; reproducibility and scope below. No flash or physical-board test was performed.|
|APPLE-VERIFY-012|SUPERSEDED           |Earlier keyboard behavior claims were incomplete; see APPLE-VERIFY-015.|
|APPLE-VERIFY-013|HOST + BUILD PASS    |2026-10-09: lowercase screen decoding, digits `1234567890`, and system-ROM `PR#3` path passed before adding virtual Shift sensing. No physical-board test.|
|APPLE-VERIFY-014|SUPERSEDED           |The tests showed lowercase commands execute, not that the stock ROM preserves lowercase screen output. The case-derived `$C063` behavior described here was removed; see APPLE-VERIFY-015.|
|APPLE-VERIFY-015|SUPERSEDED           |Keyboard data/strobe separation passed, but stock-ROM lowercase echo remained uppercase; the limitation is resolved in APPLE-VERIFY-016.|
|APPLE-VERIFY-016|PARTIAL HOST + BUILD|2026-10-09: immediate lowercase/uppercase key echo is verified in motherboard and Videx modes, and lowercase Applesoft command input still works. This does not prove lowercase Applesoft string output or LIST preservation; the remaining case loss is recorded in APPLE-VERIFY-017. Full host suite and ESP-IDF build passed. No board execution.|
|APPLE-VERIFY-017|PARKED / USER REPORTED|2026-10-09: user reports `print "abcdefghijkm"` renders `ABCDEFGHIJKM` and `10 print "Pietje Puk"` is stored/listed as `10  PRINT "PIETJE PUK"`. Immediate lowercase key echo works, but quoted string contents still lose case. No implementation or validation performed for this issue; resume with exact PRINT/LIST assertions in both display modes.|
|APPLE-VERIFY-018|HOST PASS (image builder)|2026-10-09: 14 focused builder tests pass. A generated 143,360-byte candidate from the user-authorized `romkrk.dsk` contains `TEST.TXT` plus three existing ROM-patch files; text line endings are DOS carriage returns. Catalog and VTOC inspection pass. This does not verify disk boot or runtime Disk II reads.|
|APPLE-VERIFY-019|HOST PASS (image builder)|2026-10-09: 21 focused builder tests pass. Generated `littlefs/apple2/system.dsk` from `assets/Apple DOS 3.3P.dsk`, removed only unlocked `LOCKSMITH 4.1` from the copy, and inserted tokenized Applesoft type-A `TEST-NONGR` with 20 free sectors remaining. VTOC/catalog inspection passes; boot and runtime Disk II reads are not verified. [Update 2026-10-09: host-emulated boot and reads are now covered by APPLE-VERIFY-020.]|
|APPLE-VERIFY-020|HOST + BUILD PASS    |2026-10-09: read-only Disk II. 7/7 host tests pass (`apple2Disk`, `apple2DiskDosStream`, `apple2DiskDosBoot` are new) and `idf.py build` succeeds. DOS 3.3P boot and `LOAD TEST-NONGR` observed in the host emulator; `RUN TEST-NONGR` does NOT work (APPLE-ISSUE-014). No flash, no hardware.|
|APPLE-VERIFY-021|HOST PASS (emulator)  |2026-10-09: regenerated empty-catalog `system.dsk` (only `TEST-NONGR`); `LOAD TEST-NONGR` and `RUN` print all eight tests OK and `*** ALL SYSTEM TESTS OK! ***` in the host emulator through the Disk II (asserted by `apple2DiskDosBoot`). Builder tests 23/23 in `test_createSystemDsk.py`. RGB LED colour on hardware: NOT checked. Hardware tested: NO.|
|APPLE-VERIFY-022|HOST PASS (emulator)  |2026-10-09: 80-column card active at boot via `HELLO`, `LOAD`/`RUN` of `TEST-NONGR` shows `ALL SYSTEM TESTS OK` on the 80-column screen, `PR#0` returns to 40 columns, `PR#3`, `HOME`, `VTAB`/`HTAB` positioning checked in `tests/apple2DiskTests.c` (`apple2DiskDosBoot`); 7/7 ctest, Python builder tests pass. Not proven: hardware, the terminal rendering, the LED fix (APPLE-ISSUE-015), firmware build (`idf.py` not available in this session) |
|APPLE-VERIFY-023|HOST + BUILD PASS    |2026-10-09: `cmake --build build-host --parallel`; `ctest --test-dir build-host --output-on-failure` (7/7); `idf.py build` completed; nothing flashed. `apple2SystemRom` (`testApple2BootIn80Columns`) boots without a disk with the option: card active, `]` prompt on the 80-column screen, `PRINT 2+2`, `PR#0`, `PR#3`, then an explicit `apple2CoreReset` returns to the active card. `apple2DiskDosBoot` boots `system.dsk` with the option and `HELLO` printing without `PR#3`. Not proven: hardware, the terminal rendering, and the LED colour (APPLE-ISSUE-015) |
|APPLE-VERIFY-024|HOST + BUILD PASS    |2026-10-10: multi-drive/640K/Pascal. 8/8 host tests (new `apple2MultiDisk`), `idf.py build` completed, 6/6 Python tests for the Pascal volume builder. Host evidence only; hardware tested: NO|
|APPLE-VERIFY-025|HOST + BUILD PASS    |2026-10-10: writable Apple II image support; all 8 host tests pass and the ESP-IDF build succeeds. Write-field decoding, ProDOS-order mapping, flush/reopen persistence, write-protect behavior, malformed-write refusal and RO/RW config are tested. No flash, guest-software write or hardware test.|
|APPLE-VERIFY-026|HOST + BUILD PASS    |2026-10-10: optional ProDOS 8/SmartPort block-device path. All 8 host tests and all 6 ProDOS formatter tests pass; ESP-IDF build succeeds. Host 6502 calls verify SmartPort signature, ReadBlock/WriteBlock, write-protect and out-of-range errors; image tests cover ProDOS volume probing, DOS/ProDOS block ordering, RO/RW bounds and persistence. Guest boot, filesystem writes, flash and hardware were not verified in this run.|
|APPLE-VERIFY-027|HOST DIAGNOSTIC / BLOCKED|2026-10-10: optional `apple2DiskTests --prodos-smartport-boot 'assets/apple2Downloads/appleDisks/ProDOS 2.4.2.dsk' <generated-640K-volume>` experiment. The screen remained at the `*` monitor after the 60-cycle batches; slot-6 state reported track 0, 16 sector reads and zero read failures. The user clarified that ProDOS requires the 16 KiB Language Card, which is not implemented in the current 48 KiB machine; therefore this result is not evidence of a SmartPort or boot-media defect. Defer diagnosis and guest boot/catalog/save tests until after Language Card implementation (APPLE-DEC-029). No flash or hardware test.|
|APPLE-VERIFY-028|HOST + BUILD PASS (Language Card)|2026-10-10: `cmake --build build-host --parallel && ctest --test-dir build-host --output-on-failure` passes 9/9; ESP-IDF build succeeds. Synthetic-ROM checks cover reset mapping, both 4 KiB banks, shared RAM through `$FFFF`, ROM/read selection, write protection, two odd-switch reads, read-versus-write unlock behavior, reset retention and a real NMOS 6502 switch/write/read sequence. Primary manual, UCSD/ProDOS guest boot and hardware behavior remain unverified; nothing was flashed.|
|APPLE-VERIFY-029|HOST DIAGNOSTIC / OPEN|2026-10-10: repeated `apple2DiskTests --prodos-smartport-boot` after adding the Language Card, with a generated 640K data volume and the boot image tested once as `.dsk` and once as `.po`. Both runs returned to the `*` monitor after 16 track-0 reads and zero read failures; the emulated CPU did not access `$C080–$C08F`. This does not validate or disprove the Language Card and does not establish a SmartPort defect. Boot investigation remains open (APPLE-ISSUE-020); no flash or hardware test.|
|APPLE-VERIFY-030|HOST + BUILD PASS|2026-10-10: `apple2DiskTests --prodos-smartport-boot <ProDOS 2.4.2.dsk> <generated 800K volume>`: boot via `PR#5` from SmartPort unit 1 (read-only), Bitsy Bye selector, `BASIC.SYSTEM`, `CATALOG /DATA800` (1600 blocks total), `PREFIX`, `SAVE TEST`; the file `TEST` is found in the volume directory of the image. `ctest` 9/9 passes (including 800K profile, config and track-limit tests); ESP-IDF build succeeds; nothing was flashed. Python ProDOS tests pass; two unrelated CP/M Python tests fail (`test_createSystemDsk` import error, `test_build_cpm80_matches_checked_in_image`) and were not touched. Not verified: hardware, SD durability, other ProDOS versions.|
|APPLE-VERIFY-031|HOST + BUILD PASS|2026-10-10: `apple2DiskTests --prodos-autostart-boot <ProDOS system.dsk> <800K volume>` boots ProDOS from slot 6 drive 1 with the Autostart ROM (no `PR#`): a generated PRODOS `system.dsk`, `bootDisks/apple2/prodosEmpty.po` and ProDOS 2.4.2 each boot; `CATALOG,S6,D2` lists the 800K volume and `SAVE` stores a file that is found in the volume directory. The SmartPort test still passes. ctest registers both tests with freshly generated images (13/13 pass; the data volume is copied to a scratch file first). Python tests for the ProDOS reader/writer, sector-order conversion, name mapping, `createSystemDsk.py --profile PRODOS`, `buildDiskImage.py --os apple2` and the upload/rebuild messages pass; the only Python failure is the unrelated `test_build_cpm80_matches_checked_in_image`. `idf.py build` succeeds with the default and with `-DAPPLE2_SYSTEM_DISK_PROFILE=PRODOS` (the build was set back to DOS33); nothing was flashed. Not verified: hardware, SD durability, ProDOS versions other than 2.4.2, a DOS image sharing a ProDOS slot.|
|APPLE-VERIFY-032|HOST + BUILD PASS|2026-10-10: after the DOS removal `ctest` passes 12/12 (`apple2DiskStream` replaces the two DOS tests; drive-config tests expect no default drive; the ProDOS boot tests still pass) and the Python tests pass except the unrelated `test_build_cpm80_matches_checked_in_image`. `idf.py build` succeeds and regenerates the ProDOS `littlefs/apple2/system.dsk`; nothing was flashed. `data1.po` (/DATA1) and `data2.po` (/DATA2) were created with `buildDiskImage.py --os apple2` (800K). Not verified on hardware: the start-up error and fall-back to the system menu when SD6.1 is missing, and the shipped drives.cfg on the SD card.|
|APPLE-VERIFY-033|HOST + BUILD PASS|2026-10-11: `hostTests --apple2-core` (translator: all eight cursor sequences, modified arrow, dropped Delete/Home, lone/doubled ESC, all control codes, CR/LF/DEL) and `hostTests --apple2-system-rom` (Applesoft `GET A$: PRINT ASC(A$)` receives `$01 $07 $08 $0A $0B $0D $15 $1A $1B $1F $7F` plus `a`/`Z`; Ctrl-C (`$03`) stops a running `10 GOTO 10` with `BREAK`) pass; `idf.py build` succeeds; nothing flashed. Not verified on hardware or through minicom; `apple2DiskStream` fails opening `littlefs/apple2/system.dsk`, unrelated to this change; the host-test asset paths now point to `assets/apple2_roms/apple.rom` and `assets/apple2Downloads/appleDisks/`.|

APPLE-VERIFY-010 evidence — date: 2026-10-09. Firmware commit: none; this verification ran against the uncommitted worktree. Upstream CPU core: `floooh/chips` `ee88c35ad6427341aa6999c3b07233e1f8bd2396`. ESP-IDF: v6.0.2. Target configuration: project-configured ESP32-S3 / LOLIN S3 Pro; the firmware was built, not run on that board. Terminal: host-side test harness reading emulated 40×24 and 80×24 screen memory; no physical terminal or board was attached. Commands: `cmake --build build-host --parallel`; `ctest --test-dir build-host --output-on-failure`; ESP-IDF VS Code build command (`idf.py build`). Guest input in the ROM regression: `10 PRINT 1`, `LIST`, `PR#3`, `LIST`, `PR#0`, `PR#3`, `PRINT 2+2`. Expected: complete `PRINT` in both screen modes; a cleared destination screen and visible BASIC prompt on each mode change; cursor within the active screen; arithmetic result `4`; all host tests pass and firmware links. Actual: all expectations passed; CTest reported 4/4 passing and the ESP-IDF build completed. Evidence locations: `tests/hostTests.c` (`testApple2SystemRom`, `testApple2VidexCard`, `testApple2VidexScrolling`) and the host CTest target definitions in `tests/CMakeLists.txt`. This is host/build evidence only; hardware behavior remains unverified.

APPLE-VERIFY-018 evidence — date: 2026-10-09. Firmware commit: none; tests ran against the uncommitted worktree. Upstream core: not exercised. ESP-IDF: not used. Board and terminal: not applicable. Python environment: project `.venv-1`, Python 3.12.14. Test command: `PYTHONPATH=tools /Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/.venv-1/bin/python -m unittest discover -s tests -p 'test_createSystemDsk.py'`; actual: 14 tests passed, covering the CP/M regression, synthetic DOS VTOC/catalog insertion, and text line-ending normalization. Image command: extract `romkrk.dsk` from `assets/apple2_roms/APPLE_IIe_ROM_KRK.zip` to a temporary base, then run `PYTHONPATH=tools <project-python> tools/createSystemDsk.py --os apple2 --profile DOS33 --base-image <temporary-base>`. Actual: `littlefs/apple2/system.dsk` is 143,360 bytes; its catalog lists `TEST.TXT`, `ROMPATCH2E_P2_KRK`, `ROMPATCH2E_P1_KRK`, and `ROMPATCH2E_P3_KRK`. The `TEST.TXT` entry is DOS text type and the stored content uses carriage-return lines. The candidate's bootability and all runtime Disk II reads remain unverified because the emulator has no Disk II controller.

APPLE-VERIFY-019 evidence — date: 2026-10-09. Firmware commit: none; focused tests and image generation ran against the uncommitted worktree. Upstream core and ESP-IDF: not exercised. Board and terminal: not applicable. Python environment: project `.venv-1`, Python 3.12.14. Test command: `PYTHONPATH=tools .venv-1/bin/python -m unittest discover -s tests -p 'test_createSystemDsk.py'`; actual: 21 tests passed, including unlocked-file sector reclamation, locked-file refusal, input-image preservation, and extensionless Applesoft catalog naming. Image command: `PYTHONPATH=tools .venv-1/bin/python tools/createSystemDsk.py --os apple2 --profile DOS33 --base-image "assets/Apple DOS 3.3P.dsk" --remove-existing "LOCKSMITH 4.1"`. Actual: `littlefs/apple2/system.dsk` is 143,360 bytes, has 21 catalog entries, contains `TEST-NONGR` with DOS Applesoft type 2 and its full 2,097-byte tokenized payload, omits `LOCKSMITH 4.1`, and has 20 free sectors; the base image remained unchanged. `diskImageApple2Dos33.inspect_image` validates its VTOC and catalog. Neither bootability nor emulator-side disk access was tested.

APPLE-VERIFY-020 evidence — date: 2026-10-09. Firmware commit: none; tests and build ran against the uncommitted worktree; host/build evidence only; hardware behavior remains unverified. Host compiler: clang (macOS arm64) with `-Wall -Wextra -Werror -UNDEBUG -funsigned-char` and ASan/UBSan. ESP-IDF: v6.0.2. No flash or upload was performed. Commands: `cmake -S tests -B build-host`; `cmake --build build-host`; `ctest --test-dir build-host --output-on-failure`; `idf.py build`. Actual: ctest 7/7 passed (`hostCore`, `apple2Core`, `apple2VidexCard`, `apple2SystemRom`, `apple2Disk`, `apple2DiskDosStream`, `apple2DiskDosBoot`); the firmware build completed (app partition 67% free). Coverage by test (all in `tests/apple2DiskTests.c`): image open, size and bounds errors, read-only behaviour (file unchanged after the run); sector selection and the GCR stream equal to every sector of both `littlefs/apple2/system.dsk` and `assets/Apple DOS 3.3P.dsk` (controller/sector tests); softswitch, drive, motor, phase, Q6/Q7 behaviour through the core at the slot-6 addresses, motor coast-down and its cancellation and reset; a synthetic boot sector read and executed by the guest through the slot-6 ROM and the real interface; `--dos-boot`: system.dsk boots to the DOS banner, 229 sector reads, 0 read failures, 0 write attempts, head at half-track 18, image file unchanged; `LOAD TEST-NONGR` finishes without `ERROR` or `NOT FOUND`, program end `$1032` (2097 bytes). Not passed: `RUN` of `TEST-NONGR` on the shipped image (APPLE-ISSUE-014); `RUN` output is printed by the test for information only and is not asserted. Not proven: hardware behaviour, ESP32-S3 timing/performance of sector reads from LittleFS, compatibility with other DOS software, and the uncertainties listed in APPLE-DEC-020. Classification: sector/controller tests PASSED; DOS boot and LOAD PASSED in the host emulator; hardware tested: NO.

APPLE-VERIFY-021 evidence — date: 2026-10-09. Firmware commit: none; uncommitted worktree; host evidence only, no flash or upload. Commands: `python3 tools/createSystemDsk.py --os apple2 --profile DOS33` (historical: this evidence used the removed `--remove-all-existing` option on the original base; superseded by APPLE-DEC-021/VERIFY-022); `cmake --build build-host`; `ctest --test-dir build-host --output-on-failure` (7/7); `PYTHONPATH=tools python3 -m unittest test_createSystemDsk` from `tests/` (23/23). `tests/test_diskImage.py::test_build_cpm80_matches_checked_in_image` fails with and without these changes (CP/M-80, unrelated). Observed: catalog is `['TEST-NONGR']`; boot shows `FILE NOT FOUND` once (no HELLO file) and the `]` prompt, 183 sector reads, 0 failures, 0 write attempts; `LOAD` ends at `$103D` (2108 bytes); `RUN` prints TEST 1 to TEST 8 `OK`. The 1,000,000-cycle coast-down value and the other APPLE-DEC-020 uncertainties are unchanged. Not proven: hardware behaviour and the RGB LED colours (APPLE-ISSUE-015).

Lowercase follow-up — 2026-10-09: the core independently selects lowercase text decoding and lowercase keyboard input. The custom display decoder maps `$C1–$DA` to uppercase A–Z and `$E1–$FA` to lowercase a–z. Host unit tests verify character decoding and that keyboard data retains both cases. This project mapping is not a verified dump of a historical lowercase character ROM.

Keyboard correction — 2026-10-09: USB Serial/JTAG carries characters, not physical key/modifier events; no Shift signal is wired to the Apple II game port. The core does not synthesize Shift on `$C063`. It stores keyboard data separately from the `$C000` ready strobe, which `$C010` clears. The stock ROM normalizes lowercase input to uppercase in its echo path. APPLE-VERIFY-016 adds a bounded, case-preserving correction only to the matching screen write after the keyboard strobe is acknowledged; it leaves the incoming keyboard data and ROM unchanged. This correction covers the key echo only, not Applesoft string printing or the case stored in BASIC program text.

Text-code correction — 2026-10-09: lowercase mapping now recognizes lowercase screen bytes `$E1–$FA` before stripping Apple II text attributes. The earlier low-six-bit conversion mistakenly treated digit codes `$B0–$B9` as lowercase letters, rendering `1234567890` incorrectly. Digits and punctuation remain decoded through the standard character mapping. Regressions cover all ten digits and all 26 lowercase letters. Existing system-ROM host tests type `PR#3` and exercise the Videx output path; the digit rendering bug made terminal echo unreliable to read.

APPLE-VERIFY-011 evidence — date: 2026-10-09. Firmware commit: none; tests ran against the uncommitted worktree. Upstream CPU core: `floooh/chips` `ee88c35ad6427341aa6999c3b07233e1f8bd2396`. ESP-IDF: v6.0.2. Target configuration: project-configured ESP32-S3 / LOLIN S3 Pro; host tests only, no board execution. Terminal: host-side test harness; no physical terminal attached. Commands: `cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-core`; `ctest --test-dir build-host --output-on-failure`; ESP-IDF extension `build` command. Expected: preserve uppercase behavior, read lowercase and uppercase through the keyboard latch, decode lowercase text bytes only when the lowercase character mapping is enabled, pass all host regressions and link firmware. Actual: targeted Apple II core test passed; full host suite passed 4/4; ESP-IDF build succeeded. Evidence location: `tests/hostTests.c` (`testApple2Core`); hardware behavior remains unverified.

APPLE-VERIFY-012 evidence — date: 2026-10-09. Earlier tests did not exercise keyboard firmware with case-derived virtual Shift sensing; see correction in APPLE-VERIFY-015.

APPLE-VERIFY-013 evidence — date: 2026-10-09. Lowercase screen codes `$E1–$FA`, digits `$B0–$B9`, and uppercase system-ROM `PR#3` input passed before the virtual Shift-sense shim. It does not demonstrate lowercase guest keyboard handling.

APPLE-VERIFY-014 evidence — date: 2026-10-09. This record was superseded: successful execution of lowercase BASIC commands did not prove lowercase echo/display, and its expected case-derived `$C063` state was erroneous. See APPLE-VERIFY-015.

APPLE-VERIFY-015 evidence — date: 2026-10-09. This intermediate result was superseded: it verified case-preserving keyboard data and strobe clearing but did not implement lowercase echo; see APPLE-VERIFY-016.

APPLE-VERIFY-016 evidence — date: 2026-10-09. Firmware commit: none; tests/build ran against the uncommitted worktree. Target configuration: project-configured ESP32-S3 / LOLIN S3 Pro; host tests and firmware build only, no board execution. Commands: `cmake --build build-host --target hostTests && ./build-host/hostTests --apple2-core && ./build-host/hostTests --apple2-system-rom`; `ctest --test-dir build-host --output-on-failure`; ESP-IDF extension `build` command. The system-ROM test reads back exact text-cell bytes after terminal `a` and `A` input (`$E1` and `$C1`) in motherboard mode and verifies lowercase Videx echo. It also verifies lowercase Applesoft command input, `PR#3`, digits, strobe clearing, and that the host keyboard bytes are preserved. Full CTest and ESP-IDF build pass. This confirms host-emulated behavior only; no physical-board execution was performed.

APPLE-VERIFY-017 evidence — date: 2026-10-09. User-reported device/terminal behavior; not independently reproduced in the host harness. Input `print "abcdefghijkm"` displayed `ABCDEFGHIJKM`. Entering `10 print "Pietje Puk"` and then `list` displayed `10  PRINT "PIETJE PUK"`. The already-verified case-preserving character echo therefore does not preserve lowercase through Applesoft `PRINT` string output or program storage/listing. Work is explicitly parked at the user's request. On resumption, add regression coverage for literal string output and stored/listed string case before changing the ROM/core interception; test motherboard and Videx output and retain existing uppercase, digits, lowercase command parsing and `PR#3` regressions.

APPLE-VERIFY-024 evidence — date: 2026-10-10. Firmware commit: none; uncommitted worktree; host/build evidence only, no flash or upload. Commands: `cmake --build build-host`; `ctest --test-dir build-host --output-on-failure` (8/8: the seven earlier tests plus `apple2MultiDisk`); `source "$HOME/.espressif/tools/activate_idf_v6.0.2.sh" && idf.py build` (completed, no warnings found in its output); `PYTHONPATH=../tools python3 -m unittest test_diskImageApple2Pascal` from `tests/`. `tests/apple2MultiDiskTests.c` observed: DOS and ProDOS sector order in the decoded nibble stream (tracks 0 and 17); 160-track stepping to half-track 319 and the clamp there, track 100 and 159 decoded; two drives with independent heads, an empty second drive reading `$00`, write-protect sense `$80` per drive; slot 5 through the core (ROM bytes `$C501/$C503/$C505/$C507` = `20 00 03 3C`, `LDA #$50`, motor and write-attempt counting on slot 5 only, slot 6 absent, invalid slot/drive/track arguments refused); image profile sizes in both directions; Apple Pascal probe for 640K `.po`, 140K DOS-order and forced wrong order (suspect flag), damaged headers; DOS 3.3 probe in both orders; a 640K Pascal volume read through the controller (block 1000 and block 1279 decoded from the nibble stream, image file unchanged); `drives.cfg` valid lines, 17 invalid cases, defaults, `/retro` path resolution and over-long lines. Not proven: any guest software booting or reading from slots 4, 5 or 7 or from a 640K drive (APPLE-ISSUE-016/019), Pascal on this controller, hardware behaviour.

APPLE-VERIFY-025 evidence — date: 2026-10-10. Firmware commit: none; tests/build ran against the uncommitted worktree; no flash or upload. Commands: `cmake --build build-host --parallel`; `ctest --test-dir build-host --output-on-failure` (8/8); ESP-IDF extension `build` command (completed). `tests/apple2DiskTests.c` feeds 6-and-2 encoded Disk II data fields through Q7/Q6 write mode into a 640K ProDOS-order image at track 159; it verifies physical-to-file sector mapping, exact data after close/reopen, no image extension, malformed checksum refusal and RO write failure. `tests/apple2MultiDiskTests.c` verifies RO/RW config parsing, default system image write protection and controller write-protect sense. This does not verify DOS/ProDOS/Pascal guest software issuing writes or hardware behavior.

APPLE-VERIFY-026 evidence — date: 2026-10-10. Firmware commit: none; this ran against the uncommitted worktree. Upstream core: `floooh/chips` `ee88c35ad6427341aa6999c3b07233e1f8bd2396`. ESP-IDF: v6.0.2; configured ESP32-S3 target; built but not flashed. Terminal: host test harness; no physical board or terminal. Commands: `cmake --build build-host --parallel`; `ctest --test-dir build-host --output-on-failure` (8/8); `/Users/willema/Documents/espidfProjects.nosync/esp32s3-retro/.venv-1/bin/python -m unittest discover -s tests -p 'test_diskImageApple2Prodos.py' -v` (6/6); ESP-IDF extension `build` command. Expected/observed: the synthetic 6502 caller reads and writes a 512-byte SmartPort unit; RO writes return the write-protected status, out-of-range requests return bad-block status, and Cn signature/capability bytes match the configured unit. Image tests detect a ProDOS volume in both sector orders, write/read bounded blocks and reject out-of-range and RO writes. The ProDOS formatter tests validate 140K/640K volume headers, directory chain, bitmap, name/profile validation and CLI output. The firmware build produced `retroHost.bin`; no flash/upload was performed. Not proven: booting the locally supplied ProDOS 2.4.2 disk, ProDOS guest catalog/save operations through SmartPort, SD durability under interruption, or hardware behavior.

Earlier 2026-10-04 Apple IIe/ProDOS source-review notes in the prior revision are not acceptance evidence for this new original-Apple-II baseline. Preserve the original document in version control for provenance.

## Next implementation session

Before accepting additional Apple II milestones, confirm HOST-M1/CP/M acceptance status and actual repository layout. Resolve ROM provenance and redistribution rights, verify the pinned CPU core against a reproducible original Apple II desktop reference, then complete one milestone at a time. Keep failed experiments and regression evidence; never relabel source review or a host-only test as a passing hardware test.
````

</details>
