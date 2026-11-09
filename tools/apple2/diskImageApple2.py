#!/usr/bin/env python3

# Copyright (C) 2026 Willem Aandewiel
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

"""Engine for tools/diskImage.py with --os apple2: ProDOS 8 data volumes (.po images).

This module only adapts the generic diskImage.py engine contract (PROFILES,
create_image, parse_filename, add_files, extract_file, list_files) to the
existing ProDOS code in diskImageApple2Prodos, which is used unchanged.
"""

import sys
from pathlib import Path

INCLUDE_DIR = Path(__file__).resolve().parents[1] / "include"
if str(INCLUDE_DIR) not in sys.path:
    sys.path.insert(0, str(INCLUDE_DIR))

import diskImageApple2Prodos as prodos
import extractApple2Files
from diskImageCommon import DiskImageError, atomic_write_image

PROFILES = {name: {"image_size": blocks * prodos.BLOCK_SIZE} for name, blocks in prodos.VOLUME_BLOCKS.items()}
DEFAULT_PROFILE = "800K"


def _volume_name_from_path(image_path):
    try:
        return prodos.normalize_volume_name(Path(image_path).stem)
    except DiskImageError:
        return "DATA"


def create_image(image_path, force=False, profile=DEFAULT_PROFILE):
    """Create the volume and return a detail string for diskImage.py's uniform result line."""
    profile = profile.upper()
    if profile not in PROFILES:
        raise DiskImageError(f"Unknown profile {profile!r}; use one of {sorted(PROFILES)}")
    volume_name = _volume_name_from_path(image_path)
    data = prodos.build_blank_volume(volume_name, profile)
    atomic_write_image(image_path, data, replace_existing=force)
    return f"volume /{volume_name}"


def parse_filename(name):
    return prodos.normalize_file_name(name)


def add_files(image_path, files):
    path = Path(image_path)
    original = path.read_bytes()
    image = bytearray(prodos.load_volume(path))
    if bytes(image) != original:
        #-- load_volume accepts a 140K DOS-order .dsk too; only a .po image is written back
        raise DiskImageError(
            f"Only ProDOS-order (.po) images can be modified: {path}; a DOS-order .dsk cannot be added to in place"
        )
    for name, data in files:
        #-- the file keeps its ProDOS name; the suffix selects the type, load address and payload
        _prodos_name, file_type, aux_type, payload = prodos.prepare_source_file(name, data)
        prodos.add_file(image, name, payload, file_type, aux_type)
    atomic_write_image(path, bytes(image), replace_existing=True)


def extract_file(image_path, filename, user=0):
    image = prodos.load_volume(image_path)
    data, _entry = prodos.read_file(image, prodos.normalize_file_name(filename))
    return data


def list_files(image_path):
    image = prodos.load_volume(image_path)
    print(f"ProDOS volume /{prodos.volume_name(image)}")
    entries = prodos.list_files(image)
    if not entries:
        print("No files. The disk is formatted and empty.")
        print(f"{prodos.free_blocks(image)} free blocks")
        return
    width = max(12, *(len(entry["name"]) for entry in entries))
    print(f"  {'Name':<{width}} {'Type':<5} {'Blocks':>6} {'Length':>8}")
    for entry in sorted(entries, key=lambda item: item["name"].upper()):
        type_text = extractApple2Files.PRODOS_TYPE_NAMES.get(entry["type"], f"${entry['type']:02X}")
        print(f"  {entry['name']:<{width}} {type_text:<5} {entry['blocks']:>6} {entry['eof']:>8}")
    print(f"  {len(entries)} file(s), {prodos.free_blocks(image)} free blocks")
