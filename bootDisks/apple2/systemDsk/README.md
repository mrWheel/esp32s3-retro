# Apple II DOS 3.3 system-disk files

Place files for the Apple II system disk in this directory. The builder accepts:

- `.TXT` plain DOS text files (including Applesoft listings; use `EXEC NAME.TXT`, then `RUN`)
- `.BAS` tokenized Applesoft BASIC files or numbered ASCII source listings (the builder tokenizes source listings for DOS `LOAD`; the `.BAS` source suffix is omitted from the DOS catalog name)
- `.INT` tokenized Integer BASIC files
- `.BIN` raw binary files, with `--binary-load-address` supplied to the builder

The builder adds these files to a separate, user-supplied bootable DOS 3.3
16-sector disk image. For example, `TEST-NONGR.BAS` is cataloged as the
Applesoft file `TEST-NONGR`, so load and run it with `LOAD TEST-NONGR` and
`RUN`. It does not contain or create Apple DOS boot/system software. Keep any
licensed base image outside this source directory.
