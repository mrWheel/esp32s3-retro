#!/usr/bin/env python3
"""Lineaire lening, Python 3.9+. Alleen PDF-export vereist reportlab."""
import json
import os
import re
import tempfile
from xml.sax.saxutils import escape
from datetime import date
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path

D = Decimal
CENT = D('0.01')
MAP = Path(__file__).resolve().parent
BESTAND = MAP / 'extra_aflossingen.json'


def geld(x):
    return D(x).quantize(CENT, rounding=ROUND_HALF_UP)


def getal(tekst):
    """Accepteert 10.000,00, 10000.00 en 10.000 (Nederlandse duizendtallen)."""
    s = tekst.strip().replace('€', '').replace('%', '').replace(' ', '')
    if ',' in s:
        s = s.replace('.', '').replace(',', '.')
    elif re.fullmatch(r'[+-]?\d{1,3}(\.\d{3})+', s):
        s = s.replace('.', '')
    try:
        n = D(s)
    except InvalidOperation:
        raise ValueError('Vul een geldig getal in.') from None
    if not n.is_finite():
        raise ValueError('Vul een eindig getal in.')
    return n


def euro(n):
    return f'{n:,.2f}'.replace(',', '_').replace('.', ',').replace('_', '.')


def maand(start, offset):
    m = start.year * 12 + start.month - 1 + offset
    return date(m // 12, m % 12 + 1, 1)


def schema(data):
    """Aflossing einde maand; extra betaling werkt vanaf de opgegeven datum.

    Rente: jaarrente / 12, binnen maand gewogen naar kalenderdagen.
    Extra betaling verkort de looptijd; de vaste aflossing blijft gelijk.
    Alle geldstromen worden HALF_UP op centen afgerond.
    """
    try:
        if not isinstance(data.get('omschrijving', ''), str):
            raise ValueError('Omschrijving moet tekst zijn.')
        if data['versie'] != 1:
            raise ValueError('Onbekende bestandsversie.')
        aflossingsvrij = data.get('aflossingsvrij', False)
        if type(aflossingsvrij) is not bool:
            raise ValueError('aflossingsvrij moet true of false zijn.')
        hoofdsom = D(data['hoofdsom'])
        rente = D(data['rente_pct'])
        gift = D(data['schenking_pct'])
        maanden = data['maanden']
        start = date.fromisoformat(data['startdatum'])
        if not all(v.is_finite() for v in (hoofdsom, rente, gift)):
            raise ValueError('Niet-eindig getal in bestand.')
        if hoofdsom <= 0 or hoofdsom > D('1000000000000') or hoofdsom != geld(hoofdsom):
            raise ValueError('Hoofdsom moet positief zijn, maximaal 1 biljoen, met maximaal 2 decimalen.')
        if not 0 <= gift <= rente <= 100:
            raise ValueError('Vereist: 0 <= schenkingpercentage <= rentepercentage <= 100.')
        if type(maanden) is not int or not 1 <= maanden <= 1200:
            raise ValueError('Gebruik 1 t/m 1200 maanden.')
        vast_bedrag = data.get('vaste_aflossing')
        if vast_bedrag is not None:
            vast_bedrag = D(vast_bedrag)
            if not vast_bedrag.is_finite() or vast_bedrag <= 0 or vast_bedrag != geld(vast_bedrag):
                raise ValueError('Vaste aflossing moet positief zijn met maximaal twee decimalen.')
            if aflossingsvrij:
                raise ValueError('Kies aflossingsvrij of een vaste aflossing.')
            maanden = (9999 - start.year) * 12 + 12 - start.month
        einde = maand(start, maanden)
        extras = []
        for e in data['extra_aflossingen']:
            dag, bedrag = date.fromisoformat(e['datum']), D(e['bedrag'])
            if not bedrag.is_finite() or bedrag <= 0 or bedrag != geld(bedrag):
                raise ValueError('Extra aflossing moet positief zijn met maximaal 2 decimalen.')
            if not start <= dag < einde:
                raise ValueError('Extra aflossing valt buiten de looptijd.')
            extras.append((dag, bedrag))
    except (KeyError, TypeError, InvalidOperation, OverflowError) as exc:
        raise ValueError('Ongeldige gegevens in het leningbestand.') from exc
    extras.sort()
    saldo, vast = hoofdsom, geld(hoofdsom / data['maanden'])
    regels = []
    for i in range(maanden):
        if saldo == 0:
            break
        begin, eind = maand(start, i), maand(start, i + 1)
        openingssaldo = saldo
        aantal_dagen = (eind - begin).days
        vorige, gewogen, extra = max(begin, start), D(0), D(0)
        toegepaste_extras = []
        for dag, bedrag in (e for e in extras if begin <= e[0] < eind):
            gewogen += saldo * (dag - vorige).days
            bedrag = min(bedrag, saldo)
            saldo -= bedrag
            extra += bedrag
            if bedrag > 0:
                toegepaste_extras.append(dict(datum=dag.isoformat(), bedrag=bedrag))
            vorige = dag
        gewogen += saldo * (eind - vorige).days
        maandrente = geld(gewogen * rente / (1200 * aantal_dagen))
        schenking = geld(gewogen * gift / (1200 * aantal_dagen))
        if vast_bedrag is not None:
            if maandrente > vast_bedrag:
                raise ValueError(f'Het maandbedrag EUR {euro(vast_bedrag)} dekt de rente van EUR {euro(maandrente)} in {begin:%m-%Y} niet. Kies een hoger bedrag of een eerdere extra aflossing.')
            aflossing = min(saldo, vast_bedrag - maandrente)
            if saldo > 0 and aflossing == 0 and not any(dag >= eind for dag, _ in extras):
                raise ValueError('Het maandbedrag betaalt alleen rente; zonder toekomstige extra aflossing wordt de schuld niet nul. Kies aflossingsvrij of een hoger maandbedrag.')
        else:
            aflossing = D(0) if aflossingsvrij else (saldo if i == maanden - 1 else min(vast, saldo))
        saldo -= aflossing
        if openingssaldo == 0:
            continue
        regels.append(dict(maand=begin.strftime('%m-%Y'), rente=maandrente,
                           aflossing=aflossing, extra=extra, restschuld=saldo,
                           schenking=schenking, netto_aflossing=aflossing - schenking,
                           toegepaste_extras=toegepaste_extras))
    if vast_bedrag is not None and saldo > 0:
        raise ValueError('De lening is niet afgelost binnen het ondersteunde datumbereik.')
    return regels


VELDEN = ['rente', 'aflossing', 'netto_aflossing', 'extra', 'restschuld', 'schenking']


def totaal(regels, label):
    """Geldstromen optellen; restschuld is het eindsaldo, geen som."""
    return dict(maand=label, **{
        k: regels[-1][k] if k == 'restschuld' else sum((r[k] for r in regels), D(0))
        for k in VELDEN})


def overzichtsregels(regels):
    """Maandregels, jaarafsluitingen (ook laatste deeljaar), eindtotaal."""
    resultaat, jaarregels = [], []
    for i, regel in enumerate(regels):
        resultaat.append(('maand', regel))
        jaarregels.append(regel)
        jaar = regel['maand'][-4:]
        if i == len(regels) - 1 or regels[i + 1]['maand'][-4:] != jaar:
            resultaat.append(('jaar', totaal(jaarregels, f'Jaar {jaar}')))
            jaarregels = []
    resultaat.append(('totaal', totaal(regels, 'Totaal')))
    return resultaat


def schrijf_json(data, pad):
    tijdelijk = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=pad.parent,
                                         prefix='.lening-', suffix='.tmp', delete=False) as f:
            tijdelijk = Path(f.name)
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.write('\n')
            f.flush()
            os.fsync(f.fileno())
        os.replace(tijdelijk, pad)
    finally:
        if tijdelijk is not None and tijdelijk.exists():
            tijdelijk.unlink()


