Voeg aan een op ESP32-S3 draaiende CP/M-86-emulator een algemene API toe waarmee gastprogramma’s verstreken tijd in milliseconden kunnen meten.

Inspecteer eerst de bestaande code en pas de implementatie daarop aan. Behoud bestaande wijzigingen. Deze opdracht betreft uitsluitend de timerfunctie; maak of wijzig geen benchmarkprogramma en flash geen hardware.

## 1. Tijdbron op de ESP32-S3

Gebruik ESP-IDF:

```c
#include "esp_timer.h"

uint32_t milliseconds =
    (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL);
```

Dit is een monotone teller vanaf de initialisatie van ESP Timer. Er is geen datumklok, internetverbinding of ingestelde tijd nodig.

Eigenschappen:

- Resolutie van de aangeboden API: 1 milliseconde.
- Tellerbreedte: unsigned 32 bits.
- De teller loopt na ongeveer 49,7 dagen over naar nul.
- Reset of herinitialisatie van de onderliggende timer verbreekt een lopende meting.
- Tijd loopt door wanneer de emulator tijdelijk geen gastinstructies uitvoert.

## 2. Scheiding tussen emulator en ESP32

Maak de tijdbron injecteerbaar via een optionele callback, bijvoorbeeld:

```c
typedef uint32_t (*ReadMilliseconds)(void *context);
```

De emulatorconfiguratie krijgt:

```c
ReadMilliseconds readMilliseconds;
void *clockContext;
```

De ESP32-adapter levert de echte timer. Desktoptests kunnen een gecontroleerde teller aanbieden. De CPU-emulator zelf mag geen ESP-IDF-afhankelijkheid krijgen.

Controleer alle plaatsen waar de emulatorconfiguratie wordt aangemaakt. Niet-gebruikte callbackvelden moeten expliciet of door volledige nulinitialisatie nul zijn.

## 3. Gast-API via I/O-poorten

Gebruik onderstaande poorten alleen als ze vrij zijn. Controleer eerst op bestaande functies of conflicten. Meld een conflict voordat je een andere poortindeling kiest.

Alle toegang gebeurt met **8-bits IN-instructies**.

| Poort | Betekenis |
|---|---|
| F0h | Capability: B1h als deze versie van de timer-API beschikbaar is; anders 00h. |
| F1h | Leg de huidige 32-bits millisecondenteller vast en retourneer bits 0–7. |
| F2h | Retourneer bits 8–15 van diezelfde vastgelegde waarde. |
| F3h | Retourneer bits 16–23 van diezelfde vastgelegde waarde. |
| F4h | Retourneer bits 24–31 van diezelfde vastgelegde waarde. |

Het lezen van F1h maakt dus één momentopname. F2h–F4h mogen de tijdbron niet opnieuw aanroepen. Dit voorkomt inconsistente waarden wanneer de teller tijdens het uitlezen verspringt.

Aanvullende afspraken:

- F0h lezen verandert de momentopname niet.
- Voor de eerste F1h-read is de momentopname nul.
- Een emulatorreset wist de momentopname, maar reset niet de ESP32-timer.
- Zonder callback retourneert F0h nul en retourneren F1h–F4h nul.
- De momentopname hoort bij de emulatorinstantie.
- Deze poorten zijn alleen-lezen; schrijfoperaties volgen de bestaande foutafhandeling voor ongeldige I/O.
- 16-bits I/O op deze poorten is niet ondersteund en mag geen gedeeltelijke timeroperatie uitvoeren.
- Verander geen bestaande console-, schijf- of andere I/O-functies.
- CP/M en zijn BDOS hoeven hiervoor niet aangepast te worden.

B1h identificeert precies dit protocol. Dit is een emulatoruitbreiding, geen standaard-CP/M-functie.

## 4. Gebruik vanuit 8086-assembler

Onderstaand voorbeeld gebruikt Intel/NASM-notatie en uitsluitend oorspronkelijke 8086-instructies.

