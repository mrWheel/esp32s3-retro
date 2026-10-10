#!/usr/bin/env python3
"""Tokenize numbered Applesoft BASIC source listings."""

import re

PROGRAM_START = 0x0801
MAX_LINE_NUMBER = 63999
MAX_SOURCE_LINE_LENGTH = 239

_TOKENS = (
    ("HIMEM:", 0xA3),
    ("LOMEM:", 0xA4),
    ("HCOLOR=", 0x92),
    ("SPEED=", 0xA9),
    ("SCALE=", 0x99),
    ("COLOR=", 0xA0),
    ("NOTRACE", 0x9C),
    ("INVERSE", 0x9E),
    ("NORMAL", 0x9D),
    ("RETURN", 0xB1),
    ("RESTORE", 0xAE),
    ("RESUME", 0xA6),
    ("RECALL", 0xA7),
    ("GOSUB", 0xB0),
    ("GOTO", 0xAB),
    ("PRINT", 0xBA),
    ("INPUT", 0x84),
    ("RIGHT$", 0xE9),
    ("LEFT$", 0xE8),
    ("MID$", 0xEA),
    ("STR$", 0xE4),
    ("CHR$", 0xE7),
    ("SCRN(", 0xD7),
    ("SPC(", 0xC3),
    ("TAB(", 0xC0),
    ("ONERR", 0xA5),
    ("IN#", 0x8B),
    ("PR#", 0x8A),
    ("HGR2", 0x90),
    ("HLIN", 0x8E),
    ("VLIN", 0x8F),
    ("HPLOT", 0x93),
    ("XDRAW", 0x95),
    ("TRACE", 0x9B),
    ("SHLOAD", 0x9A),
    ("HTAB", 0x96),
    ("VTAB", 0xA2),
    ("PLOT", 0x8D),
    ("DRAW", 0x94),
    ("HGR", 0x91),
    ("TEXT", 0x89),
    ("HOME", 0x97),
    ("END", 0x80),
    ("FOR", 0x81),
    ("NEXT", 0x82),
    ("DATA", 0x83),
    ("DIM", 0x86),
    ("READ", 0x87),
    ("GR", 0x88),
    ("CALL", 0x8C),
    ("POP", 0xA1),
    ("LET", 0xAA),
    ("RUN", 0xAC),
    ("IF", 0xAD),
    ("ON", 0xB4),
    ("WAIT", 0xB5),
    ("LOAD", 0xB6),
    ("SAVE", 0xB7),
    ("DEF", 0xB8),
    ("POKE", 0xB9),
    ("CONT", 0xBB),
    ("LIST", 0xBC),
    ("CLEAR", 0xBD),
    ("GET", 0xBE),
    ("NEW", 0xBF),
    ("TO", 0xC1),
    ("FN", 0xC2),
    ("THEN", 0xC4),
    ("AT", 0xC5),
    ("NOT", 0xC6),
    ("STEP", 0xC7),
    ("AND", 0xCD),
    ("OR", 0xCE),
    ("SGN", 0xD2),
    ("INT", 0xD3),
    ("ABS", 0xD4),
    ("USR", 0xD5),
    ("FRE", 0xD6),
    ("PDL", 0xD8),
    ("POS", 0xD9),
    ("SQR", 0xDA),
    ("RND", 0xDB),
    ("LOG", 0xDC),
    ("EXP", 0xDD),
    ("COS", 0xDE),
    ("SIN", 0xDF),
    ("TAN", 0xE0),
    ("ATN", 0xE1),
    ("PEEK", 0xE2),
    ("LEN", 0xE3),
    ("VAL", 0xE5),
    ("ASC", 0xE6),
    ("REM", 0xB2),
    ("STOP", 0xB3),
    ("+", 0xC8),
    ("-", 0xC9),
    ("*", 0xCA),
    ("/", 0xCB),
    ("^", 0xCC),
    (">", 0xCF),
    ("=", 0xD0),
    ("<", 0xD1),
    ("?", 0xBA),
    ("&", 0xAF),
    ("DEL", 0x85),
    ("ROT=", 0x98),
    ("FLASH", 0x9F),
    ("STORE", 0xA8),
)
_SORTED_TOKENS = tuple(sorted(_TOKENS, key=lambda item: len(item[0]), reverse=True))
_LINE_PATTERN = re.compile(r"^\s*(\d+)(?:\s+(.*)|\s*)$")