def paden(naam):
    return (MAP / f'lening_{naam}.json', MAP / f'aflossing_{naam}.json',
            MAP / f'lening_{naam}.pdf')


def laden(naam):
    leningpad, extrapad, _ = paden(naam)
    data = json.loads(leningpad.read_text(encoding='utf-8'))
    if not isinstance(data, dict):
        raise ValueError('Het leningbestand moet een JSON-object bevatten.')
    extras = json.loads(extrapad.read_text(encoding='utf-8'))
    if not isinstance(extras, list):
        raise ValueError('Het aflossingsbestand moet een JSON-lijst bevatten.')
    data['extra_aflossingen'] = extras
    schema(data)
    return data


def bewaren_extra(data, naam):
    schema(data)
    schrijf_json(data['extra_aflossingen'], paden(naam)[1])


def aanmaken(data, naam):
    schema(data)
    leningpad, extrapad, pdfpad = paden(naam)
    bestaand = {p.name.casefold() for p in MAP.iterdir()}
    if any(p.name.casefold() in bestaand for p in (leningpad, extrapad, pdfpad)):
        raise ValueError('Deze naam is al in gebruik. Kies een andere naam.')
    schrijf_json(data['extra_aflossingen'], extrapad)
    schrijf_json({k: v for k, v in data.items() if k != 'extra_aflossingen'}, leningpad)
    print('Opgeslagen:', leningpad, 'en', extrapad)


