#!/usr/bin/env python3
"""Shared raw CP/M disk image engine (directory, extents, allocation blocks).

Geometry is not defined here: every function takes a `profiles` dict
(name -> geometry) supplied by diskImageCpm80.py or diskImageCpm86.py. Use
diskImage.py (--os) as the command line.
"""

import os
import re
import tempfile
from pathlib import Path

from diskImageCommon import DiskImageError

SECTOR_SIZE = 128
MAX_RECORDS_PER_EXTENT = 128
MAX_BLOCKS_PER_EXTENT = 16
EMPTY = 0xE5
NAME_PATTERN = re.compile(r"^[A-Z0-9_$-]{1,8}(?:\.[A-Z0-9_$-]{0,3})?$")


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


def _profile_for_size(image_size, profiles):
    candidates = list(profiles.values())
    for profile in candidates:
        if profile["image_size"] == image_size:
            return profile
    expected = " or ".join(str(profile["image_size"]) for profile in candidates)
    raise DiskImageError(f"Image size is {image_size} bytes; expected {expected}")


def _entry_geometry(profile):
    """Return (pointer_size, entry_records) for a profile.

    Profiles with more than 256 blocks use 16-bit block pointers (8 per entry);
    `exm` is the CP/M extent mask, so one entry spans (exm + 1) logical extents.
    """
    pointer_size = profile.get("block_pointer_size", 1)
    entry_records = (profile.get("exm", 0) + 1) * MAX_RECORDS_PER_EXTENT
    return pointer_size, entry_records


def inspect_image(image, profiles):
    """Validate image geometry, directory entries, extents and block allocations."""
    profile = _profile_for_size(len(image), profiles)
    block_size = profile["block_size"]
    directory_offset = profile["directory_offset"]
    directory_entries = profile["directory_entries"]
    max_block_number = profile["max_block_number"]
    pointer_size, entry_records = _entry_geometry(profile)
    exm = profile.get("exm", 0)

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
        if entry[15] > MAX_RECORDS_PER_EXTENT:
            raise DiskImageError(f"Invalid record count in directory entry {slot}")
        record_count = (extent % (exm + 1)) * MAX_RECORDS_PER_EXTENT + entry[15]
        extent //= exm + 1

        block_count = (record_count + (block_size // SECTOR_SIZE) - 1) // (block_size // SECTOR_SIZE)
        if block_count > MAX_BLOCKS_PER_EXTENT // pointer_size:
            raise DiskImageError(f"Too many blocks in directory entry {slot}")
        if pointer_size == 2:
            blocks = [entry[16 + 2 * index] | (entry[17 + 2 * index] << 8) for index in range(block_count)]
        else:
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

    pointer_size, entry_records = _entry_geometry(profile)
    exm = profile.get("exm", 0)
    extent_count = (record_count + entry_records - 1) // entry_records
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
        first_record = extent * entry_records
        extent_records = min(entry_records, record_count - first_record)
        first_block = first_record // records_per_block
        extent_block_count = (extent_records + records_per_block - 1) // records_per_block
        logical_extents = (extent_records + MAX_RECORDS_PER_EXTENT - 1) // MAX_RECORDS_PER_EXTENT
        extent_number = extent * (exm + 1) + logical_extents - 1
        entry = bytearray(32)
        entry[0] = 0
        entry[1:9] = name_bytes
        entry[9:12] = extension_bytes
        entry[12] = extent_number & 0x1F
        entry[14] = (extent_number >> 5) & 0x3F
        entry[15] = extent_records - (logical_extents - 1) * MAX_RECORDS_PER_EXTENT
        extent_blocks = allocated[first_block : first_block + extent_block_count]
        if pointer_size == 2:
            for index, block in enumerate(extent_blocks):
                entry[16 + 2 * index : 18 + 2 * index] = block.to_bytes(2, "little")
        else:
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


def create_image(image_path, profiles, profile, force=False):
    profile = profiles.get(profile.upper())
    if profile is None:
        raise DiskImageError("Unknown CP/M image profile")
    image = bytearray([EMPTY]) * profile["image_size"]
    _atomic_write(image_path, image, replace_existing=force)


def write_image(image_path, image, profiles, force=False):
    """Validate and install a complete raw image matching the project geometry."""
    inspect_image(image, profiles)
    _atomic_write(image_path, image, replace_existing=force)


def add_files(image_path, files, profiles, replace_image=True):
    """Add (CP/M filename, bytes) pairs and atomically update the image."""
    image_path = Path(image_path)
    if image_path.is_symlink() or not image_path.is_file():
        raise DiskImageError(f"Image does not exist as a regular file: {image_path}")
    image = bytearray(image_path.read_bytes())
    profile = _profile_for_size(len(image), profiles)
    entries, free_slots, used_blocks = inspect_image(image, profiles)
    existing_names = {(entry["user"], entry["filename"]) for entry in entries}
    for filename, data in files:
        _add_file(image, filename, data, free_slots, used_blocks, existing_names, profile)
    inspect_image(image, profiles)
    _atomic_write(image_path, image, replace_existing=replace_image)


def extract_file(image_path, filename, profiles, user=0):
    """Return a file's CP/M records, retaining the padded final record."""
    filename = parse_filename(filename)
    if user < 0 or user > 15:
        raise DiskImageError("CP/M user number must be between 0 and 15")
    image = Path(image_path).read_bytes()
    entries, _, _ = inspect_image(image, profiles)
    extents = sorted(
        (entry for entry in entries if entry["user"] == user and entry["filename"] == filename),
        key=lambda entry: entry["extent"],
    )
    if not extents:
        raise DiskImageError(f"{filename} was not found in user area {user}")
    if any(entry["extent"] != index for index, entry in enumerate(extents)):
        raise DiskImageError(f"{filename} has a missing CP/M extent")

    profile = _profile_for_size(len(image), profiles)
    block_size = profile["block_size"]
    extracted = bytearray()
    for entry in extents:
        remaining = entry["records"] * SECTOR_SIZE
        for block in entry["blocks"]:
            block_offset = profile["directory_offset"] + block * block_size
            count = min(remaining, block_size)
            extracted.extend(image[block_offset : block_offset + count])
            remaining -= count
        if remaining:
            raise DiskImageError(f"{filename} has incomplete allocation data")
        if entry["records"] > _entry_geometry(profile)[1]:
            raise DiskImageError(f"{filename} has an invalid record count")
    return bytes(extracted)


def list_files(image_path, profiles):
    image = Path(image_path).read_bytes()
    entries, _, _ = inspect_image(image, profiles)
    entry_records = _entry_geometry(_profile_for_size(len(image), profiles))[1]
    file_extents = {}
    for entry in entries:
        key = (entry["user"], entry["filename"])
        file_extents.setdefault(key, []).append(entry)

    if not file_extents:
        print("No files. The disk is formatted and empty.")
        return

    for (user, filename), extents in sorted(file_extents.items()):
        last_extent = max(extents, key=lambda entry: entry["extent"])
        record_length = last_extent["extent"] * entry_records + last_extent["records"]
        print(f"User {user:02d}  {filename:<12} {record_length * SECTOR_SIZE:>7} bytes")