Controleer eerst de beschikbaarheid:

```asm
    in   al, 0F0h
    cmp  al, 0B1h
    jne  timer_unavailable
```

Lees daarna de teller met deze routine:

```asm
; Uitkomst: DX:AX = unsigned 32-bits millisecondenteller.
; BX blijft behouden. Flags worden niet behouden.

read_milliseconds:
    push bx

    in   al, 0F1h
    mov  bl, al

    in   al, 0F2h
    mov  bh, al

    in   al, 0F3h
    mov  dl, al

    in   al, 0F4h
    mov  dh, al

    mov  ax, bx
    pop  bx
    ret
```

Sla de begintijd op en trek die later van de eindtijd af:

```asm
    call read_milliseconds
    mov  [start_low], ax
    mov  [start_high], dx

    ; Hier staat het te meten werk.

    call read_milliseconds
    sub  ax, [start_low]
    sbb  dx, [start_high]

    ; DX:AX bevat nu de verstreken milliseconden.
```

Unsigned aftrekking verwerkt teller-overloop correct zolang de meetduur korter is dan 2^32 milliseconden en de tijdbron tussentijds niet wordt gereset.

Let op: een beschikbaarheidscheck via F0h is alleen veilig op emulatorversies waarin deze poort is geïmplementeerd. Oude firmware kan onbekende poorten als fout behandelen. Documenteer daarom welke firmwareversie deze API introduceert.

## 5. Validatie

Voeg gerichte tests toe:

1. **Capability:** met callback geeft F0h B1h; zonder callback geeft F0h 00h.
2. **Bytevolgorde:** een aangeboden waarde 12345678h levert via F1h–F4h achtereenvolgens 78h, 56h, 34h en 12h.
3. **Consistente momentopname:** verander de testtijd na het lezen van F1h. F2h–F4h moeten de oorspronkelijke momentopname blijven teruggeven.
4. **Aantal aanroepen:** alleen F1h roept de tijdcallback aan, precies eenmaal per read.
5. **Nieuwe momentopname:** een volgende F1h-read pakt de nieuwe testtijd.
6. **Reset:** de momentopname wordt nul; de externe tijdbron blijft ongemoeid.
7. **Ontbrekende tijdbron:** alle tijdbytes zijn nul en de emulator crasht niet.
8. **Ongeldige toegang:** writes en 16-bits I/O worden volgens het vastgelegde contract afgehandeld.
9. **Overloop:** begintijd FFFFFFF0h en eindtijd 00000018h leveren 40 ms.
10. **Gastuitvoering:** voer de assemblerleesroutine daadwerkelijk door de CPU-emulator uit en controleer DX:AX en het behoud van BX.
11. **Regressie:** bestaande emulator- en I/O-tests blijven slagen.
12. **Firmwarebouw:** bouw de aangepaste ESP32-S3-firmware.

Beschrijf daarnaast een korte hardwarecontrole voor na het flashen: lees de teller, wacht ongeveer één seconde in de hostlaag en lees opnieuw. Controleer dat het verschil ongeveer 1000 ms is met een expliciet vermelde tolerantie. Gebruik geen gastinstructielus als gekalibreerde wachttijd.

## 6. Oplevering

Rapporteer:

- Welke bestaande onderdelen zijn aangepast.
- De definitieve poortindeling en API.
- Welke tests werkelijk zijn uitgevoerd en hun uitkomst.
- Eventuele afwijkingen van deze specificatie.
- Welke hardwarecontrole nog openstaat.

Maak onderscheid tussen een geslaagde firmwarebouw, desktoptests en werkelijk uitgevoerde hardwaretests.

Ontwerp de callback zo dat andere CPU-emulators dezelfde tijdbron later kunnen gebruiken. Voeg de gast-API nu alleen toe aan de CP/M-86-emulator; ondersteuning in andere emulators vereist een eigen expliciete koppeling.