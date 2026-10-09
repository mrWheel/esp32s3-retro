# Apple II DOS 3.3 system-disk files

Place files for the Apple II system disk in this directory. The builder accepts:

- `.TXT` plain DOS text files (including Applesoft listings; use `EXEC NAME.TXT`, then `RUN`)
- `.BAS` tokenized Applesoft BASIC files or numbered ASCII source listings (the builder tokenizes source listings for DOS `LOAD`; the `.BAS` source suffix is omitted from the DOS catalog name)
- `.INT` tokenized Integer BASIC files
- `.BIN` raw binary files, with `--binary-load-address` supplied to the builder

The builder writes every file in this directory to a copy of the empty bootable
DOS 3.3 base image `bootDisks/apple2/dos33Empty.dsk` (boot tracks only, no
files). `HELLO.BAS` is the greeting program DOS runs at boot
(`PRINT CHR$(4);"PR#3"` switches to the 80-column card). For example, `TEST-NONGR.BAS` is cataloged as the
Applesoft file `TEST-NONGR`, so load and run it with `LOAD TEST-NONGR` and
`RUN`. This directory holds no DOS boot/system software; that is in the base image.