def pdf(data, pad, portret=False):
    try:
        from reportlab.lib import colors
        from reportlab.pdfbase.pdfmetrics import stringWidth
        from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
        from reportlab.lib.pagesizes import A4, landscape
        from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle
    except ImportError:
        raise ValueError('PDF-export vereist reportlab. Installeer met: python3 -m pip install -r requirements.txt') from None
    pagina = A4 if portret else landscape(A4)
    regels = schema(data)
    styles = getSampleStyleSheet()
    for stijl in styles.byName.values():
        if isinstance(stijl, ParagraphStyle):
            stijl.fontSize += 2
            stijl.leading += 2
    compact = ParagraphStyle('CompactIntro', parent=styles['BodyText'], spaceBefore=0, spaceAfter=0, leading=14)
    omschrijving_stijl = ParagraphStyle('Omschrijving', parent=compact,
                                       fontName='Helvetica-Bold', fontSize=15,
                                       leading=18, spaceAfter=10)
    inhoud = []
    omschrijving = data.get('omschrijving', '').strip()
    if omschrijving:
        inhoud.append(Paragraph(escape(omschrijving).replace('\n', '<br/>'), omschrijving_stijl))
    inhoud.append(Paragraph('Lineaire lening', styles['Title']))
    for i, tekst in enumerate([
        f"Hoofdsom: EUR {euro(D(data['hoofdsom']))}",
        f"Betaalwijze: {aflossing_label(data)}",
        f"Werkelijke looptijd: {len(regels)} maanden | Laatste maand: {regels[-1]['maand']}",
        f"Start: {date.fromisoformat(data['startdatum']).strftime('%d-%m-%Y')} | Jaarrente: {data['rente_pct']}% | Schenking: {data['schenking_pct']}% per jaar",
        ('Aflossingsvrij: reguliere aflossing is EUR 0,00. Alleen rente wordt betaald, naast eventuele extra aflossingen. Restschuld blijft aan het einde staan.'
         if data.get('aflossingsvrij', False) else
         'Vast maandbedrag inclusief rente: aflossing = maandbedrag - rente. De laatste betaling kan lager zijn. Extra betalingen worden beperkt tot de restschuld; na volledige aflossing stoppen betalingen.'
         if data.get('vaste_aflossing') is not None else
         'Betaling aan het einde van elke maand. Extra aflossing verkort de looptijd; de vaste maandaflossing blijft gelijk. Alleen de slotaflossing kan afwijken door restschuld of centafronding.'),
        'Rente: jaarrente / 12, gewogen naar kalenderdagen. In de eerste maand telt alleen de periode vanaf de startdatum mee. Extra betalingen werken vanaf de betaaldatum.',
        'Netto Aflossing = Aflossing - Schenking. Jaarregels tonen jaartotalen; restschuld is het saldo aan het einde van dat jaar of bij volledige aflossing.',
        'Het schenkingpercentage is een rekeninvoer; het programma bepaalt niet of de schenking fiscaal is vrijgesteld. Alle bedragen in EUR.',
    ]):
        inhoud += [Paragraph(tekst, compact), Spacer(1, 12 if i in (2, 3) else 2)]
    uitvoer = overzichtsregels(regels)
    tabel = [['Maand', 'Rente', 'Aflossing', 'Netto\nAflossing', 'Extra', 'Restschuld', 'Schenking\nRente']]
    rijsoorten = []
    for soort, r in uitvoer:
        tabel.append([r['maand']] + [euro(r[k]) for k in VELDEN])
        rijsoorten.append(soort)
        if soort == 'jaar':
            tabel.append([''] * 7)
            rijsoorten.append('leeg')
    breedtes = ([58, 78, 82, 82, 78, 88, 81] if portret else
                [80, 105, 110, 110, 105, 125, 110])
    lettergrootte = 10
    # Laat uitzonderlijk lange bedragen afbreken, zonder kleiner lettertype.
    for rijnummer, rij in enumerate(tabel):
        vet = rijnummer == 0 or rijsoorten[rijnummer - 1] in ('jaar', 'totaal')
        font = 'Helvetica-Bold' if vet else 'Helvetica'
        for kolom, tekst in enumerate(rij):
            if any(stringWidth(regel, font, lettergrootte) > breedtes[kolom] - 8
                   for regel in tekst.split('\n')):
                celstijl = ParagraphStyle('Cel', fontName=font,
                                          fontSize=lettergrootte, leading=12,
                                          alignment=0 if kolom == 0 else 2,
                                          textColor=colors.white if rijnummer == 0 else colors.black,
                                          wordWrap='CJK')
                rij[kolom] = Paragraph(escape(tekst).replace('\n', '<br/>'), celstijl)
    t = Table(tabel, colWidths=breedtes, repeatRows=1)
    t.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#17344b')),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
        ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
        ('FONTSIZE', (0, 0), (-1, -1), lettergrootte),
        ('LEFTPADDING', (0, 0), (-1, -1), 4),
        ('RIGHTPADDING', (0, 0), (-1, -1), 4),
        ('ALIGN', (1, 0), (-1, -1), 'RIGHT'),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('TOPPADDING', (0, 0), (-1, -1), 6),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 6),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.whitesmoke, colors.white]),
        ('FONTNAME', (0, -1), (-1, -1), 'Helvetica-Bold'),
    ]))
    for i, soort in enumerate(rijsoorten, 1):
        if soort == 'leeg':
            t.setStyle(TableStyle([
                ('BACKGROUND', (0, i), (-1, i), colors.white),
                ('NOSPLIT', (0, i - 1), (-1, i)),
            ]))
        elif soort != 'maand':
            t.setStyle(TableStyle([
                ('BACKGROUND', (0, i), (-1, i), colors.HexColor('#dce8ef')),
                ('FONTNAME', (0, i), (-1, i), 'Helvetica-Bold'),
            ]))
    inhoud += [Spacer(1, 10), t]
    toegepaste_extras = [e for r in regels for e in r['toegepaste_extras']]
    if toegepaste_extras:
        inhoud += [Spacer(1, 14), Paragraph('Toegepaste extra aflossingen', styles['Heading2'])]
        for e in toegepaste_extras:
            datumtekst = date.fromisoformat(e['datum']).strftime('%d-%m-%Y')
            inhoud.append(Paragraph(f"{datumtekst}: EUR {euro(e['bedrag'])}", styles['BodyText']))
    def voet(canvas, doc):
        canvas.setFont('Helvetica', 10)
        canvas.drawRightString(pagina[0] - 24, 20, f'Pagina {doc.page}')
    SimpleDocTemplate(str(pad), pagesize=pagina, leftMargin=24, rightMargin=24,
                      topMargin=30, bottomMargin=36).build(inhoud, onFirstPage=voet, onLaterPages=voet)


