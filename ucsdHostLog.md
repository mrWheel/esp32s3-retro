# UCSD HOST-ontwikkeling — logboek

Doel: een native Apple Pascal-programma `HOST` met de interface `HOST>` en de
commando's `DIR`, `GET <bestand> [O]`, `PUT <bestand> [O]` en `QUIT`, voor
bestandsoverdracht via de slot-2-uitwisselbrug van de ESP32-S3
(`$C0A0` bytes, `$C0A1` abort; map `/microSD/retro/exchange/ucsd`).

Bronnen: `guest/ucsd/host/HOST.PAS` en `guest/ucsd/host/HOSTIO.ASM`.

## Status: KLAAR VOOR GEBRUIKERSTEST (emulator-bewijs; hardware niet getest)

- Compileren, assembleren en linken met de echte gasttools: PASS.
- GET, PUT, DIR, foutpaden, overwrite, abort bij schijf vol: PASS (zie hieronder).
- Herhaalbaar bewijs: ctest `ucsdHostGuest` (`tests/ucsdHostGuest.sh`,
  scripts in `tests/ucsdHost/`) — PASS.
- `HOST.CODE` (gelinkt, 19 blokken, met T/W-opties, volume-prefix, voortgangsregel en wildcards) staat in `littlefs/ucsd/ApplePascal1-140K.dsk`.
- Niet geflasht, niet gecommit. ESP32-S3-hardware is niet getest.

## Gebruik

1. Start Pascal (menu 3). Zet in de Filer het prefix op een schrijfbaar volume,
   bijv. `P` → `WORK1:` (HOST schrijft GET-bestanden op het prefix-volume).
2. `X(ecute)` → `APPLE1:HOST`.
3. `HOST> DIR`, `GET NAAM`, `GET NAAM O`, `PUT NAAM`, `PUT NAAM O`, `QUIT`.

Bestandsnamen zijn hoofdletter-ongevoelig (HOST zet ze om), 1–15 tekens,
beginnend met een letter, daarna letters/cijfers/punt.

## Wat is geleerd (feiten uit de echte gastcompiler/-emulator)

Staging en compiler
- Pascal-`.TEXT`-bron: header van 1 KiB, dan pagina's van 1 KiB met regels
  `0x10, 32+inspringing, tekst, CR`; regels lopen nooit over een pagina; de
  pagina wordt met NUL's aangevuld; er zijn GEEN leidende NUL's. (De eerdere
  bewering over "acht nulbytes" was fout en gaf compilerfout 400.)
- Compiler: 8 significante tekens in identifiers; geen numerieke `GOTO`, geen
  `EXIT`, geen `CONCAT(string,char)`, `WRITELN` van een Boolean is een fout,
  `COPY` op een packed array is een fout, `FILE OF 0..255` is onbruikbaar.
- Het werkvolume én het `SYSTEM:`-toolsvolume moeten schrijfbaar zijn tijdens
  compile/assemble/link (`%LINKER.INFO`); de harness werkt op /tmp-kopieën.
- Prefix `WORK1:` instellen vóór compileren (Filer `P`).

Assembler (6502)
- Constanten zijn standaard hex (`0C0A0`); `$` markeert lokale labels; macro-
  parameters zijn `%1`.
- Aanroepconventie: `.PROC naam,n` (n woorden parameters); eerst het
  returnadres poppen (laag, hoog), daarna parameters in omgekeerde volgorde;
  VAR = 16-bit adres; functieresultaat = 4 bytes poppen, daarna resultaat
  pushen (hoog, laag). Zero page $00–$07 is bruikbaar.
- `.ABSOLUTE` is niet nodig.

Bestandssemantiek
- `CLOSE(f)` zonder `LOCK` gooit een nieuw bestand weg; `CLOSE(f, LOCK)` is nodig.
- `FILE OF CHAR` lezen is tekst-achtig (CR → spatie, EOF bij NUL): voor PUT dus
  een untyped `FILE` + `BLOCKREAD` met de exacte lengte uit de directory.
