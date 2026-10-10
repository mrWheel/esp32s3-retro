#!/usr/bin/env python3
"""Build blank ProDOS 8 block volumes as ProDOS-order (.po) images."""

import argparse
import struct
import sys
from pathlib import Path

from diskImageCommon import DiskImageError

BLOCK_SIZE = 512
VOLUME_DIRECTORY_BLOCK = 2
DIRECTORY_BLOCKS = 4
BITMAP_BLOCK = VOLUME_DIRECTORY_BLOCK + DIRECTORY_BLOCKS
BLOCKS_PER_BITMAP = BLOCK_SIZE * 8
VOLUME_BLOCKS = {"140K": 280, "640K": 1280}
MAX_NAME_LENGTH = 15


def normalize_volume_name(name):
    """Return the uppercase ProDOS volume name after checking its character rules."""
    name = name.upper()
    if not 1 <= len(name) <= MAX_NAME_LENGTH or not "A" <= name[0] <= "Z":
        raise DiskImageError("ProDOS volume name must start with a letter and be 1..15 characters")
    if any(not ("A" <= character <= "Z" or "0" <= character <= "9" or character == ".")
           for character in name):
        raise DiskImageError(f"Invalid character in ProDOS volume name {name!r}")
    return name


def build_blank_volume(name, profile="640K"):
    """Return a formatted, empty ProDOS 8 data volume in ProDOS block order."""
    if profile not in VOLUME_BLOCKS:
        raise DiskImageError(f"Unknown profile {profile!r}; use one of {sorted(VOLUME_BLOCKS)}")
    name = normalize_volume_name(name)
    blocks = VOLUME_BLOCKS[profile]
    bitmap_blocks = (blocks + BLOCKS_PER_BITMAP - 1) // BLOCKS_PER_BITMAP
    first_free_block = BITMAP_BLOCK + bitmap_blocks
    if first_free_block >= blocks:
        raise DiskImageError(f"Volume profile {profile!r} is too small for ProDOS metadata")

    image = bytearray(blocks * BLOCK_SIZE)
    directory_blocks = range(VOLUME_DIRECTORY_BLOCK, BITMAP_BLOCK)
    for index, block in enumerate(directory_blocks):
        offset = block * BLOCK_SIZE
        previous = block - 1 if index > 0 else 0
        following = block + 1 if block + 1 < BITMAP_BLOCK else 0
        struct.pack_into("<HH", image, offset, previous, following)

    directory = VOLUME_DIRECTORY_BLOCK * BLOCK_SIZE
    image[directory + 4] = 0xF0 | len(name)
    image[directory + 5:directory + 5 + len(name)] = name.encode("ascii")
    image[directory + 0x22] = 0xC3
    image[directory + 0x23] = 0x27
    image[directory + 0x24] = (BLOCK_SIZE - 4) // 0x27
    struct.pack_into("<H", image, directory + 0x25, 0)
    struct.pack_into("<HH", image, directory + 0x27, BITMAP_BLOCK, blocks)

    for block_index in range(bitmap_blocks):
        bitmap_offset = (BITMAP_BLOCK + block_index) * BLOCK_SIZE
        first_block = block_index * BLOCKS_PER_BITMAP
        for block in range(first_block, min(first_block + BLOCKS_PER_BITMAP, blocks)):
            if block >= first_free_block:
                image[bitmap_offset + block // 8 - first_block // 8] |= 0x80 >> (block & 7)

    return bytes(image)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", type=Path, help="image file to write (use a .po suffix)")
    parser.add_argument("--name", default="DATA640", help="ProDOS volume name (1..15 characters)")
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
