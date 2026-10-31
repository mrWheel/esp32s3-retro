# Start CP/M-80 emulator

ESP32-S3 Retro Computer Version [v0.5.2]
By Willem Aandewiel
=============================================
SD: FAT32 SD present; retro layout v1 valid
PSRAM: 8192 KiB, free 8184 KiB

1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800 [not implemented]
6. File Transfer

Select system [1-6]: 1

CP/M-80
========
E (1918526) cpm80Machine: Could not allocate or initialize CP/M guest memory
Initialization failed: ESP_ERR_NO_MEM
Press ENTER to return to the main menu.

## After <RESET>

 Serial  Port closed

 SERIAL  session closed
Press any key to reconnect

 Serial  Port opened

ESP32-S3 Retro Computer Version [v0.5.2]
By Willem Aandewiel
=============================================
SD: FAT32 SD present; retro layout v1 valid
PSRAM: 8192 KiB, free 8184 KiB

1. CP/M-80
2. CP/M-86
3. UCSD Pascal
4. Apple II
5. SWTPC 6800 [not implemented]
6. File Transfer

Select system [1-6]: 1

CP/M-80
========

Starting CP/M-80 on the virtual A: disk.

A>

## Analyse en oplossing

De foutmelding wijst niet op CP/M-geheugen dat wel is toegewezen maar met
verkeerde inhoud is gestart. De fout treedt op in
`cpm80MachineInitialize()`, voordat `cpm80GuestColdBoot()` wordt aangeroepen.
De gast-CPU vraagt in `cpm80CpuInitialize()` met gewone `calloc()` 65.536 bytes
aan. Die aanroep vraagt niet expliciet om PSRAM. De getoonde 8 MiB vrij PSRAM
bewijst daarom niet dat de voor deze allocatie gebruikte heap een passend
contiguous blok heeft. Dit past bij een allocatie die bij een eerste start
mislukt en na een reset wel lukt.

`calloc()` initialiseert toegewezen CPU-geheugen al op nul; de CP/M cold boot
wist het geheugen bovendien opnieuw. De bestaande logregel bewijst echter niet
dat de allocatie zelf de oorzaak was: `cpm80GuestInitialize()` retourneert
alleen `false`, en de machine vertaalt iedere initialisatiefout naar
`ESP_ERR_NO_MEM`. Ook een ongeldige driveconfiguratie of een andere
initialisatievoorwaarde kan dus dezelfde melding opleveren.

### Oplossing

- `cpm80CpuInitialize()` reserveert de 64 KiB gast-RAM expliciet in 8-bit
  toegankelijk PSRAM met
  `heap_caps_calloc()`; dit initialiseert het geheugen meteen. Als PSRAM niet
  beschikbaar is, probeert de code 8-bit toegankelijk intern geheugen als
  fallback. Op ESP-IDF wordt het blok met `heap_caps_free()` vrijgegeven;
  hosttests blijven `calloc()`/`free()` gebruiken.
- De machine logt de vrije en grootste blokken van PSRAM en intern geheugen
  tijdens CP/M-80 initialisatie en bij een fout. Zo is een tekort aan geschikt
  geheugen te onderscheiden van de getoonde totale vrije PSRAM.
- Eventuele buffers die tijdens initialisatie kort leven, zoals de geladen
  CCP/BDOS-data, blijven op de gewone heap; alleen de langdurig gebruikte 64 KiB
  CPU-adresruimte hoeft in PSRAM.
- De hosttest controleert nu dat het CPU-geheugen na initialisatie op nul staat.

### Verificatie na de wijziging

1. Bouw de firmware en voer de bestaande CP/M-80 hosttests uit, met nadruk op
   CPU-initialisatie, zero-initialisatie, vernietigen en opnieuw initialiseren.
2. Test op de ESP32-S3 meerdere keren een koude start en een start direct na
   reset. Controleer de geheugenstatistieken in de log en bevestig dat de
   emulator telkens de `A>`-prompt bereikt.
3. Herhaal de hardwaretest voor `DIR`, `TYPE`, `HELLO.COM` en WBOOT. Controleer
   dat herhaald in- en uitstappen geen geheugenlek veroorzaakt.
4. Controleer een gecontroleerde allocatiefout en een ongeldige
   driveconfiguratie afzonderlijk: beide moeten een juiste, onderscheidbare
   melding geven en zonder achtergebleven diskbestanden naar het menu
   terugkeren.