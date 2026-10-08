#!/usr/bin/env python3
"""Create a LittleFS system disk with boot resources and supplied system files."""

import argparse
import sys
import tempfile
from pathlib import Path

import buildDiskImageCpm80
import diskImageCpm80
import diskImageCpm86
from diskImageCommon import DiskImageError

PROJECT_ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE_EXTENSIONS = {
    "cpm80": {".COM"},
    "cpm86": {".CMD", ".SYS"},
}
PROFILE_MAP = {
    "cpm80": {"SMALL": "SYSTEM", "LARGE": "LARGE"},
    "cpm86": {"SMALL": "CPM86", "LARGE": "LARGE"},
}
IMAGE_MODULES = {
    "cpm80": diskImageCpm80,
    "cpm86": diskImageCpm86,
}


def _source_directory(os_name, override):
    if override is not None:
        return Path(override)
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


def create_system_disk(os_name, profile, output_path, source_dir=None, project_root=PROJECT_ROOT):
    os_name = os_name.lower()
    profile = profile.upper()
    if os_name not in IMAGE_MODULES:
        raise DiskImageError(f"Operating system is not implemented for system disks: {os_name}")
    if profile not in PROFILE_MAP[os_name]:
        raise DiskImageError(f"Unsupported {os_name} system disk profile: {profile}")

    output_path = Path(output_path)
    source_dir = _source_directory(os_name, source_dir)
    source_files = _read_source_files(source_dir)

    if os_name == "cpm80":
        host_path = project_root / "guest" / "cpm80" / "host" / "HOST.COM"
        if not host_path.is_file():
            raise DiskImageError(f"Project CP/M-80 HOST.COM is missing: {host_path}")
        host_data = host_path.read_bytes()
        overridden_files = (
            ["HOST.COM"]
            if "HOST.COM" in source_files and source_files["HOST.COM"] != host_data
            else []
        )
        source_files["HOST.COM"] = host_data
        profile_name = PROFILE_MAP[os_name][profile]
        create_base = lambda staging_path: _create_cpm80_base(staging_path, profile_name, project_root)
        system_files = source_files
    else:
        system_file_path, _ = _validate_cpm86_runtime_resources(project_root)
        if "CPM.SYS" in source_files:
            raise DiskImageError(
                f"CPM.SYS is loaded from LittleFS at {system_file_path}; do not put it in the source directory"
            )
        if "HOST.CMD" not in source_files:
            raise DiskImageError(f"HOST.CMD is missing in the system disk source directory: {source_dir}")
        system_files = source_files
        overridden_files = []
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

    return skipped_files, overridden_files


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Create a bootable SMALL or LARGE CP/M system disk in littlefs/<os>/. "
            "The firmware detects the layout from the file size, so drives.cfg needs no profile choice for A:."
        )
    )
    parser.add_argument("--os", required=True, choices=("cpm80", "cpm86"))
    parser.add_argument("--profile", required=True, choices=("SMALL", "LARGE"))
    parser.add_argument("--source-dir", type=Path, help="directory containing additional CP/M 8.3 files")
    parser.add_argument("--output", type=Path, help="output path (default: littlefs/<os>/system.dsk)")
    arguments = parser.parse_args(argv)

    output_path = arguments.output or PROJECT_ROOT / "littlefs" / arguments.os / "system.dsk"
    try:
        skipped_files, overridden_files = create_system_disk(
            arguments.os,
            arguments.profile,
            output_path,
            source_dir=arguments.source_dir,
        )
    except (DiskImageError, OSError) as error:
        parser.error(str(error))

    print(f"Created {output_path} ({output_path.stat().st_size} bytes)")
    if arguments.os == "cpm86":
        print(f"CP/M-86 kernel kept outside A: at {PROJECT_ROOT / 'littlefs' / 'cpm86' / 'cpm.sys'}.")
        print(
            "CP/M-86 BIOS overlay kept outside A: at "
            f"{PROJECT_ROOT / 'littlefs' / 'cpm86' / 'retro86bios.h86'}."
        )
    for filename in overridden_files:
        print(f"Using the project HOST program instead of source-directory {filename}.")
    if skipped_files:
        print(
            f"Disk too small: skipped non-executable files: {', '.join(skipped_files)}",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()
