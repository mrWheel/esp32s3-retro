#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ -n "${Z88DK:-}" ]; then
 PATH="$Z88DK/bin:$PATH";export PATH
 ZCCCFG="$Z88DK/lib/config/";export ZCCCFG
fi
zcc +cpm -compiler=sdcc -SO3 -D__RETRO80__ -D__VTCMD__ -D__VT100__ \
 -pragma-define:noprotectmsdos=1 -pragma-define:CRT_STACK_SIZE=2048 \
 main.c edit.c normal.c misccmds.c linefunc.c cmdline.c help.c hexchars.c \
 retrowin.c retrokbd.c cpmio.c -o VI.COM -m
