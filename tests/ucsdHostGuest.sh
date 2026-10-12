#!/bin/sh
#-- Guest proof for the native Apple Pascal HOST utility.
#-- usage: ucsdHostGuest.sh <apple2DiskTests> <repoRoot>
#-- Compiles, assembles and links HOST in the emulated Apple Pascal tools, then runs GET and PUT
#-- against a real exchange directory and compares every transferred file byte for byte.
set -e
tester="$1"
root="$2"
tmp="$(mktemp -d /tmp/ucsdHostGuest.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT

boot="$root/littlefs/ucsd/ApplePascal1-140K.dsk"
rom="$root/littlefs/apple2/apple2e.rom"
tools="$root/sdcard/retro/images/ucsd/ApplePascal2-640K.po"
work="$root/sdcard/retro/images/ucsd/pascalWork1.po"
pas="$root/guest/ucsd/host/HOST.PAS"
asm="$root/guest/ucsd/host/HOSTIO.ASM"
scripts="$root/tests/ucsdHost"

mkdir "$tmp/src" "$tmp/put"
python3 -I - "$tmp/src" <<'PYEOF'
import sys
d = sys.argv[1] + '/'
open(d + 'HELLO.TXT', 'wb').write(b'Hello from HOST\r\n')
open(d + 'EMPTY.TXT', 'wb').write(b'')
open(d + 'BIN.DAT', 'wb').write(bytes((i * 29 + 3) % 256 for i in range(773)))
open(d + 'B512.DAT', 'wb').write(bytes(range(256)) * 2)
open(d + 'B513.DAT', 'wb').write(bytes((i * 7) % 256 for i in range(513)))
open(d + 'B1000.DAT', 'wb').write(bytes((i * 13 + 5) % 256 for i in range(1000)))
open(d + 'EIGHTK.DAT', 'wb').write(bytes((i * 31 + i // 256) % 256 for i in range(8192)))
open(d + 'BIGFILE.DAT', 'wb').write(bytes(i % 251 for i in range(450000)))
PYEOF

#-- Compile, assemble and link with the real guest tools; the sources are staged by the harness.
"$tester" --pascal-script "$boot" "$rom" "$tools" "$work" "$pas" "$asm" "$scripts/build.txt" "$tmp/built.po"

#-- GET: exchange directory -> Pascal volume (includes errors, overwrite and a disk-full abort).
UCSD_SKIP_STAGE=1 UCSD_EXCHANGE_DIR="$tmp/src" "$tester" --pascal-script "$boot" "$rom" "$tools" \
  "$tmp/built.po" "$pas" "$asm" "$scripts/get.txt" "$tmp/received.po"

#-- PUT: Pascal volume -> empty exchange directory.
UCSD_SKIP_STAGE=1 UCSD_EXCHANGE_DIR="$tmp/put" "$tester" --pascal-script "$boot" "$rom" "$tools" \
  "$tmp/received.po" "$pas" "$asm" "$scripts/put.txt" "$tmp/sent.po"

for name in HELLO.TXT EMPTY.TXT BIN.DAT B512.DAT B513.DAT B1000.DAT EIGHTK.DAT
do
  cmp "$tmp/src/$name" "$tmp/put/$name"
done
test -s "$tmp/put/HOSTX.CODE"
test ! -e "$tmp/put/BIGFILE.DAT"
echo "ucsdHostGuest PASS"
