#!/usr/bin/env python3
"""Build a bootable system disk or create an empty work disk (select the OS with --os)."""

import argparse
import sys
from pathlib import Path

INCLUDE_DIR = Path(__file__).resolve().parent / "include"
if str(INCLUDE_DIR) not in sys.path:
    sys.path.insert(0, str(INCLUDE_DIR))

import osProfiles
import systemDisk
from diskImageCommon import DiskImageError, confirm_plan

DISK_TYPES = ("SYSTEMDSK", "WORKDSK")
EXAMPLES = """examples:
  diskManager.py --type SYSTEMDSK --os cpm80 --profile CPM80_512K
  diskManager.py --type SYSTEMDSK --os cpm86 --profile CPM86_516K
  diskManager.py --type SYSTEMDSK --os apple2 --profile PRODOS_800K
  diskManager.py --type SYSTEMDSK --os ucsd --profile PASCAL_800K
  diskManager.py --type WORKDSK --os cpm80 --profile CPM80_512K work.dsk
  diskManager.py --type WORKDSK --os cpm86 work86.dsk
  diskManager.py --type WORKDSK --os apple2 --profile 800K data.po
  diskManager.py --type WORKDSK --os ucsd --profile 140K work.po
  diskManager.py --yes --type WORKDSK --os cpm80 work.dsk
  diskManager.py --type SYS --os ucsd --profile PASCAL_800K

--type takes SYSTEMDSK or WORKDSK; any unique abbreviation in any case is
accepted (S, SY, SYS, ..., W, WO, WORK, ...).

SYSTEMDSK writes littlefs/<os>/system.dsk (system.po for ucsd) unless --output
names another path; it is built from the OS boot resources plus every file in
bootDisks/<os>/systemDsk/ and is bootable. WORKDSK writes a bare image name to
<sd-root>/retro/images/<os>/ (default sd-root: the project's sdcard/
directory); give a path to use another location. Files are added, listed and
extracted with tools/extractFiles.py.

Every run prints a summary of the planned action and asks "Are you sure
(y/N)"; anything but y/yes aborts without writing. For scripts and
non-interactive use pass --yes (or -y): without a terminal a writing run is
refused unless --yes is given."""


def unique_disk_type(type_name):
    """Return the DISK_TYPES entry a value or unique abbreviation stands for (None if ambiguous)."""
    matches = [name for name in DISK_TYPES if name.startswith(type_name.upper())]
    if len(matches) != 1:
        return None
    return matches[0]


def build_parser(os_name, type_name):
    info = osProfiles.OS_REGISTRY.get(os_name)
    profile_choices = None
    profile_help = "disk profile (add --os <name> to -h for the profiles)"
    if info and type_name == "SYSTEMDSK" and os_name in systemDisk.PROFILE_MAP:
        profile_choices = tuple(systemDisk.PROFILE_MAP[os_name])
        profile_help = "system disk profile (" + ", ".join(profile_choices) + ")"
    elif info and info.supported and type_name == "WORKDSK":
        profile_choices = tuple(info.profiles)
        profile_help = "work disk geometry (" + ", ".join(profile_choices) + ")"
    parser = argparse.ArgumentParser(
        prog="diskManager.py",
        description="Build a bootable system disk (--type SYSTEMDSK) or create an empty work "
        "disk (--type WORKDSK) for the retro systems. Images are written to "
        "littlefs/<os>/ or <sd-root>/retro/images/<os>/. Every run prints a summary first "
        "and asks 'Are you sure (y/N)' unless --yes is given.",
        epilog=osProfiles.os_epilog(os_name) + "\n\n" + EXAMPLES,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--type",
        required=True,
        help="what to build: SYSTEMDSK (bootable LittleFS system disk) or WORKDSK (empty SD "
        "work disk); any unique abbreviation is accepted (S, SYS, WORK, ...)",
    )
    osProfiles.add_os_argument(parser)
    parser.add_argument("--profile", choices=profile_choices, type=str.upper, help=profile_help)
    parser.add_argument(
        "--source-dir",
        type=Path,
        help="SYSTEMDSK: directory with the files to add (default: bootDisks/<os>/systemDsk/)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="SYSTEMDSK: output path (default: littlefs/<os>/system.dsk, system.po for ucsd)",
    )
    parser.add_argument(
        "--base-image",
        type=Path,
        help="SYSTEMDSK --os apple2: bootable ProDOS volume to copy the boot files from",
    )
    parser.add_argument(
        "--binary-load-address",
        type=lambda value: int(value, 0),
        help="SYSTEMDSK --os apple2: load address for raw .BIN files (decimal or 0x-prefixed hexadecimal)",
    )
    parser.add_argument(
        "--volume-name",
        help=f"SYSTEMDSK --os apple2: ProDOS volume name (default: {systemDisk.PRODOS_VOLUME_NAME})",
    )
    osProfiles.add_sd_root_argument(parser)
    parser.add_argument(
        "--force",
        action="store_true",
        help="WORKDSK: replace an existing image",
    )
    parser.add_argument(
        "--yes",
        "-y",
        action="store_true",
        help="do not ask for confirmation before the planned action is executed",
    )
    parser.add_argument("image", nargs="?", type=Path, help="WORKDSK: work disk name or path")
    return parser


