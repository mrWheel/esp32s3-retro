import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

import fetchCpm80Software


class FetchCpm80SoftwareTests(unittest.TestCase):
    def test_index_parser_retains_link_description(self):
        parser = fetchCpm80Software.IndexParser()
        parser.feed('<a href="BASIC.COM">Mbasic.com</a> Microsoft BASIC v5.21<br><a href="TOOLS.ZIP">Tools</a>')
        links = parser.result()
        self.assertEqual(links[0]["text"], "Mbasic.com")
        self.assertEqual(links[0]["description"], ["Microsoft BASIC v5.21"])
        self.assertEqual(links[1]["href"], "TOOLS.ZIP")

    def test_find_link_resolves_relative_file_from_same_host(self):
        links = [{"href": "Mbasic.com", "text": "MBASIC.COM", "description": []}]
        url, _ = fetchCpm80Software.find_link(
            "http://cpmarchives.example/cpm/lang.htm", links, "mbasic.com"
        )
        self.assertEqual(url, "http://cpmarchives.example/cpm/Mbasic.com")

    def test_zip_members_are_selected_without_extracting_paths(self):
        archive_buffer = io.BytesIO()
        with zipfile.ZipFile(archive_buffer, "w") as archive:
            archive.writestr("../BASIC.COM", b"unsafe path is read as data only")
            archive.writestr("tools/ASM.COM", b"assembler")

        members = fetchCpm80Software.zip_members(archive_buffer.getvalue(), ["tools/ASM.COM"])
        self.assertEqual(members, [("ASM.COM", b"assembler")])
        with self.assertRaisesRegex(fetchCpm80Software.ArchiveError, "selected explicitly"):
            fetchCpm80Software.zip_members(archive_buffer.getvalue(), [])

    def test_link_to_different_host_is_rejected(self):
        links = [{"href": "https://other.example/file.com", "text": "FILE.COM", "description": []}]
        with self.assertRaisesRegex(fetchCpm80Software.ArchiveError, "different host"):
            fetchCpm80Software.find_link("https://archive.example/index.htm", links, "FILE.COM")

    def test_microsoft_program_is_not_downloaded_from_archive(self):
        links = [
            {
                "href": "Mbasic.com",
                "text": "Mbasic.com",
                "description": ["Microsoft BASIC Interpreter v5.21"],
            }
        ]
        with patch.object(fetchCpm80Software, "read_index", return_value=("http://archive.test/lang.htm", links)):
            with patch.object(fetchCpm80Software, "_download") as download:
                with self.assertRaisesRegex(fetchCpm80Software.ArchiveError, "Microsoft software is not downloaded"):
                    fetchCpm80Software._downloaded_files(
                        "http://archive.test/lang.htm", "Mbasic.com", []
                    )
                download.assert_not_called()

    def test_original_download_and_provenance_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            data = b"archive bytes"
            link = {"href": "packages/tool.zip", "text": "tool.zip", "description": ["Freeware"]}
            archive_path = fetchCpm80Software.preserve_download(
                temporary_directory,
                "https://archive.example/index.html",
                "https://archive.example/packages/tool.zip",
                link,
                data,
                "https://archive.example/license.html",
                ["BIN/TOOL.COM"],
            )
            self.assertEqual(archive_path.read_bytes(), data)
            manifest = json.loads(
                (Path(temporary_directory) / "cpm-resource-manifest.json").read_text(encoding="utf-8")
            )
            record = manifest["resources"][0]
            self.assertEqual(record["source_index_url"], "https://archive.example/index.html")
            self.assertEqual(record["selected_members"], ["BIN/TOOL.COM"])
            self.assertEqual(record["rights_evidence"], "https://archive.example/license.html")
            self.assertEqual(len(record["sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
