#!/usr/bin/env python3
"""List and fetch explicitly selected CP/M files from an HTML archive index."""

import argparse
import datetime
import hashlib
import io
import json
import os
import re
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from html.parser import HTMLParser
from pathlib import Path

import cpm80DiskImage

MAX_DOWNLOAD_SIZE = 64 * 1024 * 1024
USER_AGENT = "ESP32-S3-Retro-CPM-Resource-Tool/1.0"
DISK_SUFFIXES = {".DSK", ".RAW", ".IMD", ".TD0"}
MICROSOFT_PROGRAMS = {
    "MBASIC.COM",
    "MBASIC52.ZIP",
    "MBASIC.ZIP",
    "OBASIC.COM",
    "BASCOM53.ZIP",
    "BASCOM.ZIP",
}


class ArchiveError(ValueError):
    pass


class IndexParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.current_link = None

    def handle_starttag(self, tag, attributes):
        if tag.lower() == "a":
            self._finish_link()
            values = dict(attributes)
            href = values.get("href")
            if href:
                self.current_link = {"href": href, "text": [], "description": []}

    def handle_data(self, data):
        if self.current_link is not None:
            self.current_link["text"].append(data.strip())
        elif self.links and data.strip():
            self.links[-1]["description"].append(data.strip())

    def handle_endtag(self, tag):
        if tag.lower() == "a":
            self._finish_link()

    def _finish_link(self):
        if self.current_link is not None:
            text = " ".join(part for part in self.current_link["text"] if part)
            self.links.append(
                {
                    "href": self.current_link["href"],
                    "text": text,
                    "description": [],
                }
            )
            self.current_link = None

    def result(self):
        self._finish_link()
        return self.links


