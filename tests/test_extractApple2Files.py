import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "include"))

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
        self.saved = False

    def save(self):
        self.saved = True


class StubVolume:
    writable = True

    def __init__(self, entries=None):
        self.entries = entries or []

    def listFiles(self):
        return self.entries


class StubPascalVolume(extractor.PascalVolume):
    def __init__(self):
        self.image = StubImage("destination.po")
        self.imported = None

    def importFile(self, outPath, exported):
        self.imported = (outPath, exported)
        return outPath


class StubPascalSource(extractor.PascalVolume):
    def __init__(self, entry, exported):
        self.entry = entry
        self.exported = exported

    def listFiles(self):
        return [self.entry]

    def exportFile(self, entry):
        self.assertEntry = entry
        return self.exported


class ExtractApple2FilesTests(unittest.TestCase):
    def test_main_menu_shows_paths_and_disks_as_empty(self):
        with patch.object(extractor, "setupLineEditing"), \
                patch("builtins.input", side_effect=["0"]), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            result = extractor.main(["extractApple2Files.py"])

        self.assertEqual(result, 0)
        self.assertIn("1 - Source path [<empty>]", output.getvalue())
        self.assertIn("2 - Source disk [<empty>]", output.getvalue())
        self.assertIn("3 - Dest. path [<empty>]", output.getvalue())
        self.assertIn("4 - Dest. disk [<empty>]", output.getvalue())
        self.assertIn("5 - List files on Source", output.getvalue())
        self.assertIn("6 - List files on Destination", output.getvalue())
        self.assertIn("7 - Get one specific file From Source", output.getvalue())
        self.assertIn("8 - Copy file from Source to Destination", output.getvalue())
        self.assertIn("9 - Remove file from Destination", output.getvalue())
        self.assertIn("10 - Add one specific file (full path) to Destination", output.getvalue())
        self.assertIn("Q - Quit", output.getvalue())
        menuLines = [
            line for line in output.getvalue().splitlines()
            if line.startswith(("1 - ", "2 - ", "3 - ", "4 - ", "5 - ", "6 - ",
                                "7 - ", "8 - ", "9 - ", "10 - ", "Q - "))
        ]
        self.assertEqual(menuLines, [
            "1 - Source path [<empty>]",
            "2 - Source disk [<empty>]",
            "3 - Dest. path [<empty>]",
            "4 - Dest. disk [<empty>]",
            "5 - List files on Source (L)",
            "6 - List files on Destination",
            "7 - Get one specific file From Source",
            "8 - Copy file from Source to Destination (C)",
            "9 - Remove file from Destination",
            "10 - Add one specific file (full path) to Destination (A)",
            "Q - Quit",
        ])

    def test_menu_routes_listing_copy_and_remove_options(self):
        with patch.object(extractor, "setupLineEditing"), \
                patch("builtins.input", side_effect=["5", "6", "7", "8", "9", "Q"]), \
                patch.object(extractor, "listFilesOnSource") as list_source, \
                patch.object(extractor, "listFilesOnDestination") as list_destination, \
                patch.object(extractor, "getOneFileFromSource") as get_one_file, \
                patch.object(extractor, "copyFromSource") as copy_from_source, \
                patch.object(extractor, "removeFromDestination") as remove_from_destination, \
                contextlib.redirect_stdout(io.StringIO()):
            result = extractor.main(["extractApple2Files.py"])

        self.assertEqual(result, 0)
        list_source.assert_called_once()
        list_destination.assert_called_once()
        get_one_file.assert_called_once()
        copy_from_source.assert_called_once()
        remove_from_destination.assert_called_once()

    def test_menu_option_10_routes_to_add_host_file(self):
        session = extractor.Session()
        with patch.object(extractor, "setupLineEditing"), \
                patch("builtins.input", side_effect=["10", "Q"]), \
                patch.object(extractor, "addHostFile") as add_host_file, \
                patch.object(extractor, "Session", return_value=session), \
                contextlib.redirect_stdout(io.StringIO()):
            result = extractor.main(["extractApple2Files.py"])

        self.assertEqual(result, 0)
        add_host_file.assert_called_once_with(session)

    def test_add_host_text_file_stores_ucsd_text_on_pascal_destination(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            host_path = Path(temporary_directory) / "LOANCALC.TEXT"
            host_path.write_bytes(
                b"PROGRAM LOANCALC;\n  WRITELN('OK');\n"
            )
            destination = StubPascalVolume()
            session = extractor.Session()
            with patch.object(session, "requireDestination", return_value=destination), \
                    patch.object(extractor, "promptPath", return_value=str(host_path)), \
                    patch("builtins.input", return_value=""), \
                    contextlib.redirect_stdout(io.StringIO()):
                extractor.addHostFile(session)

        self.assertIsNotNone(destination.imported)
        name, exported = destination.imported
        self.assertEqual(name, "LOANCALC.TEXT")
        self.assertEqual(exported.pascalKind, 3)
        self.assertEqual(exported.data[:1024], bytes(1024))
        expectedText = (
            b"PROGRAM LOANCALC;\r"
            b"\x10\x22WRITELN('OK');\r"
        )
        self.assertEqual(exported.data[1024:1024 + len(expectedText)], expectedText)
        self.assertTrue(destination.image.saved)

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

            commands = ["1", str(source_path), "2", "2", "Q"]
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
            self.assertIn("2 - Source disk [second.po]", output.getvalue())

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

            commands = ["3", str(destination_path), "4", "1", "Q"]
            with patch.object(extractor, "setupLineEditing"), \
                    patch("builtins.input", side_effect=commands), \
                    patch.object(extractor, "loadVolume", side_effect=load_volume), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                result = extractor.main(["extractApple2Files.py"])

            self.assertEqual(result, 0)
            self.assertEqual(Path(loaded[0][0]).name, "work.po")
            self.assertEqual(loaded[0][1], destination_path.resolve())
            self.assertIn("4 - Dest. disk [work.po]", output.getvalue())

    def test_get_one_source_file_extracts_ucsd_text_to_destination_path(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            destination_path = Path(temporary_directory)
            entry = extractor.Entry("HELLO.TEXT", "TEXT", 2, 1040)
            host_data = b"PROGRAM HELLO;\n  WRITELN('OK');\n"
            import extractUcsd
            source = StubPascalSource(
                entry,
                extractor.Exported(extractUcsd.hostTextToUcsd(host_data), 0x0C, 0, 3),
            )
            session = extractor.Session()
            session.source = source
            session.pathDestination = destination_path
            with patch("builtins.input", side_effect=["1", "GREET.TEXT"]), \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                extractor.getOneFileFromSource(session)

            self.assertEqual((destination_path / "GREET.TEXT").read_bytes(), host_data)
            self.assertIs(source.assertEntry, entry)
            self.assertIn("extracted 'HELLO.TEXT' -> '", output.getvalue())
            self.assertIn("GREET.TEXT' (32 bytes)", output.getvalue())

    def test_ucsd_get_one_source_file_uses_destination_path(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_path = Path(temporary_directory) / "source"
            destination_path = Path(temporary_directory) / "destination"
            source_path.mkdir()
            destination_path.mkdir()
            entry = extractor.Entry("HELLO.TEXT", "TEXT", 1, 1040)
            host_data = b"PROGRAM HELLO;\n"
            import extractUcsd
            source = StubPascalSource(
                entry,
                extractor.Exported(extractUcsd.hostTextToUcsd(host_data), 0x0C, 0, 3),
            )
            session = extractor.Session(ucsdMode=True)
            session.source = source
            session.pathSource = source_path
            session.pathDestination = destination_path
            with patch("builtins.input", side_effect=["1", ""]), \
                    contextlib.redirect_stdout(io.StringIO()):
                extractor.getOneFileFromSource(session)

            self.assertEqual((destination_path / "HELLO.TEXT").read_bytes(), host_data)
            self.assertFalse((source_path / "HELLO.TEXT").exists())

    def test_ucsd_add_host_file_resolves_relative_to_destination_path(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            destination_path = Path(temporary_directory)
            host_file = destination_path / "HOST.DATA"
            host_file.write_bytes(b"host data")
            destination = StubPascalVolume()
            session = extractor.Session(ucsdMode=True)
            session.pathDestination = destination_path
            with patch.object(session, "requireDestination", return_value=destination), \
                    patch("builtins.input", side_effect=["HOST.DATA", ""]), \
                    contextlib.redirect_stdout(io.StringIO()):
                extractor.addHostFile(session)

        self.assertIsNotNone(destination.imported)
        name, exported = destination.imported
        self.assertEqual(name, "HOST.DATA")
        self.assertEqual(exported.data, b"host data")

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
