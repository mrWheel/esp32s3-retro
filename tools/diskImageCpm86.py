#!/usr/bin/env python3
"""CP/M-86 disk images: the RETRO86 CPM86 geometry on top of diskImageCpm.

The BIOS (components/cpm86Core/bios/retro86bios.a86) has two DPBs, one per
profile: CPM86 (160 KiB, RETRO86_DATA_V1) and LARGE (516 KiB,
RETRO86_DATA_LARGE_V1). Add further profiles only after the BIOS and firmware
support them. Running this file directly is the same as `diskImage.py --os cpm86`.
"""

import diskImageCpm
from diskImageCpm import (  # noqa: F401  (re-exported API)
    EMPTY,
    SECTOR_SIZE,
    DiskImageError,
    parse_filename,
)

PHYSICAL_SECTOR_SIZE = 512
SECTORS_PER_TRACK = 8
TRACKS = 40
RESERVED_TRACKS = 1
BLOCK_SIZE = 1024
DIRECTORY_ENTRIES = 64
IMAGE_SIZE = PHYSICAL_SECTOR_SIZE * SECTORS_PER_TRACK * TRACKS
DIRECTORY_OFFSET = RESERVED_TRACKS * PHYSICAL_SECTOR_SIZE * SECTORS_PER_TRACK
MAX_BLOCK_NUMBER = (IMAGE_SIZE - DIRECTORY_OFFSET) // BLOCK_SIZE - 1

LARGE_SECTORS_PER_TRACK = 8
LARGE_TRACKS = 129
LARGE_BLOCK_SIZE = 2048
LARGE_DIRECTORY_ENTRIES = 128
LARGE_IMAGE_SIZE = PHYSICAL_SECTOR_SIZE * LARGE_SECTORS_PER_TRACK * LARGE_TRACKS
LARGE_DSM = 255
MAX_IMAGE_SIZE = max(IMAGE_SIZE, LARGE_IMAGE_SIZE)

PROFILES = {
    "CPM86": {
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
        "directory_offset": DIRECTORY_OFFSET,
        "max_block_number": LARGE_DSM,
    },
}
DEFAULT_PROFILE = "CPM86"


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

    diskImage.main(default_os="cpm86")


if __name__ == "__main__":
    main()
