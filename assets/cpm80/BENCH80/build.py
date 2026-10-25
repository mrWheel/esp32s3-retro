#!/usr/bin/env python3
"""Build BENCH80.COM from BENCH80.ASM with z80asm (flat binary loaded at 0100h)."""
import pathlib
import subprocess
import sys

folder = pathlib.Path(__file__).resolve().parent
output = folder / 'BENCH80.COM'
subprocess.run([sys.executable, str(folder / 'reference_model.py')], check=True, stdout=subprocess.DEVNULL)
subprocess.run(['z80asm', '-I', str(folder), '-o', str(output), str(folder / 'BENCH80.ASM')],
               check=True, cwd=folder)
print(f'{output.name}: {output.stat().st_size} bytes')
