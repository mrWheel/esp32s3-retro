# migrate8086CPU.md — EMU86 → 8086tiny-stijl core

Status: **afgerond (2026-10-24)**. EMU86 is vervangen door een eigen interpreter in 8086tiny-stijl. De publieke API (`cpm86Core.h`) is, op het verwijderen van de debug-/trace-functies, gelijk gebleven.

## Resultaat

ESP32-S3 op 240 MHz, BENCH86 SCALE=1 (INTEGER / SIEVE), op de hardware gemeten door de gebruiker. Index: 100 = 5 MHz 8086-model (6292 / 4492 ms); 10 MHz-model: 3146 / 2246 ms.

| Situatie | INTEGER (ms) | SIEVE (ms) | Index |
|---|---|---|---|
| EMU86 (baseline) | 89604 | 36653 | ~7 / ~12 |
| tiny8086, trace aan, `vTaskDelay(1)` per batch | 12801 | 4755 | 49 / 94 |
| + yield pas na 50 ms-slice | 9599 | 3531 | 65 / 127 |
| + trace uit | 8531 | 3004 | 73 / 149 |
| + inlining + IRAM-linkerfragment | 7682 | 2850 | 81 / 157 |
| + jump tables + parity-tabel | 6986 | 2593 | 90 / 173 |
| + vertakkingsvrije vlaggen | 6987 | 2591 | 90 / 173 |
| + yield-slice 200 ms | 6110 | 2308 | 102 / 194 |
| + execute-guard weg (snelle run-loop wordt echt gebruikt) | 5269 | 1993 | 119 / 225 |
| + dichte opcode-sprongtabel (`-fno-bit-tests`) | 4972 | 1892 | 126 / 237 |
| + `readByte`/`writeByte`/`aluOperation`/`setFlag` always_inline (**eindstand**) | 4078 | 1516 | 154 / 296 |

Eindstand: ~22× (INTEGER) en ~24× (SIEVE) sneller dan EMU86; sneller dan een 5 MHz 8086, bij SIEVE ruim sneller dan een 10 MHz 8086-model (index 296 t.o.v. 200). Ongeveer 1070 kIPS tijdens CP/M, 1593 kIPS in een kale lus. De BENCH86-resultaten zijn PASS (AX=4ED8 BX=D678, 1028 priemgetallen).

## Structuur

```
components/cpm86Core/
  CMakeLists.txt            per-file compile-opties voor tiny8086.c (zie Performance), LDFRAGMENTS
  Kconfig.projbuild         CPM86_TINY8086_IRAM (default y)
  linker.lf                 tiny8086-object naar IRAM/DRAM ('noflash') bij CPM86_TINY8086_IRAM=y
  include/cpm86Core.h       publieke API (ongewijzigd, minus debug-API)
  cpm86Core.c               adapter: cpm86Core* <-> tiny8086, gastgeheugen in PSRAM, tijdpoorten 0xF0-0xF4
  tiny8086/tiny8086.h       CPU-state, hooks, resultaten
  tiny8086/tiny8086.c       switch-case interpreter
  bios/retro86bios.a86      ongewijzigd
```

`third_party/emu86/` is verwijderd. De bestanden staan nog in de git-geschiedenis.

## Ontwerp

- Disk- en console-I/O lopen niet via `INT` maar via byte-poorten (`IN`/`OUT`, 0xE0–0xEF); zie `main/cpm86Machine.c` en `retro86bios.a86`. guestFs/SD zit in `main/`, niet in de core. Een `intHook` bestaat naast de poort-hooks (`true` = afgehandeld, `false` = normale IVT-dispatch).
- Geheugen: `physicalAddress = ((seg << 4) + off) & 0xFFFFF` direct op de PSRAM-array, één bounds-check (`address >= memorySize`), 16-bit fast paths voor lezen/schrijven/fetch.
- Woord-`IN`/`OUT` geeft bewust `IoFault`.
- De core is een volledige 8086 plus de 80186-opcodes die de bestaande tests gebruiken (PUSHA/POPA, PUSH imm, IMUL imm, shift imm8, ENTER/LEAVE).
- Niet geïmplementeerd: externe interrupts/HLT-wakeup en single-step (TF).
- `cpm86CoreRun` roept `tiny8086Run` rechtstreeks aan; REP-string-ops lopen binnen één aanroep.

| Hook | Waar | Gebruik |
|---|---|---|
| `portRead` | `IN` | console status/input, disk-data, tijd |
| `portWrite` | `OUT` | console output, disk track/sector/commando |
| `intHook` | `INT n` | host-afhandeling van INT |
| `tiny8086ReadMemory/WriteMemory` | adapter/hooks | gecontroleerde DMA van/naar gastgeheugen |

## Performance: wat werkte

