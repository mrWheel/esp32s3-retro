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
  diskManager.py --type SYSTEMDSK          (asks for the OS and the profile)

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
refused unless --yes is given.

In a terminal every option missing on the command line is asked for: disk
type, OS, profile (SYSTEMDSK offers the profile of the system disk mounted in
littlefs/<os>/drives.cfg as the default; WORKDSK always asks for the
geometry) and the remaining values of the chosen type (work disk name, system
disk output and source directory).
ENTER accepts an offered default; invalid answers are asked again. Without a
terminal -- or when --yes is given -- nothing is asked and missing required
options are refused as before, so configure-time callers such as the top-level
CMakeLists never block on input()."""


def unique_disk_type(type_name):
    """Return the DISK_TYPES entry a value or unique abbreviation stands for (None if ambiguous)."""
    matches = [name for name in DISK_TYPES if name.startswith(type_name.upper())]
    if len(matches) != 1:
        return None
    return matches[0]


def unique_option(value, options):
    """Return the single option a value uniquely abbreviates (None when unknown or ambiguous)."""
    matches = [name for name in options if name.upper().startswith(value.upper())]
    return matches[0] if len(matches) == 1 else None


def current_system_profile(os_name):
    """The diskManager profile of the system disk mounted in littlefs/<os>/drives.cfg (None when unknown)."""
    profile_map = systemDisk.PROFILE_MAP.get(os_name)
    systemName = "system.po" if os_name == "ucsd" else "system.dsk"
    configFile = systemDisk.PROJECT_ROOT / "littlefs" / os_name / "drives.cfg"
    if not profile_map or not configFile.is_file():
        return None
    sizeToProfile = {size.upper(): profile for profile, size in profile_map.items()}
    for line in configFile.read_text(encoding="utf-8", errors="replace").splitlines():
        text = line.strip()
        if not text or text.startswith("#") or "=" not in text:
            continue
        fields = [field.strip() for field in text.split("=", 1)[1].split(",")]
        if len(fields) < 3:
            continue
        imagePath = fields[0].strip().strip("/").lower()
        if not imagePath.endswith(f"littlefs/{os_name}/{systemName}".lower()):
            continue
        sizeName = fields[2].upper()
        if sizeName.startswith("APPLE2_"):
            sizeName = sizeName[len("APPLE2_"):]
        return sizeToProfile.get(sizeName)
    return None


def _ask_line(label):
    """Read one answer line; Ctrl-C or end of input aborts the run."""
    try:
        answer = input(f"{label}: ")
    except (EOFError, KeyboardInterrupt):
        print()
        raise SystemExit(130)
    return answer.strip()


def prompt_choice(label, options, default=None, lookup=None):
    """Ask for one of the options by number, exact name or unique abbreviation; ENTER takes the default."""
    choices = ", ".join(f"{number}) {name}" for number, name in enumerate(options, start=1))
    suffix = f" [{default}]" if default is not None else ""
    while True:
        answer = _ask_line(f"{label}: {choices}{suffix}")
        if not answer:
            if default is not None:
                return default
        elif answer.isdigit() and 1 <= int(answer) <= len(options):
            return options[int(answer) - 1]
        else:
            for name in options:
                if answer.upper() == name.upper():
                    return name
            if lookup is not None:
                match = lookup(answer)
                if match is not None:
                    return match
        print(f"Choose one of: {', '.join(options)}")


def prompt_yes_no(label):
    """Ask a yes/no question; ENTER takes the default 'no'."""
    return _ask_line(f"{label} (y/N)").lower() in ("y", "yes")


def prompt_os(type_name):
    """Ask for the target OS; only OSes usable for this disk type are accepted."""
    options = tuple(osProfiles.OS_REGISTRY)
    while True:
        os_name = prompt_choice("Which operating system", options, lookup=lambda value: unique_option(value, options))
        info = osProfiles.OS_REGISTRY[os_name]
        available = info.buildable if type_name == "SYSTEMDSK" else info.supported
        if available:
            return os_name
        print(f"{info.label} cannot be used to create a {type_name}.")


def prompt_profile(type_name, info):
    """Ask for the disk profile; SYSTEMDSK offers the mounted system disk's profile as the default."""
    if type_name == "SYSTEMDSK":
        options = tuple(systemDisk.PROFILE_MAP[info.key])
        default = current_system_profile(info.key)
        if default not in options:
            default = None
    else:
        #-- WORKDSK has no default here: the geometry is always asked (the CLI default is unchanged).
        options = tuple(info.profiles)
        default = None
    return prompt_choice("Which profile", options, default=default)


