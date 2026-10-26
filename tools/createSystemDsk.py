#!/usr/bin/env python3
"""Create CP/M system disks, or Apple II ProDOS system disks from a bootable base image."""

import argparse
import re
import sys
import tempfile
from pathlib import Path

INCLUDE_DIR = Path(__file__).resolve().parent / "include"
if str(INCLUDE_DIR) not in sys.path:
    sys.path.insert(0, str(INCLUDE_DIR))

TOOLS_DIR = Path(__file__).resolve().parent
for os_directory in ("apple2", "cpm80", "cpm86"):
    module_directory = TOOLS_DIR / os_directory
    if str(module_directory) not in sys.path:
        sys.path.insert(0, str(module_directory))

import buildDiskImageCpm80
import diskImageApple2Prodos
import diskImageCpm80
import diskImageCpm86
from diskImageCommon import DiskImageError

PROJECT_ROOT = Path(__file__).resolve().parents[1]
APPLE2_PRODOS_BASE_IMAGE = Path("bootDisks") / "apple2" / "prodosEmpty.po"
PRODOS_VOLUME_NAME = "SYSTEM"
PRODOS_ORDER_SUFFIXES = {".PO", ".HDV"}
#-- An Apple II source file named NAME#ADDR.BIN (ADDR is 1-4 hex digits) is stored as NAME
#-- (without the .BIN extension) with load address ADDR, the same convention as the web page.
APPLE_LOAD_ADDRESS_RE = re.compile(r"^(.*)#([0-9A-Fa-f]{1,4})\.BIN$", re.IGNORECASE)
UCSD_BASE_IMAGE = Path("assets") / "ucsd" / "ucsdDiskImages" / "Apple Pascal 1.3 3.5.po"
#-- The 128K system files are placed under the SYSTEM.* names that the boot loader looks for.
UCSD_NAME_MAP = {
    "128K.APPLE": "SYSTEM.APPLE",
    "128K.PASCAL": "SYSTEM.PASCAL",
}
EXECUTABLE_EXTENSIONS = {
    "cpm80": {".COM"},
    "cpm86": {".CMD", ".SYS"},
}
PROFILE_MAP = {
    "cpm80": {"SMALL": "SYSTEM", "LARGE": "LARGE"},
    "cpm86": {"SMALL": "CPM86", "LARGE": "LARGE"},
    "apple2": {
        "PRODOS": "140K",
        "PRODOS_640K": "640K",
        "PRODOS_800K": "800K",
    },
    "ucsd": {"PASCAL_800K": "800K"},
}
IMAGE_MODULES = {
    "cpm80": diskImageCpm80,
    "cpm86": diskImageCpm86,
}


class _SystemDiskArgumentParser(argparse.ArgumentParser):
    system_os = None

    def error(self, message):
        if self.system_os and message == "the following arguments are required: --profile":
            profiles = ", ".join(PROFILE_MAP[self.system_os])
            message = f"--profile is required; choose one of the {self.system_os} profiles: {profiles}"
        super().error(message)


def _without_os_argument(argv):
    arguments = []
    skip_value = False
    for argument in argv:
        if skip_value:
            skip_value = False
            continue
        if argument == "--os":
            skip_value = True
        elif not argument.startswith("--os="):
            arguments.append(argument)
    return arguments


def _source_directory(os_name, override):
    if override is not None:
        return Path(override)
    if os_name == "apple2":
        candidates = (
            PROJECT_ROOT / "bootDisks" / os_name / "systemDsk",
            PROJECT_ROOT / "bootDisks" / os_name / "systemDisk",
        )
        return next((path for path in candidates if path.is_dir()), candidates[0])
    candidates = (
        PROJECT_ROOT / "bootDisks" / os_name / "systemDisk",
        PROJECT_ROOT / "bootDisks" / os_name / "systemDsk",
    )
    return next((path for path in candidates if path.is_dir()), candidates[0])


