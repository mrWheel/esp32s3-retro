#!/usr/bin/env python3
"""Run native and real-kernel tests against a local retro emulator checkout.
Usage: RETRO_REPO=/path/to/esp32s3-retro python3 test/RUN.py [VI.COM]
All test files/images are temporary; original emulator disks are not modified.
"""
from pathlib import Path
import os,sys,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
repo=Path(os.environ['RETRO_REPO']).resolve()
program=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root.parent/'VI.COM'
core=repo/'components/cpm80Core';z80=repo/'components/z80'
sys.path.insert(0,str(repo/'tools/cpm80'))
import diskImageCpm80 as disk
cc=os.environ.get('CC_HOST','cc')
with tempfile.TemporaryDirectory(prefix='vi80-test-') as folder:
 work=Path(folder)
 flags=[cc,'-std=c11','-O1','-g','-fsanitize=address,undefined','-I'+str(core/'include'),'-I'+str(z80),'-I'+str(repo/'components/hostCore/include')]
 for kind in ('native','guest'):
  sources=[str(root/'test'/(kind+'.c')),str(core/'cpm80Cpu.c'),str(z80/'z80.c')]
  if kind=='guest':sources.append(str(core/'cpm80Guest.c'))
  subprocess.run(flags+sources+['-o',str(work/kind)],check=True)
 subprocess.run([str(work/'native'),str(program)],check=True)
 cases=[('create',None,b'iHello from Z80\x1b:wq\r',b'Hello from Z80\r\n'),('replace',b'one\r\ntwo\r\nthree\r\n',b'/two\rdd:wq\r',b'one\r\nthree\r\n'),('undo',b'old\r\n',b'iX\x1bu:wq\r',b'old\r\n'),('clipboard',b'one\r\ntwo\r\n',b'yyjp:wq\r',b'one\r\ntwo\r\none\r\n'),('arrows',b'one\r\ntwo\r\n',b'\x1b[B\x1b[Cxi!\x1b:wq\r',b'one\r\nt!o\r\n'),('capacity',b'a'*7998+b'\n',b':wq\r',b'a'*7998+b'\r\n'),('empty',b'old\r\n',b'dd:wq\r',b'')]
 for name,original,keys,expected in cases:
  image=work/(name+'.dsk');disk.create_image(image,force=True)
  files=[('VI.COM',program.read_bytes())]
  if original is not None:files.append(('TEST.TXT',original+b'\x1a'))
  disk.add_files(image,files,replace_image=True)
  keyfile=work/(name+'.keys');keyfile.write_bytes(b'VI TEST.TXT\r'+keys)
  subprocess.run([str(work/'guest'),str(core/'os/ccp-64k.bin'),str(core/'os/bdos-64k.bin'),str(image),str(keyfile),str(work/(name+'.log'))],check=True)
  result=disk.extract_file(image,'TEST.TXT').split(b'\x1a',1)[0]
  assert result==expected,(name,result,expected)
  print('PASS on-disk result:',name,flush=True)
