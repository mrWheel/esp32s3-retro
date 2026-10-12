# UCSD Pascal HOST utility

`HOST.PAS` is the native Apple Pascal command program. It provides `DIR`,
`GET <file> [O]`, `PUT <file> [O]`, and `QUIT` at the `HOST>` prompt and
exchanges files with the ESP32-S3 directory `/microSD/retro/exchange/ucsd`
through the slot-2 bridge (`$C0A0` data, `$C0A1` abort).

`HOSTIO.ASM` holds the 6502 helpers linked into `HOST.CODE`: `EXCHREAD`,
`EXCHWRT`, `EXCHABT`, `EXCHRDB`, `EXCHWRB` and `EXCHCRC` (CRC-32).

## Build (in the guest, with the real tools)

Set the Filer prefix to a writable volume (`P` `WORK1:`), then:

1. `C(ompile` `HOST` to `HOST`
2. `A(ssemble` `HOSTIO` to `HOSTIO`
3. `L(ink` `HOST`, library `HOSTIO`, output `HOSTX`
4. Filer `T(ransfer` `HOSTX.CODE` to the final volume as `HOST.CODE`

The work volume and the `SYSTEM:` tools volume must be writable. Pascal source
files must be Pascal `.TEXT` files (1 KiB header, 1 KiB pages), not plain text.
`tests/ucsdHostGuest.sh` (ctest `ucsdHostGuest`) performs the whole build and
the transfer tests in the emulator.

## Use

`X(ecute)` `APPLE1:HOST` (the linked `HOST.CODE` is on the boot image
`littlefs/ucsd/ApplePascal1-140K.dsk`). Files are read from and written to the
Filer prefix volume, so set it to a writable volume first. Names are 1-15
characters (letters, digits, `.`; first a letter) and are upper-cased.

* `GET name`  - copy exchange file to the prefix volume; fails if it exists
* `GET name O` - replace an existing file (the old file survives a failed GET)
* `PUT name` / `PUT name O` - copy a Pascal file to the exchange directory
* `DIR` - list the exchange directory

Every transfer is CRC-32 checked; a failed GET stores nothing.

## Implementation notes

* GET writes whole blocks with `BLOCKWRITE` (a per-byte `PUT` is far too slow)
  and then sets the exact last-byte count in the volume directory. The Pascal
  runtime keeps a directory cache that overwrites that count on its next
  directory write, so HOST re-applies the lengths of all files received in the
  session (up to 24) after every GET. Beyond 24 files the data is correct but
  the length is rounded up to a whole block.
* PUT reads files with `BLOCKREAD` and the exact length from the directory;
  `FILE OF CHAR` reads are text-like (CR becomes space, NUL ends the file).
* `(*$I-*)` is used and `IORESULT` is checked after file operations.

Evidence and history: `ucsdHostLog.md`.
