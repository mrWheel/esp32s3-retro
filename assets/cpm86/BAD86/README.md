# BAD86

CP/M-86 fault-recovery acceptance program for the firmware (CP/M-86 phase 86-09).

It prints `@BEGIN BAD86` and then executes the invalid opcode `FF /7`
(the tiny8086 core rejects `FF /2..7`, see
`components/cpm86Core/tiny8086/tiny8086.c`). The firmware must stop the
guest and return to the host `Select system` menu WITHOUT a host reset, and
a relaunch must work from that menu.

`tools/cpm86/cpm86HardwareTest.py` runs it automatically (`faultTest`): it
requires `@BEGIN BAD86` followed by `Select system` within 20 s, then boots
CP/M-86 again and checks the `A>` prompt.

Rebuild with `python3 build.py` (requires NASM, CPU 8086 enforced; BAD86.ASM
uses NASM syntax, not native ASM86 - the native ASM86 workflow is covered
separately by the 86-08 editor test). The checked-in
`bootDisks/cpm86/systemDsk/BAD86.CMD` matches this build and is verified by
`tests/test_bootDisksBench.py`; hardware results are recorded in
`designCPM86.md`.
