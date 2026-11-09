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

"""Definitions shared by every OS disk-image module."""

import os
import sys
import tempfile
from pathlib import Path


class DiskImageError(ValueError):
    pass


def show_plan(lines):
    """Print the planned action so every run shows what is about to happen."""
    for line in lines:
        print(line)
    print()


def confirm_plan(lines, assume_yes):
    """Show the planned action and require 'y' before anything is written to disk."""
    show_plan(lines)
    if assume_yes:
        return
    if not sys.stdin.isatty():
        print("Aborted: confirmation is impossible without a terminal; re-run with --yes.", file=sys.stderr)
        raise SystemExit(1)
    try:
        answer = input("Are you sure (y/N) ")
    except EOFError:
        answer = ""
    if answer.strip().lower() not in ("y", "yes"):
        print("Aborted by user.", file=sys.stderr)
        raise SystemExit(1)


def atomic_write_image(path, data, replace_existing=False):
    """Write bytes to path atomically (temporary file plus os.replace), creating the parent directory."""
    path = Path(path)
    if path.is_symlink():
        raise DiskImageError(f"Refusing to replace a symbolic link: {path}")
    if path.exists() and not replace_existing:
        raise DiskImageError(f"File already exists; use --force to replace it: {path}")
    if path.exists() and not path.is_file():
        raise DiskImageError(f"Not a regular file: {path}")

    path.parent.mkdir(parents=True, exist_ok=True)
    mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
    descriptor, temporary_name = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as temporary_file:
            temporary_file.write(data)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.chmod(temporary_name, mode)
        os.replace(temporary_name, path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise
