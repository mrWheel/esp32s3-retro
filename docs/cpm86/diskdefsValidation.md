# CP/M-86 diskdefs validation (step 86-04)

Date: 2026-11-01. Tool: cpmtools 2.23 (Homebrew). Object under test:
`tools/cpm86/diskdefs` against the BIOS DPBs in
`components/cpm86Core/bios/retro86bios.a86` and the in-repo image tool
`tools/cpm86/diskImageCpm86.py` (shared engine `tools/include/diskImageCpm.py`).

## 1. Environment recipe (reproducible)

These steps were required to make cpmtools 2.23 usable with the project file
on this machine; without them the tools fail in misleading ways:

- `mkfs.cpm` ignores `$DISKDEF` on this build. It only reads a file named
  `diskdefs` from the current working directory, so run it from a scratch
  directory containing a symlink `diskdefs -> <repo>/tools/cpm86/diskdefs`.
- `mkfs.cpm` refuses to create the target and stays silent if the output file
  does not already exist at the exact image size. Pre-create it with
  `mkfile <size> <name>` (163840 / 528384 / 8392704 bytes).
- `cpmls` and `cpmcp` honour `$DISKDEF` but need an explicit libdsk type for
  plain images: always pass `-T raw`.
- The in-repo CLI `diskImageCpm86.py` rewrites the image suffix to `.dsk` and
  resolves bare names under `sdcard/retro/images/cpm86/`; a `*.img` left next
  to a same-named `*.dsk` is therefore silently shadowed (this produced a
  misleading "No files. The disk is formatted and empty." report once). Use
  the Python API (`inspect_image`/`extract_file`) for cross-checks.

## 2. Geometry audit (diskdef vs DPB vs diskImageCpm86.py)

| Field | retro86-160 / DPB0 | retro86-large / DPB1 | retro86-big / DPB2 |
|-------|--------------------|----------------------|--------------------|
| seclen / SPT | 512 / 32 | 512 / 32 | 512 / 32 |
| tracks | 40 (40x8x512) | 129 (129x8x512) | 2049 (2049x8x512) |
| blocksize / BSH | 1024 / 3 | 2048 / 4 | 16384 / 7 |
| maxdir / DRM | 64 / 63h | 128 / 7Fh | 512 / 1FFh |
| EXM | 0 | 0 (fixed in VERIFY-028) | 7 |
| DSM | 9Bh (155) | FFh (255) | 1FFh (511) |
| AL0 / CKS / OFF | C0 / 10h / 1 | C0 / 20h / 1 | 80 / 0 / 1 |
| diskdef os | 2.2 | 2.2 | 2.2 |
| firmware tool | BLOCK_SIZE 1024 | LARGE_BLOCK_SIZE 2048 | BIG_BLOCK_SIZE 16384, `block_pointer_size 2`, `first_data_block 1`, `exm 7` |

`retro86-big` was added to `tools/cpm86/diskdefs` in this phase; the earlier
file only covered 160 and LARGE. The DPB fields, the firmware tool constants
and the diskdef geometry agree for all three profiles (sectrk 8, one reserved
track, image sizes 163840 / 528384 / 8392704 bytes). cpmtools 2.23 has no
`exm` diskdef keyword, so the EXM column cannot be expressed in a diskdef;
cpmtools derives an extent layout internally from `blocksize` instead, which
is the source of the interop differences in section 4.

## 3. cpmcp round trip (exit criterion of 86-04)

For each profile: `mkfs.cpm` + `cpmcp` copy-in + `cpmls` + `cpmcp` copy-out,
byte-identical (size and content) for every file transferred: 11/11.

| File | size | 160 | large | big |
|------|------|-----|-------|-----|
| TINY.BIN | 128 B | OK | OK | OK |
| BOUNDARY.BIN | 16384 B | OK | OK | OK |
| MULTI.BIN | 20000 B | OK | OK | OK |
| HUGE.BIN | 65536 B | not placed | OK | OK |

`cpmcp` could not place the 64 KiB file on the 160 profile at all (no
directory entry was created and it reported no usable copy); that content
shape is therefore only proven on LARGE and BIG.

## 4. Cross-validation with the firmware decoder (interop)

The round trip only proves cpmtools agrees with itself. Every image was
therefore re-read with the in-repo decoder that implements the firmware
layout (same extent/rc/pointer semantics as DPB0/1/2): `extract_file` per
file, compared against the original source bytes.

| File | 160 | large | big |
|------|-----|-------|-----|
| TINY.BIN | extract MATCH | extract MATCH | extract MATCH |
| BOUNDARY.BIN | extract MATCH | extract MATCH | extract MATCH |
| MULTI.BIN | extract MATCH (two entries EX=0/EX=1) | FAIL: missing CP/M extent | extract MATCH |
| HUGE.BIN | (absent) | FAIL: missing CP/M extent | extract MATCH |

Directory-dump detail for the LARGE failures: cpmtools stored the 20 KiB file
as a single entry `EX=1 rc=29` with 10 block bytes spilling into the reserved
bytes 24..25, and the 64 KiB file as two entries `EX=1`/`EX=3` with 12 block
bytes each. That is a CP/M-3-style two-entries-per-32-KiB-group layout. The
firmware DPB1 (`EXM=0`) expects one 16 KiB extent (128 records, 8 two-KiB
block pointers in bytes 16..23) per entry with extent numbers 0,1,2,3,... -
exactly the mismatch already noted as a caveat in CPM86-VERIFY-028, now
proven with images. On BIG the derived cpmtools extents happen to decode
correctly under `EXM=7`.

## 5. Verdict per profile

- `retro86-160`: geometry valid; cpmtools round trips files up to ~20 KiB in
  the firmware layout, but a 64 KiB file cannot be written by `cpmcp` and the
  1-KiB block / EXM=0 combination addresses at most 8 blocks per entry, so
  files above 8 KiB on a 160-profile drive remain suspect for the real guest
  BDOS (the firmware tool itself refuses such entries). Not a validator for
  anything beyond small files; use `tools/cpm86/diskImageCpm86.py`.
- `retro86-large`: geometry valid and consistent with DPB1 after the
  VERIFY-028 EXM fix, but cpmtools' internal extent layout (EXM=1 style) is
  incompatible with DPB1 EXM=0 for files >16 KiB: cpmcp-written multi-extent
  LARGE files are unreadable by the firmware decoder and must not be assumed
  readable by the guest. Validator for multi-extent LARGE content remains
  `tools/cpm86/diskImageCpm86.py` plus the `testCpm86Boot` two-extent `TYPE`
  fixture (CPM86-VERIFY-028).
- `retro86-big` (new): geometry valid (mkfs accepts the 16 KiB blocks); all
  four content shapes extract byte-identically through the firmware decoder.
  Multi-extent BIG content has not yet been exercised by the real guest on
  hardware (hardware fill tests use ~7.5 KiB single-extent files), so that
  remains open evidence.

## 6. Conclusions recorded to the work log

1. `tools/cpm86/diskdefs` is trustworthy as a geometry/format validator
   (mkfs.cpm + cpmls agree with the BIOS DPBs on every auditable field).
2. It is not a trustworthy content validator for multi-extent LARGE files;
   `diskImageCpm86.py` + guest `TYPE` fixtures stay authoritative there.
3. `retro86-big` diskdef added and validated.
4. cpmtools cannot express EXM; the 160-profile EXM=0-with-1-KiB-block design
   limit (max ~8 KiB per guest file) and the unproven multi-extent BIG guest
   case are handed to the geometry audit (CPM86-ISSUE-005).
