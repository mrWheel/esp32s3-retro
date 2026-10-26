# Opdracht: `tools/diskImage.py` uitbreiden voor `--os apple2` en `--os ucsd`

## Context

Repository: `esp32s3-retro` (ESP32-S3 retro-emulator). `tools/diskImage.py` kan nu alleen CP/M-80 en CP/M-86 images maken en bewerken. Voor `apple2` (ProDOS 8) en `ucsd` (UCSD/Apple Pascal) geeft het `--os apple2 (Apple II) is not implemented yet`. Die twee OS'en worden alleen door `tools/buildDiskImage.py` ondersteund (blanco volume bouwen). De code voor het bewerken van volumes bestaat al. Die zit in de eigen modules van die OS'en en is nog niet aan `diskImage.py` gekoppeld.

**Doel:** `diskImage.py` moet de subcommando's `create`, `add`, `list` en `extract` voor `apple2` en `ucsd` laten werken, met dezelfde opties en uitvoer als bij CP/M. CP/M-gedrag mag niet veranderen.

## Eerst lezen (verplicht)

- `tools/diskImage.py` (de hele CLI, inclusief `collect_additions`, `build_parser` en `main`)
- `tools/include/osProfiles.py` (`OsInfo`, `OS_REGISTRY`, `require_supported`, `resolve_image_path`, `image_extension`)
- `tools/include/diskImageCpm.py` en `tools/cpm86/diskImageCpm86.py` (voorbeeld van de engine-interface)
- `tools/apple2/diskImageApple2Prodos.py`, `tools/apple2/buildDiskImageApple2.py`
- `tools/ucsd/diskImageApple2Pascal.py`, `tools/ucsd/buildDiskImageUcsd.py`, `tools/ucsd/extractUcsd.py`
- `tools/apple2/extractApple2Files.py` (de "core" van `extractUcsd.py`: `DiskImage`, `openVolume`, `PascalVolume`, `findEntry`, `importFile`, `exportFile`, `removeFile`, `printListing`)
- `README.md`, regel 12, 20 en 34 en de voorbeelden daaronder

## Het contract van een "engine"

`diskImage.py` roept `info.image()` aan. Dat laadt de module uit `OsInfo.image_module` en gebruikt de volgende attributen. De CP/M-modules leveren ze al:

| Attribuut | Gebruik in `diskImage.py` |
|---|---|
| `PROFILES` (dict, sleutel = profielnaam, waarde bevat `"image_size"`) | `--profile` keuzes, en de regel "Created empty ... (N bytes)" |
| `DEFAULT_PROFILE` | standaardprofiel (via `OsInfo.default_profile`) |
| `create_image(image_path, force=False, profile=...)` | `create` |
| `parse_filename(name)` | naam controleren en omzetten in `collect_additions` |
| `add_files(image_path, [(name, data), ...])` | `add` |
| `extract_file(image_path, filename, user=0)` returns `bytes` | `extract` |
| `list_files(image_path)` (print zelf) | `list` |

## Aan te passen bestanden en hoe

### 1. `tools/include/osProfiles.py`

- Zet voor `apple2` en `ucsd` in `OS_REGISTRY` `supported=True` en vul `image_module` in met de nieuwe adaptermodules (zie punt 2 en 3: `"diskImageApple2"` respectievelijk `"diskImageUcsd"`). Laat `build_module` en `build_only=True` staan: `buildDiskImage.py` moet blijven werken.
- Controleer `os_overview()`, `require_supported()` en `require_buildable()` en pas ze aan zodat de overzichtstekst voor beide OS'en "supported" toont in plaats van "buildDiskImage.py only".
- `OsInfo.profiles` en `default_profile` lezen `PROFILES` en `DEFAULT_PROFILE` uit de engine. Zorg dat de nieuwe modules die leveren.
- Pas de `details`-teksten aan: noem ook de nieuwe diskImage.py-ondersteuning.
- Het afdwingen van de extensie in `resolve_image_path` (`.po` voor apple2 en ucsd, `.dsk` voor CP/M) werkt al. Niet aanraken.

### 2. Apple II: nieuwe adaptermodule `tools/apple2/diskImageApple2.py`

Maak een **nieuwe** module die de engine-API biedt en de bestaande functies uit `diskImageApple2Prodos.py` importeert. Laat `diskImageApple2Prodos.py` ongewijzigd: het heeft al `list_files(image)` en `read_file(image, name)` met een ander contract, en andere code gebruikt die (controleer met `grep -rn "list_files\|read_file\|add_file" tools`). Schrijf geen nieuwe ProDOS-logica.