- Per-byte `PUT` op een `FILE OF CHAR` kost ≈13 000 cycles (~70 B/s): te traag.
  Daarom schrijft GET hele blokken met `BLOCKWRITE` (8 KiB in < 1 s).
- `BLOCKWRITE` geeft altijd `lastbyte = 512`. De exacte lengte wordt daarom na
  `CLOSE(…, LOCK)` in de directory (blok 2–5, veld +22) gezet.
- BELANGRIJK: de Pascal-runtime houdt een directory-cache per draaiend
  programma. Elke latere OS-directoryschrijfactie (`CLOSE LOCK` van een volgend
  bestand, maar ook het weggooien van een mislukt `REWRITE`) schrijft die cache
  terug en draait eerder gezette `lastbyte`-waarden terug naar 512 (bewezen:
  alleen het laatste bestand behield zijn lengte; ook een mislukte GET draaide
  alles terug). HOST onthoudt daarom per sessie (max. 24 bestanden) de exacte
  lengte per naam en past de hele lijst opnieuw toe aan het einde van elke GET
  waarbij een bestand werd aangemaakt, vóór de melding `Done`. Na het einde van
  het programma (Filer, volgende programma) wordt de directory vers gelezen en
  blijven de lengtes behouden (bewezen met Filer `M(ake)` na `QUIT`).
  Bij meer dan 24 verschillende bestanden in één sessie meldt HOST
  "stored, but the exact length could not be set" (de data is wel correct, de
  lengte wordt naar boven afgerond op 512 met NUL-opvulling).
- `REWRITE` op een bestaande naam vervangt het oude bestand pas bij
  `CLOSE LOCK`; een mislukte GET met `O` laat het oude bestand intact.

Protocol
- QUERY(0), DIR(1), GET(2), PUT(3), PUT-overwrite(4), 32-bit lengte (little
  endian), CRC-32, statuscodes 0–5. Host-zijde: `hostExchange`.
- Beperking: bestanden tot 32767·256 bytes in GET (`QuadParts`), PUT tot 16000
  blokken.

## Ontwerp (definitief)

- `HOSTIO.ASM`: `EXCHREAD` (func), `EXCHWRT`, `EXCHABT`, `EXCHRDB` (N bytes van
  `$C0A0` naar buffer), `EXCHWRB` (buffer naar `$C0A0`), `EXCHCRC` (CRC-32 over
  een buffer).
- `HOST.PAS`: hoofdlus `HOST> `; `FileInfo`/`FindEntry` lezen de directory van
  het prefix-volume; `ScanFile` (CRC-pass en verzend-pass met `BLOCKREAD`);
  `RecvFile` (`REWRITE` → blokken → CRC → `CLOSE LOCK` of weggooien);
  `NoteLast`/`ApplyLast` (exacte lengtes); `SendFile`; `PrStatus`.
- Geen tijdelijke bestanden (HOST.TMP/HOST.BAK zijn vervallen).
- PUT leest eerst alles voor de CRC, stuurt dan lengte + CRC, wacht op de status
  van de host (bestaat al → fout 3), en verzendt daarna de data.

## Bewijs (alle runs in de Apple IIe-emulator, echte gasttools)

