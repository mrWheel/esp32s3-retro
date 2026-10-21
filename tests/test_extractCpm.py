import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

repositoryRoot = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repositoryRoot / "tools" / "include"))
sys.path.insert(0, str(repositoryRoot / "tools"))

import diskImageCpm80
import extractCpm


def arguments(image, **options):
    values = dict(image=str(image) if image else None, add=None, name=None, extract=None, remove=None, dest=None)
    values.update(options)
    return SimpleNamespace(**values)


class ExtractCpmTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.image = self.root / "disk.dsk"
        diskImageCpm80.create_image(self.image, profile="LARGE")

    def run_quiet(self, args):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            status = extractCpm.run(args)
        return status, output.getvalue()

    def test_add_list_extract_remove(self):
        host = self.root / "hi.txt"
        host.write_bytes(b"hello\n")
        self.assertEqual(self.run_quiet(arguments(self.image, add=str(host), name="HI.TXT"))[0], 0)
        self.assertIn("HI.TXT", self.run_quiet(arguments(self.image))[1])
        out = self.root / "out"
        out.mkdir()
        self.assertEqual(self.run_quiet(arguments(self.image, extract="HI.TXT", dest=str(out)))[0], 0)
        self.assertTrue((out / "HI.TXT").read_bytes().startswith(b"hello\n\x1a"))
        self.assertEqual(self.run_quiet(arguments(self.image, remove="HI.TXT"))[0], 0)
        self.assertNotIn("HI.TXT", self.run_quiet(arguments(self.image))[1])

    def test_extract_asks_before_overwriting(self):
        host = self.root / "hi.txt"
        host.write_bytes(b"hello\n")
        self.run_quiet(arguments(self.image, add=str(host), name="HI.TXT"))
        out = self.root / "out"
        out.mkdir()
        (out / "HI.TXT").write_bytes(b"old")
        extract = arguments(self.image, extract="HI.TXT", dest=str(out))
        for answer in ("", "n", "anything"):
            with patch("builtins.input", return_value=answer):
                status, output = self.run_quiet(extract)
            self.assertEqual(status, 0)
            self.assertIn("Not overwritten", output)
            self.assertEqual((out / "HI.TXT").read_bytes(), b"old")
        with patch("builtins.input", side_effect=EOFError):
            self.run_quiet(extract)
        self.assertEqual((out / "HI.TXT").read_bytes(), b"old")
        with patch("builtins.input", return_value="y") as ask:
            status, output = self.run_quiet(extract)
        self.assertIn("Overwrite", ask.call_args.args[0])
        self.assertIn("y/N?", ask.call_args.args[0])
        self.assertEqual(status, 0)
        self.assertTrue((out / "HI.TXT").read_bytes().startswith(b"hello\n\x1a"))

    def test_add_asks_before_replacing_a_file_on_the_image(self):
        first = self.root / "first.txt"
        second = self.root / "second.txt"
        first.write_bytes(b"one\n")
        second.write_bytes(b"two\n")
        self.run_quiet(arguments(self.image, add=str(first), name="HI.TXT"))
        for answer in ("", "n"):
            with patch("builtins.input", return_value=answer):
                status, _ = self.run_quiet(arguments(self.image, add=str(second), name="HI.TXT"))
            self.assertEqual(status, 1)
        out = self.root / "out"
        out.mkdir()

        def extracted():
            self.run_quiet(arguments(self.image, extract="HI.TXT", dest=str(out)))
            return (out / "HI.TXT").read_bytes()

        self.assertTrue(extracted().startswith(b"one\n\x1a"))
        with patch("builtins.input", return_value="y") as ask:
            status, _ = self.run_quiet(arguments(self.image, add=str(second), name="HI.TXT"))
        self.assertEqual(status, 0)
        self.assertEqual(ask.call_args.args[0], "Overwrite HI.TXT y/N? ")
        with patch("builtins.input", return_value="y"):
            self.assertTrue(extracted().startswith(b"two\n\x1a"))
        self.assertEqual(self.run_quiet(arguments(self.image))[1].count("HI.TXT"), 1)

    def test_interactive_mode_uses_cpm_opener(self):
        with patch.object(extractCpm.core, "main", return_value=0) as main:
            extractCpm.run(arguments(None))
        main.assert_called_once_with(["extractFiles"], ucsdMode=True, opener=extractCpm.openCpm)


if __name__ == "__main__":
    unittest.main()
