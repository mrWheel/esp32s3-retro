"""UCSD Pascal disk operations; menu options 7 and 10 use Dest. Path."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import extractApple2Files as core

TEXT_PAGE = 1024
DLE = 0x10
KIND_BY_SUFFIX = {".TEXT": 3, ".CODE": 2, ".DATA": 5}
KIND_BY_OPTION = {"text": 3, "code": 2, "data": 5}


def hostTextToUcsd(data):
    """Plain host text (LF/CRLF) to a UCSD TEXT file: 1024 byte header page, CR lines, DLE indent, NUL padded pages."""
    lines = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    pages = []
    page = bytearray()
    for line in lines:
        indent = len(line) - len(line.lstrip(b" "))
        indent = min(indent, 223)
        encoded = (bytes([DLE, 32 + indent]) if indent >= 2 else b" " * indent) + line[indent:] + b"\r"
        if len(encoded) > TEXT_PAGE - 1:
            raise core.ToolError("a text line is too long for a UCSD text page")
        if len(page) + len(encoded) > TEXT_PAGE - 1:
            pages.append(bytes(page).ljust(TEXT_PAGE, b"\0"))
            page = bytearray()
        page += encoded
    if page:
        pages.append(bytes(page).ljust(TEXT_PAGE, b"\0"))
    return bytes(TEXT_PAGE) + b"".join(pages)


def ucsdTextToHost(data):
    out = bytearray()
    body = data[TEXT_PAGE:]
    for start in range(0, len(body), TEXT_PAGE):
        page = body[start:start + TEXT_PAGE].rstrip(b"\0")
        i = 0
        while i < len(page):
            c = page[i]
            if c == DLE and i + 1 < len(page):
                out += b" " * (page[i + 1] - 32)
                i += 2
                continue
            out.append(0x0A if c == 0x0D else c)
            i += 1
    return bytes(out)


def add_arguments(parser):
    parser.add_argument("--type", choices=sorted(KIND_BY_OPTION), help="file kind (default: from the file suffix, else data)")
    parser.add_argument("--raw", action="store_true", help="add/extract bytes unchanged (no UCSD text conversion)")


def openPascal(imagePath):
    volume = core.openVolume(core.DiskImage(imagePath))
    if not isinstance(volume, core.PascalVolume):
        raise core.ToolError(f"{imagePath}: not a UCSD Pascal volume")
    return volume


def run(arguments):
    if not arguments.image and not (arguments.add or arguments.remove or arguments.extract):
        return core.main(["extractFiles"], ucsdMode=True)
    try:
        volume = openPascal(arguments.image)
        if arguments.add:
            hostPath = Path(arguments.add).expanduser()
            if not hostPath.is_file():
                raise core.ToolError(f"file not found: {hostPath}")
            name = arguments.name or hostPath.name
            kind = KIND_BY_OPTION.get(arguments.type) or KIND_BY_SUFFIX.get(hostPath.suffix.upper(), 5)
            data = hostPath.read_bytes()
            if kind == 3 and not arguments.raw:
                data = hostTextToUcsd(data)
            stored = volume.importFile(name, core.Exported(data, 0x0C, 0, kind))
            volume.image.save()
            print(f"Added {hostPath} as {stored} ({len(data)} bytes) to {volume.name}:")
        elif arguments.remove:
            entry = core.findEntry(volume, arguments.remove)
            if entry is None:
                raise core.ToolError(f"'{arguments.remove}' not found")
            volume.removeFile(entry)
            volume.image.save()
            print(f"Removed {entry.path}")
        elif arguments.extract:
            entry = core.findEntry(volume, arguments.extract)
            if entry is None:
                raise core.ToolError(f"'{arguments.extract}' not found")
            exported = volume.exportFile(entry)
            data = ucsdTextToHost(exported.data) if exported.pascalKind == 3 and not arguments.raw else exported.data
            target = Path(arguments.dest or ".").expanduser() / entry.path
            target.write_bytes(data)
            print(f"Extracted {entry.path} to {target}")
        else:
            core.printListing(volume)
    except core.ToolError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0
