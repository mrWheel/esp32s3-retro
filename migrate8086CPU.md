# migrate8086CPU.md — EMU86 → 8086tiny-style core

## Bevindingen (belangrijk)

- De component heet `components/cpm86Core` (niet `components/cpm86`).
- Disk-I/O en console-I/O lopen **niet** via `INT`-interceptie maar via byte-poorten (`IN`/`OUT`, 0xE0–0xEF, zie `main/cpm86Machine.c` `portRead`/`portWrite` en `retro86bios.a86`). guestFs/SD-toegang zit in `main/cpm86Machine.c`, niet in de core. De poort-hooks blijven daarom het primaire integratiepunt; een INT-hook is extra aanwezig (bv. voor debug/trace of een latere BIOS-via-INT variant).
- 8086tiny zelf is tabel-gedreven (decode-tabellen uit een BIOS-binary) en bedoeld voor een PC-BIOS-omgeving. We nemen alleen het *concept* over: compacte interpreter, platte geheugenarray, geen decode-cache. De nieuwe core (`tiny8086/`) is eigen C-code in 8086tiny-stijl; geen upstream-code gekopieerd. Als je upstream-code wilt vendoren: MIT-licentie, pinnen en vastleggen in `third_party/` zoals bij EMU86.

## Nieuwe structuur

```
components/cpm86Core/
  CMakeLists.txt            (aanpassen)
  include/cpm86Core.h       (PUBLIEKE API BLIJFT GELIJK -> main/ ongewijzigd)
  cpm86Core.c               (adapter: cpm86Core* <-> tiny8086, alloc PSRAM, tijdpoorten 0xF0-0xF4)
  tiny8086/tiny8086.h       (NIEUW: CPU-state, hooks)
  tiny8086/tiny8086.c       (NIEUW: switch-case interpreter)
  third_party/emu86/        (verwijderen in de laatste stap)
  bios/retro86bios.a86      (ongewijzigd)
```

## Stappen

1. **Naast elkaar bouwen.** `tiny8086/` toevoegen (gedaan: skeleton compileert en draait een smoke-test op de host). EMU86 blijft nog actief.
2. **Adapter herschrijven** in `cpm86Core.c`, API uit `cpm86Core.h` behouden:
   - `cpm86CoreCreate`: `heap_caps_malloc(ramSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` (fallback intern, zoals nu) → `tiny8086Init`. De `activeCore`-singleton en alle EMU86-globals (`mem_stat`, `op_code_base`, `_int_tab`, …) vervallen.
   - `portRead`/`portWrite` van de config → `tiny8086PortReadHook/WriteHook`; tijdpoorten 0xF0–0xF4 blijven in de adapter-hook (zelfde gedrag als nu).
   - `cpm86CoreLoad/Read/Write` → `tiny8086ReadMemory/WriteMemory` (bounds-checked, dezelfde `cpm86CoreMemoryRange`).
   - `cpm86CoreStep/Run` → `tiny8086Step/Run`; resultaat mappen: `tiny8086Halted→cpm86CoreHalted`, `InvalidInstruction→cpm86CoreInvalidInstruction`, `MemoryFault→cpm86CoreMemoryFault`, `IoFault→cpm86CoreIoError`.
   - `SetEntry`, `GetProgramCounter`, `Reset`, `InstructionCount` rechtstreeks op `cpu->seg[CS]`/`ip`.
   - `SetExecuteGuard` en de trace-API (`GetRecentTrace/GetHeadTrace`): in de adapter houden. Guard = controle op `physicalAddress(CS,IP)` vóór `tiny8086Step`; trace = opcodebytes loggen (geen disassembler meer; `instruction`-veld wordt hex-bytes).