def vraag(label, omzetter=str):
    while True:
        try:
            return omzetter(input(label + ': ').strip())
        except (ValueError, InvalidOperation) as e:
            print('Ongeldige invoer:', e)


def bereik(tekst, minimum, maximum, centen=False):
    n = getal(tekst)
    if not minimum <= n <= maximum:
        raise ValueError(f'Gebruik een getal van {minimum} t/m {maximum}.')
    if centen and n != geld(n):
        raise ValueError('Gebruik maximaal twee decimalen.')
    return n


def datum(tekst):
    for formaat in ('%Y-%m-%d', '%d-%m-%Y', '%d/%m/%Y'):
        try:
            from datetime import datetime
            return datetime.strptime(tekst, formaat).date()
        except ValueError:
            pass
    raise ValueError('Gebruik JJJJ-MM-DD of DD-MM-JJJJ.')


def ja_nee(tekst):
    if tekst.lower() in ('ja', 'j'):
        return True
    if tekst.lower() in ('nee', 'n'):
        return False
    raise ValueError('Antwoord met ja of nee.')


def vraag_vast_bedrag(data):
    print('Dit is het totale maandbedrag INCLUSIEF rente. Het restant is aflossing. Extra betalingen worden begrensd op de restschuld.')
    while True:
        tekst = input('Vast maandbedrag inclusief rente (bijv. 200; Enter of a = annuleren): ').strip()
        if not tekst or tekst.lower() in ('a', 'annuleren'):
            raise ValueError('Geannuleerd; er is niets opgeslagen.')
        try:
            bedrag = bereik(tekst, D('0.01'), D('1000000000000'), True)
        except (ValueError, InvalidOperation) as e:
            print('Ongeldige invoer:', e)
            continue
        break
    nieuw = dict(data, aflossingsvrij=False, vaste_aflossing=str(bedrag))
    # Schemafouten horen bij de lening, niet bij de getalinvoer.
    # Laat de menu-afhandeling deze tonen, zonder de invoerlus te herhalen.
    schema(nieuw)
    return nieuw


def aflossing_label(data):
    if data.get('aflossingsvrij', False):
        return 'EUR 0,00 (aflossingsvrij)'
    if data.get('vaste_aflossing') is not None:
        return f"EUR {euro(D(data['vaste_aflossing']))} per maand inclusief rente, tot restschuld nul is"
    return 'lineair op basis van de ingevoerde looptijd'


def nieuwe_lening():
    # Elk veld wordt direct gecontroleerd; eerdere antwoorden blijven behouden.
    omschrijving = vraag('Omschrijving (mag leeg blijven)')
    hoofdsom = vraag('Hoofdsom (bijv. 10.000,00)',
                     lambda s: bereik(s, D('0.01'), D('1000000000000'), True))
    rente = vraag('Te betalen jaarrente % (bijv. 6)', lambda s: bereik(s, 0, 100))
    gift = vraag('Schenking als jaarlijks % van de schuld (bijv. 3)',
                 lambda s: bereik(s, 0, rente))
    def looptijd(s):
        n = int(s)
        if not 1 <= n <= 1200:
            raise ValueError('Gebruik 1 t/m 1200 maanden.')
        return n
    maanden = vraag('Looptijd in maanden', looptijd)
    def startdatum(s):
        d = datum(s)
        maand(d, maanden)
        return d.isoformat()
    start = vraag('Startdatum (bijv. 01-10-2026)', startdatum)
    data = dict(versie=1, omschrijving=omschrijving, hoofdsom=str(hoofdsom), rente_pct=str(rente),
                schenking_pct=str(gift), maanden=maanden, startdatum=start,
                extra_aflossingen=[])
    data['aflossingsvrij'] = vraag('Reguliere aflossing op 0 zetten? (ja/nee)', ja_nee)
    if not data['aflossingsvrij'] and vraag('Zelf een vast maandbedrag inclusief rente invoeren? (ja/nee)', ja_nee):
        data = vraag_vast_bedrag(data)
    schema(data)
    return data