def prompt_image_name():
    """Ask for the WORKDSK image name or path (a bare name goes to the images directory)."""
    while True:
        answer = _ask_line("Work disk name")
        if answer:
            return answer


def prompt_path(label, default_path):
    """Ask for a path; ENTER keeps the default (the default is shown relative to the project folder)."""
    answer = _ask_line(f"{label} [{systemDisk.display_path(default_path)}]")
    return Path(answer) if answer else default_path


def interactive_mode(argv):
    """True when missing options may be asked for: an attached terminal and no -h/--help request.

    --yes (or -y) forces the scripted path: prompts for missing options are never shown and
    every missing option keeps the default the prompt would have offered. Without it a
    configure-time caller such as the CMakeLists diskManager job would block on input()."""
    return sys.stdin.isatty() and not any(token in ("-h", "--help", "--yes", "-y") for token in argv)


def prompt_missing_options(type_name, info, arguments):
    """Ask for every option that was not given on the command line (terminal runs only)."""
    if arguments.profile is None:
        arguments.profile = prompt_profile(type_name, info)
    if type_name == "WORKDSK":
        if arguments.image is None:
            arguments.image = Path(prompt_image_name())
        if not arguments.force:
            image_path = osProfiles.resolve_image_path(arguments.sd_root, info.key, arguments.image)
            if image_path.exists() and prompt_yes_no(f"{systemDisk.display_path(image_path)} already exists, replace it"):
                arguments.force = True
        return
    if arguments.output is None:
        arguments.output = prompt_path("Output path", systemDisk.system_disk_output_path(info.key, None))
    if arguments.source_dir is None:
        arguments.source_dir = prompt_path("Source directory", systemDisk.source_directory(info.key, None))
    if info.key == "apple2" and arguments.volume_name is None:
        answer = _ask_line(f"Volume name [{systemDisk.PRODOS_VOLUME_NAME}]")
        if answer:
            arguments.volume_name = answer


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
        help="do not ask for confirmation before the planned action is executed; also never "
        "prompts for missing options (scripted use: defaults and the normal error paths apply)",
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
    plan.append(f"  image   : {systemDisk.display_path(image_path)}")
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
    print(f"Created empty {info.label} {profile} work disk: {systemDisk.display_path(image_path)} ({size} bytes{extra})")


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
    plan.append(f"  sources : {systemDisk.display_path(source_dir)}")
    plan.append(f"  output  : {systemDisk.display_path(output_path)}")
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

    print(f"Created {systemDisk.display_path(output_path)} ({output_path.stat().st_size} bytes)")
    if info.key == "apple2" and profile == "PRODOS":
        print(f"Added ProDOS files from {systemDisk.display_path(source_dir)}:")
        for source_name, name, _, _, _ in systemDisk.read_prodos_source_files(
            source_dir, arguments.binary_load_address
        ):
            print(f"  {source_name} -> {name}")
        volume = systemDisk.diskImageApple2Prodos.normalize_volume_name(
            arguments.volume_name or systemDisk.PRODOS_VOLUME_NAME
        )
        print(f"Volume /{volume} boots ProDOS 8 with BASIC.SYSTEM.")
    elif info.key == "cpm86":
        print(
            "CP/M-86 kernel kept outside A: at "
            f"{systemDisk.display_path(systemDisk.PROJECT_ROOT / 'littlefs' / 'cpm86' / 'cpm.sys')}."
        )
        print(
            "CP/M-86 BIOS overlay kept outside A: at "
            f"{systemDisk.display_path(systemDisk.PROJECT_ROOT / 'littlefs' / 'cpm86' / 'retro86bios.h86')}."
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
    ask = interactive_mode(argv)
    if ask and type_name is None:
        type_name = prompt_choice("Create what", DISK_TYPES, lookup=unique_disk_type)
        argv = argv + ["--type", type_name]
    if ask and os_name is None:
        os_name = prompt_os(type_name)
        argv = argv + ["--os", os_name]
    parser = build_parser(os_name, type_name)
    arguments = parser.parse_args(argv)
    info = osProfiles.require_supported(parser, arguments.os_name)
    if ask:
        prompt_missing_options(type_name, info, arguments)
    if type_name == "SYSTEMDSK":
        run_system_disk(parser, info, arguments)
    else:
        run_work_disk(parser, info, arguments)


if __name__ == "__main__":
    main()
