#!/usr/bin/env python3
"""List, add, extract and remove files on disk images, per operating system.

Usage: extractFiles.py --os {cpm,cpm86,apple2,swtpc,ucsd} [image] [options]
  image                  disk image (default action: list its files; without image the menu starts)
  --add HOSTFILE         add a host file (full path) to the image
  --name NAME            name on the image for --add (default: host file name)
  --extract NAME         copy a file from the image to --dest (default: current directory)
  --remove NAME          remove a file from the image
Currently implemented: ucsd (all actions), apple2 (interactive menu, no image given).
"""

import argparse
import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "include"))

OS_MODULES = {
    "cpm": "extractCpm",
    "cpm86": "extractCpm86",
    "apple2": "extractApple2",
    "swtpc": "extractSwtpc",
    "ucsd": "extractUcsd",
}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--os", required=True, choices=sorted(OS_MODULES), dest="os_key")
    parser.add_argument("image", nargs="?")
    parser.add_argument("--add", metavar="HOSTFILE")
    parser.add_argument("--name")
    parser.add_argument("--extract", metavar="NAME")
    parser.add_argument("--remove", metavar="NAME")
    parser.add_argument("--dest")
    module = importlib.import_module(OS_MODULES[parser.parse_known_args(argv)[0].os_key])
    module.add_arguments(parser)
    arguments = parser.parse_args(argv)
    if not arguments.image and (arguments.add or arguments.remove or arguments.extract):
        parser.error("image is required")
    if not arguments.image and arguments.os_key not in ("apple2", "ucsd"):
        parser.error("image is required")
    return module.run(arguments)


if __name__ == "__main__":
    sys.exit(main())
