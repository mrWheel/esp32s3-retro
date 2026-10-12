import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

extractorPath = Path(__file__).resolve().parents[1] / "tools" / "extractApple2Files.py"
extractorSpec = importlib.util.spec_from_file_location("extractApple2Files", extractorPath)
if extractorSpec is None or extractorSpec.loader is None:
    raise RuntimeError(f"cannot load {extractorPath}")
extractor = importlib.util.module_from_spec(extractorSpec)
sys.modules[extractorSpec.name] = extractor
extractorSpec.loader.exec_module(extractor)


class StubImage:
    def __init__(self, path):
        self.path = Path(path)


class StubVolume:
    writable = True

    def __init__(self, entries=None):
        self.entries = entries or []

    def listFiles(self):
        return self.entries


class ExtractApple2FilesTests(unittest.TestCase):
    def test_main_menu_shows_paths_and_disks_as_empty(self):
        with patch.object(extractor, "setupLineEditing"), \
                patch("builtins.input", side_effect=["0"]), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            result = extractor.main(["extractApple2Files.py"])

        self.assertEqual(result, 0)
        self.assertIn("1 - Source path [<empty>]", output.getvalue())
        self.assertIn("2 - Dest. path [<empty>]", output.getvalue())
        self.assertIn("3 - Source disk [<empty>]", output.getvalue())
        self.assertIn("4 - Dest. disk [<empty>]", output.getvalue())
        self.assertIn("5 - List files on Source", output.getvalue())
        self.assertIn("6 - List files on Destination", output.getvalue())
        self.assertIn("7 - Copy file from Source to Destination", output.getvalue())
        self.assertIn("8 - Remove file from Destination", output.getvalue())

    def test_menu_routes_listing_copy_and_remove_options(self):
        with patch.object(extractor, "setupLineEditing"), \
                patch("builtins.input", side_effect=["5", "6", "7", "8", "0"]), \
                patch.object(extractor, "listFilesOnSource") as list_source, \
                patch.object(extractor, "listFilesOnDestination") as list_destination, \
                patch.object(extractor, "copyFromSource") as copy_from_source, \
                patch.object(extractor, "removeFromDestination") as remove_from_destination, \
                contextlib.redirect_stdout(io.StringIO()):
            result = extractor.main(["extractApple2Files.py"])

        self.assertEqual(result, 0)
        list_source.assert_called_once()
        list_destination.assert_called_once()
        copy_from_source.assert_called_once()
        remove_from_destination.assert_called_once()

    def test_list_files_on_source_numbers_disk_entries(self):
        session = extractor.Session()
        session.source = StubVolume([
            extractor.Entry("HELLO", "BAS", 1, 10),
            extractor.Entry("SUBDIR", "DIR", 1, 10, isDir=True),
            extractor.Entry("WORLD", "TXT", 1, 10),
        ])
        with contextlib.redirect_stdout(io.StringIO()) as output:
            extractor.listFilesOnSource(session)

        self.assertEqual(output.getvalue().splitlines(), ["1. HELLO", "2. WORLD"])

    def test_list_files_on_destination_numbers_disk_entries(self):
        session = extractor.Session()
        session.dest = StubVolume([extractor.Entry("WORK", "TXT", 1, 10)])
        with contextlib.redirect_stdout(io.StringIO()) as output:
            extractor.listFilesOnDestination(session)

        self.assertEqual(output.getvalue().splitlines(), ["1. WORK"])

    def test_number_from_source_path_list_selects_source_image(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_path = Path(temporary_directory) / "source"
            source_path.mkdir()
            (source_path / "first.po").touch()
            (source_path / "second.po").touch()
            loaded = []

            def load_volume(text, _other_image, base_directory=None):
                self.assertIsNone(_other_image)
                loaded.append((text, base_directory))
                return StubImage(text), StubVolume()

            commands = ["1", str(source_path), "3", "2", "0"]
            with patch.object(extractor, "setupLineEditing"), \
                    patch("builtins.input", side_effect=commands), \
                    patch.object(extractor, "loadVolume", side_effect=load_volume), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                result = extractor.main(["extractApple2Files.py"])

            self.assertEqual(result, 0)
            self.assertEqual(Path(loaded[0][0]).name, "second.po")
            self.assertEqual(loaded[0][1], source_path.resolve())
            self.assertIn("1. first.po", output.getvalue())
            self.assertIn("2. second.po", output.getvalue())
            self.assertIn("3 - Source disk [second.po]", output.getvalue())

    def test_number_from_destination_path_list_selects_destination_image(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            destination_path = Path(temporary_directory) / "destination"
            destination_path.mkdir()
            (destination_path / "work.po").touch()
            loaded = []

            def load_volume(text, _other_image, base_directory=None):
                self.assertIsNone(_other_image)
                loaded.append((text, base_directory))
                return StubImage(text), StubVolume()

            commands = ["2", str(destination_path), "4", "1", "0"]
            with patch.object(extractor, "setupLineEditing"), \
                    patch("builtins.input", side_effect=commands), \
                    patch.object(extractor, "loadVolume", side_effect=load_volume), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                result = extractor.main(["extractApple2Files.py"])

            self.assertEqual(result, 0)
            self.assertEqual(Path(loaded[0][0]).name, "work.po")
            self.assertEqual(loaded[0][1], destination_path.resolve())
            self.assertIn("4 - Dest. disk [work.po]", output.getvalue())

    def test_copy_menu_lists_source_files_and_copies_selected_number(self):
        session = extractor.Session()
        entry = extractor.Entry("HELLO", "BAS", 1, 10)
        session.source = StubVolume([entry])
        session.dest = StubVolume()
        with patch("builtins.input", side_effect=["1", ""]), \
                patch.object(extractor, "copyFile") as copy_file, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            extractor.copyFromSource(session)

        copy_file.assert_called_once_with(session.source, session.dest, "HELLO", None)
        self.assertIn("1. HELLO", output.getvalue())

    def test_remove_menu_lists_destination_files_and_removes_selected_number(self):
        session = extractor.Session()
        entry = extractor.Entry("OLD", "BAS", 1, 10)
        session.dest = StubVolume([entry])
        with patch("builtins.input", return_value="1"), \
                patch.object(extractor, "removeFiles") as remove_files, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            extractor.removeFromDestination(session)

        remove_files.assert_called_once_with(session.dest, "OLD")
        self.assertIn("1. OLD", output.getvalue())


if __name__ == "__main__":
    unittest.main()
