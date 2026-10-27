# CP/M-80 HELP Utility Design

## Purpose

`HELP.ASM` is the 8080 assembly source for `HELP.COM`, an interactive help
reader for the project's CP/M-80 system. It searches `.HLP` files on the
current CP/M drive and user area, displays topic text, and lets the user move
through a topic hierarchy. `HELP.COM` is the executable command; `.HLP` files
are its documentation data.

The maintained source and help text are
[`bootDisks/cpm80/utilities/HELP.ASM`](../../bootDisks/cpm80/utilities/HELP.ASM)
and
[`bootDisks/cpm80/utilities/HELP.HLP`](../../bootDisks/cpm80/utilities/HELP.HLP).
The CP/M-80 system-disk input directory is
[`bootDisks/cpm80/systemDsk/`](../../bootDisks/cpm80/systemDsk/). Keep the
built `HELP.COM` and the matching `HELP.HLP` there so the system-disk builder
includes the intended versions. The separate `HELP.COM` in
`components/cpm80Core/os/utilities/` is an upstream CP/M 3.0 utility binary,
not the executable built from this source.

## Required behavior

### Command line and interaction

- Accept `HELP [file [topic ...]]`. A file name is a CP/M 8.3 basename with
  the `.HLP` extension optional; a drive prefix such as `B:` may be supplied.
- With no arguments, list `.HLP` files from the current drive and user area,
  then display the `HELP>` prompt.
- Accept up to ten command tokens, including the file name. Topic names may
  contain up to twelve characters; file basenames are limited to eight.
- Show the current file/topic path in the interactive prompt.
- Let the user enter a child topic or a unique topic prefix. An empty line
  moves up one level; an empty line at `HELP>` exits to CP/M.
- Prefer an exact topic match to a prefix match. Report ambiguous and
  unknown topics without changing the current selection.
- Support `.NAME` as a relative-navigation shortcut. Search below the current
  topic first; when the first relative component is absent, a sibling-topic
  lookup can be attempted.
- Support topic levels 1 through 9. When a topic is opened, reset the page
  counter. After twenty displayed lines, show `-- More --`; Return continues
  and `Q` stops the current text or topic-list output.
- Close an open help file before returning to CP/M.

The current file-list routine prints each eight-character basename on its
own line. It does not lay file names out in multi-column rows.

### Supported help-file content

The reader supports topic-oriented `.HLP` files:

1. **Plain-text topic files.** A topic marker is recognized anywhere in a line
   and uses `///` followed by a level digit from `1` through `9` and a topic
   name. Both `///1NAME` and `///1 NAME` are accepted. Legacy `///NAME`
   markers whose name starts with an alphabetic character are treated as
   level-one topics. Leading spaces or tabs after the marker and trailing
   spaces or tabs in the topic name are ignored.
2. **Indexed CP/M Plus-style files.** The reader recognizes a binary topic
   index at the start of the file, validates its entries, and then scans the
   text markers to navigate. It does not use index offsets or indexed levels
   to locate or display topics.

Text lines may end in CRLF, LF, or CR. Ctrl-Z (`0x1A`) ends the file. NUL
padding is accepted only when all remaining file bytes are also NUL. Lines
longer than 255 bytes, read failures, malformed detected indexes, and line
counter overflow are reported as errors rather than silently truncated.

The reader does not treat an unmarked text file as a single document. It also
does not interpret `.PA` page directives or strip control-B emphasis bytes.
An `.HLP` extension alone does not guarantee that a file uses a supported
format.

## Implementation

### Program and BDOS interface

The program is written in Digital Research ASM syntax for the Intel 8080 and
uses only 8080 instructions. `ORG 100H` places the transient program at the
standard CP/M command address. The program initializes a private stack in its
allocated memory.

CP/M services are called through BDOS entry point 5. The main interfaces are:

- Function 9 for `$`-terminated messages and function 2 for individual
  console characters.
- Function 10 for buffered interactive command input and function 1 for the
  single-character response at `-- More --`.
- FCB functions 15 and 16 to open and close a help file, and function 20 to
  read 128-byte sequential records. Function 26 sets the DMA address before
  each record read.
