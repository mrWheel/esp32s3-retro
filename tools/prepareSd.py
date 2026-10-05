import argparse
from pathlib import Path


def prepareSd():
  parser = argparse.ArgumentParser(description="Create the Retro layout on an already formatted FAT32 card. Never formats or removes data.")
  parser.add_argument("mountPoint", type=Path)
  arguments = parser.parse_args()
  root = arguments.mountPoint.resolve()
  if not root.is_dir():
    parser.error("The mount point must already exist")
  retro = root / "retro"
  marker = retro / "layout.txt"
  drivesConfig = retro / "images" / "cpm" / "drives.cfg"
  expected = "ESP32-S3-RETRO\nlayout=1\n"
  defaultDrives = (
      "A=/littlefs/cpm/system.dsk,RO,SYSTEM\n"
      "B=/retro/images/cpm/languages.dsk,RO,LARGE\n"
      "C=/retro/images/cpm/tools.dsk,RO,LARGE\n"
      "D=/retro/images/cpm/utilities.dsk,RO,LARGE\n"
      "E=/retro/images/cpm/work.dsk,RW,LARGE\n"
      "F=/retro/images/cpm/archive.dsk,RW,LARGE\n"
  )
  required = [retro / "backup", retro / "exchange" / "common"]
  for group in ["images", "exchange"]:
    for machine in ["cpm", "ucsd", "apple2", "mpm", "swtpc"]:
      required.append(retro / group / machine)
  for path in [marker, drivesConfig, *required]:
    if not path.resolve().is_relative_to(root):
      parser.error("A layout path escapes the mount point through a symbolic link")
  if marker.exists() and marker.read_text().rstrip("\n") != expected.rstrip("\n"):
    parser.error("Existing layout marker is different; refusing to overwrite it")
  for path in required:
    path.mkdir(parents=True, exist_ok=True)
  if not marker.exists():
    with marker.open("x", newline="\n") as target:
      target.write(expected)
  if not drivesConfig.exists():
    with drivesConfig.open("x", newline="\n") as target:
      target.write(defaultDrives)
  print("Retro layout v1 prepared at " + str(retro))
  print("Filesystem type was not checked by this tool. Firmware validates FAT32. Eject the card safely.")


if __name__ == "__main__":
  prepareSd()
