#!/usr/bin/env python3
"""Build ProDOS 8 block volumes (ProDOS-order .po images) and add, list and read files in them."""

import argparse
import struct
import sys
from pathlib import Path

import apple2Basic
from diskImageCommon import DiskImageError

BLOCK_SIZE = 512
VOLUME_DIRECTORY_BLOCK = 2
DIRECTORY_BLOCKS = 4
BITMAP_BLOCK = VOLUME_DIRECTORY_BLOCK + DIRECTORY_BLOCKS
BLOCKS_PER_BITMAP = BLOCK_SIZE * 8
VOLUME_BLOCKS = {"140K": 280, "640K": 1280, "800K": 1600}
MAX_NAME_LENGTH = 15
ENTRY_SIZE = 0x27
ENTRIES_PER_BLOCK = 13
STORAGE_SEEDLING = 1
STORAGE_SAPLING = 2
STORAGE_TREE = 3
STORAGE_VOLUME_HEADER = 0xF
INDEX_ENTRIES = 256
MAX_FILE_SIZE = 0xFFFFFF
FILE_TYPES = {".BAS": 0xFC, ".TXT": 0x04, ".BIN": 0x06, ".SYS": 0xFF}
BASIC_LOAD_ADDRESS = 0x0801
BOOT_FILES = ("PRODOS", "BASIC.SYSTEM", "QUIT.SYSTEM", "BITSY.BOOT")
PRODOS_LOGICAL_PHYSICAL = (0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15)
DOS_FILE_INDEX_OF_PHYSICAL = (0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15)


def normalize_volume_name(name):
    """Return the uppercase ProDOS volume name after checking its character rules."""
    name = name.upper()
    if not 1 <= len(name) <= MAX_NAME_LENGTH or not "A" <= name[0] <= "Z":
        raise DiskImageError("ProDOS volume name must start with a letter and be 1..15 characters")
    if any(not ("A" <= character <= "Z" or "0" <= character <= "9" or character == ".")
           for character in name):
        raise DiskImageError(f"Invalid character in ProDOS volume name {name!r}")
    return name


def build_blank_volume(name, profile="800K"):
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


def prodos_order_from_dos_order(data):
    """Return a 140K DOS-order (.dsk) image as a ProDOS-order block image."""
    if len(data) != VOLUME_BLOCKS["140K"] * BLOCK_SIZE:
        raise DiskImageError("A DOS-order ProDOS image must be exactly 143360 bytes")
    image = bytearray(len(data))
    for block in range(VOLUME_BLOCKS["140K"]):
        track, index = divmod(block, 8)
        for half in range(2):
            physical = PRODOS_LOGICAL_PHYSICAL[2 * index + half]
            source = (track * 16 + DOS_FILE_INDEX_OF_PHYSICAL[physical]) * 256
            target = block * BLOCK_SIZE + half * 256
            image[target:target + 256] = data[source:source + 256]
    return bytes(image)


def dos_order_from_prodos_order(data):
    """Return a 140K ProDOS-order block image as a DOS-order (.dsk) image."""
    if len(data) != VOLUME_BLOCKS["140K"] * BLOCK_SIZE:
        raise DiskImageError("Only a 140K ProDOS volume (143360 bytes) can be stored in DOS order")
    image = bytearray(len(data))
    for block in range(VOLUME_BLOCKS["140K"]):
        track, index = divmod(block, 8)
        for half in range(2):
            physical = PRODOS_LOGICAL_PHYSICAL[2 * index + half]
            target = (track * 16 + DOS_FILE_INDEX_OF_PHYSICAL[physical]) * 256
            source = block * BLOCK_SIZE + half * 256
            image[target:target + 256] = data[source:source + 256]
    return bytes(image)


def _has_volume_header(image):
    header = VOLUME_DIRECTORY_BLOCK * BLOCK_SIZE
    return (
        len(image) >= BITMAP_BLOCK * BLOCK_SIZE
        and image[header + 4] >> 4 == STORAGE_VOLUME_HEADER
        and 1 <= image[header + 4] & 0x0F <= MAX_NAME_LENGTH
        and image[header + 0x23] == ENTRY_SIZE
    )


