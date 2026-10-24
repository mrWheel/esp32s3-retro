#!/usr/bin/env python3
"""Fixtures and cross-checks for tests/guestFsTests.c (native guest file system).

  build <dir>      create Python-built CP/M images, expected images and manifests
  real <dir>       dump files of the real sdcard CP/M images for read comparison
  verify <dir>     check images written by guestFsTests with the Python tools
"""

import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools" / "include"))
sys.path.insert(0, str(root / "tools"))
sys.path.insert(0, str(root / "tools" / "cpm80"))
sys.path.insert(0, str(root / "tools" / "cpm86"))

import diskImageCpm
import diskImageCpm80
import diskImageCpm86

MODULES = {"cpm80": diskImageCpm80, "cpm86": diskImageCpm86}


def content(size, seed):
    return bytes(((index * 7 + seed * 13 + (index >> 8)) & 0xFF) for index in range(size))


def freeCounts(module, data):
    entries, freeSlots, usedBlocks = diskImageCpm.inspect_image(data, module.PROFILES)
    profile = diskImageCpm._profile_for_size(len(data), module.PROFILES)
    total = profile["max_block_number"] - profile.get("first_data_block", 2) + 1
    return total - len(usedBlocks), len(freeSlots), profile


def build(outDir):
    outDir.mkdir(parents=True, exist_ok=True)
    for osId, module in MODULES.items():
        for profileName, profile in module.PROFILES.items():
            blockSize = profile["block_size"]
            multiSize = 300000 if blockSize == 16384 else 40000
            base = [("ONE.BIN", 1, 1), ("A128.DAT", 128, 2), ("B129.DAT", 129, 3), ("MID.TXT", 1024, 4),
                    ("EXACT.BIN", blockSize * 3, 5)]
            added = [("MULTI.DAT", multiSize, 6), ("NEW.COM", 3000, 7), ("Z$-_.X-Y", 77, 8)]
            stem = f"{osId}-{profileName}"
            path = outDir / f"{stem}.base.dsk"
            expected = outDir / f"{stem}.expected.dsk"
            module.create_image(path, force=True, profile=profileName)
            module.add_files(path, [(name, content(size, seed)) for name, size, seed in base])
            baseData = path.read_bytes()
            freeBlocks, freeSlots, _ = freeCounts(module, baseData)
            expected.write_bytes(baseData)
            module.add_files(expected, [(name, content(size, seed)) for name, size, seed in added])
            afterBlocks, afterSlots, _ = freeCounts(module, expected.read_bytes())
            lines = [f"os {osId}", f"profile {profileName}",
                     f"geometry {profile['directory_offset']} {blockSize} {profile['directory_entries']} "
                     f"{profile['max_block_number']} {profile.get('first_data_block', 2)}"]
            lines += [f"base {name} {size} {seed}" for name, size, seed in base]
            lines += [f"add {name} {size} {seed}" for name, size, seed in added]
            lines += [f"free {freeBlocks} {freeSlots}", f"freeAfter {afterBlocks} {afterSlots}"]
            (outDir / f"{stem}.manifest").write_text("\n".join(lines) + "\n")


def dumpFiles(module, image, target):
    data = image.read_bytes()
    entries, _, _ = diskImageCpm.inspect_image(data, module.PROFILES)
    freeBlocks, freeSlots, _ = freeCounts(module, data)
    target.mkdir(parents=True, exist_ok=True)
    names = sorted({(entry["user"], entry["filename"]) for entry in entries})
    for user, name in names:
        (target / f"{user}-{name}").write_bytes(module.extract_file(image, name, user=user))
    (target / "free.txt").write_text(f"{freeBlocks} {freeSlots}\n")
    return names


def real(outDir):
    for osId, module in MODULES.items():
        for image in sorted((root / "sdcard" / "retro" / "images" / osId).glob("*.dsk")):
            dumpFiles(module, image, outDir / f"real-{osId}-{image.stem}")


def verify(outDir):
    failures = 0
    for image in sorted(outDir.glob("*.dsk")):
        osId = image.name.split("-", 1)[0]
        if osId not in MODULES:
            continue
        module = MODULES[osId]
        filesDir = outDir / f"{image.name}.files"
        try:
            names = dumpFiles(module, image, outDir / f"{image.name}.pythonfiles")
        except diskImageCpm.DiskImageError as error:
            print(f"FAIL {image.name}: {error}")
            failures += 1
            continue
        if not filesDir.is_dir():
            continue
        expected = sorted(path.name for path in filesDir.iterdir())
        actual = sorted([f"{user}-{name}" for user, name in names] + ["free.txt"])
        if expected != actual:
            print(f"FAIL {image.name}: file lists differ {expected} != {actual}")
            failures += 1
            continue
        for name in expected:
            if (filesDir / name).read_bytes() != (outDir / f"{image.name}.pythonfiles" / name).read_bytes():
                print(f"FAIL {image.name}: {name} differs")
                failures += 1
        print(f"checked {image.name}: {len(names)} files")
    if failures:
        return 1
    print("PASS: Python verified all images written by guestFsTests")
    return 0


def main(argv):
    if len(argv) != 3 or argv[1] not in ("build", "real", "verify"):
        print(__doc__)
        return 2
    directory = Path(argv[2])
    if argv[1] == "build":
        build(directory)
    elif argv[1] == "real":
        real(directory)
    else:
        return verify(directory)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
