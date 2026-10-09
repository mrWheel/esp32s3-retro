#!/usr/bin/env python3
"""Add files to 16-sector DOS 3.3-order Apple II disk images."""

import os
import tempfile
from pathlib import Path

import apple2Basic
from diskImageCommon import DiskImageError

SECTOR_SIZE = 256
SECTORS_PER_TRACK = 16
TRACKS = 35
IMAGE_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK * TRACKS
VTOC_TRACK = 17
VTOC_SECTOR = 0
MAX_TS_PAIRS_PER_LIST = 122
CATALOG_ENTRY_OFFSET = 0x0B
CATALOG_ENTRY_SIZE = 35
CATALOG_ENTRIES_PER_SECTOR = 7
TS_LIST_OFFSET = 0x0C
TS_PAIRS_PER_LIST = (SECTOR_SIZE - TS_LIST_OFFSET) // 2
FILE_TYPES = {
    ".TXT": 0x00,
    ".INT": 0x01,
    ".BAS": 0x02,
    ".BIN": 0x04,
}
EXECUTABLE_EXTENSIONS = {".INT", ".BAS", ".BIN"}


def _sector_offset(track, sector):
    if not 0 <= track < TRACKS or not 0 <= sector < SECTORS_PER_TRACK:
        raise DiskImageError(f"Invalid DOS 3.3 track/sector address: {track}/{sector}")
    return (track * SECTORS_PER_TRACK + sector) * SECTOR_SIZE


def parse_filename(filename):
    """Return an uppercase DOS 3.3 filename after validating its file type."""
    filename = _normalize_catalog_name(filename)
    if Path(filename).suffix.upper() not in FILE_TYPES:
        supported = ", ".join(sorted(FILE_TYPES))
        raise DiskImageError(f"Unsupported Apple DOS file type for {filename}; use {supported}")
    return filename


def _normalize_catalog_name(filename):
    try:
        encoded = filename.encode("ascii").upper()
    except UnicodeEncodeError as error:
        raise DiskImageError(f"Apple DOS filenames must be ASCII: {filename}") from error
    if (
        not encoded
        or len(encoded) > 30
        or any(
            value < 0x20 or value > 0x7E or value in (ord(":"), ord("/"), ord("\\"))
            for value in encoded
        )
    ):
        raise DiskImageError(f"Not a supported Apple DOS 3.3 filename: {filename}")
    return encoded.decode("ascii")


def _read_vtoc(image):
    if len(image) != IMAGE_SIZE:
        raise DiskImageError(f"Apple II DOS 3.3 image must be {IMAGE_SIZE} bytes, found {len(image)}")
    vtoc_offset = _sector_offset(VTOC_TRACK, VTOC_SECTOR)
    vtoc = image[vtoc_offset : vtoc_offset + SECTOR_SIZE]
    if (
        vtoc[0x03] != 3
        or vtoc[0x34] != TRACKS
        or vtoc[0x35] != SECTORS_PER_TRACK
        or int.from_bytes(vtoc[0x36:0x38], "little") != SECTOR_SIZE
        or vtoc[0x27] != MAX_TS_PAIRS_PER_LIST
    ):
        raise DiskImageError("Image does not contain a supported 35-track, 16-sector DOS 3.3 VTOC")
    catalog_track = vtoc[1]
    catalog_sector = vtoc[2]
    if catalog_track == 0 or catalog_track >= TRACKS or catalog_sector >= SECTORS_PER_TRACK:
        raise DiskImageError("DOS 3.3 VTOC contains an invalid catalog location")
    if _is_free(vtoc, VTOC_TRACK, VTOC_SECTOR):
        raise DiskImageError("DOS 3.3 VTOC marks itself as free")
    return vtoc_offset, vtoc, catalog_track, catalog_sector