def _read_source_files(source_dir):
    files = {}
    if not source_dir.is_dir():
        raise DiskImageError(f"System disk source directory does not exist: {source_dir}")

    for path in sorted(source_dir.iterdir(), key=lambda item: item.name.upper()):
        if path.name == ".DS_Store":
            continue
        if path.is_symlink():
            raise DiskImageError(f"Refusing symbolic link in system disk source: {path}")
        if path.is_dir():
            raise DiskImageError(f"Nested directories are not supported in system disk source: {path}")
        if not path.is_file():
            raise DiskImageError(f"Not a regular system disk source file: {path}")
        try:
            filename = diskImageCpm80.parse_filename(path.name)
        except DiskImageError as error:
            raise DiskImageError(f"Invalid CP/M filename in system disk source: {path.name}") from error
        if filename in files:
            raise DiskImageError(f"Duplicate CP/M filename in system disk source: {filename}")
        files[filename] = path.read_bytes()

    return files


def _apple_load_address(filename):
    """Return (effective filename, load address) for a NAME#ADDR.BIN source file, else None."""
    match = APPLE_LOAD_ADDRESS_RE.match(filename)
    if match is None or match.group(1) == "":
        return None
    return f"{match.group(1)}.BIN", int(match.group(2), 16)


def _read_prodos_source_files(source_dir, binary_load_address):
    """Return the files from the system-directory."""
    if not source_dir.is_dir():
        raise DiskImageError(f"System disk source directory does not exist: {source_dir}")
    files = []
    names = set()
    source_paths = sorted(source_dir.iterdir(), key=lambda item: item.name.upper())
    for path in source_paths:
        if path.name in (".DS_Store", "README.md") or path.suffix.upper() == ".ADDR":
            continue
        if path.is_symlink() or path.is_dir() or not path.is_file():
            raise DiskImageError(f"Only regular files are supported in the system disk source: {path}")
        derived = _apple_load_address(path.name)
        if derived is not None:
            #-- NAME#ADDR.BIN: the name carries the load address and the file is stored as NAME
            effectiveName, loadAddress = derived
        else:
            effectiveName = path.name
            loadAddress = binary_load_address
            addressFile = path.with_suffix(".ADDR")
            if loadAddress is None and path.suffix.upper() == ".BIN" and addressFile.is_file():
                #-- NAME.ADDR next to NAME.BIN holds that file's load address (hex, e.g. 6000)
                loadAddress = int(addressFile.read_text().strip(), 16)
        name, file_type, aux_type, payload = diskImageApple2Prodos.prepare_source_file(
            effectiveName, path.read_bytes(), loadAddress
        )
        if name in names:
            raise DiskImageError(f"Duplicate ProDOS filename in system disk source: {name}")
        names.add(name)
        files.append((path.name, name, file_type, aux_type, payload))
    return files


def _create_prodos_system_disk(output_path, source_dir, project_root, base_image, binary_load_address,
                               volume_name, volume_profile):
    base_image = Path(project_root / APPLE2_PRODOS_BASE_IMAGE if base_image is None else base_image)
    if base_image.is_symlink() or not base_image.is_file():
        raise DiskImageError(f"Apple II base image does not exist as a regular file: {base_image}")
    if base_image.resolve() == output_path.resolve():
        raise DiskImageError("Apple II output path must not replace the base image")
    boot_image = diskImageApple2Prodos.load_volume(base_image)
    image = bytearray(diskImageApple2Prodos.build_blank_volume(volume_name, volume_profile))
    diskImageApple2Prodos.copy_boot_files(image, boot_image)
    diskImageApple2Prodos.set_volume_name(image, volume_name)
    names = []
    for source_name, name, file_type, aux_type, payload in _read_prodos_source_files(
        source_dir, binary_load_address
    ):
        #-- A source file replaces a destination file with the same name
        if any(existing["name"] == name for existing in diskImageApple2Prodos.list_files(image)):
            diskImageApple2Prodos.remove_file(image, name)
        diskImageApple2Prodos.add_file(image, name, payload, file_type, aux_type)
        names.append((source_name, name))
    data = bytes(image)
    if volume_profile == "140K" and output_path.suffix.upper() not in PRODOS_ORDER_SUFFIXES:
        #-- A .dsk file holds the 140K volume in DOS sector order, like every other ProDOS .dsk image.
        data = diskImageApple2Prodos.dos_order_from_prodos_order(data)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    return names


