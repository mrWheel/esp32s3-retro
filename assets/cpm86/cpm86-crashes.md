# CP/M-86 crash log

## What you can do

You may, in this session, build and flash the esp32s3-retro board so you can test on the real implementation

when a program writes to a non-existent device, show a "Device xyz not available" message instead of crashing.

## Analyze and solve thes crashes

Status: **under investigation** (2026-10-26). Two third-party CP/M-86 utilities stop the guest.
Both run from the D: prompt; the .CMD executables live on the A: system disk. Result codes are
`cpm86CoreResult` values: **6 = `cpm86CoreMemoryFault`** (guest memory access out of the 640 KiB
RAM), **8 = `cpm86CoreIoError`** (IN/OUT to a port RETRO86_V1 does not implement, or word I/O).

Note: in the stop log, `CS:IP` / `physical` / `bytes` are the **post-fault** program counter — the
CPU has already advanced IP past the faulting instruction. The bytes shown are the *next*
instruction, not the one that faulted.

## Reported symptoms

### UNERA.CMD
```text
D>unera ws-*.*
UNERA ver 3.1 (2-16-84) for CP/M-86, CP/M-86+ & CCP/M-86   // for HELP
Recovered File(s) will be on D: User 0
WS-FIX  .CMD - Extent # 0
WS-FIX  .DOC - Extent # 0
WS-FIX  .HLP - Extent # 0
Do you wish to recover the File(s) (Y/N)? y
E (334576) cpm86Machine: CP/M-86 stopped: result=6 CS:IP=9E5E:01E2 physical=9E7C2
bytes=75 32 80 7F 01 E5 instructions=225877614 executed=3694
```

### TT.CMD
```text
D>tt teletype.hlp
E (646706) cpm86Machine: CP/M-86 stopped: result=8 CS:IP=9FD3:026C physical=9FF9C
bytes=58 E6 42 8A C4 E6 instructions=176295373 executed=3533
```

## Findings

### UNERA.CMD (result=6, memory fault)
- `UNERA.CMD` is available locally (on A: and in `86filutl.zip`; identical 2816 bytes), so it can be
  analysed statically. `TT.CMD` is also now available locally for static analysis.
- The post-fault PC `0x9E7C2` maps to `UNERA.CMD` file offset `0x262` (the distinctive byte run
  `75 32 80 7F 01 E5`). The **faulting instruction** is the one just before it: `CMP [BX],0E5h`
  (file offset `0x25F`), with `BX` set to `0x062E` by the preceding `MOV BX,062Eh` (file `0x25C`).
  It is a **read of `DS:0x062E`**.
- Load layout: file offset 0 → physical `0x9E560`; code (file `0x80`) → physical `0x9E5E0` = `CS:0`
  with `CS=0x9E5E`. So the program image is small (~2.7 KiB) and sits near the top of the 640 KiB RAM.
- For the read of `DS:0x062E` to fault (`>= 0xA0000`), `DS` must be `>= 0x9F9E` — i.e. roughly
  `0x140` paragraphs (~5 KiB) **above** `CS` (`0x9E5E`). The program's data area therefore straddles
  the top of RAM. UNERA had already printed its banner and listed the three files (so the directory
  scan and lower `DS` offsets worked); the fault is specifically in the recovery path reading a high
  data offset.
- **Current status:** the original three-file recovery was re-run on the current firmware with
  `D>a:unera ws-fix.*` and `WS-FIX.TXT`, `WS-FIX.ASM`, and `WS-FIX.HLP` present. UNERA listed all
  three, accepted `Y`, printed its normal completion message, and the recovered files appeared in
  the following directory listing. The memory fault did not recur. Its cause is still unknown; no
  memory-map change was made. If it recurs, the new dump's `DS`/`SS`/`SP` and fault address are needed
  to distinguish a loader/MRD issue from a program memory-map issue.

### TT.CMD (result=8, I/O fault)
- `TT.CMD` is now available locally. Its post-fault PC maps to `OUT 43h,AL`; the next instructions
  also write to port `42h`.