def inspect_image(image):
    """Validate DOS 3.3 geometry and catalog structure; return existing filenames."""
    _, vtoc, catalog_track, catalog_sector = _read_vtoc(image)
    entries = []
    visited_catalog_sectors = set()
    while catalog_track != 0 or catalog_sector != 0:
        catalog_address = (catalog_track, catalog_sector)
        if catalog_address in visited_catalog_sectors:
            raise DiskImageError("DOS 3.3 catalog contains a loop")
        visited_catalog_sectors.add(catalog_address)
        if _is_free(vtoc, catalog_track, catalog_sector):
            raise DiskImageError("DOS 3.3 VTOC marks a catalog sector as free")
        offset = _sector_offset(catalog_track, catalog_sector)
        catalog = image[offset : offset + SECTOR_SIZE]
        for slot in range(CATALOG_ENTRIES_PER_SECTOR):
            entry_offset = CATALOG_ENTRY_OFFSET + slot * CATALOG_ENTRY_SIZE
            entry = catalog[entry_offset : entry_offset + CATALOG_ENTRY_SIZE]
            if entry[0] in (0, 0xFF):
                continue
            try:
                filename = bytes(value & 0x7F for value in entry[3:33]).decode("ascii").rstrip()
            except UnicodeDecodeError as error:
                raise DiskImageError("DOS 3.3 catalog contains a non-ASCII filename") from error
            if not filename:
                raise DiskImageError("DOS 3.3 catalog contains an empty active filename")
            if entry[0] >= TRACKS or entry[1] >= SECTORS_PER_TRACK:
                raise DiskImageError(f"DOS 3.3 catalog entry has an invalid T/S list: {filename}")
            entries.append(filename.upper())
        catalog_track, catalog_sector = catalog[1], catalog[2]
        if (catalog_track == 0) != (catalog_sector == 0):
            raise DiskImageError("DOS 3.3 catalog has an incomplete next-sector pointer")
    if len(entries) != len(set(entries)):
        raise DiskImageError("DOS 3.3 catalog contains duplicate filenames")
    return entries


def _is_free(vtoc, track, sector):
    bitmap_offset = 0x38 + track * 4 + (0 if sector >= 8 else 1)
    bit = sector - 8 if sector >= 8 else sector
    return bool(vtoc[bitmap_offset] & (1 << bit))


def _set_free(vtoc, track, sector, is_free):
    bitmap_offset = 0x38 + track * 4 + (0 if sector >= 8 else 1)
    bit = sector - 8 if sector >= 8 else sector
    mask = 1 << bit
    if is_free:
        vtoc[bitmap_offset] |= mask
    else:
        vtoc[bitmap_offset] &= ~mask


def _allocate_sector(image, vtoc, reserved_tracks=(0, 1, 2, 17), tracks=None, reverse_sectors=False):
    for track in tracks if tracks is not None else range(TRACKS):
        if track in (0, 1, 2) or track in reserved_tracks:
            continue
        sector_order = range(SECTORS_PER_TRACK - 1, -1, -1) if reverse_sectors else range(SECTORS_PER_TRACK)
        for sector in sector_order:
            if _is_free(vtoc, track, sector):
                _set_free(vtoc, track, sector, False)
                vtoc[0x30] = track
                offset = _sector_offset(track, sector)
                image[offset : offset + SECTOR_SIZE] = bytes(SECTOR_SIZE)
                return track, sector
    raise DiskImageError("DOS 3.3 disk is full")


def _find_catalog_slot(image, vtoc, catalog_track, catalog_sector):
    previous = None
    visited = set()
    while catalog_track != 0 or catalog_sector != 0:
        address = (catalog_track, catalog_sector)
        if address in visited:
            raise DiskImageError("DOS 3.3 catalog contains a loop")
        visited.add(address)
        offset = _sector_offset(catalog_track, catalog_sector)
        catalog = image[offset : offset + SECTOR_SIZE]
        for slot in range(CATALOG_ENTRIES_PER_SECTOR):
            entry_offset = CATALOG_ENTRY_OFFSET + slot * CATALOG_ENTRY_SIZE
            if catalog[entry_offset] in (0, 0xFF):
                return offset + entry_offset, previous
        previous = (offset, catalog_track, catalog_sector)
        catalog_track, catalog_sector = catalog[1], catalog[2]

    if previous is None:
        raise DiskImageError("DOS 3.3 catalog is missing")
    track, sector = _allocate_sector(
        image,
        vtoc,
        reserved_tracks=(0, 1, 2),
        tracks=(VTOC_TRACK,),
        reverse_sectors=True,
    )
    new_offset = _sector_offset(track, sector)
    previous_offset, _, _ = previous
    image[previous_offset + 1] = track
    image[previous_offset + 2] = sector
    return new_offset + CATALOG_ENTRY_OFFSET, None


def _encode_catalog_name(filename):
    encoded = filename.encode("ascii")
    return bytes(value | 0x80 for value in encoded.ljust(30, b" "))


def _catalog_filename(filename):
    if Path(filename).suffix.upper() == ".BAS":
        return Path(filename).stem
    return filename


