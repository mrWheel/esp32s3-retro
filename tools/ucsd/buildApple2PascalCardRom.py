#!/usr/bin/env python3
"""Build the project-authored Apple Pascal 1.1 firmware-protocol layer of the slot-3 80-column card.

The card keeps its existing PR#3 entry ($C300) and the $C800 COUT firmware. This tool adds
- the slot-3 ROM page ($C300..$C3FF) with the Pascal 1.1 identification bytes ($Cn05=$38, $Cn07=$18,
  $Cn0B=$01) and the four entry offsets (init, read, write, status), and
- the Pascal entry routines in the expansion ROM at $CA00.

Apple Pascal 1.1 detects the card by those bytes and then sends every console character through the write
entry (X=$Cn, Y=$n0, A=character) and reads keys through the read/status entries. The control characters
are the ones the Pascal built-in 40-column driver understands (see designUCSD.md, UCSD-ISSUE-009).
The program is written from the published protocol and from the project's own card (CRTC at $C0B0,
2 KiB VRAM in four banks, cursor in the card workspace $06..$0D that the host maps while firmware runs).
"""

import argparse
import sys
from pathlib import Path

APPLE2_TOOLS_DIR = Path(__file__).resolve().parents[1] / "apple2"
sys.path.insert(0, str(APPLE2_TOOLS_DIR))
from buildApple2DiskBootRom import Asm  # noqa: E402

PAGE_BASE = 0xC300
FIRMWARE_BASE = 0xCA00
FIRMWARE_LIMIT = 0xCC00

OLD_INIT_CRTC = 0xC812  # CRTC register programming of the original firmware, ends with RTS
OLD_INIT = 0xC800
OLD_COUT = 0xC8B3
OLD_ROW_LOW = 0xC941
OLD_ROW_HIGH = 0xC959

COL = 0x06
ROW = 0x07
ADDR_LOW = 0x08
ADDR_HIGH = 0x09
CHAR = 0x0A
STATE = 0x0B
END_LOW = 0x0C
END_HIGH = 0x0D

BANK_SELECT = 0xC0B0
VRAM_EVEN = 0xCC00
VRAM_ODD = 0xCD00
KEYBOARD = 0xC000
STROBE = 0xC010

TEXT_ROWS = 24
TEXT_COLUMNS = 80
SCREEN_CELLS = TEXT_ROWS * TEXT_COLUMNS


def jmp(a, name):
    a.absolute(0x4C, name)


def jsr(a, name):
    a.absolute(0x20, name)


def controlDispatch(a, character, handler):
    skip = f"skip{handler}"
    a.imm(0xC9, character)  # CMP #character
    a.branch(0xD0, skip)  # BNE skip
    jmp(a, handler)  # JMP handler
    a.label(skip)