| # | Waarneming / commando | Uitslag |
|---|---|---|
| 1 | Compile `HOST.PAS` (693 regels), 0 fouten | PASS |
| 2 | Assemble `HOSTIO.ASM` (238 regels), 0 errors | PASS |
| 3 | Link HOST+HOSTIO → `HOSTX.CODE`; kopieert EXCHREAD, EXCHWRT, EXCHABT, EXCHRDB, EXCHWRB, EXCHCRC | PASS |
| 4 | `HOST> DIR` (lege map: "No files."; gevulde map: namen) | PASS |
| 5 | GET HELLO.TXT (17 B), EMPTY.TXT (0), BIN.DAT (773), B512 (512), B513 (513), B1000, EIGHTK (8192); directory-lastbyte exact (17, 512, 261, 512, 1, 488, 512) en inhoud bit-identiek | PASS |
| 6 | PUT van dezelfde bestanden + `HOSTX.CODE` naar lege exchange-map; `cmp` met bron | PASS |
| 7 | GET/PUT bestaand zonder `O` → "destination already exists" / "Destination already exists."; met `O` vervangt | PASS |
| 8 | GET/PUT ontbrekend bestand; ongeldige naam; onbekend commando | PASS (nette melding) |
| 9 | GET BIGFILE.DAT (450 000 B) naar vol volume → lokale I/O-fout, `EXCHABT`, "nothing was stored", geen restbestand; daarna DIR, GET en PUT werken weer (exchange-toestand hersteld) | PASS |
| 10 | CRC-controle door de gast bij elke GET en door de host bij elke PUT (host-protocol) | PASS (impliciet; een CRC-mismatch-injectie in de gast is niet apart uitgevoerd — `hostTests` dekt de host-zijde) |
| 11 | Directory-cache-effect (bovenstaande) gereproduceerd en opgelost | PASS |
| 12 | Promotie: Filer `T` `WORK1:HOSTX.CODE` → `APPLE1:HOST.CODE` op een kopie; volume opent, 9/9 bestanden, 53 blokken vrij; start vanaf `APPLE1:HOST`, DIR/GET/PUT werken, roundtrip identiek | PASS |
| 13 | ctest `ucsdHostGuest` (build + GET + PUT + vergelijking) | PASS (≈58 s) |

Eerder (historisch, achterhaald): compilerfout 400 door NUL-padding in de
staging; een test die altijd PASS meldde; alleen-lezen `SYSTEM:`-volume
(I/O-fout 16); hex/`$`/`%1`-problemen in de assembler.

## Promotie naar `littlefs/ucsd/ApplePascal1-140K.dsk`

- Backup: de originele image is `git show HEAD:littlefs/ucsd/ApplePascal1-140K.dsk`
  (tijdens de run ook als /tmp/ucsd-promote/ApplePascal1-140K.dsk.orig bewaard).
- Werkwijze: kopie → Filer-transfer in de gastemulator → controle dat het volume
  opent en alle oorspronkelijke bestanden aanwezig zijn → pas daarna over het
  origineel heen gekopieerd.
- Toegevoegd: één bestand, `HOST.CODE` (11 blokken, gelinkt). `HOSTIO.CODE` is
  NIET nodig (reeds in HOST.CODE gelinkt). Vrije ruimte op APPLE1: 64 → 53 blokken.
- Teruggedraaid kan met `git checkout -- littlefs/ucsd/ApplePascal1-140K.dsk`.
- De boot-image wordt read-only (SD6.1) gemount; HOST draait er vanaf, maar GET
  schrijft op het prefix-volume — zet dat eerst op `WORK1:` (of ander RW-volume).

## Niet opgelost / niet getest

- ESP32-S3-hardware (flash) niet getest; de gebruiker flasht zelf.
- Geen gast-test met opzettelijk gecorrumpeerde CRC.
- GET is begrensd op 24 verschillende bestanden per HOST-sessie voor exacte lengtes.
- Het ESP-IDF-build en overige hosttests: zie het einde van dit logboek.

## Eindcontrole (2026-10-12)

- `ctest` in `build-host`: 13 van 16 PASS, inclusief `hostCore`, `ucsdPascalBoot`
  (met de gepromoveerde image) en `ucsdHostGuest`.
- 3 tests falen, ook met de ORIGINELE boot-image (vooraf bestaand, niet door HOST
  veroorzaakt): `apple2DiskStream` (image-open assertion), `apple2MultiDisk`
  (profielconfig), `ucsdPascalIIeTextOpen` (verwacht `DATA3:`, zie README).
