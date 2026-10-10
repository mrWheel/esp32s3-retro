#!/usr/bin/env python3
"""Build the disk image for an operating system (select with --os)."""

import argparse
import sys
from pathlib import Path

import osProfiles

EXAMPLES = """examples:
  buildDiskImage.py --os cpm80
  buildDiskImage.py --os cpm80 --output littlefs/cpm80/system.dsk
  buildDiskImage.py --os cpm86 --source-dir ~/cpm86/files
  buildDiskImage.py --os apple2 --profile 800K --name DATA
  buildDiskImage.py --os apple2 --bootable --name SYSTEM prodosBoot.po

Without --output the image is written to <sd-root>/retro/images/<os>/system.dsk
(Apple II: data<size>.po).
An existing image is replaced (the build is deterministic).
The work is done by buildDiskImage<Os>.py (e.g. buildDiskImageCpm80.py)."""


def build_parser(os_name, default_os):
    info = osProfiles.OS_REGISTRY.get(os_name)
    epilog = osProfiles.os_epilog(os_name, for_build=True)
    if info and info.buildable:
        epilog += "\n" + info.builder().DETAILS
    parser = argparse.ArgumentParser(
        prog="buildDiskImage.py",
        description="Build the system disk image for an operating system. "
        "The result goes to <sd-root>/retro/images/<os>/system.dsk by default.",
        epilog=epilog + "\n\n" + EXAMPLES,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    osProfiles.add_os_argument(parser, default_os)
    osProfiles.add_sd_root_argument(parser)
    parser.add_argument("output", nargs="?", type=Path, help="output image (name or path); same as --output")
    parser.add_argument("--output", dest="output_option", type=Path, help="output image (name or path)")
    if info and info.buildable:
        info.builder().add_arguments(parser)
    return parser


def print_upload_notice(info, output_path):
    """Tell the user that a new image only reaches the emulator through the SD card."""
    print()
    if (osProfiles.PROJECT_ROOT / "littlefs") in output_path.parents:
        print("*** IMPORTANT: this image is part of the firmware (LittleFS partition). ***")
        print("To use it the emulator must be rebuilt AND flashed again.")
        return
    print("*** IMPORTANT: this image is not on the emulator's SD card yet. ***")
    print('Upload it to the (physical) SD card with "File Transfer" (emulator menu option 6),')
    print(f"into /retro/images/{info.key}/ , and name it in /retro/images/{info.key}/drives.cfg where needed.")
    print(f"Local file: {output_path}")


def main(argv=None, default_os=None):
    argv = sys.argv[1:] if argv is None else argv
    os_name = osProfiles.preparse_os(argv, default_os)
    parser = build_parser(os_name, default_os)
    arguments = parser.parse_args(argv)
    info = osProfiles.require_buildable(parser, arguments.os_name)
    builder = info.builder()
    default_name = getattr(builder, "default_image_name", None)
    requested = (
        arguments.output_option
        or arguments.output
        or Path(default_name(arguments) if default_name else info.default_system_image)
    )
    output_path = osProfiles.resolve_image_path(arguments.sd_root, info.key, requested).resolve()
    try:
        builder.build(arguments, output_path, osProfiles.PROJECT_ROOT)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Created {output_path} ({output_path.stat().st_size} bytes)")
    print_upload_notice(info, output_path)


if __name__ == "__main__":
    main()
