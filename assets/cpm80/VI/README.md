# VI for RETRO80 CP/M-80

Derived from the adapted STevie sources based on tsupplis/cpm86-vi,
upstream revision `201d71cfa6d3ae73e85f45af51f2110a18c4e085`.
The original editing engine is retained; this is a native Z80 port.

Build with z88dk 2.4 (bundled SDCC backend):

    Z88DK=/path/to/z88dk sh BUILD.sh

This creates VI.COM at origin 0100h and a linker map. The program uses
standard CP/M-80 BDOS CALL 5. It does not use CP/M-86 or DOS calls.
The supplied executable needs no compiler or external runtime installation.

The source changes from the CP/M-86 adaptation include:

- CP/M-80 FCB loading/saving in cpmio.c, including capacity preflight and
  transactional temporary-file installation for new and existing files.
- z88dk BDOS calling convention in the raw ANSI console and key decoder.
- A 7999-byte usable text buffer and 512-byte editing-history arrays.
- A heap ending below a reserved 2048-byte C stack, based on CP/M's BDOS entry.
- Allocation-failure checks that refuse large linewise copy/delete operations.
- A mutable nonprintable-character display table instead of writable literals.

The target is the project's 64K Z80 CP/M-80 system, whose BDOS is near CC00h.
This build is not suitable for a plain Intel 8080 or a much smaller TPA.
The default compile flags select only the VT100/ANSI and CP/M paths.
Do not enable the inherited, inactive PC BIOS branches in edit.c.

Tests (host C compiler with sanitizers, Python 3, local emulator checkout):

    RETRO_REPO=/path/to/esp32s3-retro python3 test/RUN.py

The driver tests the packaged ../VI.COM by default. A different executable
can be supplied as its first argument. It builds temporary harnesses and
uses only private temporary test disk files, then removes them. No existing
emulator disk is changed. The full-guest tests load the checkout's real CCP,
BDOS and BIOS and run through its cpm80Cpu/Z80 core.

LICENSE.md retains the original editor license. Z88DK-LICENSE.txt contains
the toolchain's license notice; the compiler itself is not included.
