apple2.rom is the 12 KiB Apple system ROM copied from assets/apple.rom. The
matching source copy and mirror URL are recorded in assets/apple2_roms/.
SHA-256: 378ba00c86a64cca49cedaca7de8d5d351983ebc295d9d11e0752febfc346249
It boots the Apple ][ monitor and Integer BASIC in the host-side Apple II core
test. The ROM image's redistribution license has not been verified.

The separate project-authored diagnostic test fixture is at
tests/fixtures/apple2-diagnostic.rom and can be regenerated with:
  python3 tools/buildApple2TestRom.py

Phase 1 does not include the Language Card, expansion video card, disk
controller or graphics renderer.