def build():
    a = Asm(FIRMWARE_BASE)

    a.label("pr3")  # target of the $C300 entry: the PR#3 behaviour the card has always had
    a.emit(0x48)  # PHA
    jsr(a, OLD_INIT)  # JSR $C800
    a.emit(0x68)  # PLA
    jmp(a, OLD_COUT)  # JMP $C8B3

    a.label("pInit")
    a.emit(0x48, 0x8A, 0x48, 0x98, 0x48)  # PHA TXA PHA TYA PHA
    a.imm(0xA9, 0x00)  # LDA #0
    a.emit(0x85, COL)  # STA col
    a.emit(0x85, ROW)  # STA row
    a.emit(0x85, STATE)  # STA state
    jsr(a, OLD_INIT_CRTC)  # JSR CRTC init
    a.imm(0xA9, 0x00)
    a.emit(0x85, ADDR_LOW)
    a.emit(0x85, ADDR_HIGH)
    jsr(a, "clearAll")
    jmp(a, "done")

    a.label("pStatus")
    a.imm(0xC9, 0x01)  # CMP #1: input status?
    a.branch(0xD0, "outputReady")  # BNE outputReady
    a.absolute(0xAD, KEYBOARD)  # LDA $C000
    a.emit(0x0A)  # ASL: carry = key waiting
    a.imm(0xA2, 0x00)  # LDX #0
    a.emit(0x60)  # RTS
    a.label("outputReady")
    a.emit(0x38)  # SEC
    a.imm(0xA2, 0x00)
    a.emit(0x60)

    a.label("pRead")
    a.absolute(0xAD, KEYBOARD)  # LDA $C000
    a.branch(0x10, "noKey")  # BPL noKey
    a.absolute(0x2C, STROBE)  # BIT $C010 clears the strobe
    a.imm(0x29, 0x7F)  # AND #$7F
    a.imm(0xA2, 0x00)  # LDX #0
    a.emit(0x60)
    a.label("noKey")
    a.imm(0xA9, 0x00)
    a.imm(0xA2, 0x00)
    a.emit(0x60)

    a.label("pWrite")
    a.emit(0x85, CHAR)  # STA char (workspace)
    a.emit(0x48, 0x8A, 0x48, 0x98, 0x48)  # PHA TXA PHA TYA PHA
    a.emit(0xA5, CHAR)
    a.imm(0x29, 0x7F)
    a.emit(0x85, CHAR)
    a.emit(0xA5, STATE)  # LDA state
    a.branch(0xF0, "noPending")  # BEQ noPending
    jmp(a, "pending")
    a.label("noPending")
    a.emit(0xA5, CHAR)
    a.imm(0xC9, 0x20)  # CMP #$20
    a.branch(0x90, "control")  # BCC control
    jsr(a, "calcAddr")
    a.emit(0xA5, CHAR)
    jsr(a, "putCell")
    a.emit(0xA5, COL)
    a.imm(0xC9, TEXT_COLUMNS - 1)
    a.branch(0xB0, "done")  # BCS done: the cursor stays on the last column
    a.emit(0xE6, COL)  # INC col
    a.label("done")
    a.emit(0x68, 0xA8, 0x68, 0xAA, 0x68)  # PLA TAY PLA TAX PLA
    a.imm(0xA2, 0x00)  # LDX #0 (IORESULT)
    a.emit(0x60)

    a.label("control")
    controlDispatch(a, 0x0D, "doCr")
    controlDispatch(a, 0x0A, "doLf")
    controlDispatch(a, 0x08, "doBs")
    controlDispatch(a, 0x1C, "doRight")
    controlDispatch(a, 0x1F, "doUp")
    controlDispatch(a, 0x19, "doHome")
    controlDispatch(a, 0x0C, "doClearAll")
    controlDispatch(a, 0x0B, "doClearEos")
    controlDispatch(a, 0x1D, "doClearEol")
    controlDispatch(a, 0x1E, "doGoto")
    controlDispatch(a, 0x1B, "doEscape")
    jmp(a, "done")

    a.label("doCr")
    a.imm(0xA9, 0x00)
    a.emit(0x85, COL)
    jmp(a, "done")

    a.label("doLf")
    a.emit(0xA5, ROW)
    a.imm(0xC9, TEXT_ROWS - 1)
    a.branch(0x90, "lfInc")  # BCC lfInc
    a.emit(0xE6, ROW)  # INC row: 24, the host scrolls when 23 is stored next
    a.imm(0xA9, TEXT_ROWS - 1)
    a.emit(0x85, ROW)
    jmp(a, "done")
    a.label("lfInc")
    a.emit(0xE6, ROW)
    jmp(a, "done")

    a.label("doBs")
    a.emit(0xA5, COL)
    a.branch(0xF0, "bsDone")
    a.emit(0xC6, COL)  # DEC col
    a.label("bsDone")
    jmp(a, "done")

    a.label("doRight")
    a.emit(0xA5, COL)
    a.imm(0xC9, TEXT_COLUMNS - 1)
    a.branch(0xB0, "rightDone")
    a.emit(0xE6, COL)
    a.label("rightDone")
    jmp(a, "done")

    a.label("doUp")
    a.emit(0xA5, ROW)
    a.branch(0xF0, "upDone")
    a.emit(0xC6, ROW)
    a.label("upDone")
    jmp(a, "done")

    a.label("doHome")
    a.imm(0xA9, 0x00)
    a.emit(0x85, COL)
    a.emit(0x85, ROW)
    jmp(a, "done")

    a.label("doClearAll")
    a.imm(0xA9, 0x00)
    a.emit(0x85, COL)
    a.emit(0x85, ROW)
    a.emit(0x85, ADDR_LOW)
    a.emit(0x85, ADDR_HIGH)
    jsr(a, "clearAll")
    jmp(a, "done")

    a.label("doClearEos")
    jsr(a, "calcAddr")
    jsr(a, "setEndOfScreen")
    jsr(a, "clearSpan")
    jmp(a, "done")

    a.label("doClearEol")
    jsr(a, "calcAddr")
    a.emit(0xA6, ROW)  # LDX row
    a.absolute(0xBD, OLD_ROW_LOW)  # LDA rowLow,X
    a.emit(0x18)  # CLC
    a.imm(0x69, TEXT_COLUMNS)  # ADC #80
    a.emit(0x85, END_LOW)
    a.absolute(0xBD, OLD_ROW_HIGH)  # LDA rowHigh,X
    a.imm(0x69, 0x00)
    a.emit(0x85, END_HIGH)
    jsr(a, "clearSpan")
    jmp(a, "done")

    a.label("doGoto")
    a.imm(0xA9, 0x03)
    a.emit(0x85, STATE)
    jmp(a, "done")

    a.label("doEscape")
    a.imm(0xA9, 0x01)
    a.emit(0x85, STATE)
    jmp(a, "done")

    a.label("pending")
    a.imm(0xC9, 0x02)  # CMP #2
    a.branch(0x90, "clearPending")  # BCC: state 1 swallows one character
    a.branch(0xF0, "gotoRow")  # BEQ: state 2 is the row
    a.emit(0xA5, CHAR)  # state 3: the column
    a.emit(0x38)
    a.imm(0xE9, 0x20)
    a.branch(0x30, "columnBad")
    a.imm(0xC9, TEXT_COLUMNS)
    a.branch(0x90, "columnOk")
    a.label("columnBad")
    a.imm(0xA9, 0x00)
    a.label("columnOk")
    a.emit(0x85, END_LOW)
    a.imm(0xA9, 0x02)
    a.emit(0x85, STATE)
    jmp(a, "done")
    a.label("gotoRow")
    a.emit(0xA5, CHAR)
    a.emit(0x38)
    a.imm(0xE9, 0x20)
    a.branch(0x30, "rowBad")
    a.imm(0xC9, TEXT_ROWS)
    a.branch(0x90, "rowOk")
    a.label("rowBad")
    a.imm(0xA9, 0x00)
    a.label("rowOk")
    a.emit(0x85, ROW)
    a.emit(0xA5, END_LOW)
    a.emit(0x85, COL)
    a.label("clearPending")
    a.imm(0xA9, 0x00)
    a.emit(0x85, STATE)
    jmp(a, "done")

    a.label("calcAddr")  # addr = rowBase(row) + col
    a.emit(0xA6, ROW)
    a.absolute(0xBD, OLD_ROW_LOW)
    a.emit(0x18)
    a.emit(0x65, COL)
    a.emit(0x85, ADDR_LOW)
    a.absolute(0xBD, OLD_ROW_HIGH)
    a.imm(0x69, 0x00)
    a.emit(0x85, ADDR_HIGH)
    a.emit(0x60)

    a.label("setEndOfScreen")
    a.imm(0xA9, SCREEN_CELLS & 0xFF)
    a.emit(0x85, END_LOW)
    a.imm(0xA9, SCREEN_CELLS >> 8)
    a.emit(0x85, END_HIGH)
    a.emit(0x60)

    a.label("clearAll")  # clears from addr to the end of the screen
    jsr(a, "setEndOfScreen")
    a.label("clearSpan")  # fills [addr, end) with spaces
    a.emit(0xA5, ADDR_LOW)
    a.emit(0xC5, END_LOW)
    a.branch(0xD0, "clearCell")
    a.emit(0xA5, ADDR_HIGH)
    a.emit(0xC5, END_HIGH)
    a.branch(0xF0, "clearEnd")
    a.label("clearCell")
    a.imm(0xA9, 0x20)
    jsr(a, "putCell")
    a.emit(0xE6, ADDR_LOW)
    a.branch(0xD0, "clearSpan")
    a.emit(0xE6, ADDR_HIGH)
    jmp(a, "clearSpan")
    a.label("clearEnd")
    a.emit(0x60)

    a.label("putCell")  # stores A at VRAM address addr, selecting the 512-byte bank
    a.emit(0x48)  # PHA
    a.emit(0xA5, ADDR_HIGH)
    a.emit(0x4A, 0x0A, 0x0A, 0xAA)  # LSR ASL ASL TAX
    a.imm(0xA9, 0x00)
    a.absolute(0x9D, BANK_SELECT)  # STA $C0B0,X
    a.emit(0xA5, ADDR_HIGH)
    a.imm(0x29, 0x01)
    a.branch(0xD0, "oddPage")
    a.emit(0xA4, ADDR_LOW)  # LDY addr
    a.emit(0x68)  # PLA
    a.absolute(0x99, VRAM_EVEN)  # STA $CC00,Y
    a.emit(0x60)
    a.label("oddPage")
    a.emit(0xA4, ADDR_LOW)
    a.emit(0x68)
    a.absolute(0x99, VRAM_ODD)  # STA $CD00,Y
    a.emit(0x60)

    code = a.finish()
    if FIRMWARE_BASE + len(code) > FIRMWARE_LIMIT:
        raise ValueError(f"Pascal firmware is {len(code)} bytes and does not fit below ${FIRMWARE_LIMIT:04X}")
    return code, a.labels


