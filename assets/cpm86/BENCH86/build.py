#!/usr/bin/env python3
"""Build a relocatable single-code-group CP/M-86 CMD using NASM."""
import pathlib
import struct
import subprocess
import tempfile
import sys

folder = pathlib.Path(__file__).resolve().parent
subprocess.run([sys.executable, str(folder / 'reference_model.py')], check=True, stdout=subprocess.DEVNULL)
with tempfile.TemporaryDirectory() as temporary:
  payloadPath = pathlib.Path(temporary) / 'payload.bin'
  subprocess.run(['nasm', '-I', str(folder) + '/', '-f', 'bin', '-o', str(payloadPath),
                  str(folder / 'BENCH86.ASM')], check=True)
  payload = payloadPath.read_bytes()
if len(payload) % 16 or len(payload) >= 65536:
  raise SystemExit('Invalid single-group size')
paragraphs = len(payload) // 16
# Type, stored paragraphs, relocation base, minimum, maximum allocation.
header = struct.pack('<BHHHH', 1, paragraphs, 0, paragraphs, 0).ljust(128, b'\0')
output = folder / 'BENCH86.CMD'
output.write_bytes(header + payload)
print(f'{output.name}: {output.stat().st_size} bytes')
