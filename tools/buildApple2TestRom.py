#!/usr/bin/env python3
"""Build the project's original, minimal Apple II phase-1 diagnostic ROM."""

import argparse
from pathlib import Path

ROM_SIZE = 12 * 1024
ROM_BASE = 0xD000


class Assembler6502:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.relativeFixups = []
        self.absoluteFixups = []

    def label(self, name):
        if name in self.labels:
            raise ValueError(f"duplicate label: {name}")
        self.labels[name] = len(self.code)

    def emit(self, *values):
        self.code.extend(values)

    def immediate(self, opcode, value):
        self.emit(opcode, value)

    def absolute(self, opcode, address):
        if isinstance(address, str):
            offset = len(self.code)
            self.emit(opcode, 0, 0)
            self.absoluteFixups.append((offset + 1, address))
        else:
            self.emit(opcode, address & 0xFF, (address >> 8) & 0xFF)

    def relative(self, opcode, name):
        offset = len(self.code)
        self.emit(opcode, 0)
        self.relativeFixups.append((offset + 1, name))

    def finish(self):
        for operand, name in self.absoluteFixups:
            if name not in self.labels:
                raise ValueError(f"undefined label: {name}")
            address = ROM_BASE + self.labels[name]
            self.code[operand] = address & 0xFF
            self.code[operand + 1] = (address >> 8) & 0xFF
        for operand, name in self.relativeFixups:
            if name not in self.labels:
                raise ValueError(f"undefined label: {name}")
            target = self.labels[name]
            displacement = target - (operand + 1)
            if not -128 <= displacement <= 127:
                raise ValueError(f"branch to {name} is out of range")
            self.code[operand] = displacement & 0xFF
        if len(self.code) > ROM_SIZE - 6:
            raise ValueError("diagnostic program does not fit in ROM")
        return self.code


def encode_screen_text(text):
    encoded = bytearray()
    for character in text.upper():
        value = ord(character)
        if character.isalpha():
            value = value - ord("A") + 0x41
        elif character == " ":
            value = 0x20
        elif not 0x20 <= value <= 0x3F:
            raise ValueError(f"unsupported screen character: {character!r}")
        encoded.append(value | 0x80)
    encoded.append(0)
    return encoded


def build_rom():
    assembler = Assembler6502()
    assembler.emit(0x78, 0xD8)
    assembler.absolute(0x2C, 0xC051)
    assembler.immediate(0xA2, 0)
    assembler.immediate(0xA9, 0xA0)
    assembler.label("clearPage")
    for address in (0x0400, 0x0500, 0x0600, 0x0700):
        assembler.absolute(0x9D, address)
    assembler.emit(0xE8)
    assembler.relative(0xD0, "clearPage")

    assembler.immediate(0xA2, 0)
    assembler.label("copyTitle")
    assembler.absolute(0xBD, "title")
    assembler.relative(0xF0, "copyPromptSetup")
    assembler.absolute(0x9D, 0x0400)
    assembler.emit(0xE8)
    assembler.relative(0xD0, "copyTitle")

    assembler.label("copyPromptSetup")
    assembler.immediate(0xA2, 0)
    assembler.label("copyPrompt")
    assembler.absolute(0xBD, "prompt")
    assembler.relative(0xF0, "waitForKeySetup")
    assembler.absolute(0x9D, 0x0480)
    assembler.emit(0xE8)
    assembler.relative(0xD0, "copyPrompt")

    assembler.label("waitForKeySetup")
    assembler.immediate(0xA2, 0)
    assembler.label("waitForKey")
    assembler.absolute(0xAD, 0xC000)
    assembler.relative(0x10, "waitForKey")
    assembler.immediate(0x29, 0x7F)
    assembler.emit(0x85, 0x00)
    assembler.absolute(0x2C, 0xC010)
    assembler.emit(0xA5, 0x00)
    assembler.immediate(0x09, 0x80)
    assembler.absolute(0x9D, 0x0500)
    assembler.emit(0xE8)
    assembler.immediate(0xE0, 40)
    assembler.relative(0xD0, "waitForKey")
    assembler.immediate(0xA2, 0)
    assembler.absolute(0x4C, "waitForKey")

    assembler.label("title")
    assembler.code.extend(encode_screen_text("APPLE II PHASE 1 TEST ROM"))
    assembler.label("prompt")
    assembler.code.extend(encode_screen_text("TYPE ON USB KEYBOARD:"))

    rom = bytearray([0xEA]) * ROM_SIZE
    program = assembler.finish()
    rom[:len(program)] = program
    for vector_offset in (0x2FFA, 0x2FFC, 0x2FFE):
        rom[vector_offset] = ROM_BASE & 0xFF
        rom[vector_offset + 1] = (ROM_BASE >> 8) & 0xFF
    return rom


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "apple2-diagnostic.rom",
    )
    arguments = parser.parse_args()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(build_rom())
    print(f"Wrote {ROM_SIZE} byte phase-1 diagnostic ROM: {arguments.output}")


if __name__ == "__main__":
    main()
