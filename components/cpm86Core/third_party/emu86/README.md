# EMU86 CPU core

This directory contains the CPU and instruction-decoding sources from
[mfld-fr/emu86](https://github.com/mfld-fr/emu86), pinned at commit
`81b1634bde99fa70ce0dfe9cc2207a1e0a82d0ac`. The upstream MIT license is
provided in `LICENSE`.

Only the CPU, instruction, register, memory/I/O, and interrupt modules needed
by the project adapter are included. The upstream desktop entry point and
platform-specific front ends are intentionally not included.

## Project changes

The vendored sources are adapted for the ESP-IDF host and its bounded guest
memory:

- Memory has a configurable mapped size up to the 8086 20-bit address-space
  limit. Physical addresses wrap at 1 MiB; accesses outside mapped RAM set
  `mem_fault` instead of indexing outside the allocation.
- Word memory operations use explicit little-endian byte accesses.
- Instruction fetch uses the memory interface, so it observes the same
  address masking and mapped-memory checks as data accesses.
- Opcode disassembly text uses a bounded buffer. Compiler diagnostics needed
  for the ESP-IDF toolchain have also been addressed.
- The project adapter supplies port I/O and invokes one instruction at a time.
- The adapter preserves F8h/F9h for `hostExchange` and accepts optional,
  byte-wide callbacks for other guest I/O ports. Word I/O remains rejected.
- The 8086 DAA instruction is implemented and covered by the host regression
  test that reproduces the reported `ADC AL, imm8; DAA` sequence.
- ADD/ADC, SUB/SBB/CMP, logical operations, INC/DEC and NEG update their
  documented arithmetic flags; host fixtures check flag values and parity
  conditional branching.
- PUSHA saves the original SP value required by the 80186 instruction
  semantics; a host fixture checks the saved stack slot and restored registers.
- MUL/IMUL overflow flags, signed/unsigned division, and width-correct SHL/SAL,
  SHR/SAR and rotate behavior are covered by host regression fixtures.

Upstream processor state is file-static. The adapter consequently permits only
one active core and resets shared state between runs; it is not a
multi-instance CPU library. This integration has only fixture-level coverage.
It is not evidence of complete 8086 conformance, CP/M-86 BIOS compatibility,
or a bootable CP/M-86 system.