def _file_payload(filename, data, binary_load_address):
    file_type = FILE_TYPES[Path(filename).suffix.upper()]
    if file_type == FILE_TYPES[".BIN"]:
        if binary_load_address is None:
            raise DiskImageError(f"Binary load address is required for Apple DOS binary file {filename}")
        if not 0 <= binary_load_address <= 0xFFFF:
            raise DiskImageError("Apple DOS binary load address must be between 0 and 65535")
        if not data or len(data) > 0xFFFF:
            raise DiskImageError(f"Apple DOS binary file must contain 1 to 65535 bytes: {filename}")
        data = (
            binary_load_address.to_bytes(2, "little")
            + len(data).to_bytes(2, "little")
            + data
        )
    elif file_type == FILE_TYPES[".TXT"]:
        if not data:
            raise DiskImageError(f"Apple DOS files must not be empty: {filename}")
        data = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").replace(b"\n", b"\r")
        if b"\x00" in data:
            raise DiskImageError(f"Apple DOS text files must not contain NUL bytes: {filename}")
        if not data.endswith(b"\r"):
            data += b"\r"
    elif file_type == FILE_TYPES[".BAS"] and apple2Basic.is_ascii_source(data):
        try:
            program = apple2Basic.tokenize_source(data.decode("ascii"))
            # A DOS 3.3 Applesoft file starts with the 2-byte little-endian length of the program.
            data = len(program).to_bytes(2, "little") + program
        except (UnicodeDecodeError, ValueError) as error:
            raise DiskImageError(f"Invalid Applesoft BASIC source in {filename}: {error}") from error
    elif not data:
        raise DiskImageError(f"Apple DOS files must not be empty: {filename}")
    return file_type, data


def _write_file(image, vtoc, filename, data, file_type, catalog_track, catalog_sector):
    data_sector_count = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    ts_list_count = (data_sector_count + TS_PAIRS_PER_LIST - 1) // TS_PAIRS_PER_LIST
    if data_sector_count + ts_list_count > 0xFFFF:
        raise DiskImageError(f"Apple DOS file is too large: {filename}")
    data_sectors = [
        _allocate_sector(image, vtoc)
        for _ in range(data_sector_count)
    ]
    ts_list_sectors = [
        _allocate_sector(image, vtoc)
        for _ in range(ts_list_count)
    ]

    for index, (track, sector) in enumerate(data_sectors):
        start = index * SECTOR_SIZE
        chunk = data[start : start + SECTOR_SIZE]
        offset = _sector_offset(track, sector)
        image[offset : offset + SECTOR_SIZE] = chunk.ljust(SECTOR_SIZE, b"\x00")

    for list_index, (track, sector) in enumerate(ts_list_sectors):
        offset = _sector_offset(track, sector)
        first_data_sector = list_index * TS_PAIRS_PER_LIST
        listed_sectors = data_sectors[first_data_sector : first_data_sector + TS_PAIRS_PER_LIST]
        for pair_index, (data_track, data_sector) in enumerate(listed_sectors):
            pair_offset = offset + TS_LIST_OFFSET + pair_index * 2
            image[pair_offset] = data_track
            image[pair_offset + 1] = data_sector
        image[offset + 5 : offset + 7] = (first_data_sector).to_bytes(2, "little")
        if list_index + 1 < len(ts_list_sectors):
            next_track, next_sector = ts_list_sectors[list_index + 1]
            image[offset + 1] = next_track
            image[offset + 2] = next_sector

    entry_offset, _ = _find_catalog_slot(image, vtoc, catalog_track, catalog_sector)
    entry = bytearray(CATALOG_ENTRY_SIZE)
    entry[0] = ts_list_sectors[0][0]
    entry[1] = ts_list_sectors[0][1]
    entry[2] = file_type
    entry[3:33] = _encode_catalog_name(filename)
    entry[33:35] = (data_sector_count + ts_list_count).to_bytes(2, "little")
    image[entry_offset : entry_offset + CATALOG_ENTRY_SIZE] = entry


def add_files(image, files, binary_load_address=None):
    """Return a DOS 3.3-order image with files added, without changing the input."""
    image = bytearray(image)
    existing_names = set(inspect_image(image))
    vtoc_offset, vtoc, catalog_track, catalog_sector = _read_vtoc(image)
    added_names = set()
    for source_name, data in files:
        filename = parse_filename(source_name)
        catalog_name = _catalog_filename(filename)
        if catalog_name in existing_names or catalog_name in added_names:
            raise DiskImageError(f"{catalog_name} already exists on the Apple II disk")
        file_type, payload = _file_payload(filename, bytes(data), binary_load_address)
        _write_file(image, vtoc, catalog_name, payload, file_type, catalog_track, catalog_sector)
        added_names.add(catalog_name)
    image[vtoc_offset : vtoc_offset + SECTOR_SIZE] = vtoc
    inspect_image(image)
    return bytes(image)


