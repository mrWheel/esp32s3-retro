#!/usr/bin/env python3
"""Build the project-authored 256-byte boot ROMs (slots 4..7) for the Apple II Disk II controller.

The program is written from the published behaviour of the Disk II interface and from the
project's own host-side contract (see designApple2.md, APPLE-DEC-020). It is not derived from
any existing controller ROM. It reads track 0 sectors only, which is all the DOS 3.3 boot
chain needs; later disk access is done by the guest operating system through the softswitches.
"""

import argparse
from pathlib import Path

FIRST_SLOT = 4
LAST_SLOT = 7
ROM_SIZE = 256
READ_ENTRY = 0x5C

TABLE_BASE = 0x026A
AUX_BUFFER = 0x0200


class Asm:
    def __init__(self, romBase):
        self.romBase = romBase
        self.code = bytearray()
        self.labels = {}
        self.relFix = []
        self.absFix = []

    def label(self, name):
        if name in self.labels:
            raise ValueError(f"duplicate label {name}")
        self.labels[name] = len(self.code)

    def org(self, offset):
        if len(self.code) > offset:
            raise ValueError(f"code already past offset {offset:#x}: {len(self.code):#x}")
        self.code.extend(b"\x00" * (offset - len(self.code)))

    def emit(self, *values):
        self.code.extend(values)

    def imm(self, opcode, value):
        self.emit(opcode, value & 0xFF)

    def absolute(self, opcode, target):
        if isinstance(target, str):
            self.absFix.append((len(self.code) + 1, target))
            self.emit(opcode, 0, 0)
        else:
            self.emit(opcode, target & 0xFF, target >> 8)

    def branch(self, opcode, name):
        self.relFix.append((len(self.code) + 1, name))
        self.emit(opcode, 0)

    def finish(self):
        for pos, name in self.absFix:
            address = self.romBase + self.labels[name]
            self.code[pos] = address & 0xFF
            self.code[pos + 1] = address >> 8
        for pos, name in self.relFix:
            displacement = self.labels[name] - (pos + 1)
            if not -128 <= displacement <= 127:
                raise ValueError(f"branch to {name} out of range ({displacement})")
            self.code[pos] = displacement & 0xFF
        return bytes(self.code)


