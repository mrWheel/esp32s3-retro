"""Apple II (DOS 3.3 / ProDOS / Pascal) interactive browser for extractFiles.py."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import extractApple2Files as core


def add_arguments(parser):
    pass


def run(arguments):
    if arguments.add or arguments.remove or arguments.extract or arguments.image:
        print("Error: apple2 only supports the interactive menu for now (run without image/--add).", file=sys.stderr)
        return 1
    return core.main(["extractFiles"])