- `PROFILES = {"140K": {"image_size": 280*512}, "640K": {"image_size": 1280*512}, "800K": {"image_size": 1600*512}}`, afgeleid van `VOLUME_BLOCKS` en `BLOCK_SIZE`. `DEFAULT_PROFILE = "800K"`.
- `create_image(image_path, force=False, profile=DEFAULT_PROFILE)`:
  - Volumenaam = bestandsnaam zonder extensie, via `normalize_volume_name()`. Geeft dat een `DiskImageError`, gebruik dan `"DATA"`. Toon de gekozen naam in de uitvoer.
  - `build_blank_volume(name, profile)`.
  - Weiger een bestaand bestand tenzij `force`, met de foutmelding `File already exists; use --force to replace it: <pad>` (identiek aan CP/M).
  - Maak de bovenliggende map aan en schrijf atomisch (tijdelijk bestand + `os.replace`, zoals `_atomic_write` in `diskImageCpm.py`).
- `parse_filename(name)`: geef `normalize_file_name(name)` terug (hoofdletters). Gooi `DiskImageError` bij ongeldige namen.
- `add_files(image_path, files)`:
  - `image = bytearray(load_volume(path))`. `load_volume` converteert DOS-order naar ProDOS-order. Schrijf alleen terug naar een `.po`-image. Is het bronbestand een DOS-order `.dsk`, geef dan een duidelijke `DiskImageError`.
  - Per bestand: `file_type_for_suffix(suffix)` en `prepare_source_file(filename, data)` voor type, laadadres en data, daarna `add_file(image, name, data, file_type, aux_type, ...)`. Lees `add_file()` en `prepare_source_file()` eerst om te zien wat ze teruggeven en verwachten.
  - Bestaat de naam al, gooi dan `DiskImageError("<NAME> already exists")`. Een overschrijf-optie is niet nodig.
  - Eén atomaire schrijfactie aan het einde; bij een fout in de reeks mag het image niet gedeeltelijk zijn aangepast.
- `extract_file(image_path, filename, user=0)`: `read_file(image, normalize_file_name(filename))`; negeer `user`.
- `list_files(image_path)`: print de volumenaam (`volume_name`), per bestand naam, type, bloklengte en bytes (uit `list_files(image)` van de Prodos-module, onder een alias geimporteerd) en onderaan het aantal vrije blokken (`free_blocks`).

### 3. UCSD: nieuwe adaptermodule `tools/ucsd/diskImageUcsd.py`

Hergebruik `build_blank_volume` uit `diskImageApple2Pascal.py` en de core uit `tools/apple2/extractApple2Files.py` (zoals `extractUcsd.py` dat doet via `import extractApple2Files as core`; kopieer de `sys.path`-afhandeling uit dat bestand).

- `PROFILES = {"140K": {"image_size": 280*512}, "640K": {"image_size": 1280*512}}`, `DEFAULT_PROFILE = "640K"` (uit `VOLUME_BLOCKS`).
- `create_image`: volumenaam uit de bestandsnaam via `normalize_volume_name()` (max. 7 tekens, val terug op `"WORK"` bij een fout). Zelfde "exists/--force"-gedrag en atomair schrijven als bij Apple II.
- `parse_filename(name)`: valideer en normaliseer de Pascal-bestandsnaam. Kijk hoe `volume.importFile` dat doet en hergebruik het, zodat `diskImage.py` en de bestaande tool dezelfde namen opleveren.
- `add_files`: `core.openVolume(core.DiskImage(path))`, en per bestand `volume.importFile(name, core.Exported(data, 0x0C, 0, kind))` zoals in `extractUcsd.run`. Kies `kind` met `KIND_BY_SUFFIX` (standaard 5 = data). Pas bij tekstbestanden (`kind == 3`) `hostTextToUcsd` toe. Roep `volume.image.save()` een keer aan na alle bestanden.
- `extract_file`: `core.findEntry`, `volume.exportFile`, bij tekst `ucsdTextToHost`.
- `list_files`: `core.printListing(volume)`.
- Kopieer `hostTextToUcsd`, `ucsdTextToHost` en `KIND_BY_SUFFIX` niet. Importeer ze uit `extractUcsd.py` (of verplaats ze naar de nieuwe module en laat `extractUcsd.py` ze van daar importeren, zonder gedragsverandering).

### 4. `tools/diskImage.py`