def build(slot):
    romBase = 0xC000 + slot * 0x100
    io = 0xC080 + slot * 0x10
    DATA = io + 0xC
    a = Asm(romBase)
    # Autostart signature bytes ($Cn01=$20, $Cn03=$00, $Cn05=$03, $Cn07=$3C) carried by harmless BIT instructions.
    a.emit(0x24, 0x20, 0x24, 0x00, 0x24, 0x03, 0x24, 0x3C)

    a.imm(0xA9, slot * 16)  # LDA #slot*16
    a.emit(0x85, 0x2B)  # STA $2B
    a.imm(0xA9, 0x00)  # LDA #$00
    a.emit(0x85, 0x26)  # STA $26   buffer pointer low
    a.emit(0x85, 0x3D)  # STA $3D   wanted physical sector
    a.imm(0xA9, 0x08)  # LDA #$08
    a.emit(0x85, 0x27)  # STA $27   buffer pointer high
    a.absolute(0x20, "buildTable")  # JSR buildTable
    a.absolute(0xAD, io + 0x9)  # LDA $C0E9 motor on
    a.absolute(0xAD, io + 0xa)  # LDA $C0EA drive 1
    a.absolute(0xAD, io + 0xe)  # LDA $C0EE Q7 low
    a.absolute(0xAD, io + 0xc)  # LDA $C0EC Q6 low
    a.imm(0xA0, 80)  # LDY #80 half-track steps toward track 0
    a.label("step")
    a.emit(0x98)  # TYA
    a.imm(0x29, 0x03)  # AND #$03
    a.emit(0x0A)  # ASL
    a.emit(0xAA)  # TAX
    a.absolute(0xBD, io + 0x1)  # LDA $C0E1,X phase on
    a.imm(0xA9, 0x00)  # LDA #$00
    a.label("delay")
    a.imm(0xE9, 0x01)  # SBC #$01
    a.branch(0xD0, "delay")  # BNE delay
    a.absolute(0xBD, io + 0x0)  # LDA $C0E0,X phase off
    a.emit(0x88)  # DEY
    a.branch(0x10, "step")  # BPL step (Y = 80..0 so that phase 0 is energized last)
    a.branch(0x30, "readSector")  # BMI readSector (Y is $FF here)

    a.label("readByte")
    a.absolute(0xAD, DATA)  # LDA $C0EC
    a.branch(0x10, "readByte")  # BPL readByte
    a.emit(0x60)  # RTS

    a.label("findPrologue")  # returns the nibble after D5 AA
    a.absolute(0x20, "readByte")
    a.imm(0xC9, 0xD5)
    a.branch(0xD0, "findPrologue")
    a.absolute(0x20, "readByte")
    a.imm(0xC9, 0xAA)
    a.branch(0xD0, "findPrologue")
    a.absolute(0x4C, "readByte")  # JMP readByte (tail call)

    a.org(READ_ENTRY)
    a.label("readSector")
    a.label("header")
    a.absolute(0x20, "findPrologue")
    a.imm(0xC9, 0x96)  # address field?
    a.branch(0xD0, "header")
    a.imm(0xA0, 0x04)  # LDY #4 skip volume and track (4-and-4)
    a.label("skip")
    a.absolute(0x20, "readByte")
    a.emit(0x88)  # DEY
    a.branch(0xD0, "skip")
    a.absolute(0x20, "readByte")  # sector, odd bits
    a.emit(0x38)  # SEC
    a.emit(0x2A)  # ROL
    a.emit(0x85, 0x3C)  # STA $3C
    a.absolute(0x20, "readByte")  # sector, even bits
    a.emit(0x25, 0x3C)  # AND $3C
    a.emit(0xC5, 0x3D)  # CMP $3D
    a.branch(0xD0, "header")
    a.absolute(0x20, "findPrologue")
    a.imm(0xC9, 0xAD)  # data field?
    a.branch(0xD0, "header")

    a.imm(0xA0, 0x00)  # LDY #0
    a.emit(0x84, 0x3C)  # STY $3C running value
    a.label("readAux")
    a.absolute(0xAD, DATA)
    a.branch(0x10, "readAux")
    a.emit(0xAA)  # TAX
    a.absolute(0xBD, TABLE_BASE)  # LDA table,X
    a.emit(0x45, 0x3C)  # EOR $3C
    a.emit(0x85, 0x3C)  # STA $3C
    a.absolute(0x99, AUX_BUFFER)  # STA $0200,Y
    a.emit(0xC8)  # INY
    a.imm(0xC0, 0x56)  # CPY #86
    a.branch(0xD0, "readAux")

    a.imm(0xA0, 0x00)  # LDY #0
    a.label("readPrimary")
    a.absolute(0xAD, DATA)
    a.branch(0x10, "readPrimary")
    a.emit(0xAA)  # TAX
    a.absolute(0xBD, TABLE_BASE)
    a.emit(0x45, 0x3C)
    a.emit(0x85, 0x3C)
    a.emit(0x91, 0x26)  # STA ($26),Y
    a.emit(0xC8)
    a.branch(0xD0, "readPrimary")

    a.imm(0xA2, 0x00)  # LDX #0 aux index (Y is zero)
    a.label("combine")
    a.emit(0xB1, 0x26)  # LDA ($26),Y
    a.emit(0x85, 0x3C)  # STA $3C
    a.absolute(0xBD, AUX_BUFFER)  # LDA $0200,X
    a.emit(0x4A)  # LSR
    a.emit(0x26, 0x3C)  # ROL $3C
    a.emit(0x4A)  # LSR
    a.emit(0x26, 0x3C)  # ROL $3C
    a.absolute(0x9D, AUX_BUFFER)  # STA $0200,X
    a.emit(0xA5, 0x3C)  # LDA $3C
    a.emit(0x91, 0x26)  # STA ($26),Y
    a.emit(0xE8)  # INX
    a.imm(0xE0, 0x56)  # CPX #86
    a.branch(0xD0, "nextByte")
    a.imm(0xA2, 0x00)  # LDX #0
    a.label("nextByte")
    a.emit(0xC8)  # INY
    a.branch(0xD0, "combine")
    a.emit(0xE6, 0x27)  # INC $27
    a.absolute(0x4C, 0x0801)  # JMP $0801

    a.label("buildTable")
    a.imm(0xA2, 0x00)  # LDX #0 six-bit value
    a.imm(0xA0, 0x96)  # LDY #$96 candidate disk byte
    a.label("candidate")
    a.emit(0x84, 0x40)  # STY $40
    a.emit(0x98)  # TYA
    a.emit(0x4A)  # LSR
    a.emit(0x85, 0x41)  # STA $41  (n >> 1)
    a.emit(0x25, 0x40)  # AND $40
    a.imm(0x29, 0x3F)  # AND #$3F  need two adjacent ones below bit 7
    a.branch(0xF0, "nextCandidate")
    a.emit(0xA5, 0x41)  # LDA $41
    a.emit(0x05, 0x40)  # ORA $40
    a.imm(0x49, 0xFF)  # EOR #$FF  zero-pair map (bit 7 is always clear here)
    a.emit(0x85, 0x42)  # STA $42
    a.emit(0xC6, 0x42)  # DEC $42
    a.emit(0x25, 0x42)  # AND $42  at most one pair of adjacent zeros
    a.branch(0xD0, "nextCandidate")
    a.emit(0x8A)  # TXA
    a.absolute(0x99, TABLE_BASE)  # STA table,Y
    a.emit(0xE8)  # INX
    a.label("nextCandidate")
    a.emit(0xC8)  # INY
    a.branch(0xD0, "candidate")
    a.emit(0x60)  # RTS

    code = a.finish()
    if len(code) > ROM_SIZE:
        raise ValueError(f"boot ROM is {len(code)} bytes, limit {ROM_SIZE}")
    if a.labels["readSector"] != READ_ENTRY:
        raise ValueError("read entry is not at $5C")
    return code.ljust(ROM_SIZE, b"\x00"), len(code)


