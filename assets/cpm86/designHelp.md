# CP/M-86 HELP Utility Design

## Purpose

`HELP.A86` is the 8086 assembly source for `HELP.CMD`, an interactive help
reader for the project's CP/M-86 system. It searches `.HLP` files on the
current CP/M drive and user area, displays their documentation, and provides
topic navigation. `HELP.CMD` is the executable CP/M-86 command; it is not the
help data file. The source identifies itself as HELP 3.3-86; the startup
banner reads `CP/M-86 Help System 3.3`. It uses the same HLP formats and
interactive navigation as the CP/M-80 HELP 3.3.

The source is maintained in
[`bootDisks/cpm86/utilities/HELP.A86`](../../bootDisks/cpm86/utilities/HELP.A86).
The CP/M-86 system-disk input directory is
[`bootDisks/cpm86/systemDsk/`](../../bootDisks/cpm86/systemDsk/). Keep the
`HELP.A86` source and built `HELP.CMD` in that directory synchronized with
the maintained utility source. The disk builder includes files from the
system-disk input directory.

## Required behavior

### Command line and interaction

- Accept `HELP [file [topic ...]]`. The file argument is a CP/M 8.3 basename;
  the `.HLP` extension may be omitted. An optional drive prefix such as `B:`
  is accepted.
- With no arguments, list the `.HLP` files on the current drive and user area,
  then display the `HELP>` prompt.
- Accept up to ten command tokens, with topic names up to twelve characters.
- Display the current file and topic path in the interactive prompt.
- Allow users to enter a child topic, use a unique topic prefix, or press
  Return to move up one level. Return at `HELP>` exits the program. At the
  `HELP>` level a bare name is a help file; inside a file or topic a bare name
  is a direct child of the current topic. Names are case-insensitive and may
  contain only printable non-blank ASCII characters.
- Entering a topic shows its text followed by its children. Moving up with
  Return lists only the children of the new current topic, without its text.
  Returning from the file level re-displays the file list.
- If the first token is an explicit root topic name, use it; otherwise the
  first level-one topic is the default root.
- Prefer an exact topic name to a matching prefix. Report ambiguous and
  unknown topics without changing the current selection.
- Support `.NAME` as a relative navigation shortcut: search for a child first,
  then try a sibling when appropriate.
- Display up to nine topic levels. When a topic or file is opened, start its
  text display with a fresh page count. Show the prompt
  `-- More -- RETURN=continue, Q=quit` only while displaying long text, not
  while printing file or topic choices. Any key other than `Q` continues; `Q`
  (case-insensitive) skips the rest of the body, but the topic's child choices
  are still presented.
- Allow Ctrl-C to stop HELP while entering a command at any navigation level
  and at the `-- More --` input prompt. Do not poll for Ctrl-C during text,
  file-list, or topic-list output. Close an open help file before exiting.
- Show file and topic names in aligned columns suitable for a 79-column
  display. Do not split names: a topic longer than nine characters occupies
  additional column space, and a filename is always printed intact. File
  names are printed as eight characters followed by two spaces, eight per row.
  Topic names use ten-character slots, eight slots per row; a name of ten or
  more characters takes two slots.
- Report an empty file list with `No .HLP files found.` and a topic without
  children with `No subtopics.` (not for legacy documents).
- Report errors with these messages: `Invalid name or input too long.`,
  `Help file not found.`, `Help topic not found.`,
  `Ambiguous topic; type more characters.`, `No current topic.` (a `.NAME`
  with nothing selected), and
  `Read error, bad index, or text limit exceeded.`

### Supported help-file content

HELP must support both topic-oriented files and older, unindexed help text:

1. **Plain-text topic files.** Topic markers use `///` followed by a level
   digit `1` through `9` and a topic name, with either `///1NAME` or
   `///1 NAME` spelling. The marker is recognized anywhere in a line, as in
   CP/M Plus. The older `///NAME` spelling, accepted only when the name starts
   with a letter, is treated as a level-one topic. Trailing blanks and tabs
   are trimmed from the name. Topic text follows its marker and ends at the
   next topic marker or end of file.