- `idf.py build` (ESP-IDF 6.0.2): PASS. Niet geflasht.
- Voorgestelde commitboodschap: "Add native Apple Pascal HOST utility (DIR/GET/PUT)
  with guest tests; promote HOST.CODE to the Pascal 1.3 boot image".

## Tekstopties T en W (2026-10-13)

- `GET <bestand> [T] [W] [O]` en `PUT <bestand> [T] [W] [O]`: `T` zet OS-tekst
  om naar/van de Pascal TEXT-blokstructuur (header 1 KiB, pagina's van 1 KiB,
  regels als DLE, 32+inspringing, tekst, CR); `W` kiest bij PUT CRLF (anders LF).
  Zie designUCSD.md, "HOST text transfer" en UCSD-DEC-022.
- Een lege Return toont nu de Usage-tekst; HOST stopt alleen met `QUIT`/`Q`.
- ctest `ucsdHostGuest` uitgebreid (get.txt, put.txt, edit.txt): PASS in de
  hostemulator, inclusief compileren en draaien van een geconverteerd programma
  en het openen van een geconverteerd bestand in de Editor.
- Gepromoveerd naar `littlefs/ucsd/ApplePascal1-140K.dsk` (Filer `T`
  `WORK1:HOSTX.CODE` -> `APPLE1:HOST.CODE`, oude versie vervangen, op een
  kopie; daarna pas over het origineel): 16 blokken (versie met volume-prefix), 9/9
  bestanden, 48 blokken vrij; boot, `X APPLE1:HOST`, lege Return (Usage), `GET T` en `PUT T W O`
  werken vanaf de boot-image. Eerdere vermelding "11 blokken" (regel 124,
  vrije ruimte 53) betreft de vorige versie.
- ESP32-S3-hardware niet getest; niets geflasht of gecommit.

## Volume-prefix in bestandsnamen (2026-10-13)

- `GET`/`PUT` accepteren `NAME`, `:NAME` (prefix-volume), `*NAME` (systeemvolume)
  en `VOL:NAME`. Alleen de kale naam gaat naar de exchange-map. Het herstel van
  de exacte lengte (`ApplyLast`) werkt nu per volume.
- Nieuw script `tests/ucsdHost/vol.txt` (in ctest `ucsdHostGuest`): PASS; bestanden
  over APPLE1: (`*`), WORK1: (`:` en `WORK1:`) heen en weer, byte-voor-byte gelijk.
- Het nieuwe `HOST.CODE` (16 blokken) staat op `littlefs/ucsd/ApplePascal1-140K.dsk`
  (gepromoveerd via Filer-transfer op een kopie, gestart vanaf `APPLE1:HOST`).
- Opmerking: schrijven naar het alleen-lezen `SYSTEM:`-volume meldt de emulator
  als gelukt (bestaand gedrag, niet gewijzigd).
- ESP32-S3-hardware niet getest; niets geflasht of gecommit.

## Voortgangsregel en wildcards (2026-10-13)

- `GET NAME:` verschijnt direct na het commando, per blok een punt, en pas aan het
  eind ` Done` of de foutmelding op dezelfde regel (leeg bestand: `GET NAME: Done`).
- Wildcard in UCSD is `=` (niet `*`): `GET =.TEXT`, `PUT WORK2:A=`, `GET *=.TXT`.
  Een fout bij één bestand stopt de rest niet; totaalregel `N files transferred.`
  of `X of N files transferred; Y failed.`, anders `No files match.`.
- Tests: `get.txt`, `put.txt`, `vol.txt` aangepast aan het nieuwe formaat, nieuw
  `wild.txt` (ctest `ucsdHostGuest`): PASS, byte-voor-byte vergelijking.
- `HOST.CODE` (19 blokken, 45 vrij) staat op `littlefs/ucsd/ApplePascal1-140K.dsk`
  (gepromoveerd via Filer-transfer op een kopie; start vanaf `APPLE1:HOST`, Usage,
  GET, PUT en `GET =.DAT O` getest).
- ESP32-S3-hardware niet getest; niets geflasht of gecommit.
