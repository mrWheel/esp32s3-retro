#!/usr/bin/env python3
"""Build BENCH2ASM.BIN (ProDOS BIN, load address $6000) from BENCH2ASM.S with ca65/ld65."""

import pathlib
import subprocess
import tempfile

folder = pathlib.Path(__file__).resolve().parent
loadAddress = 0x6000
with tempfile.TemporaryDirectory() as temporary:
    objectPath = pathlib.Path(temporary) / "BENCH2ASM.o"
    subprocess.run(["ca65", "-I", str(folder), "-o", str(objectPath), str(folder / "BENCH2ASM.S")], check=True)
    output = folder / "BENCH2ASM.BIN"
    subprocess.run(
        ["ld65", "-t", "none", "-S", hex(loadAddress).replace("0x", "$"), "-o", str(output), str(objectPath)],
        check=True,
    )
print(f"{output.name}: {output.stat().st_size} bytes (ProDOS BIN, load address ${loadAddress:04X})")
