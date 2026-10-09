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
    ("RETURN", 0xB0),
    ("RESTORE", 0xAE),
    ("RESUME", 0xA6),
    ("RECALL", 0xA7),
    ("GOSUB", 0xAF),
    ("GOTO", 0xAB),
    ("PRINT", 0xB9),
    ("INPUT", 0x84),
    ("RIGHT$", 0xE8),
    ("LEFT$", 0xE7),
    ("MID$", 0xE9),
    ("STR$", 0xE3),
    ("CHR$", 0xE6),
    ("SCRN(", 0xD6),
    ("SPC(", 0xC2),
    ("TAB(", 0xBF),
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
    ("ON", 0xB3),
    ("WAIT", 0xB4),
    ("LOAD", 0xB5),
    ("SAVE", 0xB6),
    ("DEF", 0xB7),
    ("POKE", 0xB8),
    ("CONT", 0xBA),
    ("LIST", 0xBB),
    ("CLEAR", 0xBC),
    ("GET", 0xBD),
    ("NEW", 0xBE),
    ("TO", 0xC0),
    ("FN", 0xC1),
    ("THEN", 0xC3),
    ("AT", 0xC4),
    ("NOT", 0xC5),
    ("STEP", 0xC6),
    ("AND", 0xCC),
    ("OR", 0xCD),
    ("SGN", 0xD1),
    ("INT", 0xD2),
    ("ABS", 0xD3),
    ("USR", 0xD4),
    ("FRE", 0xD5),
    ("PDL", 0xD7),
    ("POS", 0xD8),
    ("SQR", 0xD9),
    ("RND", 0xDA),
    ("LOG", 0xDB),
    ("EXP", 0xDC),
    ("COS", 0xDD),
    ("SIN", 0xDE),
    ("TAN", 0xDF),
    ("ATN", 0xE0),
    ("PEEK", 0xE1),
    ("LEN", 0xE2),
    ("VAL", 0xE4),
    ("ASC", 0xE5),
    ("REM", 0xB1),
    ("STOP", 0xB2),
    ("+", 0xC7),
    ("-", 0xC8),
    ("*", 0xC9),
    ("/", 0xCA),
    ("^", 0xCB),
    (">", 0xCE),
    ("=", 0xCF),
    ("<", 0xD0),
    ("?", 0xB9),
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
        if value == 0xB1:
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
