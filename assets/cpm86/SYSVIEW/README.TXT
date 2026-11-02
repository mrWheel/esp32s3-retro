SYSVIEW - VT100/ANSI terminal edition for CP/M-86

Copy SYSVIEW.CMD and SYSVIEW.HLP to your CP/M-86 disk.
Keep a backup of the previous SYSVIEW.CMD before replacing it there.
The original file on your Mac has not been modified by this task.

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

Optional rebuild (requires Python 3 and NASM):
  python3 patch/REBUILD.PY /path/to/original/SYSVIEW.CMD rebuilt.CMD

The rebuild refuses a different original, an existing output file,
or an output path equal to the original. Source code in SYSVANSI.ASM
is the added terminal front end; the original binary remains required.

Original SHA256:
8fbda99b53cb377c7897d6fb750b2dd6499b12ebc440a101a49522f57e89973e
