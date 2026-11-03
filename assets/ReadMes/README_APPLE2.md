# Apple II

[← Back to the main README](../../README.md)

## What is it?

Choose **4** in the System Menu to explore a text-focused Apple II with ProDOS and BASIC. List the files on a volume, load compatible programs, and write or run BASIC programs using your USB terminal.

The supplied ProDOS system disk starts BASIC. Try `PRINT 2+3` at the `]` prompt, or `CATALOG` to explore a disk. The original Apple II profile supports 40-column text and an 80-column expansion-card mode; `PR#0` and `PR#3` select those text outputs.

## How faithful is the experience?

Apple software runs on an emulated 6502 with Apple memory, keyboard and disk behaviour. This preserves much of the text-programming experience, including BASIC and disk access. The 80-column expansion uses a compatible interface with project-written firmware; it is not a complete reproduction of an original card.

The display is rendered as terminal text. Graphics are not rendered, and games or programs needing graphics, special keys, unusual peripherals or protected disk formats should not be assumed compatible. The supplied experience centres on ProDOS; disk-manager support for DOS 3.3 files does not guarantee DOS 3.3 software compatibility.

## Screen preview

![Apple II ProDOS Catalog](../images/Apple2_Catalog.png)

![Apple II ProDOS Bench](../images/Apple2_Bench.png)

![Apple II ProDOS Bench List](../images/Apple2_BenchListing.png)

[← Back to the main README](../../README.md)