def buildPage(labels):
    entry = lambda name: FIRMWARE_BASE + labels[name]
    page = bytearray(b"\xff" * 256)

    def put(offset, *values):
        page[offset : offset + len(values)] = bytes(values)

    initOffset, readOffset, writeOffset, statusOffset = 0x20, 0x23, 0x26, 0x29
    put(0x00, 0x4C, entry("pr3") & 0xFF, entry("pr3") >> 8)  # JMP pr3: the PR#3 entry
    put(0x03, 0xEA, 0xEA)
    put(0x05, 0x38)  # Pascal firmware identification
    put(0x06, 0xEA)
    put(0x07, 0x18)
    put(0x08, 0x4C, 0x00, 0xC8)  # JMP $C800 (card initialisation entry kept from the earlier layout)
    put(0x0B, 0x01)  # Pascal 1.1 protocol
    put(0x0C, 0x31)  # device signature
    put(0x0D, initOffset, readOffset, writeOffset, statusOffset)
    for offset, name in ((initOffset, "pInit"), (readOffset, "pRead"), (writeOffset, "pWrite"), (statusOffset, "pStatus")):
        put(offset, 0x4C, entry(name) & 0xFF, entry(name) >> 8)
    return bytes(page), initOffset


def writeHeader(path, code, page, initOffset):
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "//-- Generated by tools/ucsd/buildApple2PascalCardRom.py. Project-authored Apple Pascal 1.1 firmware-protocol",
        "//-- layer of the slot-3 80-column card. Do not edit by hand.",
        "enum",
        "{",
        f"  apple2PascalFirmwareBase = 0x{FIRMWARE_BASE:04X},",
        f"  apple2PascalFirmwareSize = {len(code)},",
        f"  apple2PascalInitEntry = 0x{PAGE_BASE + initOffset:04X}",
        "};",
        "",
        "static const uint8_t apple2PascalSlotRom[256] = {",
    ]
    for offset in range(0, 256, 16):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in page[offset : offset + 16]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t apple2PascalFirmware[apple2PascalFirmwareSize] = {")
    for offset in range(0, len(code), 16):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in code[offset : offset + 16]) + ",")
    lines.append("};")
    lines.append("")
    Path(path).write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", default="components/apple2Core/apple2PascalCardRom.h")
    args = parser.parse_args()
    code, labels = build()
    page, initOffset = buildPage(labels)
    writeHeader(args.header, code, page, initOffset)
    print(f"Pascal card firmware: {len(code)} bytes at ${FIRMWARE_BASE:04X}, header written to {args.header}")


if __name__ == "__main__":
    main()
