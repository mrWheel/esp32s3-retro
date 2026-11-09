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
