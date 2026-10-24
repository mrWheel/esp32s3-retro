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

## Beperkingen / open punten

- Huidige `tiny8086.c` is een skeleton: ~70% van de veelgebruikte opcodes, onbekende opcodes geven `tiny8086InvalidInstruction` (IP wordt teruggezet). CP/M-86 draait pas na stap 4.
- Word-`IN`/`OUT` geeft bewust `IoFault` (zelfde gedrag als nu).
- Interrupts van buiten (hardware-IRQ/HLT-wakeup) en single-step (TF) zijn nog niet geïmplementeerd.
- Snelheidswinst is nog niet gemeten; de 10×-claim moet na stap 5 met cijfers onderbouwd worden.
