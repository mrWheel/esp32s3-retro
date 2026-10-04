#!/usr/bin/env python3
import argparse
from pathlib import Path

SECTOR_SIZE = 128
SECTORS_PER_TRACK = 26
TRACKS = 77
BLOCK_SIZE = 1024
RESERVED_TRACKS = 2
CCP_SIZE = 0x0800
BDOS_SIZE = 0x0E00
HEADER = b"RETROCPM" + bytes((1, 1, 0x00, 0xC4, 0x00, 0xCC, 0x00, 0xDA))
HELLO_PROGRAM = (
    b"\x0e\x09\x11\x0b\x01\xcd\x05\x00\xc3\x00\x00"
    b"HELLO FROM CP/M 2.2\r\n$"
)


def build_disk(project_root: Path, output_path: Path) -> None:
    cpm_directory = project_root / "components" / "cpmCore" / "os"
    ccp = (cpm_directory / "ccp-64k.bin").read_bytes()
    bdos = (cpm_directory / "bdos-64k.bin").read_bytes()
    if len(ccp) != CCP_SIZE:
        raise ValueError(f"CCP must be {CCP_SIZE} bytes, found {len(ccp)}")
    if len(bdos) != BDOS_SIZE:
        raise ValueError(f"BDOS must be {BDOS_SIZE} bytes, found {len(bdos)}")

    disk = bytearray(b"\xE5" * (TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE))
    disk[:CCP_SIZE] = ccp
    disk[CCP_SIZE : CCP_SIZE + BDOS_SIZE] = bdos
    header_offset = CCP_SIZE + BDOS_SIZE
    disk[header_offset : header_offset + len(HEADER)] = HEADER

    directory_offset = RESERVED_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE
    directory_entry = bytearray(b"\x00" + b"HELLO   COM" + bytes(3) + b"\x01" + bytes(16))
    directory_entry[16] = 2
    disk[directory_offset : directory_offset + len(directory_entry)] = directory_entry

    program_offset = directory_offset + 2 * BLOCK_SIZE
    disk[program_offset : program_offset + SECTOR_SIZE] = HELLO_PROGRAM.ljust(SECTOR_SIZE, b"\x1A")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(disk)


def main() -> None:
    project_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Build the minimal CP/M 2.2 LittleFS A: disk image.")
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=project_root / "littlefs" / "cpm" / "system.dsk",
        help="output image path (defaults to littlefs/cpm/system.dsk)",
    )
    arguments = parser.parse_args()
    build_disk(project_root, arguments.output.resolve())
    print(f"Created {arguments.output.resolve()} (256256 bytes)")


if __name__ == "__main__":
    main()
