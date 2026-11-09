# Copyright (C) 2026 Willem Aandewiel
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

"""CP/M-86 file operations for extractFiles.py; menu options 7 and 10 use Dest. Path.

Accepts only images matching a diskImageCpm86 profile (CPM86_160K, CPM86_516K, CPM86_1440K,
CPM86_8196K).
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

PROFILES = dict(diskImageCpm86.PROFILES)


def add_arguments(parser):
    pass


def run(arguments):
    return common.runWithProfiles(arguments, PROFILES)