def plan_header(info, type_name):
    """Common first lines of the planned-action summary, identical for every OS."""
    return [
        "Planned action",
        f"  os      : {info.label} (--os {info.key})",
        f"  type    : {type_name}",
    ]


def run_work_disk(parser, info, arguments):
    if arguments.image is None:
        parser.error("--type WORKDSK needs a work disk name or path")
    for option, value in (
        ("--source-dir", arguments.source_dir),
        ("--output", arguments.output),
        ("--base-image", arguments.base_image),
        ("--binary-load-address", arguments.binary_load_address),
        ("--volume-name", arguments.volume_name),
    ):
        if value is not None:
            parser.error(f"{option} is only valid with --type SYSTEMDSK")
    engine = info.image()
    profile = arguments.profile or info.default_profile
    if profile not in info.profiles:
        parser.error(f"--profile {profile} is not valid for --os {info.key}: use {', '.join(info.profiles)}")
    image_path = osProfiles.resolve_image_path(arguments.sd_root, info.key, arguments.image)
    size = engine.PROFILES[profile]["image_size"]
    plan = plan_header(info, "WORKDSK")
    plan.append(f"  image   : {image_path}")
    plan.append(f"  profile : {profile} ({size} bytes)")
    if image_path.exists() and not arguments.force:
        plan.append("  effect  : refused: the image already exists (use --force to replace it)")
    elif arguments.force:
        plan.append("  effect  : write a new empty work disk, replacing any existing file")
    else:
        plan.append("  effect  : write a new empty work disk")
    confirm_plan(plan, arguments.yes)
    try:
        detail = engine.create_image(image_path, force=arguments.force, profile=profile)
    except (OSError, DiskImageError) as error:
        parser.error(str(error))
    extra = f", {detail}" if detail else ""
    print(f"Created empty {info.label} {profile} work disk: {image_path} ({size} bytes{extra})")


def run_system_disk(parser, info, arguments):
    if arguments.image is not None:
        parser.error("--type SYSTEMDSK does not take an image name; use --output")
    if arguments.force:
        parser.error("--force is only valid with --type WORKDSK")
    if info.key not in systemDisk.PROFILE_MAP:
        parser.error(f"--os {info.key} has no system disk profiles")
    if arguments.profile is None:
        parser.error(
            f"--profile is required with --type SYSTEMDSK; choose one of the {info.key} profiles: "
            f"{', '.join(systemDisk.PROFILE_MAP[info.key])}"
        )
    profile = arguments.profile
    output_path = systemDisk.system_disk_output_path(info.key, arguments.output)
    source_dir = systemDisk.source_directory(info.key, arguments.source_dir)
    plan = plan_header(info, "SYSTEMDSK")
    plan.append(f"  profile : {profile}")
    plan.append(f"  sources : {source_dir}")
    plan.append(f"  output  : {output_path}")
    plan.append("  effect  : build a bootable system disk (an existing output file is replaced)")
    confirm_plan(plan, arguments.yes)
    try:
        skipped_files = systemDisk.create_system_disk(
            info.key,
            profile,
            output_path,
            source_dir=arguments.source_dir,
            base_image=arguments.base_image,
            binary_load_address=arguments.binary_load_address,
            volume_name=arguments.volume_name or systemDisk.PRODOS_VOLUME_NAME,
        )
    except (DiskImageError, OSError) as error:
        parser.error(str(error))

    print(f"Created {output_path} ({output_path.stat().st_size} bytes)")
    if info.key == "apple2" and profile == "PRODOS":
        print(f"Added ProDOS files from {source_dir}:")
        for source_name, name, _, _, _ in systemDisk.read_prodos_source_files(
            source_dir, arguments.binary_load_address
        ):
            print(f"  {source_name} -> {name}")
        volume = systemDisk.diskImageApple2Prodos.normalize_volume_name(
            arguments.volume_name or systemDisk.PRODOS_VOLUME_NAME
        )
        print(f"Volume /{volume} boots ProDOS 8 with BASIC.SYSTEM.")
    elif info.key == "cpm86":
        print(f"CP/M-86 kernel kept outside A: at {systemDisk.PROJECT_ROOT / 'littlefs' / 'cpm86' / 'cpm.sys'}.")
        print(
            "CP/M-86 BIOS overlay kept outside A: at "
            f"{systemDisk.PROJECT_ROOT / 'littlefs' / 'cpm86' / 'retro86bios.h86'}."
        )
    systemDisk.print_firmware_notice(info.key, profile, output_path)
    if skipped_files:
        print(
            f"Disk too small: skipped non-executable files: {', '.join(skipped_files)}",
            file=sys.stderr,
        )


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--os", dest="os_name")
    pre.add_argument("--type", dest="disk_type")
    known, _ = pre.parse_known_args(argv)
    os_name = (known.os_name or "").lower() or None
    type_name = unique_disk_type(known.disk_type) if known.disk_type else None
    if known.disk_type and type_name is None:
        pre.error(
            f"--type '{known.disk_type}' does not match SYSTEMDSK or WORKDSK; "
            "unique abbreviations such as SYS or WORK are accepted"
        )
    parser = build_parser(os_name, type_name)
    arguments = parser.parse_args(argv)
    info = osProfiles.require_supported(parser, arguments.os_name)
    if type_name == "SYSTEMDSK":
        run_system_disk(parser, info, arguments)
    else:
        run_work_disk(parser, info, arguments)


if __name__ == "__main__":
    main()