def load_volume(path):
    """Read a ProDOS volume file (.po, or a 140K DOS-order .dsk) as a mutable ProDOS-order block image."""
    data = Path(path).read_bytes()
    if len(data) % BLOCK_SIZE != 0:
        raise DiskImageError(f"Not a ProDOS image (size is not a multiple of {BLOCK_SIZE} bytes): {path}")
    if _has_volume_header(data):
        return bytearray(data)
    if len(data) == VOLUME_BLOCKS["140K"] * BLOCK_SIZE:
        converted = prodos_order_from_dos_order(data)
        if _has_volume_header(converted):
            return bytearray(converted)
    raise DiskImageError(f"No ProDOS volume directory found in {path}")


def volume_name(image):
    header = VOLUME_DIRECTORY_BLOCK * BLOCK_SIZE
    return bytes(image[header + 5:header + 5 + (image[header + 4] & 0x0F)]).decode("ascii")


def set_volume_name(image, name):
    name = normalize_volume_name(name)
    header = VOLUME_DIRECTORY_BLOCK * BLOCK_SIZE
    image[header + 4] = (STORAGE_VOLUME_HEADER << 4) | len(name)
    image[header + 5:header + 20] = name.encode("ascii").ljust(MAX_NAME_LENGTH, b"\0")


def normalize_file_name(name):
    """Return the uppercase ProDOS file name (letters, digits and '.', starting with a letter, 1..15 characters)."""
    return normalize_volume_name(name)


def _block(image, number):
    return memoryview(image)[number * BLOCK_SIZE:(number + 1) * BLOCK_SIZE]


def _directory_entries(image):
    """Yield (block, offset) of every slot of the volume directory, header slot excluded."""
    block = VOLUME_DIRECTORY_BLOCK
    visited = set()
    while block != 0 and block not in visited and block * BLOCK_SIZE < len(image):
        visited.add(block)
        for slot in range(ENTRIES_PER_BLOCK):
            if block == VOLUME_DIRECTORY_BLOCK and slot == 0:
                continue
            yield block, 4 + slot * ENTRY_SIZE
        block = struct.unpack_from("<H", image, block * BLOCK_SIZE + 2)[0]


def list_files(image):
    """Return the files of the volume directory as dictionaries (subdirectories are listed but not readable)."""
    files = []
    for block, offset in _directory_entries(image):
        base = block * BLOCK_SIZE + offset
        storage = image[base] >> 4
        if storage == 0:
            continue
        length = image[base] & 0x0F
        files.append({
            "name": bytes(image[base + 1:base + 1 + length]).decode("ascii", "replace"),
            "storage": storage,
            "type": image[base + 0x10],
            "key": struct.unpack_from("<H", image, base + 0x11)[0],
            "blocks": struct.unpack_from("<H", image, base + 0x13)[0],
            "eof": image[base + 0x15] | (image[base + 0x16] << 8) | (image[base + 0x17] << 16),
            "access": image[base + 0x1E],
            "aux": struct.unpack_from("<H", image, base + 0x1F)[0],
            "version": image[base + 0x1C],
            "minVersion": image[base + 0x1D],
            "created": bytes(image[base + 0x18:base + 0x1C]),
        })
    return files


def _file_blocks(image, entry):
    """Return the data block numbers of a seedling, sapling or tree file (0 for a sparse block)."""
    key = entry["key"]
    if entry["storage"] == STORAGE_SEEDLING:
        return [key]
    if entry["storage"] == STORAGE_SAPLING:
        index_blocks = [key]
    elif entry["storage"] == STORAGE_TREE:
        master = _block(image, key)
        index_blocks = [master[i] | (master[256 + i] << 8) for i in range(128)]
    else:
        raise DiskImageError(f"Unsupported ProDOS storage type {entry['storage']} for {entry['name']}")
    blocks = []
    for index_block in index_blocks:
        if index_block == 0:
            blocks.extend([0] * INDEX_ENTRIES)
            continue
        index = _block(image, index_block)
        blocks.extend(index[i] | (index[256 + i] << 8) for i in range(INDEX_ENTRIES))
    return blocks


