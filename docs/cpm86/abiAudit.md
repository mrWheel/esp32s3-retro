# CP/M-86 Primary-Manual ABI Audit (Step 86-11)

Date: 2026-11-01
Status: primary-manual evidence for CPM86-ISSUE-002/003 (BDOS E0h call ABI, BIOS entry-vector
table, DPB fields, memory-region table, segment-aware DMA) plus rights/provenance notes per
CPM86-DEC-014 and CPM86-DEC-017.

## 1. Scope and method

- Audited artifact: the project-owned guest BIOS `components/cpm86Core/bios/retro86bios.a86`
  (338 lines, read in full) and the host-side facts it depends on (`main/cpm86Machine.c`
  BDOS interrupt-vector install, `designCPM86.md` statements at lines 186, 204, 320, 323, 423, 665).
- Reference material: the three CP/M-86 1.1 manuals pinned by CPM86-DEC-014, downloaded on
  2026-11-01 into the scratch directory `/tmp/retro86-manuals-agent` (outside the repository;
  not vendored). `pdftotext` was used only to locate sections; every cited page was read
  visually from the PDF. No download or read failure occurred for any of the three files.
- Citation format: `[SG p.23 / PDF 30]` means System Guide printed page 23, PDF page 30 of the
  downloaded scan (for this scan the System Guide PDF page = printed page + 7). Unread pages are
  never cited. Where the audit says "not verifiable", the pages actually read are silent.
