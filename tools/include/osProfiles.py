"""Shared operating-system registry and command-line helpers for the host tools.

To add an OS: add diskImage<Os>.py and buildDiskImage<Os>.py under tools/<os>/,
then an OsInfo entry (with their module names) to OS_REGISTRY. Tools refuse OSes whose
`supported` flag is False, so planned systems are already visible in --help.
"""

import argparse
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
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
    build_only: bool = False

    def _import_os_module(self, module_name):
        os_directory = PROJECT_ROOT / "tools" / self.key
        if str(os_directory) not in sys.path:
            sys.path.insert(0, str(os_directory))
        return importlib.import_module(module_name)

    def image(self):
        """The OS image module (diskImageCpm80, ...) exposing PROFILES and create/add/list."""
        return self._import_os_module(self.image_module)

    def builder(self):
        """The OS build module (buildDiskImageCpm80, ...) exposing add_arguments() and build()."""
        return self._import_os_module(self.build_module)

    @property
    def buildable(self):
        """True when buildDiskImage.py can build images for this OS (also when diskImage.py cannot yet)."""
        return self.supported or self.build_only

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
            "  BIG     514 tracks x 128 x 128 B (8421376 bytes), 16 KiB blocks, 512 directory entries,\n"
            "          8 MiB of data (DSM 511, 16-bit block pointers)\n"
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
            "  BIG     2049 tracks x 8 x 512 B (8392704 bytes), 16 KiB blocks, 512 directory\n"
            "          entries; drives B: through F: (RETRO86_DATA_BIG_V1)\n"
            "Files need CP/M 8.3 names (upper-cased automatically); user area 0.\n"
            "The drive configuration is littlefs/cpm86/drives.cfg; edit its image paths\n"
            "and RO/RW access modes. buildDiskImage.py needs --source-dir with CPM.SYS\n"
            "and the .CMD utilities."
        ),
    ),
    "apple2": OsInfo(
        "apple2",
        "Apple II",
        True,
        image_module="diskImageApple2",
        build_module="buildDiskImageApple2",
        default_system_image="data800.po",
        build_only=True,
        details=(
            "diskImage.py creates and edits ProDOS 8 data volumes (.po): 800K (1600 blocks, default), 640K or 140K.\n"
            "The volume name comes from the image file name (upper-cased; 'DATA' when it is not a valid name).\n"
            "ProDOS names use A-Z, 0-9 and '.' and start with a letter (1..15 characters).\n"
            "The file type comes from the suffix: .BAS, .TXT, .BIN, .SYS; a .BIN file needs a load address,\n"
            "which diskImage.py add does not supply (use buildDiskImage.py --binary-load-address).\n"
            "buildDiskImage.py builds the same volumes and can add ProDOS boot files (--bootable, --boot-from).\n"
            "The firmware's system.dsk is made by createSystemDsk.py."
        ),
    ),
    "swtpc": OsInfo("swtpc", "SWTPC 6800", False),
    "ucsd": OsInfo(
        "ucsd",
        "UCSD p-System",
        True,
        image_module="diskImageUcsd",
        build_module="buildDiskImageUcsd",
        default_system_image="work.po",
        build_only=True,
        details=(
            "diskImage.py creates and edits blank, formatted Apple Pascal work volumes (.po): 640K (1280 blocks,\n"
            "default) or 140K (280 blocks). The volume name comes from the image file name (upper-cased, 1..7\n"
            "characters; 'WORK' when it is not a valid name).\n"
            "Pascal names are upper-cased without the characters ' $=?,[#:/' (15 characters max).\n"
            "The file kind comes from the suffix: .TEXT (3), .CODE (2), .DATA (5); anything else is data (5).\n"
            "buildDiskImage.py builds the same blank volumes. The Pascal boot disk is not built by these tools."
        ),
    ),
}


def os_overview():
    lines = ["Operating systems (--os):"]
    for info in OS_REGISTRY.values():
        if info.supported:
            state = "supported"
        elif info.build_only:
            state = "buildDiskImage.py only"
        else:
            state = "planned (not implemented yet)"
        lines.append(f"  {info.key:<8} {info.label:<14} {state}")
    return "\n".join(lines)


def os_epilog(os_name, for_build=False):
    """Epilog for -h: OS-specific details when --os is known, otherwise the OS overview."""
    info = OS_REGISTRY.get(os_name) if os_name else None
    if info is None:
        return os_overview() + "\n\nAdd --os <name> to -h for OS-specific help."
    text = f"{info.label} (--os {info.key})\n"
    available = info.buildable if for_build else info.supported
    text += info.details if available else "Planned: not implemented yet."
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


def require_buildable(parser, os_name):
    info = OS_REGISTRY.get(os_name)
    if info is None:
        parser.error(f"unknown --os {os_name!r}; choose from: {', '.join(OS_REGISTRY)}")
    if not info.buildable:
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


KNOWN_IMAGE_EXTENSIONS = (".dsk", ".po", ".do", ".img", ".hdv", ".2mg", ".nib", ".woz", ".raw", ".bin")


def image_extension(os_name):
    """The disk image extension of an OS, taken from its default image name (.dsk or .po)."""
    return Path(OS_REGISTRY[os_name].default_system_image).suffix


def resolve_image_path(sd_root, os_name, image):
    """A bare image name goes to <sd-root>/retro/images/<os>/; any path is used as given.

    The OS's own extension is always enforced: a different known disk extension is replaced
    (work2.po -> work2.dsk) and a missing one is appended (work86-2 -> work86-2.dsk)."""
    image = Path(image)
    extension = image_extension(os_name)
    if extension:
        if image.suffix.lower() in KNOWN_IMAGE_EXTENSIONS:
            image = image.with_suffix("")
        image = image.with_name(image.name + extension)
    if image.is_absolute() or image.parent != Path("."):
        return image
    return images_dir(sd_root, os_name) / image
