#!/usr/bin/env python3
"""Build blank, formatted Apple (UCSD) Pascal volumes as ProDOS-order (.po) images.

Block n of the volume is at byte offset n * 512 of the image. The Disk II emulation reads such an image as
16 sectors of 256 bytes per track, so a volume of 1280 blocks is a 640K, 160-track medium and a volume of 280
blocks is an ordinary 140K, 35-track medium.
"""

import argparse
import struct
import sys
from pathlib import Path

from diskImageCommon import DiskImageError

BLOCK_SIZE = 512
DIRECTORY_FIRST_BLOCK = 2
DIRECTORY_BLOCKS = 4
FIRST_FILE_BLOCK = DIRECTORY_FIRST_BLOCK + DIRECTORY_BLOCKS
VOLUME_BLOCKS = {"140K": 280, "640K": 1280}
MAX_NAME_LENGTH = 7
FORBIDDEN_NAME_CHARACTERS = set("$=?,[#:")


def normalize_volume_name(name):
    """Return the uppercase volume name after checking the Pascal length and character rules."""
    name = name.upper()
    if not 1 <= len(name) <= MAX_NAME_LENGTH:
        raise DiskImageError(f"Pascal volume name must be 1..{MAX_NAME_LENGTH} characters: {name!r}")
    for character in name:
        if not 0x21 <= ord(character) <= 0x7E or character in FORBIDDEN_NAME_CHARACTERS:
            raise DiskImageError(f"Invalid character {character!r} in Pascal volume name {name!r}")
    return name


def build_blank_volume(name, profile="640K"):
    """Return the bytes of an empty Pascal volume (boot blocks zeroed, empty directory)."""
    if profile not in VOLUME_BLOCKS:
        raise DiskImageError(f"Unknown profile {profile!r}; use one of {sorted(VOLUME_BLOCKS)}")
    name = normalize_volume_name(name)
    blocks = VOLUME_BLOCKS[profile]
    image = bytearray(blocks * BLOCK_SIZE)
    header = DIRECTORY_FIRST_BLOCK * BLOCK_SIZE
    struct.pack_into("<HHH", image, header, 0, FIRST_FILE_BLOCK, 0)
    image[header + 6] = len(name)
    image[header + 7:header + 7 + len(name)] = name.encode("ascii")
    struct.pack_into("<HH", image, header + 0x0E, blocks, 0)
    return bytes(image)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", type=Path, help="image file to write (use a .po suffix)")
    parser.add_argument("--name", default="BLANK", help="Pascal volume name (1..7 characters)")
    parser.add_argument("--profile", default="640K", choices=sorted(VOLUME_BLOCKS))
    arguments = parser.parse_args(argv)
    try:
        data = build_blank_volume(arguments.name, arguments.profile)
    except DiskImageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output}: {len(data)} bytes, {len(data) // BLOCK_SIZE} blocks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