2. **Indexed CP/M Plus-style HLP files.** The implementation recognizes and
   validates the indexed records before reading the topic text. It uses the
   text markers for navigation rather than relying on index offsets or level
   metadata.
3. **Legacy plain-text files with no topic markers.** If the user opens a
   single file without specifying a topic and no topic markers are found,
   display the whole file as one document. Strip control-B (`0x02`) emphasis
   delimiters and treat an exact `.PA` line as a blank line. Ctrl-Z (`0x1A`)
   ends the document.

Recognize CRLF, LF, and CR line endings. Accept trailing NUL padding only
when no non-NUL data follows it. Report overlong lines, malformed indexed
records, read failures, and other invalid input instead of silently displaying
truncated content. The `.HLP` extension alone does not guarantee that a file
uses a supported format.

## Implementation

### Program and BDOS interface

The program is written in Digital Research ASM-86 syntax for the 8086/8088.
It uses the CP/M-86 small model: code starts at `CS:0`, data starts at
`DS:100H`, and a reserved stack is initialized at startup. CP/M-86 system
calls are made with `INT 224`; the `HSYS` wrapper preserves working registers
and segment registers around calls. `HPRINT` uses BDOS function 9 for
`$`-terminated messages, and `HPUTC` uses function 2 for individual characters.

The implementation uses the BDOS FCB open, close, sequential-read, console,
and directory-search services. The directory search supplies the `.HLP` list;
the file reader sets the DMA buffer before each 128-byte sequential record
read. The DPB query uses BDOS function 31, whose result is a far pointer in
`ES:BX`; the extent mask is read from `ES:BX+4`. Program exit uses function 0.

The source is organized as a single assembly module:

- Startup parses the command tail and selects batch-open or interactive mode.
- `HRUN`/`HABSOL` resolve the requested file and topic path.
- `HNAME`, `HOPEN`, and the FCB/DMA routines validate filenames and stream
  records from the selected help file.
- `HMARK`, `HSEARCH`, and `HCHOOSE` scan topic markers, match exact names or
  unique prefixes, and reopen the file to retrieve the selected physical line.
- `HBODY` and `HBL1` display text; `HCHILD` displays the next-level topics.
- `HLIST` searches the current directory for `.HLP` files with BDOS search
  first/next (functions 17 and 18) using a separate directory DMA buffer, so
  command input and file buffers are preserved. It queries the DPB (via
  `HDPB86`) for the extent mask and lists only extent zero of each file, so
  large files are not listed more than once.
- `HPRTOP` and the file-list loop keep names intact and place them in fixed
  display columns.
- `HREADIN`, `HGETKEY`, and `HCHECK` read console input and poll for Ctrl-C.
  Polling is used only while waiting for interactive command or paging input;
  line input supports backspace, delete, and Ctrl-U.

The reader streams and rescans the file instead of keeping a complete help
file or topic index in memory. The physical line number is used to find the
selected marker again, so duplicate marker text does not make the reader seek
to the wrong occurrence.

The console poll uses BDOS function 11 (console status) and function 6
(direct console I/O) with `DL=FFH`. A Ctrl-C byte (`03H`) exits immediately.
Other available characters are queued in arrival order so polling does not
silently consume input intended for the current command. The queue is bounded
at 128 bytes; polling pauses while it is full. The file and topic display
loops do not poll the console, so Ctrl-C is not intercepted during scrolling
output.

### Indexed-file recognition

