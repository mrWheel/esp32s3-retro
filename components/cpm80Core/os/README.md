# CP/M-80 guest system files

`ccp.asm` and `bdos.asm` are the reformatted CP/M-80 sources from [`brouhaha/cpm22`](https://github.com/brouhaha/cpm22/tree/01018abbccce0bdf4874b0b2ed1a048c5fcc2987), pinned to commit `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`. `LICENSE.txt` contains the 2022 clarification granting nonexclusive rights to use, distribute, modify, enhance and otherwise make CP/M and its derivatives available.

The checked-in assembly outputs target a 64 KiB memory map:

| File | Guest address | Size |
|---|---:|---:|
| `ccp-64k.bin` | `$C400` | 2,048 |
| `bdos-64k.bin` | `$CC00` | 3,584 |

`tools/include/buildDiskImageCpm80.py` reproducibly composes `littlefs/cpm80/system.dsk` from these checked-in outputs, utility binaries in `utilities/`, and project-authored `guest/cpm80/host/HOST.COM`. It places the CCP commands' companion files, common utilities and `WELCOME.TXT` in CP/M directory entries and allocation blocks, bounded by the DPB's 243 blocks. `HOST.COM` is assembled from the 8080 source `guest/cpm80/host/HOST.ASM` inside the emulated CP/M-80 with the standard `ASM.COM` and `LOAD.COM` (see `tools/README.md`). `HOST GET` and `HOST PUT` support `*` and `?` wildcards. The upstream utility source pin, non-commercial use scope, and compatibility limits are recorded in `utilities/README.md`. The Macro Assembler AS / `p2bin` toolchain is not vendored, so regenerating the two CCP/BDOS assembly outputs requires the upstream build tools; the exact assembler tool revision used for those outputs remains unpinned. The common host resource-manifest schema is still undefined.
