# CP/M-86 HOST utility

`guest/cpm86/host/HOST.A86` is project-authored 8086 source for the native
`HOST.CMD` transfer utility. It is not a translation of `HOST.COM` machine
code. The source implements `HOST DIR`, `HOST GET name.ext`, and
`HOST PUT name.ext` over the RETRO86_V1 byte ports F8h/F9h, including QUERY,
NUL-terminated directory names, streaming payloads, CRC-32, `NAME.HST`
metadata, no-overwrite checks, USER 0 enforcement, abort handling, and
cleanup of files created by an unsuccessful GET. GET and PUT also accept
CP/M-style `*` and `?` wildcards. A failed wildcard transfer prints the
operation, filename and reason, then continues with the remaining matches.

Wildcard PUT first collects up to 128 unique local filenames with BDOS Search
First/Next, then starts the transfers; this avoids changing the BDOS search
cursor while reading files. Repeated directory extents are deduplicated, and
reserved `.HST` metadata files are reported and skipped.

## Audited CP/M-86 ABI

The ABI was checked against the Digital Research *CP/M-86 System Guide*,
June 1981, Section 4:

| Guide reference | Use in HOST |
|---|---|
| §4.1, printed p. 23 | BDOS entry is `INT 224` (`INT 0E0h`); `CL` is the function code, `DL` a byte parameter, `DX` a DS-relative offset, byte return in `AL`. |
| §2.7, printed p. 14; §4.3, printed p. 30 | The transient receives two parsed FCBs at DS-relative 005Ch and 006Ch. The FCB is 33 bytes for sequential access; the utility allocates 36 bytes. Its current-record byte `CR` is at offset 32. |
| §4.3, printed pp. 33–37 | Functions 15/16/19/20/21/22 are OPEN/CLOSE/DELETE/READ SEQUENTIAL/WRITE SEQUENTIAL/MAKE. Sequential operations transfer 128-byte records through the current DMA address. |
| §4.3, printed p. 38 and p. 47 | Function 26 sets the DMA offset in `DX`; function 51 sets the DMA base paragraph in `DX`. Both are set for the program's DS-based buffers. |
| §4.3, printed p. 41 | Function 32 uses `DL=FFh` to return the current user number in `AL`; GET and PUT refuse nonzero users. |
| §3.2–3.4, printed pp. 18–21 | ASM-86 produces the H86 input used by GENCMD to make a native CMD file; the source uses the single-code-group 8080 memory model. |

The F8h/F9h adapter is byte-only: the program uses `IN AL,0F8h`,
`OUT 0F8h,AL`, and `OUT 0F9h,AL`; it does not issue word I/O.
Multi-byte protocol values are little-endian. The sidecar is one 128-byte
record: `HST1`, exact payload length, CRC-32 of padded guest records, and
CRC-32 of exact payload in bytes 0–15; the remaining bytes are zero.

## Native build and A: installation

The CP/M-86 system disk already contains ASM86.CMD and GENCMD.CMD. The
project's host image tools cannot assemble 8086 source, so assemble in a
running CP/M-86 guest using a separate writable work image. Do not use A:
for build output and do not reuse a work disk containing user data.

ASM-86 requires carriage-return line endings; prepare a CRLF copy and put it
on a fresh work image:

```sh
mkdir -p build/cpm86-host
python3 -c 'from pathlib import Path; p=Path("guest/cpm86/host/HOST.A86"); Path("build/cpm86-host/HOST.A86").write_bytes(p.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))'
python3 tools/diskImage.py create --os cpm86 --profile LARGE \
  --sd-root build/cpm86-host hostbuild.dsk
python3 tools/diskImage.py add --os cpm86 --sd-root build/cpm86-host \
  hostbuild.dsk --name HOST.A86 build/cpm86-host/HOST.A86
```

Make this image available as a writable guest drive, then from its prompt run:

```text
E>A:ASM86 E:HOST.A86
E>A:GENCMD E:HOST
```

`ASM86` should create `HOST.H86`; `GENCMD` should create `HOST.CMD`. Keep
the work drive selected so the outputs remain on the writable image. Stop
the guest cleanly before reading that image on the host.