3. **CMake**: `SRCS` = `cpm86Core.c` + `tiny8086/tiny8086.c`; `INCLUDE_DIRS "include" "tiny8086"`; EMU86-regels weg.
4. **Instructieset compleet maken** (zie TODO-blok in `tiny8086.c`): shifts/rotates, group 3 (MUL/DIV…), group 5 (`FF`), CMPS/SCAS/STOS/LODS, LES/LDS, XLAT, BCD, far CALL, SAHF/LAHF. Per stap vergelijken met EMU86 (zelfde `.a86`-BIOS, zelfde boot, zelfde registers/flags na elk blok).
5. **Verificatie**: bestaande host-tests (`tests/hostTests.c`, build-host) groen; CP/M-86 boot tot prompt en `DIR`; herhaald starten/stoppen (geen stale state); timing meten (instructies/s) vóór/na.
6. **Opruimen**: `third_party/emu86/` verwijderen, `designCPM86.md` bijwerken (CPU-selectie: EMU86 → eigen 8086tiny-stijl core; decision log + verification log).

## Performance-richtlijnen

- Geheugen: `physicalAddress = ((seg << 4) + off) & 0xFFFFF` direct op de PSRAM-array; één bounds-check (`address >= memorySize`).
- Bij `ramSize == 1 MiB` kan de check later wegvallen (maskeren is dan genoeg); 640 KiB vereist hem wel.
- CPU-state (`tiny8086` struct) in interne RAM (statisch of `MALLOC_CAP_INTERNAL`), alleen gastgeheugen in PSRAM.
- Latere optimalisatie: opcode-fetch cachen via een lokale pointer binnen het huidige codesegment, lazy flags, `IRAM_ATTR` op `tiny8086Step`. Pas toepassen na meten.
- `tiny8086Run` is budget-gebaseerd en REP is hervatbaar, zodat `cpm86Machine` cooperatief kan yielden.

## Hooks (in `tiny8086.c`)

| Hook | Waar | Gebruik |
|---|---|---|
| `portRead` | `portInByte` (IN) | console status/input, disk-data, tijd |
| `portWrite` | `portOutByte` (OUT) | console output, disk track/sector/commando |
| `intHook` | `raiseInterrupt` (INT n) | host-afhandeling van INT; `true` = afgehandeld, `false` = normale IVT-dispatch |
| `tiny8086ReadMemory/WriteMemory` | adapter/hooks | gecontroleerde DMA van/naar gastgeheugen |

## Status (2026-10-24)

Stappen 1–5 zijn gedaan, stap 6 (opruimen) is nog open. De core is een volledige 8086-interpreter (plus de 80186-opcodes die de bestaande tests gebruiken: PUSHA/POPA, PUSH imm, IMUL imm, shift imm8, ENTER/LEAVE). Host-tests (`build-host/hostTests`) slagen; CP/M-86 boot op de hardware tot `A>` en BENCH86 geeft PASS.

Niet gedaan: volledige ESP-IDF-build door mij (de gebruiker bouwt/flasht), TF/single-step, externe IRQ's.

## Meetresultaten (ESP32-S3, 240 MHz, BENCH86 SCALE=1)

| Situatie | INTEGER (ms) | SIEVE (ms) | Index INT/SIEVE (100 = 5 MHz-model) |
|---|---|---|---|
| EMU86 (baseline, trace aan) | 89604 | 36653 | ~7 / ~12 |
| tiny8086, trace aan, `vTaskDelay(1)` per batch | 12801 | 4755 | 49 / 94 |
| + yield per 50 ms-slice | 9599 | 3531 | 65 / 127 |
| + trace uit | 8531 | 3004 | 73 / 149 |
| + inlining + IRAM-linkerfragment | 7682 | 2850 | 81 / 157 |
| + jump tables + parity-tabel | 6986 | 2593 | 90 / 173 |
| + vertakkingsvrije vlaggen | nog niet gemeten | nog niet gemeten | |

Referentie 5 MHz-model: 6292 / 4492 ms; 10 MHz-model: 3146 / 2246 ms. Totaal ~12,8× (INTEGER) en ~14× (SIEVE) sneller dan EMU86. Kale core-bench (lusje van 4 instructies): 1043 → 1348 kIPS.

## Wat we geprobeerd hebben

