# instruct128K.md — 128 KiB RAM voor de Apple II emulator

Status: **onderzoek / plan, nog niets geïmplementeerd.** Datum: 2026-10-12.
Bronnen: eigen code (`components/apple2Core/apple2Core.c`, `designApple2.md`), MAME `src/devices/bus/a2bus/ramcard128k.cpp` (Saturn 128K, BSD-3), Saturn handleiding (applelogic.org/files/SATURN128MAN.pdf, niet machine-leesbaar opgehaald), aanvullende kennis van de gebruiker. Alles gemarkeerd met **[VERIFIEER]** moet nog tegen een primaire bron/testprogramma gecontroleerd worden.

## 1. Conclusie

Ja, het is haalbaar. Er zijn twee verschillende routes naar 128 KiB:

| | Route A: Saturn 128K in Slot 0 | Route B: Apple IIe + aux-geheugen |
|---|---|---|
| Machine | Huidige Apple II (II/II+) blijft | Nieuwe machine "Apple IIe" |
| Mechanisme | 8 × 16 KiB Language-Card-banken, gekozen via slot-I/O | Tweede 64 KiB bank, via soft switches `$C000–$C00F` |
| Impact op core | Klein: uitbreiding van de bestaande Language Card | Groot: nieuwe machine (ROM, soft switches, 80-col, toetsenbord, video) |
| Past bij owner-hardware (design) | Ja (originele Apple II + LC) | Nee (design zegt expliciet "not IIe") |
| Software-ondersteuning | Alleen software die Saturn kent (RAM-disk drivers, VisiCalc Advanced, Pascal-aanpassingen) | Standaard: ProDOS `/RAM`, AppleWorks, DOS 3.3/Pascal 80-col |

**Aanbeveling:** Route A eerst (laag risico, beperkte wijziging, past bij de bestaande machine). Route B alleen als aparte, tweede machineprofiel; niet de bestaande II vervangen.

## 2. Wat de code nu doet (feiten)

- `designApple2.md`: originele Apple II, 48 KiB RAM + 16 KiB Language Card (slot 0), Integer BASIC + Autostart Monitor, géén IIe.
- `apple2Core.c`:
  - `languageCardRam[16 KiB]` = 2 × 4 KiB (bank 2 en bank 1 voor `$D000–$DFFF`) + 8 KiB gedeeld (`$E000–$FFFF`); `languageCardOffset()` rekent dit om.
  - Toestand: `languageCardBank2`, `languageCardReadEnabled`, `languageCardWriteEnabled`, `languageCardPrewrite`.
  - `accessLanguageCardSwitch()` verwerkt `$C080–$C08F` (lezen en schrijven; dubbele odd-read schakelt schrijven aan).
  - ROM: `rom[apple2RomSize]` (12 KiB, `$D000–$FFFF`); reset-vector uit `rom[0x2FFC]`.
  - Aparte, bestaande kaarten: slot 3 80×24 tekstkaart (Videx-profiel), slot 4–7 Disk II / SmartPort, Pascal-kaart-ROM.
- Aanwezige ROMs in `assets/apple2_roms/`: o.a. `APPLE2E.ROM` (32 KiB, IIe) en `APPLE2_.ROM` (20 KiB). Herkomst/rechten ongeverifieerd (zie design).

## 3. Route A: Saturn 128K (Slot 0)

### 3.1 Hardware-gedrag (uit MAME `ramcard128k.cpp`)

- 128 KiB = **8 banken van 16 KiB**. Elke bank heeft de LC-indeling: 2 × 4 KiB `$Dxxx` + 8 KiB `$E000–$FFFF`.
- Registers op `$C0N0–$C0NF`, `N = 8 + slot` (slot 0 → `$C080–$C08F`).
- Offset `0x0–0x3` en `0x8–0xB` = **normale LC-switches** (zoals nu):
  - `offset & 8 == 0` → `$Dxxx` bank 2; anders bank 1.
  - `offset & 1` → schrijven aan (pas na twee opeenvolgende reads van dezelfde odd-offset: 1, 3, 9 of B; een andere offset reset de teller).
  - `offset & 3` is 0 of 3 → lezen uit RAM (anders ROM).
  - Een even offset zet schrijven uit.