def nieuwe_naam(tekst):
    if not re.fullmatch(r'[\w -]{1,60}', tekst, flags=re.UNICODE) or not tekst.strip(' _-'):
        raise ValueError('Gebruik 1 t/m 60 letters, cijfers, spaties, streepjes of underscores.')
    bestaand = {p.name.casefold() for p in MAP.iterdir()}
    if any(p.name.casefold() in bestaand for p in paden(tekst)):
        raise ValueError('Deze naam bestaat al. Kies een andere naam.')
    return tekst


def overzicht(data, naam):
    regels = schema(data)
    print(f"\nLENING {naam} | Hoofdsom EUR {euro(D(data['hoofdsom']))}")
    print(f"Looptijd: {len(regels)} maanden; laatste maand: {regels[-1]['maand']}")
    if data.get('omschrijving'):
        print('Omschrijving:', data['omschrijving'])
    print('Betaalwijze:', aflossing_label(data))
    print(f"{'Maand':10} {'Rente':>14} {'Aflossing':>14} {'Netto Aflossing':>16} {'Extra':>14} {'Restschuld':>18} {'Schenking':>14}")
    for soort, r in overzichtsregels(regels):
        if soort != 'maand':
            print('-' * 116)
        print(f"{r['maand']:10} " + ' '.join(f"{euro(r[k]):>{18 if k == 'restschuld' else 16 if k == 'netto_aflossing' else 14}}" for k in VELDEN))