The image tool's `extract` command copies complete CP/M records, including
the final record padding. Since `add` refuses to overwrite an existing CP/M
filename, extract the other boot files and rebuild a candidate A: image with
the newly assembled command:

```sh
mkdir -p build/cpm86-host/rebuild
for file in ASM86.CMD CPM.SYS ED.CMD GENCMD.CMD PIP.CMD; do
  python3 tools/diskImage.py extract --os cpm86 littlefs/cpm86/system.dsk \
    "$file" --output "build/cpm86-host/rebuild/$file"
done
python3 tools/diskImage.py extract --os cpm86 --sd-root build/cpm86-host \
  hostbuild.dsk HOST.CMD --output build/cpm86-host/rebuild/HOST.CMD
python3 tools/diskImage.py create --os cpm86 --profile CPM86 \
  build/cpm86-host/system.dsk
python3 tools/diskImage.py add --os cpm86 build/cpm86-host/system.dsk \
  build/cpm86-host/rebuild/ASM86.CMD build/cpm86-host/rebuild/CPM.SYS \
  build/cpm86-host/rebuild/ED.CMD build/cpm86-host/rebuild/GENCMD.CMD \
  build/cpm86-host/rebuild/HOST.CMD build/cpm86-host/rebuild/PIP.CMD
python3 tools/diskImage.py list --os cpm86 build/cpm86-host/system.dsk
```

The checked-in A: disk now contains the verified command. The candidate was
booted by the host CP/M-86 fixture before installation; the fixture assembles
the current source and exercises DIR, GET, and PUT against the checked-in disk.
To repeat that test, create a separate fresh LARGE work image containing only
the CRLF `HOST.A86` source (do not reuse the image with prior assembler
outputs), then build `hostTests` and run:

```sh
python3 tools/diskImage.py create --os cpm86 --profile LARGE \
  --sd-root build/cpm86-host hosttest.dsk
python3 tools/diskImage.py add --os cpm86 --sd-root build/cpm86-host \
  hosttest.dsk --name HOST.A86 build/cpm86-host/HOST.A86
cmake --build build-host -j4
CPM86_HOST_BUILD_DISK=build/cpm86-host/retro/images/cpm86/hosttest.dsk \
CPM86_HOST_SYSTEM_DISK=littlefs/cpm86/system.dsk ./build-host/hostTests
```

The guest fixture assembles the current source on E:, then exercises the
installed `A:HOST.CMD` for DIR, GET and PUT. It uses a 43-record source file
and checks the returned bytes exactly, exercising the boundary between
consecutive BDOS records. Wildcard regressions cover both `*` and `?`, existing
destinations, unique-name handling across directory extents, a named per-file
error, and successful processing of later matches. When preparing a new
candidate command, `CPM86_HOST_COMPILE_ONLY=1` makes the fixture stop after
ASM86/GENCMD have generated `HOST.CMD`; extract it and rebuild the candidate A:
image before running the full fixture. The installed CMD and system image
hashes are recorded in `littlefs/cpm86/README.txt`.

The first on-device `HOST DIR` attempt stopped with a guest I/O error because
the CP/M-86 firmware had created its CPU core without connecting the shared
host-exchange service. The firmware now initializes the `/microSD/retro/exchange/cpm86`
service and passes it to the CPU core. The updated firmware build succeeds, but
it has not been flashed or retested on hardware; device transfers and ESP32-S3
persistence remain unverified.

## Verification status

ASM-86 reported zero errors and GENCMD created a 6,912-byte `HOST.CMD`. The
host CP/M-86 fixture booted the rebuilt system image and passed native DIR,
single-file GET/PUT and exact-byte round-trip checks for 43 records. Wildcard
GET and PUT regressions also passed: `*` and `?` match as expected, errors name
the affected file and reason, and later matches are still processed. The PUT
regression includes a multi-extent local file and confirms it is processed
once. PUT sends at most one 128-byte DMA record per BDOS read; the prior loop
walked past that buffer and faulted on larger files. The checked-in A: image
contains the rebuilt command. Image extraction tests also cover multi-extent
files, final-record padding, and refusal to overwrite an existing output.
Hardware transfer acceptance and broader CP/M-86 acceptance remain open; this
desktop guest result is not a hardware claim.
