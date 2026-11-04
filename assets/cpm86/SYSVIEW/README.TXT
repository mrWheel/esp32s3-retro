SYSVIEW - emulator information viewer for CP/M-86 (RETRO86)

Copy SYSVIEW.CMD and SYSVIEW.HLP to your CP/M-86 disk.

Start: SYSVIEW
Help:  HELP SYSVIEW
Keys:  R = refresh; Q or Esc = exit.
Terminal: VT100/ANSI, at least 80 columns and 24 rows.

The terminal front end replaces direct IBM screen writes, BIOS
keyboard input, animations and speaker effects. The executed path
uses CP/M BDOS console I/O and does not call PC BIOS interrupts.

Hardware information is guest information, not host Mac information.
Device counts come from the conventional IBM BIOS data area when
its memory-size value is plausible. Missing data is shown explicitly.
Secondary video is not actively probed. The CPU category no longer
separates a NEC V20/V30 from other 8086/8088-compatible behaviour.

The entire user-supplied SYSVIEW.DOC is included under ORIGINAL in
SYSVIEW.HLP, with a note explaining that it describes the original.

Verification: QEMU execution with simulated CP/M console and controlled
BIOS data. Checked redraw, keys, normal/missing data, zero flags and
ANSI cleanup. Actual CP/M disk/emulator integration is not tested.

Build (requires Python 3 and NASM):
  python3 BUILD.py SYSVIEW.CMD

Two programs live in one .CMD. The started program is the host
information viewer (SYSVIEW 1.0), which asks the RETRO86 firmware for
the RETRO86-SYSINFO/1 stream on ports 0xF8/0xF9 (see
main/cpm86Machine.c). This is the edition that shows real data on
RETRO86.

The VT100/ANSI terminal front end (formerly patch/SYSVANSI.ASM, now
at load offset 0x1800) is dormant: RETRO86 has no IBM BIOS data area
and no ROM date at F000:FFF5, so on this machine it can only print
'Not reported'. It is kept for IBM-compatible guests and uses CP/M
BDOS console I/O only. To start it instead of the viewer, replace the
entry jump in SYSVIEW.ASM with its stack stub (shown in the source
comment at 'start:').
