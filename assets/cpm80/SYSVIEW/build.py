#!/usr/bin/env python3
"""Build SYSVIEW.COM from SYSVIEW.ASM with z80asm (flat binary loaded at 0100h)."""
import pathlib
import subprocess

folder = pathlib.Path(__file__).resolve().parent
output = folder / 'SYSVIEW.COM'
subprocess.run(['z80asm', '-I', str(folder), '-o', str(output), str(folder / 'SYSVIEW.ASM')],
               check=True, cwd=folder)
print(f'{output.name}: {output.stat().st_size} bytes')
