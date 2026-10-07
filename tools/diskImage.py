#!/usr/bin/env python3
"""Create, populate and list raw disk images for the retro systems (select with --os)."""

import argparse
import glob
import sys
from pathlib import Path

import osProfiles
from diskImageCommon import DiskImageError

WILDCARD_CHARS = "*?["
EXAMPLES = """examples:
  diskImage.py create --os cpm80 --profile LARGE work.dsk
  diskImage.py create --os cpm80 --profile BIG big.dsk
  diskImage.py add    --os cpm80 work.dsk ~/cpm/*.COM 'utils/*.HLP'
  diskImage.py list   --os cpm86 work86.dsk
  diskImage.py extract --os cpm86 work86.dsk HOST.CMD --output HOST.CMD

A bare image name is placed in <sd-root>/retro/images/<os>/ (default sd-root:
the project's sdcard/ directory). Give a path to use another location.
Wildcards may be quoted (expanded here) or left to the shell. Files from a
wildcard whose name is not a valid 8.3 name or that are empty are skipped with
a warning; explicitly named files must be valid."""


def expand_sources(patterns):
    """Return (explicit_files, wildcard_files, warnings); raise when a wildcard matches nothing."""
    selected = []
    warnings = []
    for pattern in patterns:
        text = str(pattern)
        if not any(char in text for char in WILDCARD_CHARS):
            selected.append((Path(text).expanduser(), False))
            continue
        matches = sorted(Path(match) for match in glob.glob(str(Path(text).expanduser())))
        files = [match for match in matches if match.is_file()]
        if not files:
            raise DiskImageError(f"Wildcard matches no files: {text}")
        selected.extend((match, True) for match in files)
    return selected, warnings


def collect_additions(engine, selected, warnings):
    additions = []
    seen = {}
    for source, from_wildcard in selected:
        if not source.is_file():
            raise DiskImageError(f"Input file does not exist: {source}")
        data = source.read_bytes()
        try:
            name = engine.parse_filename(source.name)
            if not data:
                raise DiskImageError(f"CP/M files must not be empty: {name}")
        except DiskImageError as error:
            if not from_wildcard:
                raise
            warnings.append(f"skipped {source}: {error}")
            continue
        if name in seen and seen[name] != source:
            raise DiskImageError(f"{source} and {seen[name]} both map to {name}")
        if name not in seen:
            seen[name] = source
            additions.append((name, data))
    return additions


def build_parser(os_name, default_os):
    parser = argparse.ArgumentParser(
        prog="diskImage.py",
        description="Create, populate and list raw disk images for the retro systems. "
        "Images are written to <sd-root>/retro/images/<os>/.",
        epilog=osProfiles.os_epilog(os_name) + "\n\n" + EXAMPLES,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    common = argparse.ArgumentParser(add_help=False)
    osProfiles.add_os_argument(common, default_os)
    osProfiles.add_sd_root_argument(common)
    parser.add_argument(
        "--os",
        dest="_os_help",
        default=argparse.SUPPRESS,
        metavar="{" + ",".join(osProfiles.OS_REGISTRY) + "}",
        help="target operating system (required; may also be given after the command)",
    )

    def sub(name, help_text):
        return commands.add_parser(
            name,
            parents=[common],
            help=help_text,
            description=help_text,
            epilog=osProfiles.os_epilog(os_name) + "\n\n" + EXAMPLES,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )

    commands = parser.add_subparsers(
        dest="command", required=True, metavar="{create,add,extract,list}"
    )
    known = osProfiles.OS_REGISTRY.get(os_name)
    profiles = known.profiles if known and known.supported else ()
    create_parser = sub("create", "create an empty, formatted disk image")
    create_parser.add_argument("image", type=Path)
    create_parser.add_argument(
        "--profile",
        choices=profiles or None,
        type=str.upper,
        help="disk geometry" + (f" ({', '.join(profiles)})" if profiles else " (see --os help)"),
    )
    create_parser.add_argument("--force", action="store_true", help="replace an existing image")

    add_parser = sub("add", "add local files (wildcards allowed) to an existing image")
    add_parser.add_argument("image", type=Path)
    add_parser.add_argument("files", nargs="+", help="files or wildcard patterns such as '*.COM'")
    add_parser.add_argument("--name", help="CP/M 8.3 name (only when adding one explicit file)")

    extract_parser = sub("extract", "extract one file's CP/M records from an image")
    extract_parser.add_argument("image", type=Path)
    extract_parser.add_argument("filename", help="CP/M 8.3 filename")
    extract_parser.add_argument("--output", required=True, type=Path, help="new local output file")
    extract_parser.add_argument("--user", type=int, default=0, choices=range(16), help="CP/M user area")

    list_parser = sub("list", "validate and list an image")
    list_parser.add_argument("image", type=Path)
    return parser


def main(argv=None, default_os=None):
    argv = sys.argv[1:] if argv is None else argv
    os_name = osProfiles.preparse_os(argv, default_os)
    argv = osProfiles.strip_os_option(argv)
    parser = build_parser(os_name, os_name or default_os)
    arguments = parser.parse_args(argv)
    info = osProfiles.require_supported(parser, arguments.os_name)
    engine = info.image()
    image_path = osProfiles.resolve_image_path(arguments.sd_root, info.key, arguments.image)
    try:
        if arguments.command == "create":
            profile = arguments.profile or info.default_profile
            if profile not in info.profiles:
                parser.error(f"--profile {profile} is not valid for --os {info.key}: use {', '.join(info.profiles)}")
            engine.create_image(image_path, force=arguments.force, profile=profile)
            size = engine.PROFILES[profile]["image_size"]
            print(f"Created empty {info.label} {profile} image: {image_path} ({size} bytes)")
        elif arguments.command == "add":
            if arguments.name and (len(arguments.files) != 1 or any(c in arguments.files[0] for c in WILDCARD_CHARS)):
                parser.error("--name can only be used with one explicit (non-wildcard) input file")
            selected, warnings = expand_sources(arguments.files)
            additions = collect_additions(engine, selected, warnings)
            if arguments.name:
                additions = [(arguments.name, additions[0][1])]
            for warning in warnings:
                print("warning: " + warning, file=sys.stderr)
            if not additions:
                raise DiskImageError("No usable files to add")
            engine.add_files(image_path, additions)
            for name, data in additions:
                print(f"  {name:<12} {len(data):>8} bytes")
            print(f"Added {len(additions)} file(s) to {image_path}")
        elif arguments.command == "extract":
            output_path = arguments.output.expanduser()
            if output_path.resolve() == image_path.resolve():
                raise DiskImageError("Output file must not be the disk image")
            data = engine.extract_file(image_path, arguments.filename, user=arguments.user)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_created = False
            try:
                with output_path.open("xb") as output_file:
                    output_created = True
                    output_file.write(data)
            except Exception:
                if output_created:
                    output_path.unlink(missing_ok=True)
                raise
            print(f"Extracted {arguments.filename.upper()} ({len(data)} record bytes) to {output_path}")
        else:
            engine.list_files(image_path)
    except (OSError, DiskImageError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
