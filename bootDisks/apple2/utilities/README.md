# Apple II utilities

`HOST.C` is the ProDOS 8 file-transfer utility. On macOS, cc65 writes an AppleSingle container; extract its data fork
to produce the raw ProDOS SYS payload:

```sh
cl65 -t apple2 --start-addr 0x2000 -Oirs -o HOST.APPLESINGLE HOST.C HOSTEXIT.S
dd if=HOST.APPLESINGLE of=HOST.SYS bs=1 skip=58
rm HOST.APPLESINGLE *.o
```

`HOSTEXIT.S` moves the C stack below `$9600` (ProDOS puts it at `$BEFF`, on top of BASIC.SYSTEM) and leaves HOST by
closing all files and restarting BASIC.SYSTEM through `$BE00`. Keep the resulting `HOST.SYS` beside this source. To put it on the system disk, copy it into
`bootDisks/apple2/systemDsk`; the builder then stores it as ProDOS SYS file `HOST` (auxiliary address `$2000`).

Run the utility from ProDOS BASIC.SYSTEM with `-HOST`; it stays at the `HOST>` prompt until an empty line or `QUIT`. BASIC.SYSTEM rejects extra words after `-HOST` as a
syntax error, so HOST asks for its command itself at the `HOST>` prompt:

```text
-HOST
HOST> DIR
HOST> GET filename
HOST> GET pattern O
HOST> PUT filename
HOST> PUT pattern O
```

`O` explicitly permits replacement of existing files. `*` and `?` wildcards are supported. Files are exchanged
with `/retro/exchange/apple2` on the host; transfers verify exact byte length and CRC-32. ProDOS metadata is
reconstructed from the filename (`.TXT`, `.BAS`, and `.SYS` receive matching file types); other downloads use BIN.

All output goes through the active output vector (CSW), so it appears on the 80-column card; keys are read straight
from `$C000`/`$C010`. `PUT` prints `PUT name: ` and one `.` per 2 KB while it reads the file and computes the CRC-32
before sending; `GET` prints one `.` per 2 KB received. Download goes through `HOST.TMP` in the current prefix, so the
prefix must be writable (for example `PREFIX /DATA800` when the boot disk is read-only or full).