The first 128-byte record is checked for a plausible indexed entry. An entry
is 16 bytes: twelve printable name bytes (`20H`..`7EH`), two offset bytes, and
a level byte. For detection, the second offset byte must be below 128 and the
level byte must be 1 through 9; once an index is detected, later entries are
validated for name and offset only, and the level is ignored. The index ends
with an entry whose name is `$` followed by eleven spaces (a name merely
starting with `$` is not a terminator). Entries are validated across records,
eight per record. The offsets and indexed levels are not
used for navigation; topic text is still found by scanning its `///` markers.
If the first record is not a valid index entry and terminator, it is treated
as the start of ordinary text.

### Legacy text handling

When the initial scan reaches the end of a file without finding a valid topic
marker, the legacy fallback is permitted only for a command containing just
the filename. HELP reopens the file and displays it as one document. This
prevents a misspelled topic request from silently becoming a request to print
an unrelated whole file.

Legacy rendering is handled as each line is read: an exact `.PA` line emits
no characters before the normal line ending, and control-B bytes are skipped.
The file reader stops at Ctrl-Z. Legacy documents have no topic tree, so HELP
does not print a “No subtopics” message for them.

### Input and display limits

- CP/M filename basename: at most 8 characters; extension: `.HLP`.
- Topic names: at most 12 characters.
- Command tail: at most 10 tokens.
- Topic depth: levels 1 through 9.
- Interactive command line: at most 127 characters; excess keystrokes ring
  the bell.
- Tokens contain only characters `21H`..`7EH` and are stored upper-case in
  fixed 13-byte slots.
- Input text line: at most 255 bytes, stored NUL-terminated.
- Line counter: 16 bits; a file whose line count wraps to zero is reported as
  an error.
- Reserved stack: 512 bytes.
- File reads: 128-byte CP/M records.
- Paging threshold: 20 displayed text lines; file and topic listings do not
  increment the text page counter.
- Pending console input: 128 queued characters while HELP waits for input.

## Building `HELP.CMD`

ASM-86 assembles `.A86` source into hexadecimal `.H86` records.
GENCMD converts `.H86` records into the executable `.CMD`; GENCMD does not
assemble source code. Build the source with the CP/M-86 ASM-86 and GENCMD
programs, preferably on a writable work disk:

```text
B>A:ASM86 HELP
B>A:GENCMD HELP
B>DIR HELP.*
```

Here ASM86 and GENCMD are on drive A:, while `HELP.A86` is the current file
on writable drive B:. The commands create `HELP.H86`, assembler output such
as `HELP.LST` and `HELP.SYM`, and then `HELP.CMD` on B:. Correct every
assembler error before running GENCMD. Do not give the source extension in
these commands, and do not add the `8080` option: this is 8086 code with
separate code and data segments.

After building, copy the new `HELP.CMD` into the CP/M-86 system-disk source
directory, replacing its older executable only after verifying the build.
Rebuild the system disk from the repository root when the executable is to
be included in the generated image:

```sh
python3 tools/createSystemDsk.py --os cpm86 --profile LARGE
```

The generated `littlefs/cpm86/system.dsk` is a build output and may be
overwritten by the next system-disk build. The checked-in source directory
`bootDisks/cpm86/systemDsk/` is the durable input for that build.

## Verification

After building, verify that `HELP.CMD` exists on the destination disk, start
it in CP/M-86, and check at least:

1. The file list and interactive `HELP>` prompt.
2. Opening an ordinary `///` topic file, navigating into a child, and using
   Return to go back.
3. Case-insensitive exact topic matching, a unique prefix, an ambiguous
   prefix, and an unknown topic.
4. Opening a legacy text file with control-B emphasis, `.PA` page breaks,
   and a Ctrl-Z end marker.
5. Paging on a long text body, confirming a newly opened file or topic starts
   with a fresh page count, topic listings do not trigger `-- More --`, `Q`
   at `-- More --` still shows the child topics, and output scrolling does
   not poll for Ctrl-C.
6. Opening a supported indexed HLP file and reporting a malformed index or
   unreadable file as an error.

An assembly or disk-image build alone does not verify behavior on an actual
CP/M-86 system. Record separately whether verification used the emulator or
real hardware.
