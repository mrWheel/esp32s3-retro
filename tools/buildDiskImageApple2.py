#!/usr/bin/env python3
"""Build a ProDOS 8 data (or bootable) volume for the Apple II emulator."""

from pathlib import Path

import diskImageApple2Prodos
from diskImageCommon import DiskImageError

PRODOS_BASE_IMAGE = Path("bootDisks") / "apple2" / "prodosEmpty.po"
DEFAULT_PROFILE = "800K"
PRODOS_ORDER_SUFFIXES = (".PO", ".HDV", ".2MG")

DETAILS = (
    "Builds a ProDOS 8 volume (default 800K = 1600 blocks, also 640K and 140K).\n"
    "Use it as the second drive (PRn.2) in drives.cfg, or with --bootable as a ProDOS boot disk.\n"
    "Without --output the volume is written to <sd-root>/retro/images/apple2/data<size>.po.\n"
    "Files in --source-dir get a ProDOS type from the suffix (.BAS, .TXT, .BIN, .SYS); characters\n"
    "ProDOS does not allow in names become '.' and HELLO.BAS becomes STARTUP.\n"
    "A .po file is in ProDOS block order; any other suffix (.dsk) is written in DOS sector order,\n"
    "which is only possible for the 140K profile."
)


def default_image_name(arguments):
    return f"data{arguments.profile[:-1]}.po"


def add_arguments(parser):
    parser.add_argument(
        "--profile",
        type=str.upper,
        choices=sorted(diskImageApple2Prodos.VOLUME_BLOCKS),
        default=DEFAULT_PROFILE,
        help="volume size (default: %(default)s)",
    )
    parser.add_argument("--name", default="DATA", help="ProDOS volume name (default: %(default)s)")
    parser.add_argument("--source-dir", type=Path, help="directory with files to add to the volume")
    parser.add_argument(
        "--binary-load-address",
        type=lambda value: int(value, 0),
        help="load address for all raw .BIN files (decimal or 0x-prefixed hexadecimal)",
    )
    parser.add_argument(
        "--bootable",
        action="store_true",
        help=f"make the volume a ProDOS boot disk (boot blocks and system files from {PRODOS_BASE_IMAGE})",
    )
    parser.add_argument("--boot-from", type=Path, help="bootable ProDOS image to copy the boot files from (implies --bootable)")


def _source_files(source_dir, binary_load_address):
    if not source_dir.is_dir():
        raise DiskImageError(f"Source directory does not exist: {source_dir}")
    files = []
    names = set()
    for path in sorted(source_dir.iterdir(), key=lambda item: item.name.upper()):
        if path.name in (".DS_Store", "README.md"):
            continue
        if path.is_symlink() or not path.is_file():
            raise DiskImageError(f"Only regular files are supported in the source directory: {path}")
        name, file_type, aux_type, payload = diskImageApple2Prodos.prepare_source_file(
            path.name, path.read_bytes(), binary_load_address
        )
        if name in names:
            raise DiskImageError(f"Duplicate ProDOS filename in source directory: {name}")
        names.add(name)
        files.append((path.name, name, file_type, aux_type, payload))
    return files


def build(arguments, output_path, project_root):
    image = bytearray(diskImageApple2Prodos.build_blank_volume(arguments.name, arguments.profile))
    boot_image = arguments.boot_from or (project_root / PRODOS_BASE_IMAGE if arguments.bootable else None)
    if boot_image is not None:
        diskImageApple2Prodos.copy_boot_files(image, diskImageApple2Prodos.load_volume(boot_image))
    added = []
    if arguments.source_dir is not None:
        for source_name, name, file_type, aux_type, payload in _source_files(
            arguments.source_dir, arguments.binary_load_address
        ):
            diskImageApple2Prodos.add_file(image, name, payload, file_type, aux_type)
            added.append((source_name, name))
    data = bytes(image)
    if output_path.suffix.upper() not in PRODOS_ORDER_SUFFIXES:
        if arguments.profile != "140K":
            raise DiskImageError("Only the 140K profile can be written as a DOS-order .dsk; use a .po file name")
        data = diskImageApple2Prodos.dos_order_from_prodos_order(data)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    print(f"ProDOS volume /{diskImageApple2Prodos.normalize_volume_name(arguments.name)}"
          f" ({arguments.profile}{', bootable' if boot_image is not None else ''})")
    for source_name, name in added:
        print(f"  {source_name} -> {name}")


def main():
    import buildDiskImage

    buildDiskImage.main(default_os="apple2")


if __name__ == "__main__":
    main()