def read_file(image, name):
    """Return (data, entry) of a file in the volume directory."""
    name = normalize_file_name(name)
    for entry in list_files(image):
        if entry["name"] == name:
            blocks = _file_blocks(image, entry)
            data = bytearray()
            for number in blocks[:(entry["eof"] + BLOCK_SIZE - 1) // BLOCK_SIZE]:
                data += _block(image, number) if number else bytes(BLOCK_SIZE)
            return bytes(data[:entry["eof"]]), entry
    raise DiskImageError(f"ProDOS file not found: {name}")


def _bitmap_geometry(image):
    header = VOLUME_DIRECTORY_BLOCK * BLOCK_SIZE
    bitmap_block, total_blocks = struct.unpack_from("<HH", image, header + 0x27)
    if total_blocks * BLOCK_SIZE > len(image) or bitmap_block == 0:
        raise DiskImageError("The ProDOS volume header does not match the image size")
    return bitmap_block, total_blocks


def free_blocks(image):
    """Return the number of free blocks according to the volume bitmap."""
    bitmap_block, total_blocks = _bitmap_geometry(image)
    bitmap = image[bitmap_block * BLOCK_SIZE:]
    return sum(1 for block in range(total_blocks) if bitmap[block // 8] & (0x80 >> (block & 7)))


def _allocate(image, count):
    bitmap_block, total_blocks = _bitmap_geometry(image)
    bitmap = memoryview(image)[bitmap_block * BLOCK_SIZE:]
    chosen = []
    for block in range(total_blocks):
        if bitmap[block // 8] & (0x80 >> (block & 7)):
            chosen.append(block)
            if len(chosen) == count:
                break
    if len(chosen) < count:
        raise DiskImageError("ProDOS disk is full")
    for block in chosen:
        bitmap[block // 8] &= ~(0x80 >> (block & 7)) & 0xFF
    return chosen


def _store_index(image, block, pointers):
    image[block * BLOCK_SIZE:(block + 1) * BLOCK_SIZE] = bytes(BLOCK_SIZE)
    base = block * BLOCK_SIZE
    for position, pointer in enumerate(pointers):
        image[base + position] = pointer & 0xFF
        image[base + 256 + position] = pointer >> 8


def add_file(image, name, data, file_type, aux_type=0, access=0xC3, version=0, min_version=0,
             created=b"\0\0\0\0"):
    """Add a file to the volume directory of a mutable ProDOS-order image. The disk is left unchanged on error."""
    name = normalize_file_name(name)
    if not data or len(data) > MAX_FILE_SIZE:
        raise DiskImageError(f"ProDOS file {name} must contain 1 to {MAX_FILE_SIZE} bytes")
    if any(entry["name"] == name for entry in list_files(image)):
        raise DiskImageError(f"ProDOS file already exists: {name}")
    slot = next(((block, offset) for block, offset in _directory_entries(image)
                 if image[block * BLOCK_SIZE + offset] >> 4 == 0), None)
    if slot is None:
        raise DiskImageError("ProDOS directory is full")

    data_blocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
    if data_blocks == 1:
        storage, index_count = STORAGE_SEEDLING, 0
    elif data_blocks <= INDEX_ENTRIES:
        storage, index_count = STORAGE_SAPLING, 1
    else:
        storage = STORAGE_TREE
        index_count = (data_blocks + INDEX_ENTRIES - 1) // INDEX_ENTRIES + 1
    if data_blocks > INDEX_ENTRIES * 128:
        raise DiskImageError(f"ProDOS file {name} is too large")

    snapshot = bytes(image)
    try:
        allocated = _allocate(image, index_count + data_blocks)
        index_blocks, file_blocks = allocated[:index_count], allocated[index_count:]
        for position, number in enumerate(file_blocks):
            chunk = data[position * BLOCK_SIZE:(position + 1) * BLOCK_SIZE]
            image[number * BLOCK_SIZE:(number + 1) * BLOCK_SIZE] = chunk.ljust(BLOCK_SIZE, b"\0")
        if storage == STORAGE_SEEDLING:
            key = file_blocks[0]
        elif storage == STORAGE_SAPLING:
            key = index_blocks[0]
            _store_index(image, key, file_blocks)
        else:
            key = index_blocks[0]
            subindex = index_blocks[1:]
            _store_index(image, key, subindex)
            for position, number in enumerate(subindex):
                _store_index(image, number, file_blocks[position * INDEX_ENTRIES:(position + 1) * INDEX_ENTRIES])
    except DiskImageError:
        image[:] = snapshot
        raise

    block, offset = slot
    base = block * BLOCK_SIZE + offset
    image[base:base + ENTRY_SIZE] = bytes(ENTRY_SIZE)
    image[base] = (storage << 4) | len(name)
    image[base + 1:base + 1 + len(name)] = name.encode("ascii")
    image[base + 0x10] = file_type
    struct.pack_into("<HH", image, base + 0x11, key, len(allocated))
    image[base + 0x15:base + 0x18] = len(data).to_bytes(3, "little")
    image[base + 0x18:base + 0x1C] = created
    image[base + 0x1C] = version
    image[base + 0x1D] = min_version
    image[base + 0x1E] = access
    struct.pack_into("<H", image, base + 0x1F, aux_type)
    struct.pack_into("<H", image, base + 0x25, VOLUME_DIRECTORY_BLOCK)
    header = VOLUME_DIRECTORY_BLOCK * BLOCK_SIZE
    count = struct.unpack_from("<H", image, header + 0x25)[0]
    struct.pack_into("<H", image, header + 0x25, count + 1)


def copy_boot_files(image, source, names=BOOT_FILES):
    """Copy the boot blocks 0 and 1 and the named system files from a bootable ProDOS image into a volume."""
    image[:2 * BLOCK_SIZE] = source[:2 * BLOCK_SIZE]
    for name in names:
        data, entry = read_file(source, name)
        add_file(image, name, data, entry["type"], entry["aux"], entry["access"], entry["version"],
                 entry["minVersion"], entry["created"])


def file_type_for_suffix(suffix):
    try:
        return FILE_TYPES[suffix.upper()]
    except KeyError:
        raise DiskImageError(f"Unsupported ProDOS file type {suffix!r}; use {', '.join(sorted(FILE_TYPES))}") from None


def prepare_source_file(filename, data, binary_load_address=None):
    """Return (prodosName, fileType, auxType, payload) for a system-disk source file.

    The suffix selects the type: .BAS (Applesoft listing or already tokenized), .TXT, .BIN (needs a load address)
    or .SYS. Characters ProDOS does not allow in names ('-' and '_') become '.'; HELLO.BAS, the greeting
    program, becomes STARTUP, which BASIC.SYSTEM runs at boot.
    """
    suffix = Path(filename).suffix.upper()
    file_type = file_type_for_suffix(suffix)
    name = Path(filename).stem.upper().replace("-", ".").replace("_", ".")
    aux_type = 0
    if file_type == FILE_TYPES[".BAS"]:
        if apple2Basic.is_ascii_source(data):
            try:
                data = apple2Basic.tokenize_source(data.decode("ascii"))
            except (UnicodeDecodeError, ValueError) as error:
                raise DiskImageError(f"Invalid Applesoft BASIC source in {filename}: {error}") from error
        aux_type = BASIC_LOAD_ADDRESS
        if name == "HELLO":
            name = "STARTUP"
    elif file_type == FILE_TYPES[".TXT"]:
        data = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").replace(b"\n", b"\r")
        if data and not data.endswith(b"\r"):
            data += b"\r"
    elif file_type == FILE_TYPES[".BIN"]:
        if binary_load_address is None or not 0 <= binary_load_address <= 0xFFFF:
            raise DiskImageError(f"A load address (0..65535) is required for ProDOS binary file {filename}")
        aux_type = binary_load_address
    elif file_type == FILE_TYPES[".SYS"]:
        aux_type = 0x2000
    return normalize_file_name(name), file_type, aux_type, data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", type=Path, help="image file to write (use a .po suffix)")
    parser.add_argument("--name", default="DATA800", help="ProDOS volume name (1..15 characters)")
    parser.add_argument("--profile", default="800K", choices=sorted(VOLUME_BLOCKS))
    parser.add_argument(
        "--boot-from",
        type=Path,
        help="bootable ProDOS image (.po or 140K .dsk) whose boot blocks and system files (" + ", ".join(BOOT_FILES) +
        ") are copied into the new volume",
    )
    arguments = parser.parse_args(argv)
    try:
        data = build_blank_volume(arguments.name, arguments.profile)
        if arguments.boot_from is not None:
            image = bytearray(data)
            copy_boot_files(image, load_volume(arguments.boot_from))
            data = bytes(image)
    except DiskImageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    arguments.output.write_bytes(data)
    print(f"wrote {arguments.output}: {len(data)} bytes, {len(data) // BLOCK_SIZE} blocks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