def writeHeader(path, roms, used):
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "//-- Generated by tools/buildApple2DiskBootRom.py. Project-authored boot ROMs for slots 4..7",
        f"//-- (index = slot - 4); {used} of 256 bytes used per ROM. Do not edit by hand.",
        "static const uint8_t apple2DiskBootRoms[4][256] = {",
    ]
    for slot, rom in zip(range(FIRST_SLOT, LAST_SLOT + 1), roms):
        lines.append(f"    //-- slot {slot}")
        lines.append("    {")
        for offset in range(0, ROM_SIZE, 16):
            chunk = ", ".join(f"0x{value:02X}" for value in rom[offset : offset + 16])
            lines.append(f"        {chunk},")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    Path(path).write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", default="components/apple2Core/apple2DiskBootRom.h")
    parser.add_argument("--binary", help="write the slot-6 ROM as a raw binary")
    args = parser.parse_args()
    built = [build(slot) for slot in range(FIRST_SLOT, LAST_SLOT + 1)]
    roms = [rom for rom, _ in built]
    used = built[0][1]
    writeHeader(args.header, roms, used)
    if args.binary:
        Path(args.binary).write_bytes(roms[6 - FIRST_SLOT])
    print(f"boot ROMs: {used} bytes used each, header written to {args.header}")


if __name__ == "__main__":
    main()
