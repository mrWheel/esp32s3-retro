#!/usr/bin/env python3
"""Create and populate raw CP/M-80 images for the project's SYSTEM and LARGE DPBs."""

import argparse
import os
import re
import tempfile
from pathlib import Path

SECTOR_SIZE = 128
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
MAX_RECORDS_PER_EXTENT = 128
MAX_BLOCKS_PER_EXTENT = 16
EMPTY = 0xE5
NAME_PATTERN = re.compile(r"^[A-Z0-9_$-]{1,8}(?:\.[A-Z0-9_$-]{0,3})?$")
IMAGE_PROFILES = {
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


class DiskImageError(ValueError):
    pass


def parse_filename(filename):
    """Return a validated uppercase CP/M 8.3 filename."""
    name = filename.upper()
    if not NAME_PATTERN.fullmatch(name):
        raise DiskImageError(f"Not a supported CP/M 8.3 filename: {filename}")
    return name


def _decode_directory_name(entry):
    try:
        name_bytes = bytes(value & 0x7F for value in entry[1:9]).decode("ascii").rstrip()
        extension_bytes = bytes(value & 0x7F for value in entry[9:12]).decode("ascii").rstrip()
    except UnicodeDecodeError as error:
        raise DiskImageError("Directory contains a non-ASCII filename") from error
    filename = name_bytes + (("." + extension_bytes) if extension_bytes else "")
    return parse_filename(filename)


def _profile_for_size(image_size):
    for profile in IMAGE_PROFILES.values():
        if profile["image_size"] == image_size:
            return profile
    expected = " or ".join(str(profile["image_size"]) for profile in IMAGE_PROFILES.values())
    raise DiskImageError(f"Image size is {image_size} bytes; expected {expected}")


def inspect_image(image):
    """Validate image geometry, directory entries, extents and block allocations."""
    profile = _profile_for_size(len(image))
    block_size = profile["block_size"]
    directory_offset = profile["directory_offset"]
    directory_entries = profile["directory_entries"]
    max_block_number = profile["max_block_number"]

    entries = []
    free_slots = []
    used_blocks = set()
    filenames = set()
    for slot in range(directory_entries):
        offset = directory_offset + slot * 32
        entry = image[offset : offset + 32]
        user = entry[0]
        if user == EMPTY:
            free_slots.append(slot)
            continue
        if user > 15:
            raise DiskImageError(f"Invalid CP/M user number {user} in directory entry {slot}")

        filename = _decode_directory_name(entry)
        extent = (entry[12] & 0x1F) | ((entry[14] & 0x3F) << 5)
        record_count = entry[15]
        if record_count > MAX_RECORDS_PER_EXTENT:
            raise DiskImageError(f"Invalid record count in directory entry {slot}")

        block_count = (record_count + (block_size // SECTOR_SIZE) - 1) // (block_size // SECTOR_SIZE)
        if block_count > MAX_BLOCKS_PER_EXTENT:
            raise DiskImageError(f"Too many blocks in directory entry {slot}")
        blocks = entry[16 : 16 + block_count]
        for block in blocks:
            if block < 2 or block > max_block_number:
                raise DiskImageError(f"Invalid allocation block {block} in directory entry {slot}")
            if block in used_blocks:
                raise DiskImageError(f"Allocation block {block} is used more than once")
            used_blocks.add(block)

        key = (user, filename, extent)
        if key in filenames:
            raise DiskImageError(f"Duplicate extent for user {user}: {filename}")
        filenames.add(key)
        entries.append(
            {
                "slot": slot,
                "user": user,
                "filename": filename,
                "extent": extent,
                "records": record_count,
                "blocks": tuple(blocks),
            }
        )

    return entries, free_slots, used_blocks


def _add_file(image, source_name, data, free_slots, used_blocks, existing_names, profile):
    filename = parse_filename(source_name)
    if not data:
        raise DiskImageError(f"CP/M files must not be empty: {filename}")
    if (0, filename) in existing_names:
        raise DiskImageError(f"{filename} already exists in user area 0")

    record_count = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    record_data = data.ljust(record_count * SECTOR_SIZE, b"\x1A")
    block_size = profile["block_size"]
    directory_offset = profile["directory_offset"]
    blocks_needed = (len(record_data) + block_size - 1) // block_size
    free_blocks = [block for block in range(2, profile["max_block_number"] + 1) if block not in used_blocks]
    if blocks_needed > len(free_blocks):
        raise DiskImageError(f"CP/M disk is full while adding {filename}")

    extent_count = (record_count + MAX_RECORDS_PER_EXTENT - 1) // MAX_RECORDS_PER_EXTENT
    if extent_count > len(free_slots):
        raise DiskImageError(f"CP/M directory is full while adding {filename}")

    allocated = free_blocks[:blocks_needed]
    for index, block in enumerate(allocated):
        source_offset = index * block_size
        block_data = record_data[source_offset : source_offset + block_size].ljust(block_size, b"\x1A")
        destination_offset = directory_offset + block * block_size
        image[destination_offset : destination_offset + block_size] = block_data
        used_blocks.add(block)

    name, separator, extension = filename.partition(".")
    name_bytes = name.encode("ascii").ljust(8, b" ")
    extension_bytes = extension.encode("ascii").ljust(3, b" ")
    records_per_block = block_size // SECTOR_SIZE

    for extent in range(extent_count):
        first_record = extent * MAX_RECORDS_PER_EXTENT
        extent_records = min(MAX_RECORDS_PER_EXTENT, record_count - first_record)
        first_block = first_record // records_per_block
        extent_block_count = (extent_records + records_per_block - 1) // records_per_block
        entry = bytearray(32)
        entry[0] = 0
        entry[1:9] = name_bytes
        entry[9:12] = extension_bytes
        entry[12] = extent & 0x1F
        entry[14] = (extent >> 5) & 0x3F
        entry[15] = extent_records
        extent_blocks = allocated[first_block : first_block + extent_block_count]
        entry[16 : 16 + len(extent_blocks)] = bytes(extent_blocks)
        slot = free_slots.pop(0)
        offset = directory_offset + slot * 32
        image[offset : offset + 32] = entry
        existing_names.add((0, filename))


def _atomic_write(path, data, replace_existing):
    path = Path(path)
    if path.is_symlink():
        raise DiskImageError(f"Refusing to replace a symbolic link: {path}")
    if path.exists() and not replace_existing:
        raise DiskImageError(f"File already exists; use --force to replace it: {path}")
    if path.exists() and not path.is_file():
        raise DiskImageError(f"Not a regular file: {path}")

    path.parent.mkdir(parents=True, exist_ok=True)
    mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
    descriptor, temporary_name = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as temporary_file:
            temporary_file.write(data)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.chmod(temporary_name, mode)
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def create_image(image_path, force=False, profile="SYSTEM"):
    profile = IMAGE_PROFILES.get(profile.upper())
    if profile is None:
        raise DiskImageError("Unknown CP/M image profile")
    image = bytearray([EMPTY]) * profile["image_size"]
    _atomic_write(image_path, image, replace_existing=force)


def write_image(image_path, image, force=False):
    """Validate and install a complete raw image matching the project geometry."""
    inspect_image(image)
    _atomic_write(image_path, image, replace_existing=force)


def add_files(image_path, files, replace_image=True):
    """Add (CP/M filename, bytes) pairs and atomically update the image."""
    image_path = Path(image_path)
    if image_path.is_symlink() or not image_path.is_file():
        raise DiskImageError(f"Image does not exist as a regular file: {image_path}")
    image = bytearray(image_path.read_bytes())
    profile = _profile_for_size(len(image))
    entries, free_slots, used_blocks = inspect_image(image)
    existing_names = {(entry["user"], entry["filename"]) for entry in entries}
    for filename, data in files:
        _add_file(image, filename, data, free_slots, used_blocks, existing_names, profile)
    inspect_image(image)
    _atomic_write(image_path, image, replace_existing=replace_image)


def list_files(image_path):
    image = Path(image_path).read_bytes()
    entries, _, _ = inspect_image(image)
    file_extents = {}
    for entry in entries:
        key = (entry["user"], entry["filename"])
        file_extents.setdefault(key, []).append(entry)

    if not file_extents:
        print("No files. The disk is formatted and empty.")
        return

    for (user, filename), extents in sorted(file_extents.items()):
        last_extent = max(extents, key=lambda entry: entry["extent"])
        record_length = last_extent["extent"] * MAX_RECORDS_PER_EXTENT + last_extent["records"]
        print(f"User {user:02d}  {filename:<12} {record_length * SECTOR_SIZE:>7} bytes")


def main():
    parser = argparse.ArgumentParser(
        description="Create and populate SYSTEM or LARGE CP/M-80 disk images for drives B:–F:."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    create_parser = commands.add_parser("create", help="create an empty, formatted CP/M disk image")
    create_parser.add_argument("image", type=Path)
    create_parser.add_argument("--profile", choices=tuple(IMAGE_PROFILES), default="SYSTEM")
    create_parser.add_argument("--force", action="store_true", help="replace an existing image")

    add_parser = commands.add_parser("add", help="add local files to an existing CP/M image")
    add_parser.add_argument("image", type=Path)
    add_parser.add_argument("files", nargs="+", type=Path)
    add_parser.add_argument("--name", help="CP/M 8.3 name (only when adding one local file)")

    list_parser = commands.add_parser("list", help="validate and list a CP/M image")
    list_parser.add_argument("image", type=Path)

    arguments = parser.parse_args()
    try:
        if arguments.command == "create":
            profile = IMAGE_PROFILES[arguments.profile]
            create_image(arguments.image, force=arguments.force, profile=arguments.profile)
            print(f"Created empty CP/M {arguments.profile} image: {arguments.image} ({profile['image_size']} bytes)")
            print("Copy it to /retro/images/cpm80/work.dsk on the SD card, then eject the card safely.")
        elif arguments.command == "add":
            if arguments.name and len(arguments.files) != 1:
                parser.error("--name can only be used with one input file")
            additions = []
            for source in arguments.files:
                if not source.is_file():
                    raise DiskImageError(f"Input file does not exist: {source}")
                name = arguments.name if arguments.name else source.name
                additions.append((name, source.read_bytes()))
            add_files(arguments.image, additions)
            print(f"Added {len(additions)} file(s) to {arguments.image}")
        else:
            list_files(arguments.image)
    except (OSError, DiskImageError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
