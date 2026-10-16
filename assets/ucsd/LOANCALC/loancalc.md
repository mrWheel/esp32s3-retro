# Ontwerp LoanCalc programma

## Uitgangspunten

- Programmeertaal: UCSD Pascal
- Omgeving: UCSD Emulator ESP32S3
- Uitgangspunt: Dezelfde functionaliteit als het Python programma
- Invoer van menu's is READ(x), noet READLN(). Geen echo van het ingevoerde character
- Invoer van tekstvelden: de input-routine staat nooit meer tekens toe dan de
  veldlengte (bijv. Name 24, Title 38, getallen 20, datum 10) om
  String overflows te voorkomen.
- Code regels moegen maximaal 79 posities lang zijn!
- UCSD Pascal 1.3 identifiers have only 8 significant characters. Keep every
  variable, parameter, constant, type, field, function and procedure name to
  at most 8 characters, and make names unique within their first 8 characters.
  Check new and renamed identifiers against this limit before compiling.
- Alle code, comments en userinterface language: Engels§
- Doelstelling: Zo MIN mogelijk scherm veranderingen. Als er 1 regel veranderd, dan 
  alleen de verandering in die regel schrijven en de rest ongemoeid laten.

### CODING rules

- BEGIN en END staan ALTIJD recht onder elkaar.2 spaties inspringen.
```
    IF <condition> THEN
    BEGIN
      ...
      ...
    END

1) Er moet een algemene "MenuRegel" procedure komen die als parameter de menu-tekst krijgt
Zorgt dat deze eventueel afgekapt wordt op maximaal 77 posities en indien nodug wordt
voorzien door een ", ?" aan het eind waarmee de afgekapte menu-tekst om beurten op
regel 0,0 wordt weergegeven. De procedure geeft de ingevoerde letter terug aan de
aanroepende prodedure.

2) Voor ieder menu moet er een procedure komen die de functies in die procedure 
aanroept en uitvoert.


### Speciale tekens

{ UCSD Apple Pascal 1.1 - Schermcodes }

- CHR(7)   { BEL - Geluidssignaal }
- CHR(8)   { BS  - Cursor links }
- CHR(9)   { TAB - Horizontale tab }
- CHR(10)  { LF  - Regel omlaag }
- CHR(11)  { Wis tot einde scherm }
- CHR(12)  { Wis volledig scherm }
- CHR(13)  { CR  - Begin van regel }
- CHR(25)  { HOME - Cursor naar linksboven }
- CHR(28)  { Cursor rechts }
- CHR(29)  { Wis tot einde regel }
- CHR(31)  { Cursor omhoog }

Overview:

Variabele in het programma  Wat het betekent in deze constructie
L(oan Amount                - Het geleende bedrag 
I(nterest Rate %            - De marktconforme hoge rente
D(uration                   - De looptijd in maanden
R(ebate %                   - Teruggave
M(onthly Payment            - De bruto annuïteit
N(et Cost                   - Netto annuiteit



## Besturing

Het programma moet exect dezelfde besturing hebben als de xyz.SYSTEM programma's
van het UCSD Besturings systeem.

Dus: Altijd op de eerste regel (positie 0,0), een menu regel die langer is dan 80 
posities voor de optie die niet meer past een "?". Door een vraagteken in te voeren
switcht de menu regel naar een volgend deel of weer terug naar het eerste deel.

## Menu's

Het scherm ziet er altijd zo uit:

-----------------
00 |Menu regel
01 |
02 |<invoer1>
03 |<invoer2>
04 |<invoer3>
05 |
06 | <Data voor dit scherm> (meerdere regels, altijd vanaf regel 06>
07 | <data>
.. | <data>
.. |
23 | <Status regel>
-----------------



Op dit moment zijn de volgende menu's geidentificeerd"

### Main menu

Loancalc: L(oans, D(ata, P(rint, E(xit program

Voorbeeld:

-----------------
00 |Loancalc: L(oans, D(ata, P(rint, E(xit program
01 |
02 |
03 |
04 |
05 |
07 | Name...................: <LoanName>
08 | Titel..................: <TitelVal>
09 | Loan Amount............: <PrincipalVal>
10 | Duration...............: <DurationVal>
11 | Interest Rate %........: <InterestRateVal>
12 | Rebate %...............: <RebateVal>
13 | Monthly Payment........: <MontlyPaymentVal>
14 | No repayment (On/Of)...: <On|Off>
15 | Fixed monthly amount...: <FixedAmountVal> 
16 | enz.
14 |
.. |
23 |<Status regel>
-----------------


### Loans menu

Loans: S(elect loan, N(ew loan, R(emove loan, Q(uit

- Clear screen
- List known loans voorafgegaan door een nummer. 
- Select Loan:

Invoer bij Select loan: nummer van de lening of <Ret> alleen is terug naar Loans-menu

Voorbeeld (Select loan):
------------
00 |Loans: S(elect, N(ew), R(emove), [previous, ]next, Q(uit
01 |
02 |Select Loan number (<Ret> is exit):
03 |
04 |  1. Lening A
05 |  2. Lening B
06 |  3. Speciale lening
07 |  4. Enz...
08 |
.. |
23 |<status regel>
------------

Loans are displayed in pages of up to 19 entries, starting at row 04, so
the selection prompt and status row remain available. Use `[` and `]` to
change pages when there are more than 19 loans.

Invoer bij New loan: <Ret> alleen is terug naar Loans-menu

Voorbeeld (New loan):
------------
00 |New Loan: N(ame, T(itel, S(ave, Q(uit
01 |
02 | Loan Name...: _________________
03 | Loan Titel..: ___________________________
04 |
.. |
.. |
23 |<Status regel (New Loan saved) enz.>
------------


Invoer bij Remove loan: nummer van de lening of <Ret> alleen is terug naar Loans-menu

Voorbeeld (Remove loan):
------------
00 |Loans: S(elect loan, N(ew loan, R(emove loan, Q(uit
01 |
02 |Remove Loan number (<Ret> is exit):
03 |
04 |  1. Lening A
05 |  2. Lening B
06 |  3. Speciale lening
.. |  4. Enz...
.. |
.. |
23 |<Status regel>
------------


### Data menu

Data: P(rincipal, D(uration, P(ercentage, S(chenking, ...Q(uit

-----------------
00 |Data: L(oan, D(uration, I(nterest, P(ayment, R(epayment, F(ixed Amount, S(ave, Q(uit
01 |
02 |
03 |
04 |
05 |
07 | Name...................: [LoanName]
08 | Titel..................: <TitelVal>
09 | Loan Amount............: <PrincipalVal>
10 | Duration...............: <DurationVal>
11 | Interest Rate %........: <InterestRateVal>
12 | Rebate %...............: <RebateVal>
13 | Monthly Payment........: <MontlyPaymentVal>
14 | No repayment (On/Of)...: <On|Off>
15 | Fixed monthly amount...: <FixedAmountVal> 
16 | enz.
14 |
.. |
23 |<Status regel>
-----------------

Als "R" (Rebate) wordt ingetoetst dan verschijnt er op regel 02:

02 | Rebate Percentage (only <Ret> keeps value): ___

Zodra een waarde is ingevuld verschijnt deze op regel 12. Als alleen
<Ret> wordt ingetoetst, dan blijft de oude (al bekend) waarde bestaan.

Deze methodiek geldt voor ieder in te voeren veld.
Het veld "Name" [LoadName] kan niet worden gemuteerd.

Als op "Q" (Quit) wordt gedrukt zonder eerst "S" (Save) dan worden de nieuwe 
waarden niet opgeslagen en wordt terug gespromen naar het Hoofd Menu
Wort er op "S" (Save) gedrukt dan verschijnt er op de status regel een
melding "Saving data". Gaat dat goed dan wordt de regel "Data Saved".
Is er een fout opgetreden dan verschijnt de melding "Error saving data".

### Report menu

The report menu offers `S(creen`, `P(rinter`, and `Q(uit`. Screen output is
shown directly in pages and does not create a report file. The printer option
writes `LOANREPORT.TEXT`. Report lines are at most 78 characters; a status dot
is added for every monthly report block written to the printer file. The
report is limited to 78 columns; screen output clears from the top-left with
`CHR(11)` before the first report line. The monthly date column is 7 characters
wide; each amount column is 10 characters, with one space between amounts.
Amounts that cannot fit their signed 10-character `0.00` field are shown as
`**********`. The month headings, monthly values and year/overall totals are
each written on one line, with the extra, balance and rebate columns following
net principal.



# Implementatiestatus en compiler-bevindingen (LOANCALC.TEXT)

Doel: LOANCALC.TEXT compileert in UCSD/Apple Pascal 1.3 en heeft dezelfde
functionaliteit als loancalc.py (zonder PDF). Namen zijn uniek binnen de
eerste 8 tekens.

## Status
- De eerdere versie compileerde zonder fouten (1241 regels, ~15000 woorden
  vrij); de wijzigingen hieronder moeten nog opnieuw worden gecompileerd.
- Nog NIET functioneel getest in de emulator (rapport vergelijken met
  loancalc.py is open).

## Gedaan
- Namen > 8 tekens hernoemd; collisiecheck schoon.
- Functionaliteit uit loancalc.py: vaste maandbetaling (N), geen aflossing
  (F), rente per maand gewogen op dagen, rebate/netto, jaartotalen en
  eindtotaal, validaties (rebate <= rente <= 100, <= 1200 maanden,
  bedrag <= MaxAmt, jaargrens 9999), extra aflossingen binnen looptijd.
- PrintRpt gesplitst in RptHead/RptYear/RptExtr/RptPay/RptMonth/RptTotal
  met globale rapportstatus (r...) wegens fout 253.
- Typed files met f^ / GET / PUT; ontbrekend databestand via (*$I-*) en
  IORESULT; CLOSE(f, LOCK) bij REWRITE.

## Werkt (geverifieerd met de compiler)
CONCAT, COPY, LENGTH, DELETE, ORD, CHR, TRUNC, ABS, GOTOXY, WRITE met
:w:d, EXIT(procnaam), STRING[n], s[i] := ch, CASE met lijst 1,3,5:,
VAR-parameters (STRING/record), WRITELN(textfile, ...), REAL-rekenwerk.

## Werkt NIET - niet opnieuw proberen
- Identifiers have only 8 significant characters: names longer than 8 can
  collide or fail to compile. Keep every declared name at 8 characters or
  fewer; ensure the first 8 characters are unique.
- EXIT zonder argument: fout 9. Gebruik EXIT(procnaam)  <- oorzaak van
  de ParseDat-fout (regel 451).
- READ/WRITE op FILE OF record: fout 125. Gebruik f^, GET, PUT.
- POS(char, str): fout 125. Handmatige lus.
- CASE ... ELSE: fout 6/13/50.
- STR(x, s) en VAL(s, x, err): fouten 6/59/104. Eigen ParseNum/ParseInt.
- STRING + STRING / STRING + char: fout 134. Gebruik CONCAT.
- Functie die STRING of record teruggeeft: fout 120. Procedure met VAR.
- LONGINT: fout 104. REAL gebruikt.
- Functieresultaat teruglezen (X := f; IF f ...): fout 126. Lokale var.
- Vergelijking met AND zonder haakjes (a > 0.0 AND b): fout 134.
- VAR STRING-parameter met STRING[12]-variabele: fout 175. Declareer
  de variabele als STRING.
- Te grote procedure (PrintRpt): fout 253. Splitsen.
- INTEGER max 32767.

## Compileren in de emulator
Zie /tmp-script: F, P, WORK1:, Q, C, WORK1:LOANCALC, WORK1:LOANCALC.
Aanroep: UCSD_STAGE_EXTRA="LOANCALC.TEXT=<pad>" apple2DiskTests
--pascal-script <boot.dsk> <rom> <tools.po> <work.po> HOST.PAS HOSTIO.ASM
<script> <out.po>  (volgorde boot, rom, tools, work).

## Open
- Run-test in emulator en vergelijken met loancalc.py.
- Compile and emulator-test screen clearing, changed-row redraw, paged Loans
  list, report destination menu, progress dots, and 79-column output.
- MaxAmt = 1.0E12 en 79-kolomsregel zijn gecontroleerd/compileren; geen
  regels > 79.

# Runtime-test (emulator) - logboek

Run-test: script `/tmp/loancalc-compile.txt` met `type`/`wait`/`run`/`dump`
(zie tests/ucsdHost/build.txt voor de syntax); na compile `type X`,
`type WORK1:LOANCALC\r`, daarna toetsen sturen. `run` > ~600 is erg traag.

## Gemelde fouten bij Opties -> Loans -> New loan  (OPGELOST)
1. Tekst "Name" sprong naar linker kantlijn: Ask() zette de prompt op
   kolom 0 van regel 2. Nu SetField(2, ...) + labels op kolom 26 (zelfde
   layout als LoanSum), invoer op FieldCol.
2. Ingetoetste tekens 2x getoond ("TTeesstt11"): READ(c) echo't zelf en
   ReadLine echo'de ook (WRITE(c)). Nu GEEN eigen echo in ReadLine; menu
   en overige losse toetsen via READ(KEYBOARD, c) (geen echo).
3. <Enter> ging naar nieuwe regel en menu onbereikbaar: een CR komt bij
   READ(c) binnen als ' ' (spatie) met EOLN = TRUE (gemeten: ORD=32,
   EOLN=1), niet als CHR(13). ReadLine beeindigt nu op EOLN. NIET doen:
   EOLN(KEYBOARD) (geeft meteen ' '/TRUE, regel bleef leeg) en niet
   testen op CHR(13) alleen.
4. Naam/titel bleef leeg na invoer: COPY(s, 1, n) met n > LENGTH(s)
   geeft in Pascal 1.3 een LEGE string. Nooit COPY met te grote lengte;
   gebruik DELETE(s, n+1, LENGTH(s)-n) om in te korten.
5. "Value range error" in SetField: old[1] op lege string, want AND
   kortsluit niet. Lus herschreven met vlag (geen BREAK in 1.3).

## Loans-menu cijferinvoer en rapport (AANGEPAST, EMULATORTEST OPEN)
- UCSD Pascal evalueert beide kanten van AND/OR; controleer een stringindex
  daarom pas nadat de indexgrens apart is gecontroleerd. Een ongeldig cijfer
  bij de Loans-menuopties mag alleen een BEL geven, geen Value range error.
- `S` opent de selectieprompt; het leningnummer wordt daarna ingevoerd.
- `S(cherm` schrijft het rapport direct naar het scherm, in pagina's, zonder
  eerst `LOANREPORT.TEXT` te maken. `P(rinter` schrijft het bestand.
- Scherm- en bestandsregels blijven maximaal 79 tekens breed.

## Data-menu "String overflow" (OPGELOST)
- STRING-parameters/CONCAT-resultaten zijn maximaal 80 tekens; de Data-menutekst
  is ~110 tekens. Nu opgebouwd met INSERT in een Str255 (STRING[255]);
  MenuLine heeft een Str255-parameter. Geen CONCAT voor tekst > 80.
- ReadLine/Ask hebben een maxLen-parameter (veldlengte).

## Extra compilerbevindingen
- WRITE van BOOLEAN: fout 125. Gebruik ORD(b).
- Geen kortsluiting bij AND/OR: s[i] nooit in dezelfde conditie als de
  LENGTH-controle.
- Geen BREAK/CONTINUE.

## Getest in emulator (werkt)
Hoofdmenu -> L -> N -> N(ame "Test1") -> T(itle "Tit") -> S -> lijst
toont "1. Test1".
## Nog niet getest
D(ata) invoer, Extra, Print/rapport vs loancalc.py, R(emove), S(elect).