- These direct I/O ports are not implemented by RETRO86_V1. The emulator now reports
  `Device 0x0043 not available.` on the guest console, and the guest keeps running: OUT to an unknown port is ignored, IN returns FFh (message once per
  port; BIOS ports E0h-EFh still fault). Hardware re-test of `D>a:tt ed.txt`: pending (the earlier run
  stopped the guest after the message). The device remains unmodeled.
- Second hardware run: after the messages (ports 43h/42h/61h) TT crashed with a memory fault because it
  also calls PC-BIOS `INT 1Ah` (tick count, delay) and `INT 16h` (keyboard), whose vectors were empty
  (jump to 0000:0000). Fix: `main/cpm86Machine.c` now installs small guest stubs at 0000:0400 for
  `INT 1Ah AH=0` (ticks from the F1h-F4h ms timer, ms/55) and `INT 16h AH=0/1` (console ports
  E0h/E1h). Build passes; hardware re-test pending.

## What was done
- Added fault-context capture to the `tiny8086` core (`components/cpm86Core/tiny8086`): on a stop it
  now records the faulting instruction's start IP, and for the failing access the physical address +
  read/write (memory faults) or the port + value + read/write (I/O faults).
- Exposed it through `cpm86CoreGetFaultInfo()` (`cpm86Core.h`/`cpm86Core.c`).
- Rewrote the stop log in `main/cpm86Machine.c` (`logCpm86Stop`) to dump: result, the faulting
  instruction (`CS:startIp` + its bytes), the post-fault IP, all segments (`ES/CS/SS/DS`), all
  registers (`AX CX DX BX SP BP SI DI`), flags, instruction count, and the specific failing access.
- This is **additive diagnostics and unavailable-device reporting** — no CPU execution semantics
  changed, and nothing removed from
  a previously-rejected design (the removed trace/execute-guard/opcode-profiler code from
  `migrate8086CPU.md` / CPM86-VERIFY-044 was **not** re-introduced).

## What worked / did not
- Local static analysis of UNERA.CMD: **worked** — pinned the faulting instruction and the
  `DS:0x062E` read.
- Local static analysis of TT.CMD: **worked** — pinned the unsupported `OUT 43h,AL`.
- Firmware build with diagnostics and unavailable-device reporting: **passed** with ESP-IDF 6.0.2.
- Host regression for unsupported `OUT 43h,AL`: **passed**.
- Hardware run `D>a:unera ws-fix.*` with the original three matching files: **completed without a
  fault**; all three files were recovered and appeared in the following `D:DIR`. The old memory
  fault is not reproducible on the current firmware; its historical cause is unknown.
- Hardware run `D>a:tt ed.txt`: **passed**; the guest displayed `Device 0x0043 not available.` and
  returned to the host prompt.

## Next steps
1. If the UNERA memory fault recurs, capture the new diagnostic dump and use its `DS/SS/SP` and
   failing physical address to identify the root cause. Do not change the memory map based on the old
   post-fault PC alone.
2. Keep the unsupported-device behavior covered by the host regression and hardware verification.

## UNERA root cause (2026-10-27, `unera help.*`, result=6)
- Dump: `DS=SS=9E9D`, `BX=164E`, fault = read at physical `A001E` (`DS:BX`, just above the 640 KiB
  RAM top `A0000`). Faulting instruction: `CMP [BX],0E5h`.
- UNERA walks a directory buffer that starts at `DS:062E` and is sized for the 512-entry directory
  of the BIG drive; the program is loaded near the top of RAM, so the scan runs past the end of RAM.
  Which entries it reaches depends on where the file sits in the directory (explains why the earlier
  `ws-fix.*` runs passed).
- A real 8086 reads FFh from absent memory and ignores writes; the emulator raised a memory fault.
- Fix: `tiny8086` data reads above the populated RAM now return FFh and writes are dropped; only
  instruction fetch from there still faults. Host regression added; firmware build passes. Hardware
  re-test of `a:unera help.*`: pending.
