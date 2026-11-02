"""CP/M-86 file operations for extractFiles.py; menu options 7 and 10 use Dest. Path.

Accepts only images matching a diskImageCpm86 profile (CPM86, LARGE, BIG).
Without an image or action the interactive menu of tools/apple2/extractApple2Files.py starts.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
for module_directory in (Path(__file__).resolve().parent, Path(__file__).resolve().parents[1] / "include"):
    if str(module_directory) not in sys.path:
        sys.path.insert(0, str(module_directory))

import diskImageCpm86
import extractCpm as common

PROFILES = {f"CPM86-{name}": profile for name, profile in diskImageCpm86.PROFILES.items()}


def add_arguments(parser):
    pass


def run(arguments):
    return common.runWithProfiles(arguments, PROFILES)