def _create_ucsd_system_disk(output_path, source_dir, project_root):
    """Copy the clean Apple Pascal 1.3 boot volume and add the files from the source directory."""
    import shutil

    import extractApple2Files

    base_image = project_root / UCSD_BASE_IMAGE
    if not base_image.is_file():
        raise DiskImageError(f"UCSD base image does not exist: {base_image}")
    if not source_dir.is_dir():
        raise DiskImageError(f"System disk source directory does not exist: {source_dir}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(base_image, output_path)
    sys.path.insert(0, str(TOOLS_DIR / "ucsd"))
    try:
        image = extractApple2Files.DiskImage(output_path)
        volume = extractApple2Files.openVolume(image)
        for path in sorted(source_dir.iterdir(), key=lambda item: item.name.upper()):
            if path.name in (".DS_Store", "README.md"):
                continue
            if path.is_symlink() or not path.is_file():
                raise DiskImageError(f"Only regular files are supported in the system disk source: {path}")
            destinationName = UCSD_NAME_MAP.get(path.name.upper(), path.name)
            #-- A source file replaces a destination file with the same name
            for entry in volume.listFiles():
                if extractApple2Files.sameName(entry.path, destinationName):
                    volume.removeFile(entry)
            extractApple2Files.addHostFileToDestination(volume, path, destinationName)
        image.save()
    except extractApple2Files.ToolError as error:
        raise DiskImageError(str(error)) from error


def _validate_cpm86_runtime_resources(project_root):
    resource_directory = project_root / "littlefs" / "cpm86"
    system_file = resource_directory / "cpm.sys"
    bios_overlay = resource_directory / "retro86bios.h86"
    if not system_file.is_file():
        raise DiskImageError(f"CP/M-86 system file is required outside A: at {system_file}")
    if system_file.stat().st_size != 10240:
        raise DiskImageError(f"CP/M-86 system file must be 10240 bytes: {system_file}")
    if not bios_overlay.is_file() or not 0 < bios_overlay.stat().st_size <= 4096:
        raise DiskImageError(f"CP/M-86 BIOS overlay is missing or invalid: {bios_overlay}")
    return system_file, bios_overlay


def _is_executable(os_name, filename):
    return Path(filename).suffix.upper() in EXECUTABLE_EXTENSIONS[os_name]


def _ordered_files(os_name, files):
    return sorted(
        files.items(),
        key=lambda item: (not _is_executable(os_name, item[0]), item[0]),
    )


def _create_cpm80_base(image_path, profile_name, project_root):
    cpm_directory = project_root / "components" / "cpm80Core" / "os"
    ccp = (cpm_directory / "ccp-64k.bin").read_bytes()
    bdos = (cpm_directory / "bdos-64k.bin").read_bytes()
    if len(ccp) != buildDiskImageCpm80.CCP_SIZE:
        raise DiskImageError(
            f"CP/M-80 CCP must be {buildDiskImageCpm80.CCP_SIZE} bytes, found {len(ccp)}"
        )
    if len(bdos) != buildDiskImageCpm80.BDOS_SIZE:
        raise DiskImageError(
            f"CP/M-80 BDOS must be {buildDiskImageCpm80.BDOS_SIZE} bytes, found {len(bdos)}"
        )

    profile = diskImageCpm80.PROFILES[profile_name]
    image = bytearray([diskImageCpm80.EMPTY]) * profile["image_size"]
    image[: buildDiskImageCpm80.CCP_SIZE] = ccp
    image[
        buildDiskImageCpm80.CCP_SIZE : buildDiskImageCpm80.CCP_SIZE
        + buildDiskImageCpm80.BDOS_SIZE
    ] = bdos
    header_offset = buildDiskImageCpm80.CCP_SIZE + buildDiskImageCpm80.BDOS_SIZE
    image[header_offset : header_offset + len(buildDiskImageCpm80.HEADER)] = buildDiskImageCpm80.HEADER
    diskImageCpm80.write_image(image_path, image, force=True)


def _add_files(os_name, image_path, files):
    image_module = IMAGE_MODULES[os_name]
    executables = []
    other_files = []
    for filename, data in _ordered_files(os_name, files):
        (executables if _is_executable(os_name, filename) else other_files).append((filename, data))

    for filename, data in executables:
        try:
            image_module.add_files(image_path, [(filename, data)])
        except DiskImageError as error:
            if "disk is full" in str(error).lower() or "directory is full" in str(error).lower():
                raise DiskImageError(f"Disk too small: required executable {filename} does not fit") from error
            raise

    skipped_files = []
    for filename, data in other_files:
        try:
            image_module.add_files(image_path, [(filename, data)])
        except DiskImageError as error:
            if "disk is full" not in str(error).lower() and "directory is full" not in str(error).lower():
                raise
            skipped_files.append(filename)
    return skipped_files


def create_system_disk(
    os_name,
    profile,
    output_path,
    source_dir=None,
    project_root=PROJECT_ROOT,
    base_image=None,
    binary_load_address=None,
    volume_name=PRODOS_VOLUME_NAME,
):
    os_name = os_name.lower()
    profile = profile.upper()
    if os_name not in IMAGE_MODULES and os_name not in ("apple2", "ucsd"):
        raise DiskImageError(f"Operating system is not implemented for system disks: {os_name}")
    if profile not in PROFILE_MAP[os_name]:
        raise DiskImageError(f"Unsupported {os_name} system disk profile: {profile}")
    if os_name not in ("apple2",) and (base_image is not None or binary_load_address is not None):
        raise DiskImageError("--base-image and --binary-load-address are Apple II-only options")

    output_path = Path(output_path)
    source_dir = _source_directory(os_name, source_dir)
    if os_name == "ucsd":
        _create_ucsd_system_disk(output_path, source_dir, project_root)
        return []
    if os_name == "apple2":
        volume_profile = PROFILE_MAP[os_name][profile]
        _create_prodos_system_disk(
            output_path,
            source_dir,
            project_root,
            base_image,
            binary_load_address,
            volume_name,
            volume_profile,
        )
        return []

    source_files = _read_source_files(source_dir)

    if os_name == "cpm80":
        profile_name = PROFILE_MAP[os_name][profile]
        create_base = lambda staging_path: _create_cpm80_base(staging_path, profile_name, project_root)
        system_files = source_files
    else:
        system_file_path, _ = _validate_cpm86_runtime_resources(project_root)
        if "CPM.SYS" in source_files:
            raise DiskImageError(
                f"CPM.SYS is loaded from LittleFS at {system_file_path}; do not put it in the source directory"
            )
        system_files = source_files
        profile_name = PROFILE_MAP[os_name][profile]
        create_base = lambda staging_path: diskImageCpm86.create_image(
            staging_path, force=True, profile=profile_name
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="create-system-dsk-") as temporary:
        staging_path = Path(temporary) / "system.dsk"
        create_base(staging_path)
        skipped_files = _add_files(os_name, staging_path, system_files)
        image_module = IMAGE_MODULES[os_name]
        image_module.write_image(output_path, staging_path.read_bytes(), force=True)

    return skipped_files


def print_firmware_notice(os_name, profile, output_path):
    """Tell the user that system.dsk lives in the firmware image, so a new one needs a rebuild and a flash."""
    print()
    print("*** IMPORTANT: system.dsk is part of the firmware (LittleFS partition). ***")
    print("To use this new system.dsk the emulator must be rebuilt AND flashed again.")
    if os_name == "apple2":
        print(f"The build recreates littlefs/apple2/system.dsk with the {profile} profile.")
        print("Run: idf.py build, then flash the firmware.")
    else:
        print("Run: idf.py build, then flash the firmware.")
    systemName = "system.po" if os_name == "ucsd" else "system.dsk"
    if output_path.resolve() != (PROJECT_ROOT / "littlefs" / os_name / systemName).resolve():
        print(f"Note: {output_path} is not littlefs/{os_name}/{systemName}, so the firmware does not use it as it is.")


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--os")
    known, _ = pre.parse_known_args(argv)
    os_name = (known.os or "").lower()
    selected_os = os_name in PROFILE_MAP
    profile_choices = tuple(PROFILE_MAP[os_name] if selected_os else PROFILE_MAP["cpm80"])
    parser = _SystemDiskArgumentParser(
        description=(
            f"Create a {os_name} system disk."
            if selected_os
            else "Create a CP/M system disk or a bootable Apple II ProDOS 8 system disk."
        )
    )
    parser.system_os = os_name if selected_os else None
    if selected_os:
        argv = _without_os_argument(argv)
        parser.set_defaults(os=os_name)
    else:
        parser.add_argument("--os", required=True, choices=("cpm80", "cpm86", "apple2", "ucsd"))
    parser.add_argument("--profile", required=True, choices=profile_choices)
    parser.add_argument("--source-dir", type=Path, help="directory containing files to add to the system disk")
    parser.add_argument("--output", type=Path, help="output path (default: littlefs/<os>/system.dsk, system.po for ucsd)")
    parser.add_argument(
        "--base-image",
        type=Path,
        help=f"bootable ProDOS volume to copy before adding Apple II files (default: {APPLE2_PRODOS_BASE_IMAGE})",
    )
    parser.add_argument(
        "--binary-load-address",
        type=lambda value: int(value, 0),
        help="load address for all raw .BIN files (decimal or 0x-prefixed hexadecimal)",
    )
    parser.add_argument(
        "--volume-name",
        default=PRODOS_VOLUME_NAME,
        help=f"ProDOS volume name of the PRODOS profile (default: {PRODOS_VOLUME_NAME})",
    )
    arguments = parser.parse_args(argv)

    systemName = "system.po" if arguments.os == "ucsd" else "system.dsk"
    output_path = arguments.output or PROJECT_ROOT / "littlefs" / arguments.os / systemName
    try:
        skipped_files = create_system_disk(
            arguments.os,
            arguments.profile,
            output_path,
            source_dir=arguments.source_dir,
            base_image=arguments.base_image,
            binary_load_address=arguments.binary_load_address,
            volume_name=arguments.volume_name,
        )
    except (DiskImageError, OSError) as error:
        parser.error(str(error))

    print(f"Created {output_path} ({output_path.stat().st_size} bytes)")
    if arguments.os == "apple2" and arguments.profile == "PRODOS":
        source_dir = arguments.source_dir or _source_directory("apple2", None)
        print(f"Added ProDOS files from {source_dir}:")
        for source_name, name, _, _, _ in _read_prodos_source_files(
            source_dir, arguments.binary_load_address
        ):
            print(f"  {source_name} -> {name}")
        print(f"Volume /{diskImageApple2Prodos.normalize_volume_name(arguments.volume_name)} boots ProDOS 8 with BASIC.SYSTEM.")
    elif arguments.os == "cpm86":
        print(f"CP/M-86 kernel kept outside A: at {PROJECT_ROOT / 'littlefs' / 'cpm86' / 'cpm.sys'}.")
        print(
            "CP/M-86 BIOS overlay kept outside A: at "
            f"{PROJECT_ROOT / 'littlefs' / 'cpm86' / 'retro86bios.h86'}."
        )
    print_firmware_notice(arguments.os, arguments.profile, output_path)
    if skipped_files:
        print(
            f"Disk too small: skipped non-executable files: {', '.join(skipped_files)}",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()