- This audit records what the manuals say. It does not restate unverified claims and does not
  close the issue log itself (that is the coordinating step's job).

### Editions, URLs and pinned commit

All three PDFs were taken from the repository and commit pinned by CPM86-DEC-014:
`https://github.com/TheBrokenPipe/isbc8612` at commit `fc31925b444701abdc0bf4d84f555e5e52eab9e7`.

| File | Edition as read | URL (raw, pinned) |
| --- | --- | --- |
| `CPM-86_System_Guide_Jun81.pdf` (177 pp.) | CP/M-86 Operating System System Guide, Digital Research, Pacific Grove; cover and copyright page read (PDF 1, 4: "Copyright (C) 1981 by Digital Research. All rights reserved."). The 1.1 release notes' correction pages identify this printing as "Correction to the First Printing - 1981" (see below). | https://github.com/TheBrokenPipe/isbc8612/raw/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_System_Guide_Jun81.pdf |
| `CPM-86_Programmers_Guide_Jun81.pdf` (131 pp.) | CP/M-86 Operating System Programmer's Guide, "Second Printing: June 1981", "Copyright (C) 1981 by Digital Research. All Rights Reserved" (PDF 2, 3). This manual is the ASM-86 assembler / DDT-86 debugger manual (Foreword, PDF 4; Table of Contents, PDF 6-8); it contains no BDOS function-call ABI chapter. | https://github.com/TheBrokenPipe/isbc8612/raw/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_Programmers_Guide_Jun81.pdf |
| `CPM-86_1.1_Release_Note_1982.pdf` (36 pp.) | CP/M-86 Operating System Release 1.1 Release Notes, "Copyright (C) 1982 Digital Research ... All Rights Reserved", "Compiled February 1982" (PDF 1, 3); read in full. Contains, as paginated sub-documents: the 1.1 Enhancements note, "SYSTEM GUIDE Correction to the First Printing - 1981 (Compiled February 1, 1982)", "SYSTEM GUIDE Enhancements to the First Printing - 1981 (Compiled February 1, 1982)", the User's Guide HELP addendum, the "Diskette Track Buffering" addendum (John R. Pierce, December 12, 1981), Programmer's Guide corrections, an iSBC 86/12 + SBC 204 Implementation Note, and three application notes. | https://github.com/TheBrokenPipe/isbc8612/raw/fc31925b444701abdc0bf4d84f555e5e52eab9e7/manuals/CPM-86_1.1_Release_Note_1982.pdf |

Pages actually read for citations:

- System Guide (PDF): 3-12, 29-34, 37-43, 49-54, 55-61, 62-71, 72-73, 73-82, 96-101 (cover,
  TOC, sections 1, 4.1-4.4, 5.1-5.4, 6.1-6.2, Appendix A).
- Release notes (PDF): 1-36 (all).
- Programmer's Guide (PDF): 1-8 (cover, copyright, Foreword, TOC). Its remaining body pages
  were not read because the manual defines no BDOS/BIOS ABI (per its own Foreword and TOC);
  this is recorded as a scoping gap in section 7.

## 2. (a) BDOS call ABI - INT E0h, function in CL, argument in DE?

Primary text (System Guide section 4.1, "BDOS Parameters and Function Codes"):

> "Entry to the BDOS is accomplished through the 8086 software interrupt #224. This interrupt
> is reserved by Intel Corporation for use by CP/M-86 and MP/M-86. The function code is passed
> in register CL with byte parameters in DL and word parameters in DX." [SG p.23 / PDF 30]

Table 4-1 "BDOS Entry Registers" (same page): `CL` function code (byte value returned in AL);
`DL` byte parameter; `DX` word parameter (word value returned in AX and BX); `DS` data segment
(double-word values: BX holds the offset, ES holds the segment; "ES is not preserved through
BDOS ... All other segment registers are saved and restored by the BDOS call").
The same page states: "In CP/M-86, the DX register performs the same function as the DE pair in
CP/M-80." Section 1.2 adds: "To make a BDOS system call, use the reserved software interrupt
#224 ... the 8086 registers CH and CL correspond to the 8080 registers B and C" [SG p.4 / PDF 11].
Table 4-1 also notes most BDOS routines destroy all registers except SS, SP, CS and IP.

| Item in the audit task premise | Manual evidence | Project side | Verdict |
| --- | --- | --- | --- |
| BDOS entered by INT 0E0h | "software interrupt #224" [SG p.23 / PDF 30; SG p.4 / PDF 11]; decimal 224 = E0h | `designCPM86.md:186` "BDOS entry uses software interrupt E0h (decimal 224)"; host installs the vector (section 6) | Matches |
| Function number in CL | "The function code is passed in register CL" [SG p.23]; Table 4-1 | design line 186 defers the full ABI to this audit | Matches |
| Argument in DE | Not supported. The manual passes byte parameters in DL and word parameters in DX (with DS as segment) and maps CP/M-80's DE pair onto DX [SG p.23]. DE is not mentioned as a BDOS argument register anywhere on the pages read | The design deliberately does not claim a DE convention (`designCPM86.md:186`: "this document does not specify a complete BDOS register ABI") | Differs (DE is the CP/M-80 convention; CP/M-86 1.1 uses DX/DS) |

Note: the BDOS function numbers themselves are Table 4-2 [SG pp.24-25 / PDF 31-32]; functions
differing from CP/M-80 or new to CP/M-86 are starred there (0, 6, 27, 31, 37, 50-59). The
1.1 release notes add BDOS functions 47 (CHAIN TO PROGRAM, `CL=2FH`, command line as a null-
terminated string in the default DMA buffer) and 49 (GET SYSDAT ADDRESS, `CL=031H`, returns
BX=offset, ES=segment; SYSDAT map begins with `dmaad` word 0 = user DMA address, `dmabase` word
2 = user DMA base, cross-referenced to "page 48 under Function 51" in the System Guide)
[RN "SYSTEM GUIDE Enhancements to the First Printing - 1981" printed p.1-2 / PDF 9-10].

## 3. (b) BIOS entry-vector table order

Primary text: System Guide section 5.2 "The BIOS Jump Vector" - "The BIOS Jump Vector begins at
location offset 2500H from the beginning of the BIOS ... Each element ... consists of a 3-byte
JMP instruction ... Although certain BIOS subroutines are non-essential ... the jump vector
element must be present in every BIOS ... Parameters ... are passed in the CX and DX registers.
CX is the first parameter; DX is used for the second argument ... Byte values are returned in
AL. Word values (16 bits) are returned in BX." [SG p.56 / PDF 63]

Table 5-1 "CP/M-86 BIOS Jump Vector" [SG p.57 / PDF 64], the order the manual defines:

| # | Offset | Entry | # | Offset | Entry |
| --- | --- | --- | --- | --- | --- |
| 0 | 2500H | INIT (arrive here from cold boot) | 11 | 2521H | SETSEC |
| 1 | 2503H | WBOOT (warm boot) | 12 | 2524H | SETDMA |
| 2 | 2506H | CONST | 13 | 2527H | READ |
| 3 | 2509H | CONIN | 14 | 252AH | WRITE |
| 4 | 250CH | CONOUT | 15 | 252DH | LISTST |
| 5 | 250FH | LIST | 16 | 2530H | SECTRAN |
| 6 | 2512H | PUNCH | 17 | 2533H | SETDMAB |
| 7 | 2515H | READER | 18 | 2536H | GETSEGB |
| 8 | 2518H | HOME | 19 | 2539H | GETIOB |
| 9 | 251BH | SELDSK | 20 | 253CH | SETIOB |
| 10 | 251EH | SETTRK | | | |

Comparison with `retro86bios.a86` (lines 10-31, `cseg org 2500h`, 21 `jmp` entries at
2500h..253Ch, matching Figure 5-1 [SG p.56 / PDF 63]):

| # | Manual (Table 5-1) | retro86bios.a86 | Verdict |
| --- | --- | --- | --- |
| 0 | INIT | `jmp INIT` (line 11) | Matches |
| 1 | WBOOT | `jmp WBOOT` (12) | Matches |
| 2-4 | CONST, CONIN, CONOUT | same (13-15) | Matches |
| 5 | LIST | `jmp LISTOUT` (16) | Matches (name differs, slot and behaviour match; LISTOUT sends CL via CONOUT) |
| 6-8 | PUNCH, READER, HOME | same (17-19) | Matches |
| 9 | SELDSK | same (20) | Matches |
| 10-14 | SETTRK, SETSEC, SETDMA, READ, WRITE | same (21-25) | Matches |
| 15-16 | LISTST, SECTRAN | same (26-27) | Matches |
| 17-18 | SETDMAB, GETSEGB | same (28-29) | Matches |
| 19-20 | GETIOB, SETIOB | same (30-31) | Matches |

Verdict: matches, with these audit notes.

- There is no separate "BOOT" entry. The audit task's "INIT/BOOT" phrasing maps to a single
  entry: INIT is function 0, called by the CP/M-86 loader after CPM.SYS is read in, and ends by
  jumping to the CCP offset 0; WBOOT is function 1, entered via BDOS function #0, ending at the
  CCP warm-start entry 06H [SG Table 5-4 p.60 / PDF 67]. The project matches: `INIT ... jmp CCP`
  (lines 41-49) and `WBOOT ... jmp CCP+6` (lines 51-53).
- The compressed order in the audit task ("... WRITE, SETDMAB, GETSEGB") omits HOME (8),
  LISTST (15), SECTRAN (16), GETIOB (19), SETIOB (20); the manual's order (above) is the
  authoritative one and the project follows it exactly, including the three JMPs that the task
  list skipped.
- Subroutine parameter/return conventions per [SG p.56 / PDF 63] are honoured by the project:
  CX for track/sector/DMA-offset/DMA-segment/IOBYTE (lines 116-131, 140-142), BX for word
  returns from SELDSK/SECTRAN/GETSEGB (lines 102-114, 132-134, 222-224), AL for byte returns
  (CONST/READ/WRITE status).
- Entry descriptions [SG Table 5-4 pp.60-65 / PDF 67-72] were read in full; per-entry checks
  are recorded as findings in section 6 (deviations: READER never yields EOF 1AH; READ/WRITE
  perform none of the recommended ~10 retries; SELDSK ignores the DL-bit-0 "first selection"
  hint, which the manual documents as available, not mandatory [SG p.62 / PDF 69]).
- INIT duty deviation: the manual assigns INIT the job of "initializing the interrupt vector to
  point to the BDOS offset (0B11H) and base" [SG p.60 / PDF 67], corrected in 1.1 to
  "BDOS offset (0B06H)" [RN "SYSTEM GUIDE Enhancements ... PAGE 60", printed p.3 / PDF 11].
  `retro86bios.a86` INIT (lines 41-49) does not write the IVT; the host installs it instead
  (section 6). The value installed (0B06H) matches the corrected 1.1 figure, not the first
  printing's 0B11H.

## 4. (c) DPB field layout and semantics

Primary text: System Guide section 6.1. The 16-byte Disk Parameter Header (DPH): XLT, three
reserved scratchpad words (BDOS-internal, initial value unimportant), DIRBUF, DPB, CSV, ALV -
"each element is a word (16-bit) value" [SG Table 6-1 pp.67-68 / PDF 74-75]. The Disk Parameter
Block (DPB), field widths as printed [SG Table 6-2 pp.69-70 / PDF 76-77]:

| Field | Width | Manual definition (Table 6-2, pp.69-70) |
| --- | --- | --- |
| SPT | 16-bit | total number of sectors per track |
| BSH | 8-bit | data allocation block shift factor |
| BLM | 8-bit | block mask |
| EXM | 8-bit | extent mask, determined by BLS and the number of disk blocks |
| DSM | 16-bit | total storage capacity (max data block number, in BLS units) |
| DRM | 16-bit | "one less than the total number of directory entries, which can take on a 16-bit value" [SG p.71 / PDF 78] |
| AL0, AL1 | 8-bit each | reserved directory blocks bitmap (16 bits together) |
| CKS | 16-bit | size of the directory check vector |
| OFF | 16-bit | number of reserved tracks at the beginning of the logical disk (auto-added by SETTRK) [SG pp.71-72 / PDF 78-79] |

Comparison with the three DPBs in `retro86bios.a86` (DPB0 lines 292-299, DPB1 lines 301-308,
DPB2 lines 310-317; 15 bytes each, exactly the manual's field sequence/widths) and against the
manual's derivation tables (BSH/BLM Table 6-3 p.70; EXM Table 6-4 p.70; entries-per-block
Table 6-5 p.71; AL0 worked example p.71; CKS rule p.70):

| Field | DPB0 | DPB1 | DPB2 | Check against manual |
| --- | --- | --- | --- | --- |
| SPT | 20h=32 | 20h=32 | 20h=32 | Width matches; value consistent with the host's 32 x 128-byte-record tracks |
| BSH/BLM | 3/07h (1024) | 4/0Fh (2048) | 7/7Fh (16384) | Exact Table 6-3 rows |
| EXM | 00h | 00h | 07h | Table 6-4: BLS1024 DSM<256 -> 0; BLS2048 DSM<256 -> 0; BLS16384 DSM>255 -> 7 |
| DSM | 9Bh=155 | 0FFh=255 | 1FFh=511 | Width matches; BLSx(DSM+1) = 155.7K/512K/8M matches the 40/129/2049-track profiles minus the OFF-reserved track |
| DRM | 3Fh=63 | 7Fh=127 | 1FFh=511 | 16-bit allowed [SG p.71] |
| AL0/AL1 | C0h/00 | C0h/00 | 80h/00 | Table 6-5 entries-per-block (32/64/512 at BLS 1K/2K/16K): 2/2/1 reserved blocks -> C0h/C0h/80h. All consistent |
| CKS | 10h=16 | 20h=32 | 0000h | Removable rule CKS=(DRM+1)/4 gives 16/32; fixed disk => 0 [SG p.70] |
| OFF | 1 | 1 | 1 | Reserved boot track; SETTRK adds it [SG p.71] (matches the design's recorded OFF handling, `designCPM86.md:372`) |

DPH comparison [SG Table 6-1 / PDF 74]: project DPH0..DPH5 (lines 244-290) are `dw 0` (XLT),
three scratch `dw 0`, DIRBUF, DPB, CSV, ALV - matches the manual layout; SELDSK returns the DPH
offset in BX and 0000H for an invalid drive (lines 102-114), matching Table 5-4 [SG p.62 / PDF 69].

Verdict (c): matches for field layout, widths and semantics of SPT, BSH, BLM, EXM, DSM, DRM,
AL0/AL1, CKS, OFF, and for the DPH. See below for what the manuals read do not settle.

Directory block numbers (the "8- or 16-bit" question). What the pages read actually say:

- The FCB data-block field is "d0...dn filled-in by CP/M, reserved for system use"
  [SG p.31 / PDF 38]; no bit width is given there.
- GENDEF rule: "if dks is greater than 255, then the block size parameter bls must be greater
  than 1024" [SG p.74 / PDF 81] - i.e. block numbers above 255 require BLS >= 2048.
- EXM is defined only as a mask "determined by the data block allocation size and the number of
  disk blocks" (Table 6-2), with its value table keyed to the DSM<256 / DSM>255 boundary
  (Table 6-4) [SG p.70 / PDF 77]; the ALV size is (DSM/8)+1 bytes [SG p.72 / PDF 79].
- Reading these together, the disk-block numbers stored in directory entries must fit in 8
  bits (max 255), which is why DSM > 255 forces larger blocks and a non-zero EXM; the project's
  DPBs respect exactly this (only DPB2 exceeds 255, and only with BLS=16384 and EXM=7).
- The manuals pages read never state the bit width of the directory block number outright, and
  the CP/M-80-style directory-record ("dr") field width is not defined in the pages read (the
  FCB description read on p.31 lists only `dr` = drive code 0-16 at offset 0). Recorded as
  not verifiable in section 7.

Verdict: field widths/semantics match; the 8-bit block-number conclusion is documented only by
implication (GENDEF dks rule + EXM/DSM table); an explicit width statement is not in the pages
read.

## 5. (d) Memory-region table and segment-aware DMA

Manual coverage found (all read):

- SETDMAB (vector #17): "Register CX contains the segment base for subsequent DMA read or write
  operations. The BIOS will use the 128 byte buffer at the memory address determined by the DMA
  base and the DMA offset during read and write operations." [SG Table 5-4 p.64 / PDF 71;
  entry list Table 5-1 p.57]
- SETDMA (vector #12): "Register CX contains the DMA offset ... until the next calls to SETDMA
  and SETDMAB occur." [SG p.63 / PDF 70]
- GETSEGB (vector #18): "Returns the address of the Memory Region Table (MRT) in BX. The
  returned value is the offset of the table relative to the start of the operating system. The
  table defines the location and extent of physical memory which is available for transient
  programs." [SG p.64 / PDF 71]
- MRT format: 8-bit `R-Cnt` (number of descriptors) followed by descriptors of `R-Base`
  (16-bit paragraph base) + `R-Length` (16-bit paragraph length); reserved interrupt locations
  (normally 0-3FFH) and the operating system are not included in the MRT; a contiguous system
  sets R-Cnt=1. [SG p.65 / PDF 72]
- BDOS side: functions 51 SET DMA BASE ("The word parameter in DX is a paragraph address ...
  initial DMA base = DS of the loaded program, DMA offset 0080H") and 52 GET DMA BASE (body
  text: returns DMA base segment in ES and DMA offset in DX; the function diagram on the same
  page prints "BX: DMA Offset" - a figure/body inconsistency in this scan) [SG pp.48-49 / PDF
  55-56]; functions 53-59 (Get Max Mem, Get Abs Max, Alloc Mem, Alloc Abs Mem, Free Mem, Free
  All Mem, Program Load) take an MCB `M-Base`/`M-Length`/`M-Ext` [SG pp.51-53 / PDF 58-60].
- Appendix A CBIOS list confirms the calling forms: `setdma: mov dma_off,CX`,
  `setdmab: mov dma_seg,CX`, `sectran: ... translate CX with table at [DX]` [SG pp.89-90 / PDF
  96-97].
- The 1.1 release notes cover the segment-aware pieces without redefining them: the track
  buffering addendum drives DMA by `dma_offset`/`dma_segment` (`setdma_seg: mov dma_segment,cx`,
  `dma_longword equ dword ptr dma_offset`) [RN "Diskette Track Buffering" printed p.4, p.7 /
  PDF 18, PDF 21], and the iSBC Implementation Note describes the BIOS "segment table" (SEGTABLE,
  System Guide Appendices D and E) whose "initial values ... define this additional RAM area to
  be 64K bytes in length ... at location 10000H (paragraph 1000H)", editable by temporary
  CP/M-86 + DDT86 runs or by editing the BIOS [RN "Implementation Note" printed p.1 / PDF 27].

Project side:

- `SETDMAB` stores CX into `DMASEGMENT` (lines 128-130); READ/WRITE move exactly 128 bytes
  between the DMA segment:offset and the host record-data port (lines 144-212) - matches the
  manual's 128-byte-buffer semantics.
- `GETSEGB` returns `offset MRT` in BX (lines 132-134); `MRT` is `db 1 / dw 0518h / dw 9AE8h`
  (lines 332-335): R-Cnt=1, one descriptor (paragraph base 0518h, length 9AE8h), i.e. the single
  region up to paragraph A000h (640 KiB profile) - same shape as the manual's contiguous case
  [SG p.65 / PDF 72].
- `SECTRAN` copies CX to BX (identity), matching the manual's no-translation case (DPH XLT=0 =>
  DX=0000H path) [SG pp.63-64, 89-90].

Verdict (d): the 1.1 manuals do cover the memory-region table and segment-aware DMA, and the
project implementation matches the documented vector positions, register conventions and MRT
format.

## 6. Findings

1. BDOS entry ABI: the "argument in DE" premise is the CP/M-80 convention and is wrong for
   CP/M-86 1.1; the manual uses CL (function), DL (byte parameter), DX+DS (word parameter),
   returns in AL/AX+BX (+ES for double words) [SG p.23]. Any documentation or test that pins a
   DE argument convention for the 86 path should be corrected (the design currently avoids the
   claim; `designCPM86.md:186`).
2. The shipped runtime matches the 1.1 corrections, not the June 1981 first printing: the host
   writes the IVT slot for INT 0E0h (`0x380`) with IP=0B06h, CS=0051h
   (`main/cpm86Machine.c` lines 351-352), which equals the release-notes-corrected INIT target
   "BDOS offset (0B06H)" [RN PDF 11], not the first-printing "0B11H" [SG p.60]. Good corroboration
   of "CP/M-86 1.1 shipped"; recorded here as the audit's edition-consistency evidence.
3. BIOS jump vector: full positional match (section 3), 21 entries, base 2500h, all three-byte
   JMPs. Naming note: project entry #5 is `LISTOUT` where the manual calls it `LIST`; position
   and behaviour (emit CL) match.
4. INIT duty split (documented deviation, not an ABI break): the manual gives INIT the console
   sign-on message, BIOS variable initialisation and the BDOS interrupt-vector installation
   [SG p.60]; the project BIOS omits the IVT write and sign-on, the host performs vector
   installation at load time (`designCPM86.md:204`, `main/cpm86Machine.c:351-352`). The guest
   interrupt vector is real guest memory, satisfying `designCPM86.md:186` ("The CPU must execute
   the interrupt vector and real guest handler").
5. READER never reports EOF: Table 5-4 requires an end-of-file condition to return ASCII
   CONTROL-Z (1AH) [SG p.61 / PDF 68]; the project's `READER` is `jmp CONIN` (lines 77-78),
   which blocks forever. Low impact (no guest path uses READER here), but it is a documented
   deviation.
6. READ/WRITE retry guidance not followed: the manual states "READ and WRITE subroutines should
   perform several retries (10 is standard) before reporting the error condition to the BDOS"
   [SG p.60 / PDF 67; reinforced p.63 / PDF 70]; the project returns error code 1 on first
   status failure (lines 144-216). Error-code contract itself (0 = OK, non-zero = non-recoverable,
   zero/non-zero is all BDOS checks) is honoured.
7. SELDSK ignores the first-selection hint bit: Table 5-4 documents DL bit 0 = not previously
   selected on entry [SG p.62 / PDF 69]; the project ignores DL and re-selects the physical
   drive via port 0E8h on every call (lines 84-114). The manual presents the hint as informational
   ("it is possible to determine"), so this is compliant but forgoes the intended optimisation.
8. DPB/DPH: exact layout match; every derived value in the three project DPBs independently
   re-derives to the manual's tables (section 4), including the 16-bit DRM (511) case which is
   only legal with the matching 16K-block AL0=80h and EXM=07h.
9. Manual-internal inconsistency noted for the record: Function 52 (GET DMA BASE) diagram shows
   "BX: DMA Offset" while the paragraph below says the offset is returned in DX [SG pp.48-49 /
   PDF 55-56]. The audit does not resolve which is correct for 1.1; no project code depends on
   function 52 (checked against `retro86bios.a86`, which is BIOS-side only).
10. Host-side (outside manual scope, recorded only for traceability): the console/disk port map
    E0h-EFh is a project invention (`designCPM86.md:204`); the manuals are silent on it by
    definition - no 1981/1982 Digital Research document defines RETRO86 hardware ports.

## 7. Not verifiable from these manuals (gaps)

- Explicit bit width of the directory-entry data-block numbers (d0..dn) and of the
  directory-record/extent fields: only implied by the GENDEF `dks>255 => bls>1024` rule
  [SG p.74], the EXM/DSM table [SG p.70] and the FCB field list [SG p.31]; no page read states
  "8-bit" or "16-bit" for these fields. The Programmer's Guide (this edition) contains no FCB or
  directory-entry chapter.
- Whether the 1.1 BDOS validates or ignores the SELDSK DL-bit-0 hint beyond documentation
  [SG p.62] - the manuals describe BIOS behaviour only.
- Behavioural details of BDOS functions 51/52 on a 1.1 system (register conflict in the scan,
  finding 9); the shipped BDOS is the pinned `cpm86-kernel` binary, whose runtime behaviour is
  outside a manual audit.
- `GETSEGB` MRT consumption rules beyond the base/length descriptor format [SG p.65]; e.g. any
  minimum-region-size rules the 1.1 BDOS/loader enforces are not in the pages read.
- The System Guide's standard BIOS listing (Appendix D, incl. SEGTABLE) and CBIOS (Appendix E)
  were not read (PDF 103-177); the MRT/segment-table description here relies on sections 4.4/5.4
  and the release-notes Implementation Note [RN PDF 27]. If exact SEGTABLE initialisation
  semantics are ever audited against Appendix D, that read must be added.
- The Programmer's Guide body (ASM-86 syntax, directives, instruction set, DDT-86) was read only
  as far as its front matter proves it contains no BDOS/BIOS ABI (Foreword iii, TOC v-vii,
  PDF 4-8); any claim that the Programmer's Guide defines the BDOS ABI is unsupported by this
  edition.
- The three scanned PDFs are OCR-quality; quoted text was verified visually page-by-page, but
  fine dot-matrix details (e.g. individual hex digits in listings) are transcribed as read.

## 8. Rights and provenance

Reference material (manuals):

- All three PDFs carry Digital Research proprietary notices as read: System Guide copyright page
  "Copyright (C) 1981 by Digital Research. All rights reserved. No part of this publication may
  be reproduced ... without the prior written permission of Digital Research" [SG PDF 4];
  Programmer's Guide identical notice plus the tutorial example-program permission [PG PDF 2-3];
  Release Notes "Copyright (C) 1982 by Digital Research ... All Rights Reserved" and the package
  statement "Software included in this package is proprietary to Digital Research and contains
  internal serialization ... you are not allowed to make copies of software provided in this
  package for any third parties" [RN PDF 3].
- CPM86-DEC-014 (`designCPM86.md:320`): these CP/M-86 1.1 materials are used from
  TheBrokenPipe/isbc8612 at the pinned commit "as reference material only; user confirms CP/M-86
  source/binaries are permitted for non-commercial use. Its iSBC BIOS is not the RETRO86_V1
  BIOS." The manuals were downloaded to a scratch directory for this audit only; nothing was
  vendored into the repository. Per `designCPM86.md:665`, the isbc8612 emulator source "has no
  established license and is not to be vendored or executed without separate permission."
- CPM86-DEC-017 (`designCPM86.md:323`): the CP/M-86 1.1 OS source/reference is
  tsupplis/cpm86-kernel at commit `00927e17f43ea4241cc0809531cf68ce835e9c16`, used "under the
  repository's included rights statement and the user's non-commercial-use confirmation"; its
  IBM PC XT BIOS is not to be silently shipped as RETRO86_V1.

Guest tools on `bootDisks/cpm86/systemDsk/` (verifiable facts):

| File | Size / timestamp as found (ls) | Notes |
| --- | --- | --- |
| `GENCMD.CMD` | 5,760 B (Oct 8) | Presence confirmed |
| `GENCMD.HLP` | 1,408 B (Oct 8) | Header line "Help by Willem Aandewiel"; "GENCMD converts .H86 hexadecimal records into a .CMD load file. It does not assemble source code." |
| `ASM86.CMD` | 26,240 B (Oct 8) | Presence confirmed |
| `ASM86.HLP` | 1,280 B (Oct 8) | "Help by Willem Aandewiel"; "ASM86 - Assemble 8086 and 8088 source for CP/M-86"; .A86 -> .H86 (plus .LST/.SYM) |
| `PIP.CMD` | 7,552 B (Oct 8) | Presence confirmed |
| `PIP.HLP` | 768 B (Oct 8) | "Help by Willem Aandewiel"; "PIP - Copy files under CP/M-86"; documents `destination=source`, `[OV]`/`[V]` options, interactive `*` prompt |

- The `.HLP` texts are project-authored help files (attributed to Willem Aandewiel); the help
  text itself is the only tool-internal evidence available in-tree and it documents intended
  behaviour, not provenance.
- Binary provenance of the three `.CMD` files: UNKNOWN. No source, upstream URL, version pin or
  license is recorded in-tree for them. The only in-tree records are usage records: the design
  log documents building the project BIOS "with CP/M-86 ASM-86 1.1N" and generating `cpm.sys`
  "with GENCMD 1.1N" (`designCPM86.md:372`), and VERIFY-016 byte-compared the shipped `PIP.CMD`
  as identical to `base/pip.cmd` from the pinned `cpm86-kernel` base image
  (`designCPM86.md:369`). "1.1N" is an unverified version label from those log entries, not an
  established provenance. Under CPM86-DEC-014 the user's confirmation of non-commercial use
  covers CP/M-86 binaries taken from the isbc8612 reference set; no equivalent evidence exists
  in-tree for the origin of these three binaries, so provenance is recorded as UNKNOWN pending
  evidence (source, upstream or licence text).
- `tools/cpm86/diskdefs` (modified in the worktree) and `docs/cpm86/diskdefsValidation.md` are
  host-side image-tooling artefacts and out of scope for this manual audit.

## 9. Conclusion

Against the June 1981 System Guide (as corrected/extended by the February 1982 Release 1.1
notes), the project BIOS `retro86bios.a86` matches the documented BDOS entry mechanism (INT E0h
via the installed guest IVT), the full 21-entry BIOS jump vector order and offsets, the DPB/DPH
layout with every derived field value consistent with the manual's tables, and the MRT/SETDMAB
semantics. The audit task's "argument in DE" premise is a CP/M-80 hold-over and must be
recorded as differing (manual: DL/DX with DS, [SG p.23]). Deviations that are documented but
non-fatal: READER EOF, READ/WRITE retry guidance, SELDSK first-selection hint, and the host-side
replacement of the manual's INIT vector-installation duty. Provenance of the shipped GENCMD,
ASM86 and PIP binaries remains UNKNOWN (section 8); no manual content was invented, and no
download or read failures occurred.