def _validate_url(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
        raise ArchiveError("Use a complete http:// or https:// URL without embedded credentials")
    return parsed


def _download(url):
    _validate_url(url)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            final_url = response.geturl()
            _validate_url(final_url)
            data = response.read(MAX_DOWNLOAD_SIZE + 1)
    except (urllib.error.URLError, TimeoutError) as error:
        raise ArchiveError(f"Could not download {url}: {error}") from error
    if len(data) > MAX_DOWNLOAD_SIZE:
        raise ArchiveError(f"Download exceeds the {MAX_DOWNLOAD_SIZE}-byte safety limit")
    return final_url, data


def read_index(url):
    final_url, data = _download(url)
    parser = IndexParser()
    parser.feed(data.decode("utf-8", errors="replace"))
    return final_url, parser.result()


def find_link(index_url, links, requested_link):
    matches = [
        link
        for link in links
        if link["href"].casefold() == requested_link.casefold()
        or Path(urllib.parse.urlsplit(link["href"]).path).name.casefold() == requested_link.casefold()
        or link["text"].casefold() == requested_link.casefold()
    ]
    if not matches:
        raise ArchiveError(f"Link {requested_link!r} was not found on {index_url}; list that index first")
    if len(matches) != 1:
        raise ArchiveError(f"Link name {requested_link!r} is ambiguous; use its exact href")
    file_url = urllib.parse.urljoin(index_url, matches[0]["href"])
    _validate_url(file_url)
    if urllib.parse.urlsplit(file_url).hostname != urllib.parse.urlsplit(index_url).hostname:
        raise ArchiveError("Refusing an archive link that points to a different host")
    return file_url, matches[0]


def zip_members(archive_data, selected_members):
    try:
        archive = zipfile.ZipFile(io.BytesIO(archive_data))
    except zipfile.BadZipFile as error:
        raise ArchiveError("The selected archive is not a valid ZIP file") from error

    files = [info for info in archive.infolist() if not info.is_dir()]
    if not selected_members:
        available = [info.filename for info in files]
        raise ArchiveError(
            "ZIP archive contents must be selected explicitly with --member. "
            "Members: " + (", ".join(available) if available else "(none)")
        )
    results = []
    for member_name in selected_members:
        matches = [info for info in files if info.filename == member_name]
        if not matches:
            raise ArchiveError(f"ZIP member not found: {member_name}")
        info = matches[0]
        if info.file_size > cpm80DiskImage.MAX_IMAGE_SIZE:
            raise ArchiveError(f"ZIP member is larger than one CP/M disk: {member_name}")
        try:
            contents = archive.read(info)
        except (RuntimeError, zipfile.BadZipFile) as error:
            raise ArchiveError(f"Could not read ZIP member {member_name}: {error}") from error
        results.append((Path(info.filename).name, contents))
    return results


def _downloaded_files(index_url, requested_link, member_names):
    final_index_url, links = read_index(index_url)
    file_url, link = find_link(final_index_url, links, requested_link)
    description = " ".join([link["text"], *link["description"]])
    if "removed by request" in description.casefold():
        raise ArchiveError("The archive marks this resource as removed by request")
    link_name = Path(urllib.parse.urlsplit(link["href"]).path).name.upper()
    if link_name in MICROSOFT_PROGRAMS or "microsoft" in description.casefold():
        raise ArchiveError(
            "Microsoft software is not downloaded by this tool. Use a local copy "
            "for which you have the required license/permission."
        )
    final_file_url, data = _download(file_url)
    is_zip = zipfile.is_zipfile(io.BytesIO(data))
    link_suffix = Path(urllib.parse.urlsplit(link["href"]).path).suffix.casefold()
    final_suffix = Path(urllib.parse.urlsplit(final_file_url).path).suffix.casefold()
    if is_zip:
        files = zip_members(data, member_names)
    elif link_suffix == ".zip" or final_suffix == ".zip":
        raise ArchiveError("The archive link ends in .ZIP but the downloaded data is not a valid ZIP file")
    else:
        if member_names:
            raise ArchiveError("--member can only be used with a ZIP file")
        files = [(Path(urllib.parse.urlsplit(final_file_url).path).name, data)]
    return final_index_url, final_file_url, link, files, data


def _require_rights(evidence):
    if not evidence or not evidence.strip():
        raise ArchiveError(
            "Provide --rights-evidence with the applicable license/permission reference. "
            "This tool does not determine whether archived software may be copied."
        )


def _print_files(source_url, link, files, evidence):
    print(f"Archive link: {link['text'] or link['href']} ({source_url})")
    if link["description"]:
        print("Description: " + " ".join(link["description"]))
    print("Rights evidence supplied: " + evidence)
    for filename, contents in files:
        print(f"Selected: {filename} ({len(contents)} bytes, SHA-256 {hashlib.sha256(contents).hexdigest()})")
    print("WARNING: verify the source and checksum before use; archive downloads may use plain HTTP.")


def _safe_archive_name(url, link):
    candidate = Path(urllib.parse.unquote(urllib.parse.urlsplit(link["href"]).path)).name
    if not candidate:
        candidate = Path(urllib.parse.urlsplit(url).path).name or "download.bin"
    candidate = re.sub(r"[^A-Za-z0-9._-]", "_", candidate)
    return candidate


def _append_manifest(archive_directory, record):
    archive_directory = Path(archive_directory)
    if archive_directory.is_symlink():
        raise ArchiveError(f"Refusing a symbolic-link archive directory: {archive_directory}")
    archive_directory.mkdir(parents=True, exist_ok=True)
    if not archive_directory.is_dir():
        raise ArchiveError(f"Archive path is not a directory: {archive_directory}")

    manifest_path = archive_directory / "cpm-resource-manifest.json"
    if manifest_path.is_symlink():
        raise ArchiveError(f"Refusing a symbolic-link manifest: {manifest_path}")
    if manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ArchiveError(f"Could not read resource manifest {manifest_path}: {error}") from error
        if not isinstance(manifest, dict) or not isinstance(manifest.get("resources"), list):
            raise ArchiveError(f"Invalid resource manifest format: {manifest_path}")
    else:
        manifest = {"format": 1, "resources": []}
    manifest["resources"].append(record)

    descriptor, temporary_name = tempfile.mkstemp(prefix=".cpm-resource-manifest.", dir=archive_directory)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as temporary_file:
            json.dump(manifest, temporary_file, indent=2, sort_keys=True)
            temporary_file.write("\n")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_name, manifest_path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def preserve_download(archive_directory, source_index, source_url, link, original_data, evidence, members):
    archive_directory = Path(archive_directory)
    archive_directory.mkdir(parents=True, exist_ok=True)
    if archive_directory.is_symlink() or not archive_directory.is_dir():
        raise ArchiveError(f"Invalid archive directory: {archive_directory}")
    archive_name = _safe_archive_name(source_url, link)
    digest = hashlib.sha256(original_data).hexdigest()
    archive_path = archive_directory / archive_name
    if archive_path.is_symlink():
        raise ArchiveError(f"Refusing to overwrite a symbolic link: {archive_path}")
    if archive_path.exists():
        if archive_path.is_symlink() or not archive_path.is_file():
            raise ArchiveError(f"Refusing to overwrite non-regular archive path: {archive_path}")
        if hashlib.sha256(archive_path.read_bytes()).hexdigest() != digest:
            source_path = Path(archive_name)
            archive_path = archive_directory / f"{source_path.stem}-{digest[:8]}{source_path.suffix}"
    if archive_path.exists():
        if archive_path.is_symlink() or not archive_path.is_file():
            raise ArchiveError(f"Refusing to overwrite non-regular archive path: {archive_path}")
        if hashlib.sha256(archive_path.read_bytes()).hexdigest() != digest:
            raise ArchiveError(f"Archive filename collision: {archive_path}")
    else:
        try:
            with archive_path.open("xb") as original:
                original.write(original_data)
                original.flush()
                os.fsync(original.fileno())
        except FileExistsError:
            if hashlib.sha256(archive_path.read_bytes()).hexdigest() != digest:
                raise ArchiveError(f"Archive filename collision: {archive_path}")

    record = {
        "archive_file": archive_path.name,
        "bytes": len(original_data),
        "fetched_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "rights_evidence": evidence,
        "selected_members": members,
        "sha256": digest,
        "source_index_url": source_index,
        "source_url": source_url,
    }
    _append_manifest(archive_directory, record)
    return archive_path


def list_index(url):
    final_url, links = read_index(url)
    if not links:
        raise ArchiveError("No links were found on this index page")
    print(f"Links in {final_url}")
    for link in links:
        description = " ".join(link["description"])
        label = link["text"] or link["href"]
        print(f"{label:<28} {description}".rstrip())


def add_from_index(arguments):
    _require_rights(arguments.rights_evidence)
    index_url, source_url, link, files, original_data = _downloaded_files(
        arguments.index_url, arguments.link, arguments.member
    )
    archive_path = preserve_download(
        arguments.archive_dir,
        index_url,
        source_url,
        link,
        original_data,
        arguments.rights_evidence,
        arguments.member,
    )
    if len(files) > 1 and arguments.name:
        raise ArchiveError("--name can only be used when selecting one file")
    additions = []
    for filename, contents in files:
        extension = Path(filename).suffix.upper()
        if extension in DISK_SUFFIXES:
            raise ArchiveError(
                f"{filename} is a disk image, not a file to add. "
                "Use the 'disk' command to install a matching raw image."
            )
        cpm_name = arguments.name if arguments.name else filename
        additions.append((cpm_name, contents))

    cpm80DiskImage.add_files(arguments.image, additions)
    _print_files(source_url, link, files, arguments.rights_evidence)
    print(f"Original download preserved at {archive_path}")
    print(f"Added {len(additions)} file(s) to {arguments.image}")
    print(f"Source index: {index_url}")


def install_disk_image(arguments):
    _require_rights(arguments.rights_evidence)
    index_url, source_url, link, files, original_data = _downloaded_files(
        arguments.index_url, arguments.link, arguments.member
    )
    archive_path = preserve_download(
        arguments.archive_dir,
        index_url,
        source_url,
        link,
        original_data,
        arguments.rights_evidence,
        arguments.member,
    )
    if len(files) != 1:
        raise ArchiveError("Select exactly one raw disk image")
    filename, image = files[0]
    if Path(filename).suffix.upper() not in DISK_SUFFIXES:
        raise ArchiveError("The selected file must have a .DSK or .RAW extension")
    cpm80DiskImage.inspect_image(image)
    cpm80DiskImage.write_image(arguments.output, image, force=arguments.force)
    _print_files(source_url, link, files, arguments.rights_evidence)
    print(f"Original download preserved at {archive_path}")
    print(f"Installed matching raw CP/M image at {arguments.output}")
    print(f"Source index: {index_url}")


def main():
    parser = argparse.ArgumentParser(
        description="List archive indexes and import selected, rights-cleared CP/M resources."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    list_parser = commands.add_parser("list", help="list links and descriptions on an HTML index")
    list_parser.add_argument("index_url")

    add_parser = commands.add_parser("add", help="download a file/ZIP member and add it to an image")
    add_parser.add_argument("index_url")
    add_parser.add_argument("--link", required=True, help="exact link text or filename shown by list")
    add_parser.add_argument("--image", required=True, type=Path)
    add_parser.add_argument("--archive-dir", required=True, type=Path, help="directory to retain the original download and manifest")
    add_parser.add_argument("--member", action="append", default=[], help="ZIP member to import; repeatable")
    add_parser.add_argument("--name", help="CP/M 8.3 name to use when importing one file")
    add_parser.add_argument(
        "--rights-evidence",
        required=True,
        help="license/permission reference for these specific files (for example a license URL)",
    )

    disk_parser = commands.add_parser("disk", help="install a matching raw disk image at a destination")
    disk_parser.add_argument("index_url")
    disk_parser.add_argument("--link", required=True)
    disk_parser.add_argument("--output", required=True, type=Path)
    disk_parser.add_argument("--archive-dir", required=True, type=Path, help="directory to retain the original download and manifest")
    disk_parser.add_argument("--member", action="append", default=[], help="disk member in a ZIP file")
    disk_parser.add_argument("--force", action="store_true", help="replace an existing image")
    disk_parser.add_argument("--rights-evidence", required=True, help="license/permission reference")

    arguments = parser.parse_args()
    try:
        if arguments.command == "list":
            list_index(arguments.index_url)
        elif arguments.command == "add":
            add_from_index(arguments)
        else:
            install_disk_image(arguments)
    except (OSError, ArchiveError, cpm80DiskImage.DiskImageError, zipfile.LargeZipFile) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
