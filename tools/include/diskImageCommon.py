"""Definitions shared by every OS disk-image module."""

import os
import tempfile
from pathlib import Path


class DiskImageError(ValueError):
    pass


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
