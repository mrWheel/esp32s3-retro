import argparse
import sys
from pathlib import Path

INCLUDE_DIR = Path(__file__).resolve().parent / "include"
if str(INCLUDE_DIR) not in sys.path:
    sys.path.insert(0, str(INCLUDE_DIR))

import osProfiles


def prepareSd(argv=None):
  argv = sys.argv[1:] if argv is None else argv
  osName = osProfiles.preparse_os(argv)
  overview = osProfiles.os_overview()
  epilog = overview + "\n\nWithout --os the layout for every operating system is prepared."
  if osName:
    epilog = osProfiles.os_epilog(osName) + "\n\nOnly the directories of this OS (plus common) are prepared."
  parser = argparse.ArgumentParser(
    prog="prepareSd.py",
    description="Create the Retro layout on an already formatted FAT32 card. Never formats or removes data. "
    "Without a mount point the project's sdcard/ directory is prepared.",
    epilog=epilog + "\n\nexamples:\n  prepareSd.py /Volumes/SDCARD\n  prepareSd.py /Volumes/SDCARD --os cpm86\n  prepareSd.py",
    formatter_class=argparse.RawDescriptionHelpFormatter,
  )
  parser.add_argument("mountPoint", type=Path, nargs="?", default=osProfiles.DEFAULT_SD_ROOT)
  parser.add_argument("--os", dest="osName", type=str.lower, choices=tuple(osProfiles.OS_REGISTRY),
                      help="prepare only this operating system (default: all)")
  arguments = parser.parse_args(argv)
  machines = [arguments.osName] if arguments.osName else list(osProfiles.OS_REGISTRY)
  root = arguments.mountPoint.resolve()
  if not root.is_dir():
    parser.error("The mount point must already exist")
  retro = root / "retro"
  marker = retro / "layout.txt"
  drivesConfigs = {
      "cpm80": (
          retro / "images" / "cpm80" / "drives.cfg",
          "A=/littlefs/cpm80/system.dsk,RO,SYSTEM\n"
          "B=/retro/images/cpm80/languages.dsk,RO,LARGE\n"
          "C=/retro/images/cpm80/tools.dsk,RO,LARGE\n"
          "D=/retro/images/cpm80/utilities.dsk,RO,LARGE\n"
          "E=/retro/images/cpm80/work.dsk,RW,LARGE\n"
          "F=/retro/images/cpm80/work1.dsk,RW,SYSTEM\n",
      ),
      "cpm86": (
          retro / "images" / "cpm86" / "drives.cfg",
          "A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM\n"
          "B=/retro/images/cpm86/languages.dsk,RO,RETRO86_DATA_V1\n"
          "C=/retro/images/cpm86/tools.dsk,RO,RETRO86_DATA_V1\n"
          "D=/retro/images/cpm86/utilities.dsk,RO,RETRO86_DATA_V1\n"
          "E=/retro/images/cpm86/work86.dsk,RW,RETRO86_DATA_V1\n"
          "F=/retro/images/cpm86/archive.dsk,RW,RETRO86_DATA_V1\n",
      ),
  }
  expected = "ESP32-S3-RETRO\nlayout=1\n"
  required = [retro / "backup", retro / "exchange" / "common"]
  for group in ["images", "exchange"]:
    for machine in machines:
      required.append(retro / group / machine)
  selectedDriveConfigs = [drivesConfigs[machine][0] for machine in machines if machine in drivesConfigs]
  for path in [marker, *selectedDriveConfigs, *required]:
    if not path.resolve().is_relative_to(root):
      parser.error("A layout path escapes the mount point through a symbolic link")
  if marker.exists() and marker.read_text().rstrip("\n") != expected.rstrip("\n"):
    parser.error("Existing layout marker is different; refusing to overwrite it")
  for path in required:
    path.mkdir(parents=True, exist_ok=True)
  if not marker.exists():
    with marker.open("x", newline="\n") as target:
      target.write(expected)
  for machine in machines:
    if machine in drivesConfigs:
      drivesConfig, defaultDrives = drivesConfigs[machine]
      if not drivesConfig.exists():
        with drivesConfig.open("x", newline="\n") as target:
          target.write(defaultDrives)
  print("Retro layout v1 prepared at " + str(retro))
  print("Filesystem type was not checked by this tool. Firmware validates FAT32. Eject the card safely.")


if __name__ == "__main__":
  prepareSd()