**Heeft gewerkt**
- Core vervangen door tiny8086-stijl interpreter met directe `((seg << 4) + off) & 0xFFFFF`-adressering: ~7× sneller dan EMU86 op zich.
- `vTaskDelay(1)` na elke batch van 4096 instructies verwijderd; nu pas yielden na 50 ms. De 100 Hz-tick kostte ~25%. Dit was de grootste enkele winst.
- `CONFIG_CPM86_INSTRUCTION_TRACE` uit: de trace (snprintf per instructie) schakelt het fast path uit en kostte ~10–15%.
- Helpers `always_inline` (callx8-aanroepen 100 → 12) en de hele `tiny8086`-object naar IRAM/DRAM via `linker.lf` (Kconfig `CPM86_TINY8086_IRAM`).
- IDF compileert standaard met `-fno-jump-tables -fno-tree-switch-conversion`; per bestand `-O2 -fjump-tables -ftree-switch-conversion` gezet. Veilig omdat code én rodata dan in intern RAM staan.
- `__builtin_parity` (libgcc-aanroep) vervangen door tabel.
- 16-bit lees/schrijf/fetch fast paths.

**Heeft niet (of nauwelijks) gewerkt**
- `IRAM_ATTR` op alleen `tiny8086Step`: GCC splitst `tiny8086Step$part$0` af, die bleef in flash; geen effect. Hele object via linkerfragment is de oplossing.
- Het gastgeheugen in intern RAM i.p.v. PSRAM: identieke kIPS (1348 vs 1348); de 32 KB data-cache vangt de werkset van de testlus op. Bij echte programma's kan PSRAM nog wel schelen, nog niet apart gemeten.
- Eerste `CPU bench`-programma had verkeerde jump-offsets en gaf misleidende cijfers; gecorrigeerd (`E2 F9 EB F4`).
- Aanname 300 MHz was fout: CPU draait op 240 MHz.

**Analyse restant (~178 cycli/instructie in de benchlus)**
- `aluOperation` was out-of-line en vertakkingsrijk (per vlag een `setFlag`, ~80 instructies per ALU-op) → herschreven (zie boven), nog te meten.
- Opcode-dispatch is een bit-test-boom plus gedeeltelijke jump table in plaats van één dichte tabel.
- `ip` wordt per gefetchte byte naar geheugen teruggeschreven; 20-bit masker per byte; `cpu->fault` wordt per stap geschreven.
- `tiny8086Step` is ~23 KB met veel stack-spills.

## Volgende stappen

1. Bouwen/flashen en meten: `CPU bench`-regels, BENCH86 en de `CP/M-86 timing`-regel voor de vlaggenherschrijving.
2. Dichte opcode-dispatch (computed goto / label-tabel) zodat elke opcode direct bij zijn handler uitkomt.
3. Lokale `codeBase`-pointer voor opcode-fetch en `ip` in een register houden; `fault`-store per stap weghalen.
4. Eventueel lazy flags (groot effect, invasief).
5. Overige ideeën: eerste 64 KB gastgeheugen in intern RAM, 64-byte data-cache lines (`CONFIG_ESP32S3_DATA_CACHE_LINE_64B`), yield-slice 100–200 ms, bounds-check weg bij 1 MiB RAM.
6. Opruimen (stap 6): `third_party/emu86/` verwijderen, `designCPM86.md` CPU-selectie/decision log bijwerken (gedaan voor verificatie-log; EMU86-passages blijven historisch), `CPM86_SELFTEST_BENCH` standaard uit, beslissen of 80186-opcodes blijven.

## Beperkingen / open punten

- Onbekende opcodes geven `tiny8086InvalidInstruction` (IP wordt teruggezet).
- Word-`IN`/`OUT` geeft bewust `IoFault` (zelfde gedrag als nu).
- Interrupts van buiten (hardware-IRQ/HLT-wakeup) en single-step (TF) zijn nog niet geïmplementeerd.
- Zonder trace bevat een crashrapport geen tracevenster.
