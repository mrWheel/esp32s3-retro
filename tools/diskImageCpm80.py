#!/usr/bin/env python3
"""CP/M-80 disk images: SYSTEM and LARGE geometries on top of diskImageCpm.

Running this file directly is the same as `diskImage.py --os cpm80`.
"""

import diskImageCpm
from diskImageCpm import (  # noqa: F401  (re-exported API)
    EMPTY,
    SECTOR_SIZE,
    DiskImageError,
    parse_filename,
)

SECTORS_PER_TRACK = 26
TRACKS = 77
BLOCK_SIZE = 1024
RESERVED_TRACKS = 2
DIRECTORY_ENTRIES = 64
IMAGE_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK * TRACKS
DIRECTORY_OFFSET = RESERVED_TRACKS * SECTOR_SIZE * SECTORS_PER_TRACK
DIRECTORY_SIZE = DIRECTORY_ENTRIES * 32
DSM = 242
MAX_BLOCK_NUMBER = DSM
LARGE_SECTORS_PER_TRACK = 52
LARGE_BLOCK_SIZE = 2048
LARGE_DIRECTORY_ENTRIES = 128
LARGE_IMAGE_SIZE = SECTOR_SIZE * LARGE_SECTORS_PER_TRACK * TRACKS
LARGE_DIRECTORY_OFFSET = RESERVED_TRACKS * SECTOR_SIZE * LARGE_SECTORS_PER_TRACK
LARGE_DIRECTORY_SIZE = LARGE_DIRECTORY_ENTRIES * 32
MAX_IMAGE_SIZE = max(IMAGE_SIZE, LARGE_IMAGE_SIZE)

PROFILES = {
    "SYSTEM": {
        "image_size": IMAGE_SIZE,
        "sectors_per_track": SECTORS_PER_TRACK,
        "block_size": BLOCK_SIZE,
        "directory_entries": DIRECTORY_ENTRIES,
        "directory_offset": DIRECTORY_OFFSET,
        "max_block_number": MAX_BLOCK_NUMBER,
    },
    "LARGE": {
        "image_size": LARGE_IMAGE_SIZE,
        "sectors_per_track": LARGE_SECTORS_PER_TRACK,
        "block_size": LARGE_BLOCK_SIZE,
        "directory_entries": LARGE_DIRECTORY_ENTRIES,
        "directory_offset": LARGE_DIRECTORY_OFFSET,
        "max_block_number": DSM,
    },
}
IMAGE_PROFILES = PROFILES
DEFAULT_PROFILE = "SYSTEM"


def inspect_image(image):
    return diskImageCpm.inspect_image(image, PROFILES)


def create_image(image_path, force=False, profile=DEFAULT_PROFILE):
    diskImageCpm.create_image(image_path, PROFILES, profile, force=force)


def write_image(image_path, image, force=False):
    diskImageCpm.write_image(image_path, image, PROFILES, force=force)


def add_files(image_path, files, replace_image=True):
    diskImageCpm.add_files(image_path, files, PROFILES, replace_image=replace_image)


def list_files(image_path):
    diskImageCpm.list_files(image_path, PROFILES)


def main():
    import diskImage

    diskImage.main(default_os="cpm80")


if __name__ == "__main__":
    main()
