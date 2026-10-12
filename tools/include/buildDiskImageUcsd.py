#!/usr/bin/env python3
"""Build a blank, formatted UCSD (Apple) Pascal work volume for the Pascal profile."""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import diskImageApple2Pascal
from diskImageCommon import DiskImageError

DEFAULT_PROFILE = "640K"

DETAILS = (
    "Builds an empty, formatted Apple Pascal volume (.po, block order): 640K (1280 blocks, default) or\n"
    "140K (280 blocks). It is a work volume, not a boot disk: the Pascal boot disk is a separate image.\n"
    "Without --output the volume is written to <sd-root>/retro/images/ucsd/<name>.po (name as typed,\n"
    "'pietJe' gives pietJe.po; the volume name inside the image is upper-cased).\n"
    "Add files afterwards with extractApple2Files.py."
)


def default_image_name(arguments):
    return f"{arguments.name}.po"


def volume_name(text):
    if "/" in text or "\\" in text:
        raise argparse.ArgumentTypeError(f"invalid character in name {text!r}")
    try:
        diskImageApple2Pascal.normalize_volume_name(text)
    except DiskImageError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return text


def add_arguments(parser):
    parser.add_argument(
        "--profile",
        type=str.upper,
        choices=sorted(diskImageApple2Pascal.VOLUME_BLOCKS),
        default=DEFAULT_PROFILE,
        help="volume size (default: %(default)s)",
    )
    parser.add_argument(
        "--name",
        type=volume_name,
        default="WORK",
        help="Pascal volume name, 1..7 characters; also the image file name <name>.po (default: %(default)s)",
    )


def build(arguments, output_path, project_root):
    try:
        data = diskImageApple2Pascal.build_blank_volume(arguments.name, arguments.profile)
    except DiskImageError as error:
        raise ValueError(str(error)) from error
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    print(f"Pascal volume {diskImageApple2Pascal.normalize_volume_name(arguments.name)}:"
          f" ({arguments.profile})")


def main():
    import buildDiskImage

    buildDiskImage.main(default_os="ucsd")


if __name__ == "__main__":
    main()