- Functions 17 and 18 to search the current directory for `.HLP` files.
  Function 31 reads the DPB so the directory listing can skip duplicate
  extents.

The implementation is a single assembly module. Its main routines divide
responsibility as follows:

- Startup parses the CP/M command tail and enters batch-open or interactive
  mode.
- `RUN`, `ABSOL`, `NAME`, `OPEN`, `SEARCH`, and `CHOOSE` resolve a file and
  topic path, match exact names or unique prefixes, and reopen the selected
  physical line.
- `BODY`, `BL1`, and `CHILD` display a topic's text and its child topics.
- `LIST` searches the current directory for `.HLP` files.
- `RLINE`, `BYTE`, and `RECORD` read bounded text lines from sequential
  records, normalize line endings, and track physical line numbers.

The reader rescans the help file instead of keeping a complete index or
document in memory. It saves the selected physical line number and reopens
the file to reach that occurrence, so duplicate marker text does not select
the wrong topic.

### Indexed-file recognition

An indexed entry occupies 16 bytes: twelve printable name bytes, two offset
bytes, a level byte, and one remaining byte. A terminator has `$` followed
by eleven spaces in the name field. The first entry must have a plausible
offset and a level from 1 through 9 for the file to be recognized as indexed.
After recognition, subsequent entries are checked for valid names and
offsets until the terminator is found. Indexed levels and offsets are not
used for navigation; the reader determines topic hierarchy from the text
markers.

If the initial bytes do not form a plausible indexed entry or terminator,
the file is read as plain text. Once a binary index has been recognized,
invalid entries or a missing terminator are reported as a read/index error.

### Input, display, and storage limits

- CP/M filename basename: at most 8 characters; extension: `.HLP`.
- Topic names: at most 12 characters.
- Command tail: at most 10 tokens, including the file name.
- Topic depth: levels 1 through 9.
- Interactive input buffer: 127 characters.
- Input text line: at most 255 bytes, stored NUL-terminated.
- Physical line counter: 16 bits.
- File reads: 128-byte CP/M records.
- Paging threshold: 20 displayed lines. Topic names are counted as displayed
  lines too.

## Building `HELP.COM`

Use the CP/M-80 `ASM.COM` and `LOAD.COM` utilities on a writable drive. Put
`HELP.ASM` on that drive and assemble it with the extension omitted:

```text
A>ASM HELP
A>LOAD HELP
A>DIR HELP.*
```

The assembler produces `HELP.HEX`; `LOAD` converts that Intel HEX file into
the executable `HELP.COM`. Resolve all assembler errors before running
`LOAD`. Do not use the CP/M-86 `ASM86` or `GENCMD` tools for this 8080 source.

Copy the verified `HELP.COM` into `bootDisks/cpm80/systemDsk/`. Keep the
matching help text as `HELP.HLP` in that directory. From the repository root,
rebuild the CP/M-80 system disk when the executable or help text should be
included in the generated image:

```sh
python3 tools/createSystemDsk.py --os cpm80 --profile SMALL
```

Use `--profile LARGE` instead when building the LARGE profile. The durable
inputs are in `bootDisks/cpm80/systemDsk/`; the generated image under
`littlefs/cpm80/` is a build output and may be replaced by the next build.

## Verification

After building, verify the executable on the intended CP/M-80 system. At
minimum, check:

1. Starting `HELP` lists `.HLP` files and displays the interactive prompt.
2. Opening a topic file displays its first level-one topic and available
   subtopics.
3. Selecting a child topic and pressing Return navigates down and back up;
   Return at `HELP>` exits.
4. Exact names, unique prefixes, ambiguous prefixes, unknown topics, and
   `.NAME` relative navigation behave as described.
5. A long topic and a long child list exercise the paging prompt; Return
   continues and `Q` stops the current output.
6. Plain-text marker files and a supported indexed `.HLP` file are readable;
   malformed indexes and overlong lines report errors.
7. An unmarked legacy text file is not mistaken for a topic document.

An assembly or system-disk build alone does not verify behavior in the
emulator or on an ESP32-S3. Record separately whether verification used a
host test, the emulator, or physical hardware.
