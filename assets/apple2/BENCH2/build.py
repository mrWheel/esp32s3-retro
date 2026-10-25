#!/usr/bin/env python3
"""Build the tokenized ProDOS Applesoft BASIC program."""

import pathlib
import sys

folder = pathlib.Path(__file__).resolve().parent
apple2Tools = folder.parents[2] / "tools" / "apple2"
sys.path.insert(0, str(apple2Tools))
import apple2Basic

source = (folder / "BENCH2.SRC").read_text(encoding="ascii")
program = apple2Basic.tokenize_source(source)
output = folder / "BENCH2.BAS"
output.write_bytes(program)
print(f"{output.name}: {len(program)} bytes (ProDOS BASIC file type, load address $0801)")
