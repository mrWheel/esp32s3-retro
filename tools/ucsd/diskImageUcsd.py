#!/usr/bin/env python3
"""Engine for tools/diskImage.py with --os ucsd: UCSD (Apple) Pascal work volumes (.po images).

This module only adapts the generic diskImage.py engine contract (PROFILES,
create_image, parse_filename, add_files, extract_file, list_files) to the
existing Pascal volume builder in diskImageApple2Pascal and the shared Apple II
volume core in extractApple2Files (UCSD text conversions from extractUcsd).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "apple2"))

INCLUDE_DIR = Path(__file__).resolve().parents[1] / "include"
if str(INCLUDE_DIR) not in sys.path:
    sys.path.insert(0, str(INCLUDE_DIR))

import diskImageApple2Pascal as pascal
import extractApple2Files as core
import extractUcsd
from diskImageCommon import DiskImageError, atomic_write_image

PROFILES = {name: {"image_size": blocks * pascal.BLOCK_SIZE} for name, blocks in pascal.VOLUME_BLOCKS.items()}
DEFAULT_PROFILE = "640K"


def _volume_name_from_path(image_path):
    try:
        return pascal.normalize_volume_name(Path(image_path).stem)
    except DiskImageError:
        return "WORK"


def create_image(image_path, force=False, profile=DEFAULT_PROFILE):
    profile = profile.upper()
    if profile not in PROFILES:
        raise DiskImageError(f"Unknown profile {profile!r}; use one of {sorted(PROFILES)}")
    volume_name = _volume_name_from_path(image_path)
    data = pascal.build_blank_volume(volume_name, profile)
    atomic_write_image(image_path, data, replace_existing=force)
    print(f"Pascal volume {volume_name} ({profile})")


def parse_filename(name):
    try:
        return core.sanitizePascalName(name)
    except core.ToolError as error:
        raise DiskImageError(f"Not a supported Pascal filename: {name}") from error


def _pascal_volume(image_path):
    try:
        image = core.DiskImage(Path(image_path))
        volume = core.openVolume(image)
    except core.ToolError as error:
        raise DiskImageError(str(error)) from error
    if not isinstance(volume, core.PascalVolume):
        raise DiskImageError(f"Not a UCSD Pascal volume: {image_path}")
    return volume


def add_files(image_path, files):
    volume = _pascal_volume(image_path)
    try:
        for name, data in files:
            kind = extractUcsd.KIND_BY_SUFFIX.get(Path(name).suffix.upper(), 5)
            payload = extractUcsd.hostTextToUcsd(data) if kind == 3 else data
            volume.importFile(name, core.Exported(payload, 0x0C, 0, kind))
    except core.ToolError as error:
        raise DiskImageError(str(error)) from error
    volume.image.save()


def extract_file(image_path, filename, user=0):
    volume = _pascal_volume(image_path)
    try:
        entry = core.findEntry(volume, filename)
        exported = volume.exportFile(entry)
    except core.ToolError as error:
        raise DiskImageError(str(error)) from error
    return extractUcsd.ucsdTextToHost(exported.data) if exported.pascalKind == 3 else exported.data


def list_files(image_path):
    core.printListing(_pascal_volume(image_path))
