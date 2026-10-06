"""Shared operating-system registry and command-line helpers for the host tools.

To add an OS: add diskImage<Os>.py and buildDiskImage<Os>.py, then an OsInfo entry
(with their module names) to OS_REGISTRY. Tools refuse OSes whose
`supported` flag is False, so planned systems are already visible in --help.
"""

import argparse
import importlib
from dataclasses import dataclass
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SD_ROOT = PROJECT_ROOT / "sdcard"


@dataclass(frozen=True)
class OsInfo:
    key: str
    label: str
    supported: bool
    image_module: str = ""
    build_module: str = ""
    default_system_image: str = "system.dsk"
    details: str = ""

    def image(self):
        """The OS image module (diskImageCpm80, ...) exposing PROFILES and create/add/list."""
        return importlib.import_module(self.image_module)

    def builder(self):
        """The OS build module (buildDiskImageCpm80, ...) exposing add_arguments() and build()."""
        return importlib.import_module(self.build_module)

    @property
    def profiles(self):
        return tuple(self.image().PROFILES)

    @property
    def default_profile(self):
        return self.image().DEFAULT_PROFILE


OS_REGISTRY = {
    "cpm80": OsInfo(
        "cpm80",
        "CP/M-80",
        True,
        image_module="diskImageCpm80",
        build_module="buildDiskImageCpm80",
        details=(
            "Profiles:\n"
            "  SYSTEM  77 tracks x 26 x 128 B (256256 bytes), 1 KiB blocks, 64 directory entries\n"
            "  LARGE   77 tracks x 52 x 128 B (512512 bytes), 2 KiB blocks, 128 directory entries\n"
            "Files need CP/M 8.3 names (upper-cased automatically); user area 0."
        ),
    ),
    "cpm86": OsInfo(
        "cpm86",
        "CP/M-86",
        True,
        image_module="diskImageCpm86",
        build_module="buildDiskImageCpm86",
        details=(
            "Profiles:\n"
            "  CPM86   40 tracks x 8 x 512 B (163840 bytes), track 0 reserved for boot,\n"
            "          1 KiB blocks, 64 directory entries; drives A: through F: (RETRO86_DATA_V1)\n"
            "  LARGE   129 tracks x 8 x 512 B (528384 bytes), 2 KiB blocks, 128 directory\n"
            "          entries; drives B: through F: (RETRO86_DATA_LARGE_V1)\n"
            "Files need CP/M 8.3 names (upper-cased automatically); user area 0.\n"
            "prepareSd.py creates /retro/images/cpm86/drives.cfg; edit its image paths\n"
            "and RO/RW access modes. buildDiskImage.py needs --source-dir with CPM.SYS\n"
            "and the .CMD utilities."
        ),
    ),
    "apple2": OsInfo("apple2", "Apple II", False),
    "swtpc": OsInfo("swtpc", "SWTPC 6800", False),
    "ucsd": OsInfo("ucsd", "UCSD p-System", False),
}


def os_overview():
    lines = ["Operating systems (--os):"]
    for info in OS_REGISTRY.values():
        state = "supported" if info.supported else "planned (not implemented yet)"
        lines.append(f"  {info.key:<8} {info.label:<14} {state}")
    return "\n".join(lines)


def os_epilog(os_name):
    """Epilog for -h: OS-specific details when --os is known, otherwise the OS overview."""
    info = OS_REGISTRY.get(os_name) if os_name else None
    if info is None:
        return os_overview() + "\n\nAdd --os <name> to -h for OS-specific help."
    text = f"{info.label} (--os {info.key})\n"
    text += info.details if info.supported else "Planned: not implemented yet."
    return text


def preparse_os(argv, default_os=None):
    """Find --os anywhere on the command line (needed before -h is processed)."""
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--os", dest="os_name", default=default_os)
    known, _ = pre.parse_known_args(argv)
    return (known.os_name or "").lower() or None


def strip_os_option(argv):
    """Remove --os before the subcommand so a single definition on the subcommand applies."""
    result = []
    skip = False
    for token in argv:
        if skip:
            skip = False
        elif token == "--os":
            skip = True
        elif not token.startswith("--os="):
            result.append(token)
    return result


def add_os_argument(parser, default_os=None):
    parser.add_argument(
        "--os",
        dest="os_name",
        type=str.lower,
        choices=tuple(OS_REGISTRY),
        default=default_os,
        required=default_os is None,
        help="target operating system: " + ", ".join(OS_REGISTRY),
    )


def require_supported(parser, os_name):
    info = OS_REGISTRY.get(os_name)
    if info is None:
        parser.error(f"unknown --os {os_name!r}; choose from: {', '.join(OS_REGISTRY)}")
    if not info.supported:
        parser.error(f"--os {os_name} ({info.label}) is not implemented yet")
    return info


def add_sd_root_argument(parser):
    parser.add_argument(
        "--sd-root",
        type=Path,
        default=DEFAULT_SD_ROOT,
        help="SD-card layout root that contains retro/ (default: %(default)s)",
    )


def images_dir(sd_root, os_name):
    return Path(sd_root) / "retro" / "images" / os_name


def resolve_image_path(sd_root, os_name, image):
    """A bare image name goes to <sd-root>/retro/images/<os>/; any path is used as given."""
    image = Path(image)
    if image.is_absolute() or image.parent != Path("."):
        return image
    return images_dir(sd_root, os_name) / image
