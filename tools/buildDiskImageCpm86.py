#!/usr/bin/env python3
"""Build the CP/M-86 system disk image (CPM86 profile) from a directory of files."""

from pathlib import Path

import diskImageCpm86

DETAILS = (
    "Composes system.dsk (CPM86 profile) from every file in --source-dir that has a\n"
    "valid CP/M 8.3 name; CPM.SYS is added first. --source-dir is required."
)


def add_arguments(parser):
    parser.add_argument("--source-dir", type=Path, help="directory with CPM.SYS and the .CMD files (cpm86)")


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


def main():
    import buildDiskImage

    buildDiskImage.main(default_os="cpm86")


if __name__ == "__main__":
    main()
