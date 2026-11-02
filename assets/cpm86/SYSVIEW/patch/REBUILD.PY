#!/usr/bin/env python3
"""Rebuild using the original SYSVIEW.CMD and NASM. Never edits the input."""
from pathlib import Path
import argparse, hashlib, struct, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('original',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args()
if a.original.resolve()==a.output.resolve():p.error('Input and output must differ')
original=a.original.read_bytes()
expected='8fbda99b53cb377c7897d6fb750b2dd6499b12ebc440a101a49522f57e89973e'
if hashlib.sha256(original).hexdigest()!=expected:p.error('Original SHA256 differs; patch not applied')
with tempfile.TemporaryDirectory() as temp:
 payload_path=Path(temp)/'payload.bin'
 subprocess.run(['nasm','-f','bin',str(Path(__file__).with_name('SYSVANSI.ASM')),'-o',str(payload_path)],check=True)
 payload=payload_path.read_bytes()
b=bytearray(original)
assert b[128+0x100:128+0x10a]==bytes.fromhex('fa0e17bcd201fbe95c12')
b[128+0x107:128+0x10a]=b'\xe9'+struct.pack('<H',0x1800-0x107-3)
length=(0x1800+len(payload)+15)&~15
b.extend(bytes(max(0,128+length-len(b))))
b[128+0x1800:128+0x1800+len(payload)]=payload
struct.pack_into('<H',b,1,length//16)
struct.pack_into('<H',b,5,length//16)
b.extend(bytes((-len(b))%128))
with a.output.open('xb') as f:f.write(b)
print('Written:',a.output)