def _catalog_entries(image, catalog_track, catalog_sector):
    entries = []
    visited = set()
    while catalog_track != 0 or catalog_sector != 0:
        address = (catalog_track, catalog_sector)
        if address in visited:
            raise DiskImageError("DOS 3.3 catalog contains a loop")
        visited.add(address)
        offset = _sector_offset(catalog_track, catalog_sector)
        catalog = image[offset : offset + SECTOR_SIZE]
        for slot in range(CATALOG_ENTRIES_PER_SECTOR):
            entry_offset = CATALOG_ENTRY_OFFSET + slot * CATALOG_ENTRY_SIZE
            entry = catalog[entry_offset : entry_offset + CATALOG_ENTRY_SIZE]
            if entry[0] in (0, 0xFF):
                continue
            filename = bytes(value & 0x7F for value in entry[3:33]).decode("ascii").rstrip().upper()
            entries.append((filename, offset + entry_offset, entry))
        catalog_track, catalog_sector = catalog[1], catalog[2]
    return entries


def _file_sectors(image, entry, vtoc):
    track, sector = entry[0], entry[1]
    visited_lists = set()
    data_sectors = []
    list_sectors = []
    while track != 0 or sector != 0:
        address = (track, sector)
        if address in visited_lists:
            raise DiskImageError("DOS 3.3 file contains a track/sector-list loop")
        if _is_free(vtoc, track, sector):
            raise DiskImageError("DOS 3.3 file track/sector list is marked free")
        visited_lists.add(address)
        list_sectors.append(address)
        offset = _sector_offset(track, sector)
        ts_list = image[offset : offset + SECTOR_SIZE]
        if int.from_bytes(ts_list[5:7], "little") != len(data_sectors):
            raise DiskImageError("DOS 3.3 file has an invalid track/sector-list offset")
        for pair_offset in range(TS_LIST_OFFSET, SECTOR_SIZE, 2):
            data_track, data_sector = ts_list[pair_offset : pair_offset + 2]
            if data_track == 0 and data_sector == 0:
                break
            if data_track == 0 or data_track >= TRACKS or data_sector >= SECTORS_PER_TRACK:
                raise DiskImageError("DOS 3.3 file contains an invalid data-sector address")
            if _is_free(vtoc, data_track, data_sector):
                raise DiskImageError("DOS 3.3 file data sector is marked free")
            data_sectors.append((data_track, data_sector))
        track, sector = ts_list[1], ts_list[2]
        if (track == 0) != (sector == 0):
            raise DiskImageError("DOS 3.3 file has an incomplete track/sector-list pointer")

    if len(data_sectors) != len(set(data_sectors)):
        raise DiskImageError("DOS 3.3 file contains duplicate data-sector references")
    if set(data_sectors) & set(list_sectors):
        raise DiskImageError("DOS 3.3 file uses a track/sector-list sector as data")
    if len(data_sectors) + len(list_sectors) != int.from_bytes(entry[33:35], "little"):
        raise DiskImageError("DOS 3.3 file sector count does not match its track/sector lists")
    return data_sectors + list_sectors


def remove_files(image, filenames, allow_locked=False):
    """Remove explicitly named files from a DOS 3.3 image copy; locked files need allow_locked."""
    image = bytearray(image)
    inspect_image(image)
    vtoc_offset, vtoc, catalog_track, catalog_sector = _read_vtoc(image)
    entries = _catalog_entries(image, catalog_track, catalog_sector)
    entries_by_name = {filename: (offset, entry) for filename, offset, entry in entries}
    removal_names = [_normalize_catalog_name(filename) for filename in filenames]
    if len(removal_names) != len(set(removal_names)):
        raise DiskImageError("Duplicate Apple DOS filename requested for removal")

    files_to_remove = []
    for filename in removal_names:
        if filename not in entries_by_name:
            raise DiskImageError(f"{filename} does not exist on the Apple II disk")
        entry_offset, entry = entries_by_name[filename]
        if entry[2] & 0x80 and not allow_locked:
            raise DiskImageError(f"Refusing to remove locked Apple DOS file: {filename}")
        files_to_remove.append((filename, entry_offset, entry))

    target_sectors = set()
    for filename, _, entry in files_to_remove:
        file_sectors = set(_file_sectors(image, entry, vtoc))
        if target_sectors & file_sectors:
            raise DiskImageError(f"Apple DOS files share allocated sectors near {filename}")
        target_sectors.update(file_sectors)

    removal_set = set(removal_names)
    for filename, _, entry in entries:
        if filename in removal_set:
            continue
        if target_sectors & set(_file_sectors(image, entry, vtoc)):
            raise DiskImageError(f"Refusing to free sectors still used by another Apple DOS file: {filename}")

    for filename, entry_offset, _ in files_to_remove:
        image[entry_offset] = 0xFF
    for track, sector in target_sectors:
        _set_free(vtoc, track, sector, True)
    image[vtoc_offset : vtoc_offset + SECTOR_SIZE] = vtoc
    inspect_image(image)
    return bytes(image)


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


def write_image(image_path, image, force=False):
    """Validate and atomically install a complete DOS 3.3-order image."""
    inspect_image(image)
    _atomic_write(image_path, image, replace_existing=force)
