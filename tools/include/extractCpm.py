"""CP/M (80 and 86) disk operations; menu options 7 and 10 use Dest. Path.

Without an image or action the interactive menu of extractApple2Files.py starts,
with CP/M images (user area 0) as source and destination.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import diskImageCpm
import diskImageCpm80
import diskImageCpm86
import extractApple2Files as core

PROFILES = {f"CPM80-{name}": profile for name, profile in diskImageCpm80.PROFILES.items()}
PROFILES.update({f"CPM86-{name}": profile for name, profile in diskImageCpm86.PROFILES.items()})
RECORD = diskImageCpm.SECTOR_SIZE


class CpmImage:
    """Raw CP/M image held in memory, with the attributes the menu expects of a disk image."""

    def __init__(self, path):
        self.path = Path(path).expanduser()
        if not self.path.is_file():
            raise core.ToolError(f"image not found: {self.path}")
        self.data = bytearray(self.path.read_bytes())

    def save(self):
        try:
            diskImageCpm.write_image(self.path, self.data, PROFILES, force=True)
        except diskImageCpm.DiskImageError as error:
            raise core.ToolError(str(error)) from error


class CpmVolume:
    fsName = "CP/M"
    writable = True

    def __init__(self, image):
        self.image = image
        try:
            self.profile = diskImageCpm._profile_for_size(len(image.data), PROFILES)
        except diskImageCpm.DiskImageError as error:
            raise core.ToolError(f"{image.path.name}: {error}") from error

    def description(self):
        _, _, usedBlocks = self.scan()
        total = self.profile["max_block_number"] - self.profile.get("first_data_block", 2) + 1
        return f"CP/M disk {self.image.path.name}, {total} blocks of {self.profile['block_size']} bytes, {total - len(usedBlocks)} free"

    def scan(self):
        try:
            return diskImageCpm.inspect_image(self.image.data, PROFILES)
        except diskImageCpm.DiskImageError as error:
            raise core.ToolError(f"{self.image.path.name}: {error}") from error

    def listFiles(self):
        entries, _, _ = self.scan()
        entryRecords = diskImageCpm._entry_geometry(self.profile)[1]
        files = {}
        for entry in entries:
            if entry["user"] == 0:
                files.setdefault(entry["filename"], []).append(entry)
        result = []
        for name, extents in sorted(files.items()):
            last = max(extents, key=lambda entry: entry["extent"])
            length = (last["extent"] * entryRecords + last["records"]) * RECORD
            blocks = sum(len(entry["blocks"]) for entry in extents)
            result.append(core.Entry(name, "CPM", blocks, length, "", False, {"slots": [e["slot"] for e in extents]}))
        return result

    def exportFile(self, entry):
        entries, _, _ = self.scan()
        extents = sorted((e for e in entries if e["user"] == 0 and e["filename"] == entry.path), key=lambda e: e["extent"])
        if not extents or any(e["extent"] != index for index, e in enumerate(extents)):
            raise core.ToolError(f"{entry.path} has a missing CP/M extent")
        blockSize = self.profile["block_size"]
        data = bytearray()
        for extent in extents:
            remaining = extent["records"] * RECORD
            for block in extent["blocks"]:
                offset = self.profile["directory_offset"] + block * blockSize
                count = min(remaining, blockSize)
                data += self.image.data[offset:offset + count]
                remaining -= count
        return core.Exported(bytes(data), 0x06, 0, 5)

    def importFile(self, outPath, exported):
        if "/" in outPath:
            raise core.ToolError("CP/M volumes have no directories; '/' is not allowed in the name")
        backup = bytes(self.image.data)
        try:
            name = diskImageCpm.parse_filename(outPath)
            entries, freeSlots, usedBlocks = self.scan()
            oldSlots = [entry["slot"] for entry in entries if entry["user"] == 0 and entry["filename"] == name]
            if oldSlots:
                if not core.askYesNo(f"Overwrite {name} y/N? "):
                    raise core.ToolError(f"{name} not overwritten")
                directoryOffset = self.profile["directory_offset"]
                for slot in oldSlots:
                    self.image.data[directoryOffset + slot * 32] = diskImageCpm.EMPTY
                entries, freeSlots, usedBlocks = self.scan()
            existing = {(entry["user"], entry["filename"]) for entry in entries}
            diskImageCpm._add_file(self.image.data, name, exported.data, freeSlots, usedBlocks, existing, self.profile)
        except (diskImageCpm.DiskImageError, core.ToolError) as error:
            self.image.data[:] = backup
            if isinstance(error, core.ToolError):
                raise
            raise core.ToolError(str(error)) from error
        return name

    def removeFile(self, entry):
        offset = self.profile["directory_offset"]
        for slot in entry.meta["slots"]:
            self.image.data[offset + slot * 32] = diskImageCpm.EMPTY


def openCpm(path, shared=None):
    image = shared if isinstance(shared, CpmImage) else CpmImage(path)
    return image, CpmVolume(image)


def add_arguments(parser):
    pass


def run(arguments):
    if not arguments.image and not (arguments.add or arguments.remove or arguments.extract):
        return core.main(["extractFiles"], ucsdMode=True, opener=openCpm)
    try:
        image, volume = openCpm(arguments.image)
        if arguments.add:
            hostPath = Path(arguments.add).expanduser()
            if not hostPath.is_file():
                raise core.ToolError(f"file not found: {hostPath}")
            data = hostPath.read_bytes()
            stored = volume.importFile(arguments.name or hostPath.name, core.Exported(data, 0x06, 0, 5))
            image.save()
            print(f"Added {hostPath} as {stored} ({len(data)} bytes) to {image.path.name}")
        elif arguments.remove:
            entry = core.findEntry(volume, arguments.remove)
            volume.removeFile(entry)
            image.save()
            print(f"Removed {entry.path}")
        elif arguments.extract:
            entry = core.findEntry(volume, arguments.extract)
            target = Path(arguments.dest or ".").expanduser() / entry.path
            if target.exists() and not core.askYesNo(f"Overwrite {target} y/N? "):
                print(f"Not overwritten: {target}")
                return 0
            target.write_bytes(volume.exportFile(entry).data)
            print(f"Extracted {entry.path} to {target}")
        else:
            core.printListing(volume)
    except core.ToolError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0