- `collect_additions`: nu staat er `if not data: raise DiskImageError(f"CP/M files must not be empty: {name}")`. Maak die controle OS-onafhankelijk (bijvoorbeeld "Empty files cannot be added: <naam>"), of laat de engine bepalen (`getattr(engine, "allow_empty", False)`). Controleer of ProDOS en Pascal lege bestanden aankunnen. Zo niet, dan blijft de controle bestaan.
- `--name` help: nu "CP/M 8.3 name". Maak de tekst OS-neutraal ("target file name on the image").
- `extract` help ("extract one file's CP/M records") en de `--user` optie: `--user` is alleen voor CP/M. Documenteer dat het voor andere OS'en genegeerd wordt, of waarschuw als het expliciet is opgegeven.
- `EXAMPLES`: voeg regels toe voor apple2 en ucsd (`create`, `add`, `list`, `extract`).
- `build_parser`: `profiles = known.profiles if known and known.supported else ()` blijft werken zodra `supported=True`. Controleer `--profile` met `type=str.upper` voor namen als `800K`.
- De `create`-uitvoer gebruikt `engine.PROFILES[profile]["image_size"]`; houd dat intact.

### 5. Documentatie

- `README.md`, regel 12 (tabel), regel 20 en de alinea op regel 34: `diskImage.py` ondersteunt nu cpm80, cpm86, apple2 en ucsd. Verwijder "not part of the normal diskImage.py dispatcher" en "SWTPC and UCSD are registered but not implemented" (SWTPC blijft wel niet geimplementeerd). Voeg voorbeeldcommando's toe.
- Pas ook `OsInfo.details` en de help-tekst (`os_epilog`) aan, zie punt 1.

## Wat niet mag veranderen

- Gedrag van CP/M-80 en CP/M-86 in `diskImage.py` en `buildDiskImage.py`.
- Het bestaande `buildDiskImage.py` voor apple2/ucsd, inclusief het gebruik van `resolve_image_path` (de extensie `.po` wordt altijd afgedwongen).
- `swtpc` blijft `supported=False`.
- Geen wijzigingen aan firmware (`components/`, `main/`).

## Verificatie (alles moet slagen; voer de commando's echt uit)

Gebruik `--sd-root /tmp/sd` zodat de echte SD-map niet wordt aangeraakt.

```sh
cd <repo>
for OS in apple2 ucsd; do
  python3 tools/diskImage.py create --os $OS --sd-root /tmp/sd work2.dsk      # moet work2.po maken
  echo "HELLO WORLD" > /tmp/hello.txt
  python3 tools/diskImage.py add     --os $OS --sd-root /tmp/sd work2 /tmp/hello.txt
  python3 tools/diskImage.py list    --os $OS --sd-root /tmp/sd work2        # toont HELLO.TXT
  python3 tools/diskImage.py extract --os $OS --sd-root /tmp/sd work2 HELLO.TXT --output /tmp/out.txt
  diff /tmp/hello.txt /tmp/out.txt                                          # moet gelijk zijn (let op: UCSD tekstconversie)
  python3 tools/diskImage.py create --os $OS --sd-root /tmp/sd work2         # moet falen zonder --force
done
```

Controleer bovendien:

- `--profile 140K` en `--profile 640K` (en `800K` voor apple2) geven de juiste bestandsgrootte (143360 / 655360 / 819200 bytes).
- Een binair bestand (`.BIN`/`.SYS` bij ProDOS) en een groot bestand (meer dan 1 blok, en groot genoeg voor de tree-structuur) round-trippen byte-exact.
- `add` met een wildcard (`'/tmp/files/*'`) en met `--name`.
- Een image gemaakt door `tools/buildDiskImage.py --os apple2` is te lezen en uit te breiden met `diskImage.py`.
- `python3 tools/diskImage.py -h`, `... --os apple2 -h` en `... --os ucsd -h` tonen "supported".
- CP/M-regressie: `python3 tools/diskImage.py create --os cpm86 --profile BIG --sd-root /tmp/sd t.dsk`, daarna `add`, `list` en `extract` werken zoals voorheen.
- Draai de bestaande tests die Python-tools raken (zie `tests/`, bijvoorbeeld `guestFsDos33Fixture.py`) en laat zien dat er geen nieuwe fouten zijn. Ruim `/tmp/sd` en tijdelijke bestanden daarna op.

## Opleverafspraken

- Kleine, gerichte wijzigingen; geen ongerelateerde refactors; commentaar alleen waar nodig.
- Foutmeldingen via `DiskImageError`, zodat `diskImage.py` ze netjes via `parser.error()` toont.
- Rapporteer aan het eind kort welke bestanden zijn gewijzigd en de uitvoer van de verificatiecommando's.
