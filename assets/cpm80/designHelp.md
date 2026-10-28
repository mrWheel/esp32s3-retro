# CP/M-80 HELP Utility Design

## Purpose

`HELP.ASM` is the 8080 assembly source for `HELP.COM`, an interactive help
reader for the project's CP/M-80 system. It searches `.HLP` files on the
current CP/M drive and user area, displays topic text, and lets the user move
through a topic hierarchy. `HELP.COM` is the executable command; `.HLP` files
are its documentation data.

`HELP.ASM` (HELP 3.3, banner `CP/M Help System 3.3`) must provide the same
functionality, HLP formats, messages, and interactive navigation as the
CP/M-86 `HELP.A86` described in
[`assets/cpm86/designHelp.md`](../cpm86/designHelp.md). The two programs differ
only in the CPU, assembler, and CP/M interface (BDOS call 5 instead of
`INT 224`, `.COM` instead of `.CMD`, and no segment registers). When the two
documents disagree on behavior, `HELP.A86` is the reference.

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
  moves up one level; an empty line at `HELP>` exits to CP/M. At the `HELP>`
  level a bare name is a help file; inside a file or topic a bare name is a
  direct child of the current topic. Names are case-insensitive and may
  contain only printable non-blank ASCII characters.
- Entering a topic shows its text followed by its children. Moving up with
  Return lists only the children of the new current topic, without its text.
  Returning from the file level re-displays the file list.
- If the first token is an explicit root topic name, use it; otherwise the
  first level-one topic is the default root.
- Prefer an exact topic match to a prefix match. Report ambiguous and
  unknown topics without changing the current selection.
- Support `.NAME` as a relative-navigation shortcut. Search below the current
  topic first; when the first relative component is absent, a sibling-topic
  lookup can be attempted (not after an ambiguity, an I/O error, or for a
  later component).
- Support topic levels 1 through 9. When a topic or file is opened, start its
  text display with a fresh page count. After twenty displayed text lines,
  show `-- More -- RETURN=continue, Q=quit`; any key other than `Q`
  (case-insensitive) continues, and `Q` skips the rest of the body but the
  topic's child choices are still presented. File and topic listings do not
  count as text lines and never trigger `-- More --`.
- Allow Ctrl-C to stop HELP while entering a command at any navigation level
  and at the `-- More --` input prompt. Do not poll for Ctrl-C during text,
  file-list, or topic-list output. Close an open help file before exiting.
- Show file and topic names in aligned columns suitable for a 79-column
  display. File names are printed intact as eight characters followed by two
  spaces, eight per row. Topic names use ten-character slots, eight slots per
  row; a name of ten or more characters takes two slots and is never split.
- Report an empty file list with `No .HLP files found.` and a topic without
  children with `No subtopics.` (not for legacy documents).
- Report errors with these messages: `Invalid name or input too long.`,
  `Help file not found.`, `Help topic not found.`,
  `Ambiguous topic; type more characters.`, `No current topic.` (a `.NAME`
  with nothing selected), and
  `Read error, bad index, or text limit exceeded.`

### Supported help-file content

The reader supports topic-oriented and older, unindexed help files:

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
3. **Legacy plain-text files with no topic markers.** If the user opens a
   single file without specifying a topic and no topic markers are found,
   display the whole file as one document. Strip control-B (`0x02`) emphasis
   delimiters and treat an exact `.PA` line as a blank line. Ctrl-Z (`0x1A`)
   ends the document. A request that names a topic in a file without markers
   reports `Help topic not found.` instead of printing the whole file. No
   `No subtopics.` message is printed for a legacy document.

Text lines may end in CRLF, LF, or CR. Ctrl-Z (`0x1A`) ends the file. NUL
padding is accepted only when all remaining file bytes are also NUL. Lines
longer than 255 bytes, read failures, malformed detected indexes, and line
counter overflow are reported as errors rather than silently truncated.
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
- Function 11 (console status) followed by function 1 (console input) for all
  keyboard input, including the single-character response at `-- More --`.
  Function 11 moves a pending key into the BDOS type-ahead byte and function 1
  returns it, so a key typed during output is not lost. (Function 6 must not
  be used: it bypasses that byte.) Function 1 echoes the key, so the program
  does not echo; it only repairs the echo of keys that are not stored. It edits
  the line itself instead of using function 10: backspace (`08H`/`7FH`)
  deletes, Ctrl-U clears the line, other control keys are ignored, and the
  bell (`07H`) sounds at the 127-character limit.
