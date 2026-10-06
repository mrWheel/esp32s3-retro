# CP/M-80 guest system files

`ccp.asm` and `bdos.asm` are the reformatted CP/M-80 sources from [`brouhaha/cpm22`](https://github.com/brouhaha/cpm22/tree/01018abbccce0bdf4874b0b2ed1a048c5fcc2987), pinned to commit `01018abbccce0bdf4874b0b2ed1a048c5fcc2987`. `LICENSE.txt` contains the 2022 clarification granting nonexclusive rights to use, distribute, modify, enhance and otherwise make CP/M and its derivatives available.

The checked-in assembly outputs target a 64 KiB memory map:

| File | Guest address | Size | SHA-256 |
|---|---:|---:|---|
| `ccp-64k.bin` | `$C400` | 2,048 | `3098242f551142a6d8bf3dda3e76cf537938c01915d2cfb628eca6a796a1d431` |
| `bdos-64k.bin` | `$CC00` | 3,584 | `1f57d67afe999dd3fb397d1685da4e3cafeeb3ea49fe42c078a2016d441013a8` |

Source SHA-256:

| File | SHA-256 |
|---|---|
| `ccp.asm` | `015dc575a8e4d8d54057a7d014b90cf17df490c0e356346cc740cb0d3a73d262` |
| `bdos.asm` | `2f715ad338278d5b7c63546492ae1b25b2871ab7d42a70cc2b90ff8aa0f52930` |
| `LICENSE.txt` | `a9bcdbc66bb31b86882e84469f133b3bd5598f46423b4c6bbb6bedb9f2eac754` |

`tools/buildDiskImageCpm80.py` reproducibly composes `littlefs/cpm80/system.dsk` from these checked-in outputs and utility binaries in `utilities/`, plus project-authored `host/HOST.COM`. It places the CCP commands' companion files, common utilities and `WELCOME.TXT` in CP/M directory entries and allocation blocks, bounded by the DPB's 243 blocks. `HOST.COM` is assembled from `host/HOST.ASM` with Homebrew `z80asm`; its source and binary checksums are tracked with the other resources. The upstream utility source pin, non-commercial use scope, per-file hashes and compatibility limits are recorded in `utilities/README.md`. The Macro Assembler AS / `p2bin` toolchain is not vendored, so regenerating the two CCP/BDOS assembly outputs requires the upstream build tools; the exact assembler tool revision used for those outputs remains unpinned. The common host resource-manifest schema is still undefined.