- Offset met bit 2 gezet = **bankselectie**, géén LC-schakeling:

  | Offset | Bank | Basis in 128K |
  |---|---|---|
  | `$4` | 0 | `$00000` |
  | `$5` | 1 | `$04000` |
  | `$6` | 2 | `$08000` |
  | `$7` | 3 | `$0C000` |
  | `$C` | 4 | `$10000` |
  | `$D` | 5 | `$14000` |
  | `$E` | 6 | `$18000` |
  | `$F` | 7 | `$1C000` |

- RAM-adres: `$Dxxx` → `(addr & 0xFFF) + dxxxBank(0 of 0x1000) + bankBase`; `$E000+` → `(addr & 0x1FFF) + 0x2000 + bankBase`.
- Reset (MAME): ROM actief, schrijven uit, `$Dxxx`-bank 2, hoofdbank 0. **[VERIFIEER]** tegen de echte kaart.
- **Let op:** op een echte Saturn is in de handleiding de exacte aansluiting van het selectiebit (bit 2) en van bank 0–3 vs. 4–7 beschreven; MAME is de huidige referentie. **[VERIFIEER]** tegen de Saturn-PDF.

### 3.2 Wat te doen in de code

1. `languageCardRam` van 16 KiB naar 128 KiB (of dynamisch toewijzen; zie §5 geheugenbudget).
2. Nieuw veld `languageCardMainBank` (bankbasis); `languageCardOffset()` telt dit erbij op.
3. `accessLanguageCardSwitch()`: als `address & 0x04` én Saturn-profiel actief → alleen `languageCardMainBank` zetten (RAM/ROM-toestand en prewrite ongemoeid laten). Zonder Saturn-profiel: huidig gedrag, ongewijzigd.
4. Reset (`~regel 1078`): bank 0 en bestaande LC-reset.
5. Wijzigingen in de bestaande standaard-LC houden: `offset & 4` wordt nu genegeerd (`$C084–$C087` = bank 2 enz.). Alleen in het Saturn-profiel afwijkend gedrag.
6. Profiel/optie: machine-config of `drives.cfg`-achtige instelling, bijv. `RAMCARD=SATURN128` / `LC16`. Standaard `LC16` zodat bestaande tests en Pascal-opstart niet veranderen.
7. Snapshots/state: indien de core serialisatie heeft, bankindex en 128 KiB meenemen. **[VERIFIEER]** of dit bestaat.
8. Let op het ROM-leespad voor `PC >= 0xF800 && !languageCardReadEnabled` (`apple2Core.c ~regel 213`, autostart-kenmerken): blijft geldig, maar controleer of het met een bank ≠ 0 niet misgaat.

### 3.3 Andere slot-0-kaarten (compatibiliteit)

- Saturn Systems / Titan "Saturn 128K" (de standaard), Vergecourt RAMEX 128 (Saturn-compatibel), moderne replica's (JD Micro RAMX128K, Garrett's Workshop RAM128). Doel: **alleen Saturn-protocol** emuleren; dit dekt alle genoemde kaarten.
- Niet hetzelfde: Microsoft RAMcard / Multi-RAM / RamWorks (Applied Engineering) — andere schakelmechaniek, niet in scope.

## 4. Route B: Apple IIe (apart machineprofiel)

Alleen doen als er software is die echt een IIe vereist.