- **Yield per tijdslice** (200 ms) i.p.v. `vTaskDelay(1)` per batch: bij een 100 Hz-tick slaapt elke `vTaskDelay(1)` tot 10 ms. Grootste enkele winst.
- **Trace en execute-guard weg**: die schakelden het snelle pad (`tiny8086Run`) uit en deden per instructie `snprintf`. De guard stond bovendien altijd aan, ook zonder trace, waardoor CP/M nooit door het snelle pad liep.
- **Hele `tiny8086`-object in IRAM/DRAM** via `linker.lf` (code én jump-/parity-tabellen).
- **Jump tables**: IDF compileert met `-fno-jump-tables -fno-tree-switch-conversion`; voor `tiny8086.c` staan nu `-O2 -fjump-tables -ftree-switch-conversion -fno-bit-tests --param=jump-table-max-growth-ratio-for-speed=2000`. Zonder `-fno-bit-tests` maakt GCC een boom van bit-tests; nu is de dispatch één tabel-sprong. Veilig omdat tabellen via het fragment in intern RAM staan.
- **`always_inline` voor `readByte`, `writeByte`, `aluOperation`, `setFlag`** en `stepImpl` in `tiny8086Run`. In de disassembly was `readByte` 144× een echte `call8` (elke fetch!); dat kostte ~50 cycli per instructie.
- Parity via 256-bytes tabel i.p.v. `__builtin_parity` (libgcc-aanroep).

## Performance: wat niet (genoeg) werkte

- `IRAM_ATTR` op alleen `tiny8086Step`: GCC splitst `tiny8086Step$part$0` af, die bleef in flash. Het linkerfragment voor het hele object is de oplossing.
- Gastgeheugen in intern RAM i.p.v. PSRAM: identieke kIPS; de 32 KB data-cache vangt de werkset van de testlus op.
- Vertakkingsvrije vlaggenberekening: geen meetbare winst (BENCH86 6986 → 6987 ms); vlaggen waren niet de bottleneck.
- `stepImpl` in `tiny8086Run` inlinen terwijl `readByte` nog een aanroep was: geen effect (hypothese over window-overflow bleek onjuist; de aanroepen zaten elders).
- **`codeBase`-pointer voor de opcode-fetch** (CS-basis cachen, geen masker/bounds-check per byte): **trager**, INTEGER 4078 → 4558 ms, SIEVE 1516 → 1765 ms. De berekening per instructie en het extra struct-veld kostten meer dan ze opleverden. Teruggedraaid.
- Een aanname van 300 MHz CPU was fout: de CPU draait op 240 MHz.

## Meetmethode en valkuilen

- De CP/M-86-console zit op het scherm, dus `idf.py monitor` is niet bruikbaar tijdens het draaien; tijdelijke meetcode schreef daarom naar de console. Die code is nu verwijderd.
- Een eerste kale benchmark-lus had verkeerde jump-offsets en schreef over zijn eigen code (`mov [si],ax` met SI=0); daardoor waren vroege kIPS-cijfers misleidend. Gebruik BENCH86 als referentie.
- Een opcode-profiler (cycli per opcode) liet zien dat de vaste kosten per instructie (~100 cycli) dominant waren, en dat het BIOS op het `A>`-prompt veel tijd in een `IN al,imm`-poll doorbrengt (idle, niet BENCH86).

## Mogelijke vervolgstappen (niet gedaan)

- Opcode-fetch/`ip` lokaal houden (`ip` in een register, terugschrijven aan het eind van de instructie). Hoger risico: raakt alle jumps, calls, INT en foutherstel. De `codeBase`-variant bleek trager.
- 256-entry classificatietabel voor prefixen (verwacht ~3%).
- Lazy flags (groot effect, invasief).
- Eerste 64 KB gastgeheugen in intern RAM; 64-byte data-cache lines (`CONFIG_ESP32S3_DATA_CACHE_LINE_64B`).
- Idle-detectie bij het `A>`-prompt (poll op console-status) om CPU-tijd in rust te besparen.
- Bounds-check weglaten bij 1 MiB gastgeheugen.

## Verwijderd bij de afronding

Debug- en diagnosecode is verwijderd: `CONFIG_CPM86_INSTRUCTION_TRACE` (instructie-trace), `cpm86CoreGetRecentTrace`/`cpm86CoreGetHeadTrace`, `cpm86CoreSetExecuteGuard`, `captureInstructionTrace` in `cpm86CoreConfig`, de opcode-profiler, de CPU-selftest en de timing-uitvoer (`logExecutionMetrics`) in `main/cpm86Machine.c`. Bij een stop door een fout blijft de melding met resultaat, CS:IP en de bytes op die plek bestaan; er is geen tracevenster meer.