def _tokenize_line(source):
    output = bytearray()
    index = 0
    in_string = False
    data_mode = False

    while index < len(source):
        character = source[index]
        if character == '"':
            in_string = not in_string
            output.append(ord(character))
            index += 1
            continue
        if in_string or data_mode:
            output.append(ord(character))
            if data_mode and character == ":":
                data_mode = False
            index += 1
            continue
        if character.isspace():
            index += 1
            continue

        upper_source = source[index:].upper()
        token = None
        for spelling, value in _SORTED_TOKENS:
            if not upper_source.startswith(spelling):
                continue
            end = index + len(spelling)
            if not spelling[0].isalnum() or spelling.endswith(("(", ":")):
                token = (spelling, value)
                break
            if end == len(source) or not (source[end].isalpha() or source[end] == "$"):
                token = (spelling, value)
                break
        if token is None:
            output.append(ord(character.upper()))
            index += 1
            continue

        spelling, value = token
        output.append(value)
        index += len(spelling)
        if value == 0xB2:
            output.extend(source[index:].encode("ascii"))
            break
        if value == 0x83:
            data_mode = True

    return bytes(output)


def tokenize_source(source):
    """Convert a numbered text listing to the in-memory Applesoft program format."""
    if not isinstance(source, str):
        raise TypeError("Applesoft source must be text")
    try:
        source.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("Applesoft source must contain ASCII only") from error

    lines_by_number = {}
    for line_number, source_line in enumerate(source.splitlines(), start=1):
        if not source_line.strip():
            continue
        match = _LINE_PATTERN.fullmatch(source_line)
        if match is None:
            raise ValueError(f"Applesoft source line {line_number} must start with a line number")
        number = int(match.group(1))
        if number > MAX_LINE_NUMBER:
            raise ValueError(f"Applesoft line number {number} exceeds {MAX_LINE_NUMBER}")
        body = match.group(2) or ""
        if len(body) > MAX_SOURCE_LINE_LENGTH:
            raise ValueError(f"Applesoft source line {number} exceeds {MAX_SOURCE_LINE_LENGTH} characters")
        if number in lines_by_number:
            raise ValueError(f"Duplicate Applesoft line number: {number}")
        try:
            tokenized = _tokenize_line(body)
        except UnicodeEncodeError as error:
            raise ValueError(f"Applesoft source line {number} must contain ASCII only") from error
        lines_by_number[number] = tokenized

    if not lines_by_number:
        raise ValueError("Applesoft source contains no numbered program lines")

    program = bytearray()
    for number, tokenized in sorted(lines_by_number.items()):
        line_size = 4 + len(tokenized) + 1
        next_address = PROGRAM_START + len(program) + line_size
        if next_address > 0xFFFF:
            raise ValueError("Tokenized Applesoft program exceeds the 6502 address space")
        program.extend(next_address.to_bytes(2, "little"))
        program.extend(number.to_bytes(2, "little"))
        program.extend(tokenized)
        program.append(0)
    program.extend(b"\x00\x00")
    return bytes(program)


def is_ascii_source(data):
    """Return whether bytes look like a numbered ASCII Applesoft listing."""
    if not data or any(value < 0x09 or 0x0D < value < 0x20 or value > 0x7E for value in data):
        return False
    first_line = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n", 1)[0]
    return bool(re.match(rb"^\s*\d+\b", first_line))
