#!/usr/bin/env python3
"""Build SYSVIEW.CMD using NASM, without a CP/M assembler."""
from pathlib import Path
import argparse, struct, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path,help='Output SYSVIEW.CMD path')
a=p.parse_args()
with tempfile.TemporaryDirectory() as temp:
 raw=Path(temp)/'sysview.bin'
 subprocess.run(['nasm','-f','bin',str(Path(__file__).with_name('SYSVIEW.ASM')),'-o',str(raw)],check=True)
 payload=raw.read_bytes()
payload+=bytes((-len(payload))%16)
if len(payload)>65536:raise SystemExit('Guest segment too large')
header=bytearray(128);header[0]=1
struct.pack_into('<H',header,1,len(payload)//16)
struct.pack_into('<H',header,5,len(payload)//16)
result=header+payload;result+=bytes((-len(result))%128)
a.output.write_bytes(result)
print('Written:',a.output,len(result),'bytes')
