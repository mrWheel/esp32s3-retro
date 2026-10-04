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
UTILITY_FILES = (
    "ASM.COM",
    "DDT.COM",
    "DUMP.COM",
    "ED.COM",
    "HELP.COM",
    "HELP.HLP",
    "LIB.COM",
    "LINK.COM",
    "LOAD.COM",
    "MAC.COM",
    "PIP.COM",
    "RMAC.COM",
    "STAT.COM",
    "SUBMIT.COM",
    "XREF.COM",
    "XSUB.COM",
    "ZSID.COM",
)
HELLO_PROGRAM = (
    b"\x0e\x09\x11\x0b\x01\xcd\x05\x00\xc3\x00\x00"
    b"HELLO FROM CP/M 2.2\r\n$"
)
WELCOME_TEXT = b"CP/M utilities are installed.\r\nUse DIR, STAT, and HELP for help.\r\n\x1A"


def add_file(
    disk: bytearray,
    directory: bytearray,
    directory_offset: int,
    directory_entry_count: int,
    filename: str,
    data: bytes,
    next_block: int,
    next_directory_entry: int,
) -> tuple[int, int]:
    name, extension = filename.upper().split(".", 1)
    if not name or len(name) > 8 or not extension or len(extension) > 3:
        raise ValueError(f"CP/M filename must use the 8.3 format: {filename}")
    if not data:
        raise ValueError(f"CP/M file must not be empty: {filename}")

    record_count = (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    record_data = data.ljust(record_count * SECTOR_SIZE, b"\x1A")
    block_count = (len(record_data) + BLOCK_SIZE - 1) // BLOCK_SIZE
    allocation_block_count = (
        (TRACKS - RESERVED_TRACKS) * SECTORS_PER_TRACK * SECTOR_SIZE // BLOCK_SIZE
    )
    if next_block + block_count > allocation_block_count:
        raise ValueError(f"CP/M disk is full while adding {filename}")

    allocated_blocks = list(range(next_block, next_block + block_count))
    for block_index, block_number in enumerate(allocated_blocks):
        source_offset = block_index * BLOCK_SIZE
        block_data = record_data[source_offset : source_offset + BLOCK_SIZE].ljust(BLOCK_SIZE, b"\x1A")
        destination_offset = directory_offset + block_number * BLOCK_SIZE
        disk[destination_offset : destination_offset + BLOCK_SIZE] = block_data

    extent_count = (record_count + 127) // 128
    for extent_index in range(extent_count):
        if next_directory_entry >= directory_entry_count:
            raise ValueError(f"CP/M directory is full while adding {filename}")

        first_record = extent_index * 128
        extent_records = min(128, record_count - first_record)
        first_block = first_record // (BLOCK_SIZE // SECTOR_SIZE)
        extent_blocks = (extent_records + BLOCK_SIZE // SECTOR_SIZE - 1) // (BLOCK_SIZE // SECTOR_SIZE)
        entry = bytearray(32)
        entry[0] = 0
        entry[1:9] = name.encode("ascii").ljust(8, b" ")
        entry[9:12] = extension.encode("ascii").ljust(3, b" ")
        entry[12] = extent_index & 0x1F
        entry[14] = (extent_index >> 5) & 0x3F
        entry[15] = extent_records
        for allocation_index, block_number in enumerate(
            allocated_blocks[first_block : first_block + extent_blocks]
        ):
            entry[16 + allocation_index] = block_number
        entry_offset = next_directory_entry * 32
        directory[entry_offset : entry_offset + 32] = entry
        next_directory_entry += 1

    return next_block + block_count, next_directory_entry


def build_disk(project_root: Path, output_path: Path) -> None:
    cpm_directory = project_root / "components" / "cpmCore" / "os"
    utility_directory = cpm_directory / "utilities"
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
    directory_entry_count = 64
    directory_size = directory_entry_count * 32
    directory = bytearray(b"\xE5" * directory_size)
    next_block = 2
    next_directory_entry = 0
    next_block, next_directory_entry = add_file(
        disk,
        directory,
        directory_offset,
        directory_entry_count,
        "HELLO.COM",
        HELLO_PROGRAM,
        next_block,
        next_directory_entry,
    )
    next_block, next_directory_entry = add_file(
        disk,
        directory,
        directory_offset,
        directory_entry_count,
        "WELCOME.TXT",
        WELCOME_TEXT,
        next_block,
        next_directory_entry,
    )
    for filename in UTILITY_FILES:
        utility_path = utility_directory / filename
        if not utility_path.is_file():
            raise FileNotFoundError(f"Missing CP/M utility resource: {utility_path}")
        next_block, next_directory_entry = add_file(
            disk,
            directory,
            directory_offset,
            directory_entry_count,
            filename,
            utility_path.read_bytes(),
            next_block,
            next_directory_entry,
        )
    disk[directory_offset : directory_offset + directory_size] = directory

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(disk)


def main() -> None:
    project_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Build the CP/M 2.2 LittleFS A: disk image and utility directory.")
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