- Nieuwe ROM-set (`APPLE2E.ROM`, 32 KiB = `$C100–$FFFF` incl. `$C800`-gebied intern). Herkomst en licentie vastleggen.
- Auxiliary 64 KiB: `$C000–$C00F` soft switches (`RAMRD/RAMWRT` = `$C002–$C005`, `ALTZP` = `$C008/$C009`, `80STORE`, `PAGE2`, `HIRES`), statusregisters `$C011–$C01F`.
- Language card ingebouwd; op de IIe heeft de aux-bank een eigen LC-bereik (ALTZP schakelt zero page, stack én LC mee).
- Intern 80-kolommen: `80COL`, `ALTCHARSET`, MouseText, `INTCXROM`, `SLOTC3ROM`.
- Toetsenbord (open-apple/closed-apple op PB0/PB1, lowercase, pijltoetsen) en 65C02/Enhanced IIe (opcodes) — **[VERIFIEER]** welke CPU-variant.
- Interactie met bestaande kaarten: slot 3 80-kolomskaart wordt overbodig (intern 80-col); Pascal-kaart/Disk II-slotfirmware moeten werken met `INTCXROM`.
- Extended 80-col kaart (64 KiB aux in "aux slot") is standaard in de IIe; Enhanced IIe heeft 64 KiB aux alleen via die kaart. **[VERIFIEER]** gewenste configuratie (64 KiB aux = totaal 128 KiB).
- Dit is aanzienlijk meer werk dan Route A en geeft onder meer een andere video-/toetsenbordimplementatie. Valt buiten het huidige design ("not IIe"); designApple2.md zou dan een tweede machineprofiel krijgen.

## 5. Geheugenbudget (ESP32-S3)

- Extra 112 KiB bovenop de huidige 16 KiB LC. **[VERIFIEER]** of PSRAM beschikbaar is; core gebruikt reeds `esp_heap_caps` (zie include in `apple2Core.c`). Het RAM-array hoort dan in PSRAM (`MALLOC_CAP_SPIRAM`), alleen alloceren als het Saturn-profiel gekozen is.
- Geheugentoegang in het LC-venster is niet snelheidskritisch (bankindex-optelling); PSRAM-latency is acceptabel voor emulatiesnelheid van 1 MHz. **[VERIFIEER]** met meting.

## 6. Welke software gebruikt dit? (om te testen)

- Saturn-bewuste software: RAM-disk drivers voor ProDOS/DOS 3.3, VisiCalc Advanced, diverse Pascal-aanpassingen, Saturn-testdisk (diagnostics van fabrikant). **[VERIFIEER]** beschikbaarheid van images.
- UCSD/Apple Pascal 1.x gebruikt standaard maar de 16 KiB LC; extra banken worden níet automatisch benut. Dus winst voor Pascal is alleen via een aangepaste P-system/driver. Dit moet duidelijk zijn voordat we werk investeren.

## 7. Testplan

1. Regressie: bestaande tests (`tests/`) en Pascal-boot met `LC16` moeten identiek blijven.
2. Unit-test Saturn: schrijf patroon in elke bank (8 × 16 KiB), lees terug, controleer onderlinge isolatie en dat ROM zichtbaar wordt na `$C082`.
3. Prewrite/dubbele-read-gedrag in Saturn-profiel en dat bankselectie (`$C084…`) de LC-toestand niet verandert.
4. Reset-gedrag.
5. Echte Saturn-diagnostics-diskette (indien beschikbaar) in emulator.

## 8. Open vragen voor de eigenaar

1. Route A (Saturn), Route B (IIe), of beide?
2. Doelsoftware: welke programma's moeten 128 KiB gebruiken?
3. Is PSRAM aanwezig/ingeschakeld op het gebruikte board?
4. Mogen ROM-images als `APPLE2E.ROM` worden gebruikt (herkomst/licentie, zie design)?
5. Moet de configuratie via `drives.cfg` of via de machinekeuze-menu's?

## 9. Voorgestelde volgorde (zodra we mogen beginnen)

1. Saturn-handleiding en MAME-gedrag verifiëren (§3.1 **[VERIFIEER]**-punten).
2. Optie `RAMCARD=SATURN128` + 128 KiB buffer (PSRAM) toevoegen.
3. `languageCardOffset()` + `accessLanguageCardSwitch()` aanpassen, reset uitbreiden.
4. Host-tests uit §7.
5. `designApple2.md` en README bijwerken.
6. Daarna pas beslissen over Route B.