- A Ctrl-C byte (`03H`) closes the open file and exits immediately. Input is
  only awaited at the command prompt and at `-- More --`; output loops never
  poll the console.
- A jump to `0000H` (warm boot) to return to CP/M.
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
- `LIST` searches the current directory for `.HLP` files with a separate
  directory DMA buffer, using the DPB extent mask to list only extent zero of
  each file. File names are printed intact in fixed columns.
- `RLINE`, `BYTE`, and `RECORD` read bounded text lines from sequential
  records, normalize line endings, and track physical line numbers.
- `MARK` recognizes `///` markers, legacy output drops
  control-B bytes and an exact `.PA` line, and the topic-name printer places
  names in ten-character slots.
- `READIN` and `GETKEY` read console input; Ctrl-C is only seen while waiting
  for command or paging input.

The reader rescans the help file instead of keeping a complete index or
document in memory. It saves the selected physical line number and reopens
the file to reach that occurrence, so duplicate marker text does not select
the wrong topic.

### Indexed-file recognition

An indexed entry occupies 16 bytes: twelve printable name bytes, two offset
bytes, a level byte, and one remaining byte. A terminator has `$` followed
by eleven spaces in the name field (a name merely starting with `$` is not a
terminator). Name bytes must be printable (`20H`..`7EH`). The first entry must
have a second offset byte below 128 and a level from 1 through 9 for the file
to be recognized as indexed. After recognition, subsequent entries (eight per
record, validated across records) are checked for valid names and offsets
until the terminator is found. Indexed levels and offsets are not
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
- Tokens are stored upper-case in fixed 13-byte slots.
- Interactive input buffer: 127 characters.
- Input text line: at most 255 bytes, stored NUL-terminated.
- Physical line counter: 16 bits; a wrap to zero is reported as an error.
- File reads: 128-byte CP/M records.
- Paging threshold: 20 displayed text lines; file and topic listings are not
  counted.
- Reserved stack: at least 256 bytes.

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
python3 tools/createSystemDsk.py --os cpm80 --profile LARGE
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
5. A long topic exercises the paging prompt: Return continues, `Q` skips the
   rest of the text but still shows the child topics, a newly opened file or
   topic starts with a fresh page count, and topic listings never trigger
   `-- More --`.
6. Plain-text marker files and a supported indexed `.HLP` file are readable;
   malformed indexes and overlong lines report errors.
7. An unmarked legacy text file opened by file name alone is displayed whole,
   with control-B bytes stripped, `.PA` lines blank, and Ctrl-Z ending the
   text; naming a topic in such a file reports `Help topic not found.`
8. Ctrl-C exits (closing the file) at the `HELP>` prompt and at `-- More --`,
   and is not polled during output.

An assembly or system-disk build alone does not verify behavior in the
emulator or on an ESP32-S3. Record separately whether verification used a
host test, the emulator, or physical hardware.

## Verification record

The host test `testCpm80Help` in [`tests/hostTests.c`](../../tests/hostTests.c)
(ctest `cpm80Help`) assembles `HELP.ASM` in the emulated CP/M-80 with the stock
`ASM.COM` and `LOAD.COM`, asserts that the built executable is byte-identical
to `bootDisks/cpm80/systemDsk/HELP.COM`, and exercises the items above as far as the test covers them:
file listing and prompt, root and child navigation with up/exit, exact/prefix/
ambiguous/unknown topics and `.NAME` sibling navigation, paging with Return
and Q, legacy `///NAME` markers, a recognized two-entry indexed file, drive
prefixes, a malformed index, an overlong line, and an unmarked text file.
This is host-test verification (2026-10-27); emulator and hardware
verification remain separate.