def leningmenu(data, naam):
    overzicht(data, naam)
    while True:
        print(f'\nLening: {naam}')
        print('1 Overzicht  2 Extra aflossing toevoegen  3 Extra aflossing verwijderen')
        print('4 PDF liggend  5 PDF portret  6 Terug naar leningenlijst  7 Aflossing op 0 aan/uit  8 Vast maandbedrag  9 Omschrijving  0 Stoppen')
        keuze = input('Keuze: ').strip()
        try:
            if keuze in ('0', '6'):
                return keuze
            if keuze == '1':
                overzicht(data, naam)
            elif keuze in ('2', '3'):
                nieuw = dict(data, extra_aflossingen=[dict(e) for e in data['extra_aflossingen']])
                if keuze == '2':
                    nieuw['extra_aflossingen'].append(dict(
                        datum=vraag('Datum extra aflossing (DD-MM-JJJJ)', lambda s: datum(s).isoformat()),
                        bedrag=str(vraag('Bedrag extra aflossing',
                                        lambda s: bereik(s, D('0.01'), D('1000000000000'), True)))))
                else:
                    if not nieuw['extra_aflossingen']:
                        print('Er zijn geen extra aflossingen.')
                        continue
                    for i, e in enumerate(nieuw['extra_aflossingen'], 1):
                        print(f"{i}: {e['datum']} - EUR {euro(D(e['bedrag']))}")
                    n = vraag('Nummer verwijderen (0 = annuleren)', int)
                    if n == 0:
                        continue
                    if not 1 <= n <= len(nieuw['extra_aflossingen']):
                        raise ValueError('Onbekend nummer.')
                    nieuw['extra_aflossingen'].pop(n - 1)
                bewaren_extra(nieuw, naam)
                data = nieuw
                print('Opgeslagen:', paden(naam)[1])
                overzicht(data, naam)
            elif keuze in ('7', '8'):
                if keuze == '8':
                    nieuw = vraag_vast_bedrag(data)
                else:
                    print('Ja = aflossingsvrij; nee = lineair volgens de oorspronkelijk ingevoerde looptijd.')
                    nieuw = dict(data, aflossingsvrij=vraag('Reguliere aflossing op 0 zetten voor de hele lening? (ja/nee)', ja_nee))
                    nieuw.pop('vaste_aflossing', None)
                schema(nieuw)
                schrijf_json({k: v for k, v in nieuw.items() if k != 'extra_aflossingen'}, paden(naam)[0])
                data = nieuw
                print('Instelling opgeslagen; gehele schema herberekend. Maak de PDF opnieuw om deze bij te werken.')
                overzicht(data, naam)
            elif keuze == '9':
                print('Huidige omschrijving:', data.get('omschrijving', '') or '(geen)')
                print('Enter = behouden; - = omschrijving wissen.')
                omschrijving = input('Nieuwe omschrijving: ').strip()
                if not omschrijving:
                    continue
                nieuw = dict(data, omschrijving='' if omschrijving == '-' else omschrijving)
                schema(nieuw)
                schrijf_json({k: v for k, v in nieuw.items() if k != 'extra_aflossingen'}, paden(naam)[0])
                data = nieuw
                print('Omschrijving opgeslagen. Maak de PDF opnieuw om deze bij te werken.')
            elif keuze in ('4', '5'):
                pad = paden(naam)[2]
                print('PDF-bestand:', pad)
                if pad.exists():
                    overschrijven = vraag('PDF bestaat. Overschrijven? (ja/j of nee/n)', ja_nee)
                    if not overschrijven:
                        print('PDF maken geannuleerd; het bestaande bestand is behouden.')
                        continue
                print('PDF wordt gemaakt...')
                pdf(data, pad, portret=(keuze == '5'))
                print('PDF opgeslagen:', pad)
            else:
                print('Kies 0 t/m 9.')
        except (ValueError, OSError) as e:
            print('Niet uitgevoerd:', e)


def main():
    print('LINEAIRE LENING - versie PDF-herstel 2\nBestandenmap:', MAP)
    while True:
        bestanden = sorted(MAP.glob('lening_*.json'), key=lambda p: p.name.casefold())
        print('\nLENINGEN')
        for i, p in enumerate(bestanden, 1):
            print(f'{i}) {p.name}')
        if not bestanden:
            print('Nog geen leningen ingevoerd.')
        print('n) Nieuwe lening')
        if BESTAND.exists():
            print('i) Lening uit oude extra_aflossingen.json importeren')
        print('0) Stoppen')
        keuze = input('Keuze: ').strip().lower()
        try:
            if keuze == '0':
                return 0
            if keuze in ('n', 'i'):
                if keuze == 'i' and not BESTAND.exists():
                    raise ValueError('Geen oud bestand gevonden.')
                naam = vraag('Naam van de lening (bijv. Truus)', nieuwe_naam)
                if keuze == 'n':
                    data = nieuwe_lening()
                else:
                    data = json.loads(BESTAND.read_text(encoding='utf-8'))
                    schema(data)
                aanmaken(data, naam)
            else:
                if not keuze.isdigit() or not 1 <= int(keuze) <= len(bestanden):
                    raise ValueError('Kies een nummer uit de lijst of n.')
                naam = bestanden[int(keuze) - 1].stem[len('lening_'):]
                data = laden(naam)
            if leningmenu(data, naam) == '0':
                return 0
        except (ValueError, OSError) as e:
            print('Niet uitgevoerd:', e)
            print('Bestaande bestanden zijn niet overschreven.')


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (KeyboardInterrupt, EOFError):
        print('\nGestopt. Eerder opgeslagen gegevens blijven bewaard.')
    except OSError as exc:
        print('Bestandsfout:', exc)
        raise SystemExit(1)
