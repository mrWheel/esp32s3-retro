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

"""Build the CP/M-86 system disk image (CPM86_160K profile) from a directory of files."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import diskImageCpm86

DETAILS = (
    "Composes system.dsk (CPM86_160K profile) from every file in --source-dir that has a\n"
    "valid CP/M 8.3 name; CPM.SYS is added first. --source-dir is required."
)


def add_arguments(parser):
    parser.add_argument(
        "--source-dir",
        type=Path,
        required=True,
        help="directory with CPM.SYS and the .CMD files (cpm86)",
    )


def build(arguments, output_path, project_root):
    source_dir = arguments.source_dir
    if source_dir is None:
        raise diskImageCpm86.DiskImageError("--source-dir is required for --os cpm86")
    source_dir = Path(source_dir)
    if not source_dir.is_dir():
        raise diskImageCpm86.DiskImageError(f"--source-dir is not a directory: {source_dir}")
    files = sorted(
        (path for path in source_dir.iterdir() if path.is_file()),
        key=lambda path: (path.name.upper() != "CPM.SYS", path.name.upper()),
    )
    if not any(path.name.upper() == "CPM.SYS" for path in files):
        raise diskImageCpm86.DiskImageError(f"CPM.SYS not found in {source_dir}")
    additions = [(path.name, path.read_bytes()) for path in files]
    diskImageCpm86.create_image(output_path, force=True)
    try:
        diskImageCpm86.add_files(output_path, additions)
    except diskImageCpm86.DiskImageError:
        output_path.unlink(missing_ok=True)
        raise
