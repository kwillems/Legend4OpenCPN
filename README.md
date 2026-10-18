# Legend4OpenCPN

Legend4OpenCPN is een OpenCPN-plugin voor het tonen van legenda's, notities en andere compacte informatie naast de kaart.

De plugin is begonnen als een manier om een legenda bij kaartlagen te tonen, maar is bewust breder opgezet. Naast losse afbeeldingen kan Legend4OpenCPN ook Markdown-bestanden tonen. Daardoor kan dezelfde plugin worden gebruikt voor bijvoorbeeld routebeschrijvingen, vaarinstructies, checklists en eigen aantekeningen.

> Status: vroege ontwikkelversie / alpha.

## Huidige mogelijkheden

Op dit moment ondersteunt Legend4OpenCPN:

- een eigen knop in de OpenCPN-toolbar;
- een afzonderlijk legenda-/informatievenster;
- PNG-afbeeldingen;
- Markdown (`.md` en `.markdown`);
- lokale afbeeldingen in Markdown;
- GitHub-flavoured Markdown via MD4C, waaronder tabellen;
- klikbare externe links;
- direct kiezen van een item via een keuzelijst;
- bladeren door meerdere items met `Vorige` en `Volgende`;
- bladeren met de pijltoetsen `←` en `→`;
- gebruik van `↑` en `↓` in de keuzelijst wanneer die focus heeft;
- `Esc` om het Legend-venster te verbergen;
- onthouden van de scrollpositie per Markdown-document tijdens de OpenCPN-sessie;
- alfabetische sortering van beschikbare items;
- opnieuw scannen van de Legend-map wanneer het venster wordt geopend;
- onthouden van het laatst getoonde item;
- onthouden van vensterpositie en -grootte;
- automatisch passend schalen van grote PNG-afbeeldingen;
- automatisch aanmaken van de benodigde gebruikersmappen;
- een dynamische legenda van de symbolen die in beeld zijn, uit waypoint-lagen
  en uit de vectorkaart, met betekenissen in het Nederlands of Engels, kopjes,
  een zoekveld en een markering op de kaart (zie hieronder);
- lichtkarakters in woorden, met hun ritme, en lichten die op de kaart gaan
  branden;
- het marifoonkanaal van het gebied waar de boot is, en wat er het komende
  half uur op koers ligt;
- een lichte en een donkere weergave die macOS volgt, scherp op een
  Retina-scherm.

Bij nul of één item wordt overbodige navigatie niet getoond of uitgeschakeld. Bij meerdere items verschijnt compact een keuzelijst met daarnaast de positie, bijvoorbeeld `2 van 5`.

Markdown wordt verwerkt met [MD4C](https://github.com/mity/md4c).

## Dynamische legenda (symbolen in beeld)

Het eerste item in de keuzelijst is altijd de dynamische legenda. Die toont
de symbolen die op dat moment in het kaartbeeld zichtbaar zijn, gegroepeerd
per symbool met het aantal (`×7`):

- de waypoint-symbolen, waaronder die uit GPX-bestanden en GPX-lagen, met
  hun eigen icoon;
- de kaartsymbolen van o-charts- en ENC-kaarten (boeien, bakens, lichten,
  toptekens, meerpalen enzovoort), met het plaatje zoals OpenCPN het op de
  kaart tekent. Zie *Kaartsymbolen* hieronder;
- de lichtkarakters van de lichten in beeld, onder de boei of het baken waar
  ze op staan. Zie *Lichten* hieronder.

Bovenaan staan, als ze van toepassing zijn, de kopjes **Marifoon** en
**Vooruit**. Zie *Marifoon en Vooruit* hieronder.

### Bediening

- Klik of tik ergens op een rij om het dichtstbijzijnde exemplaar aan te
  wijzen. Nog een klik gaat naar het volgende (`2/7`). De rij die het laatst
  is gekozen, licht op.
- Het gekozen exemplaar wordt ongeveer zes seconden gemarkeerd met een ring
  en een kruisje op de kaart. Staat het niet ruim in beeld, dan verschuift de
  kaart naar dat punt; de zoomstand blijft gelijk.
- De lijst past zich vanzelf aan zodra de kaart ongeveer 0,4 seconde
  stilstaat na verschuiven, zoomen, draaien of het wijzigen van de
  venstergrootte. Blijven dezelfde waypoints in beeld, dan blijven volgorde
  en teller staan.
- Het verbergen of tonen van een GPX-laag wordt vanzelf opgemerkt; de lijst
  past zich binnen enkele seconden aan.
- De teller rechts in een rij (`×7 ◎`) is een knop: een klik wijst alle
  exemplaren van dat symbool in beeld tegelijk aan.
- Boven elk klikbaar onderdeel verschijnt een tooltip die zegt wat een klik
  doet.
- Andersom kan ook: met de legenda open wijst een klik op een symbool op de
  kaart de rij in de legenda aan, en **Toon in legenda** in het
  rechtsklikmenu van de kaart doet hetzelfde. Het kopje gaat open, de rij
  licht op en wordt in beeld gescrold.
- Het kopblok is één regel: de titel met rechts een tandwiel voor
  **Instellingen** en een pijl voor **Ververs**. Instellingen opent een klein
  menu met vinkjes: Lichten, Ook op deze schaal verborgen tonen, Engelse
  namen, Alle lichten laten branden, Alles in- of uitklappen en Volgorde
  wijzigen. Hoeveel symbolen, waypoints, kaartobjecten en lichtkarakters er
  in beeld zijn, staat onderaan de lijst.
- **Ook op deze schaal verborgen tonen** voegt grijze rijen toe voor
  symbolen die de kaart op deze schaal niet tekent vanwege hun schaalgrens,
  met een gesloten oogje en de schaal waarop ze verschijnen (`1:200k`), per
  schaalgrens een rij. Een klik op zo'n rij zoomt in tot net voorbij die
  schaal en markeert het dichtstbijzijnde exemplaar; ◎ zoomt in rond alle
  exemplaren en markeert ze. Wat je zelf hebt uitgezet bij *Gebruikers
  standaard* blijft weg. De schaal is die van de statusbalk van OpenCPN.
- **Ververs** laadt alle waypoints opnieuw en inventariseert de kaartcellen
  en de marifoongebieden opnieuw. Gebruik dit na het verplaatsen van een
  waypoint of het wijzigen van een icoon zonder dat de kaart beweegt.

### Zoeken

Boven de dynamische legenda staat een zoekveld (`Cmd+F`). Alleen de rijen
waarvan de naam, het kopje of de icoon- of symboolnaam alle getypte woorden
bevat, blijven over. Hoofdletters en accenten tellen niet mee: `waterskien`
vindt *Waterskiën*. Passende rijen worden ook onder een ingeklapt kopje
getoond. Een boei en haar lichten worden samen gevonden. `Esc` maakt het
zoekveld eerst leeg en sluit pas daarna het venster.

### Kopjes, in- en uitklappen en volgorde

Met kopjes in `symbolen.txt` (zie hieronder) wordt de lijst in groepen
verdeeld.

- Klik op een kopje (▼/▶) om de groep in of uit te klappen. Een ingeklapte
  groep toont het aantal symbolen en waypoints in beeld en een strook met de
  eerste iconen.
- In het menu Instellingen staat **Alles inklappen** of **Alles
  uitklappen**, afhankelijk van wat er nog openstaat.
- Welke kopjes ingeklapt zijn, wordt per kopjesnaam onthouden, ook na het
  herstarten van OpenCPN.
- **Volgorde wijzigen** (in het menu) toont alleen de kopjes, met grote
  knoppen ▲ en ▼ die ook op een aanraakscherm goed werken. Een verplaatsing
  wordt direct in `symbolen.txt` bewaard. **Klaar** brengt je terug naar de
  lijst.
- **Kaartsymbolen** en **Lichtkarakters** zijn kopjes van de plugin. Bij
  Volgorde wijzigen zet de plugin `[Kaartsymbolen | Chart symbols]` en
  `[Lichtkarakters | Light characters]` in `symbolen.txt` als ze daar nog
  ontbreken; daarna kun je ze verplaatsen zoals de andere.
- Symbolen die onder geen kopje vallen, staan onderaan onder **Overig**.
  **Marifoon** en **Vooruit** staan altijd bovenaan.

### Kaartsymbolen (o-charts en ENC)

De plugin leest de puntobjecten van de vectorkaarten in beeld uit twee
bronnen:

- **o-charts-cellen** (`.oesu`) via de kaartklasse van de o-charts-plugin
  zelf, op dezelfde manier als OpenCPN dat doet voor *Kaartobjectgegevens*.
  De o-charts-plugin blijft de ontsleuteling en de licentiecontrole doen; de
  plugin slaat geen kaartgegevens op en exporteert niets.
- **Gewone ENC-cellen** (S-57, `.000` met bijwerkingen `.001` enzovoort),
  zoals de gratis binnenvaartkaarten van Rijkswaterstaat, met een eigen
  lezer in de plugin.

Waar beide bronnen hetzelfde water bedekken, telt een object één keer.
Alleen de dekking en schaal van elke cel worden bewaard (`ocharts-cells.txt`
en `s57-cells.txt` in de instellingenmap), zodat latere starts direct zijn.

- Elk object krijgt het S-52-symbool dat OpenCPN ervoor kiest, met dezelfde
  opzoektabellen (`chartsymbols.xml`) en dezelfde symboolstijl (vereenvoudigd
  of papieren kaart). Het plaatje komt van het symboolblad voor dag, schemer
  of nacht.
- De legenda toont dezelfde objecten als de kaart. Per objectklasse vraagt de
  plugin aan de tekenbibliotheek van OpenCPN of die klasse op dit moment
  getekend wordt, met de weergavecategorie (Basis, Standaard, Alles,
  Gebruikers standaard), de lijst van aan- en uitgezette klassen en de
  schaalgrens (SCAMIN) zoals OpenCPN ze nu gebruikt. Verander je een
  instelling, dan past de lijst zich direct aan. Lijn- en vlakobjecten
  (kustlijn, dieptegebieden) doen niet mee.
- Cellen die veel gedetailleerder zijn dan het kaartbeeld worden
  overgeslagen, zoals ook de quilt van OpenCPN ze niet toont. Er blijven
  hooguit zestien cellen tegelijk geopend.
- Symbolen zonder regel in `symbolen.txt` staan onder het kopje
  **Kaartsymbolen**. De naam wordt opgebouwd uit de objectklasse en een paar
  kenmerken, in het Nederlands of Engels, bijvoorbeeld "Laterale boei:
  bakboord, rood, stompe ton". De Nederlandse namen komen uit
  `data/s57-nl.txt`, met de termen van Kaart 1 en BPR bijlage 7 en 8; namen
  zonder officiële term zijn daar als vrij vertaald gemarkeerd. Een eigen
  `s57-nl.txt` in de Legend-map vult die aan of verbetert ze. In
  `symbolen.txt` kun je ook de symboolnaam gebruiken, bijvoorbeeld
  `BOYPIL60 = Rode pilaarton`; de tooltip van een rij toont die naam.

De o-charts-bron werkt alleen als de o-charts-plugin geladen is. Versleutelde
S-63-cellen worden niet gelezen.

De o-charts-plugin tekent met een eigen tekenbibliotheek. De plugin zet het
kleurenschema op elke cel die hij zelf opent, zodat de kaart in de schemer-
en nachtstand haar kleuren houdt.

### Lichten

De plugin leest het lichtkarakter van de lichten in beeld: uit de kaart
(S-57-kenmerken van het object LIGHTS) en uit waypoints, waar het na
`Lichtkarakter:` in de beschrijving staat of op een eigen regel van de naam,
zoals in de Nautin-laag.

- Elk lichtkarakter krijgt een rij met een lampje in de kleur van het licht,
  de afkorting zoals op de kaart en de betekenis in woorden, met de termen
  van Kaart 1 (P 10), bijvoorbeeld *Fl(2+1)G.10s — groen groepschitterlicht
  met samengestelde groep: 2+1 schitteringen, elke 10 seconden*. Daaronder
  staat het ritme als schematische tekening.
- Een licht staat als ingesprongen subrij onder de boei of het baken waar
  het op staat. Bij een kaart koppelt de plugin het licht aan het object op
  dezelfde plek. Lichten die op niets uit de lijst staan, hebben het kopje
  **Lichtkarakters**.
- Een klik op een lichtrij laat het licht op de kaart branden in zijn ritme,
  een kwartier tot een minuut lang, met een gloed en stralen. Periode en
  groepen komen uit de kaart; de duur van een schittering of verduistering
  is een gangbare waarde. Een klik op een verlichte boei op de kaart wijst
  de rij van de boei en die van haar licht samen aan.
- **Alle lichten laten branden** (in het menu) laat alle lichten in beeld
  tegelijk branden, elk in zijn eigen ritme en niet gelijk op, tot je het
  weer uitzet. Ze volgen de kaart.
- Een sectorlicht wordt één licht met zijn sectoren in woorden, bijvoorbeeld
  *rood 090°–115°, wit 115°–120°*, als peilingen naar het licht. Brandt het
  op de kaart, dan staan de sectoren als gekleurde taartpunten om het licht.
- Met **Lichten** in het menu gaan de lichtrijen uit of aan.

### Marifoon en Vooruit

**Marifoon** toont het marifoonkanaal van het gebied waar de boot is, uit de
laag *VHFinfo Nederland* van
[VHF2OpenCPN](https://github.com/kwillems/VHF2OpenCPN): kanaal, oproepnaam,
soort post, wat je moet doen (uitluisteren, melden of aanmelden) en het
telefoonnummer. Zonder recente GPS-positie geldt het midden van de kaart; dat
staat er dan bij. Ingeklapt noemt het kopje nog steeds het kanaal. Staat de
laag uit in OpenCPN, dan verdwijnt het kopje.

**Vooruit** verschijnt tijdens het varen (een recente positie en minstens een
halve knoop). Het toont tot acht objecten uit de legenda die je de komende
dertig minuten op koers tegenkomt, op volgorde van aankomst, met de kant
waar ze liggen en hun lichtkarakter. De plugin kijkt in een waaier vóór de
boot die verder weg breder wordt. Een klik wijst het object aan op de kaart.
Testen zonder te varen kan met de plugin ShipDriver.

### Weergave

- De legenda volgt de kleurenschema's dag, schemer en nacht van OpenCPN
  (de knop met zon en maan): venster, tekst, iconen, PNG-legenda's en de
  markering op de kaart dimmen mee. Overdag volgt het venster de lichte of
  donkere weergave van macOS.
- De iconen volgen de schaalfactor voor kaartobjecten in de instellingen van
  OpenCPN, zodat ze even groot zijn als op de kaart. Tekst en knoppen behouden
  hun grootte.
- Alle rijen zijn even hoog; het grootste icoon bepaalt de hoogte.
- In de donkere weergave staan de iconen op een licht tegeltje, zodat
  kaartsymbolen goed te onderscheiden blijven.
- Elk plaatje in de lijst bestaat ook op dubbele grootte (`naam@2x.png`), die
  de HTML-weergave van wxWidgets op een Retina-scherm gebruikt. Zo blijven
  iconen, lampjes en ritmes scherp.
- Het eerste laden van alle waypoints gebeurt op de achtergrond kort na het
  starten van OpenCPN. Wordt de legenda eerder geopend, dan toont het venster
  de voortgang.

### Beperkingen

- Kaartsymbolen komen uit o-charts- en S-57-cellen; andere kaartformaten
  (CM93, rasterkaarten, S-63) doen niet mee. De plugin-API geeft
  kaartobjecten niet direct door.
- Bijwerkingen van ENC-cellen worden toegepast op objecten, kenmerken en
  knooppunten; zeldzame vormen van bijwerkingen kunnen worden gemist.
- De markering is getest met **OpenGL ingeschakeld**. Zonder OpenGL wordt de
  markering niet betrouwbaar getoond.
- Iconen worden gelezen uit de PNG-bestanden van OpenCPN (`UserIcons` en de
  ingebouwde markicons). Ontbreekt het bestand, dan vraagt de plugin het icoon
  aan OpenCPN; dat kan op macOS een leeg icoon geven.
- Dezelfde boei staat soms twee keer in de lijst: als waypoint uit een laag
  en als kaartsymbool. De plugin voegt die nog niet samen.
- Vooruit kijkt alleen naar wat in beeld is.
- Sectoren komen alleen uit kaarten, niet uit waypoint-lagen.

## Betekenis en indeling: `symbolen.txt`

De tekst bij een symbool wordt gekozen uit, in deze volgorde:

1. `symbolen.txt` in de Legend-map;
2. de eerste regel van de beschrijving, als alle waypoints met hetzelfde
   icoon (minstens twee) die regel delen;
3. de beschrijving die OpenCPN zelf van het icoon kent, als die anders is dan
   de icoonnaam;
4. de icoonnaam, in grijs. Zo zie je welke symbolen nog geen betekenis hebben.

De plugin maakt `symbolen.txt` bij het eerste gebruik aan, met uitleg in
commentaar. Een voorbeeld:

```text
[Marifoon]
vhf_b11b_*    = Meldplicht marifoon, kanaal *
vhf_*         =

[Bruggen en sluizen]
Brug_dicht    =
Sluis         = Sluis

[Betonning | Buoyage]
011           = Groene spitse ton | Green conical buoy
```

- Per regel: `icoonnaam = betekenis`. De icoonnaam is de bestandsnaam van het
  icoon zonder `.png`.
- Een `*` in de icoonnaam staat voor een willekeurig stuk tekst; een `*` in de
  betekenis wordt vervangen door dat stuk. Exacte namen gaan voor, daarna telt
  het eerste passende patroon.
- `[Kopje]` begint een groep. Kopjes en regels verschijnen in de volgorde van
  het bestand.
- Twee talen: `betekenis | meaning` en `[Kopje | Heading]`. De legenda toont
  het deel van de gekozen taal; ontbreekt dat, dan het andere.
- Een lege betekenis (`vhf_* =`) deelt een icoon alleen in; de tekst komt dan
  uit stap 2 tot en met 4.
- Regels die met `#` beginnen worden overgeslagen.
- Wijzigingen zijn zichtbaar na **Ververs** of zodra de kaart verschuift.

## Gebruikersbestanden

Legenda's en notities worden momenteel gelezen uit:

```text
~/Documents/OpenCPN/Legend/
```

Bijvoorbeeld:

```text
~/Documents/OpenCPN/Legend/
├── VHF-Nederland.png
├── Route-IJsselmeer.md
├── Checklist.md
├── symbolen.txt
├── s57-nl.txt        (optioneel: eigen namen voor kaartsymbolen)
└── images/
    ├── brug.png
    └── sluis.png
```

De plugin maakt de hoofdmap automatisch aan wanneer dat nodig is.

PNG-bestanden in de hoofdmap worden als afzonderlijke legenda-items getoond. Afbeeldingen die alleen in Markdown-documenten worden gebruikt, kunnen daarom het best in een submap zoals `images/` worden geplaatst.

`symbolen.txt` bevat de betekenissen en kopjes van de dynamische legenda; het verschijnt niet als legenda-item.

De interne configuratie van de plugin wordt apart opgeslagen onder:

```text
~/Library/Preferences/opencpn/Legend/
```

Dat is de map `Legend` in de privémap van OpenCPN. Daar staan `legend.ini`, met het laatst gekozen item, de vensterpositie en -grootte, de ingeklapte kopjes en de keuzes uit het menu Instellingen (taal, lichten, verborgen tonen), en de caches `ocharts-cells.txt` en `s57-cells.txt` met de dekking van de kaartcellen.

De marifoongebieden leest de plugin uit de GPX-lagen in de map `layers` van
OpenCPN (`~/Library/Preferences/opencpn/layers/`).

De locatie van de gebruikersmap is op dit moment nog vast. Een instelbare locatie is voorzien voor een latere versie.

## Markdown

Een eenvoudige notitie kan bijvoorbeeld zo worden geschreven:

```markdown
# Route IJsselmeer

## Vertrek

- Controleer weer en route.
- Controleer het marifoonkanaal.
- Zet de gewenste kaartlagen aan.

## Onderweg

**Let op:** houd beroepsvaart in de gaten.

1. Controleer brug- en sluisinformatie.
2. Werk de routebeschrijving bij als dat nodig is.
3. Noteer bijzonderheden onderweg.
```

Als de eerste niet-lege regel een H1-kop is, bijvoorbeeld:

```markdown
# Route IJsselmeer
```

dan gebruikt Legend4OpenCPN die tekst als zichtbare titel. De H1 wordt vervolgens niet nogmaals bovenaan in de inhoud weergegeven. Als geen H1 aanwezig is, gebruikt de plugin de bestandsnaam als titel.

### Afbeeldingen in Markdown

Afbeeldingen kunnen relatief ten opzichte van het Markdown-bestand worden opgenomen.

Een afbeelding in dezelfde map:

```markdown
![Nautin test](Nautin-test.png)
```

Een afbeelding in een submap:

```markdown
![Brug](images/brug.png)
```

Voor grotere verzamelingen is de tweede vorm aan te raden, omdat ondersteunende afbeeldingen dan niet als zelfstandige legenda-items in de keuzelijst verschijnen.

### Tabellen

Tabellen kunnen met GitHub-flavoured Markdown worden geschreven:

```markdown
| Object | VHF | Opmerking |
|---|---:|---|
| Ketelbrug | 18 | Luister uit op kanaal 18 |
| Houtribsluizen | 22 | Meld je voor passage |
| Oranjesluizen | 18 | Controleer actuele aanwijzingen |
```

### Links

Normale externe links kunnen vanuit de Markdown-weergave worden geopend, bijvoorbeeld:

```markdown
[OpenCPN](https://opencpn.org)
```

Ook `mailto:`- en `tel:`-links worden als externe links behandeld.

## Waarom Markdown?

Markdown is een eenvoudig, open tekstformaat dat door veel editors en notitieprogramma's wordt ondersteund.

Daarmee blijft de inhoud onafhankelijk van Legend4OpenCPN zelf. Gebruikers kunnen notities maken met hun eigen editor, bestanden eenvoudig uitwisselen en de inhoud ook buiten OpenCPN blijven gebruiken.

Op termijn kan een aparte import- of conversielaag informatie uit andere notitieformaten naar Markdown omzetten. Als mogelijke toekomstige importlaag wordt onder andere gekeken naar oplossingen zoals Obsidian Importer.

Het uitgangspunt is om Legend4OpenCPN zelf klein te houden: de plugin is in de eerste plaats een stabiele weergavelaag voor eenvoudige, uitwisselbare inhoud.

## Bediening

Klik op de Legend-knop in de OpenCPN-toolbar om het venster te openen of te verbergen.

Bij meerdere items:

- kies direct een legenda of notitie in de keuzelijst;
- gebruik `Vorige` en `Volgende`;
- gebruik `←` en `→` om tussen items te bladeren;
- als de keuzelijst focus heeft, kunnen `↑` en `↓` worden gebruikt om een andere keuze te maken;
- druk op `Esc` om het Legend-venster te verbergen.

Grote PNG-afbeeldingen worden automatisch proportioneel verkleind zodat ze binnen het beschikbare venster passen. Kleinere afbeeldingen worden niet automatisch vergroot.

Markdown-tekst blijft scrollbaar. De scrollpositie wordt per Markdown-document onthouden zolang OpenCPN draait. Als je naar een ander document gaat en later terugkeert, wordt de eerdere positie hersteld.

## Broncode en dependencies

Legend4OpenCPN gebruikt Git-submodules voor externe dependencies:

- `opencpn-libs`
- `third_party/md4c`

Clone daarom bij voorkeur met:

```bash
git clone --recurse-submodules https://github.com/kwillems/Legend4OpenCPN.git
```

Als de repository al zonder submodules is gekloond:

```bash
git submodule update --init --recursive
```

## Bouwen op macOS

De huidige ontwikkelomgeving is:

- macOS op Apple Silicon (`arm64`);
- OpenCPN 5.14.2;
- wxWidgets 3.2.x;
- CMake;
- Apple Clang.

De plugin moet gebouwd worden tegen dezelfde wxWidgets-versie als OpenCPN
(3.2.9). Die staat hier in `/opt/wxwidgets-3.2.9`; geef de bijbehorende
`wx-config` expliciet op:

```bash
cd ~/Development/Legend4OpenCPN

rm -rf build
mkdir build
cd build

cmake -DwxWidgets_CONFIG_EXECUTABLE=/opt/wxwidgets-3.2.9/bin/wx-config ..
cmake --build . --target tarball -j8
```

Na de eerste configuratie is `cmake ..` genoeg; de keuze voor `wx-config`
blijft bewaard in `build/CMakeCache.txt`.

CMake meldt bij de eerste configuratie dat `wxWidgets_CONFIG_EXECUTABLE` niet
gebruikt is. Die melding klopt niet: de variabele wordt pas gelezen tijdens
het bouwen van het pakket. Zonder deze optie mislukt de build met
`Could NOT find wxWidgets`, omdat `wx-config` niet in het zoekpad staat.

### Versienummer en pakketnaam

De naam van het pakket komt uit de git-tag op de huidige commit:

- met een tag, bijvoorbeeld `v0.2.0-alpha14`, heet het pakket
  `Legend-v0.2.0-alpha14_darwin-wx32-10.13.6-arm64.tar.gz`;
- zonder tag krijgt het pakket de versie uit `Plugin.cmake` met een datum en
  commit-id, bijvoorbeeld `Legend-0.2.0+2610161810.b60bf8e_…`.

Voer na het zetten van een tag opnieuw `cmake ..` uit, zodat de nieuwe naam
wordt opgepikt:

```bash
git tag v0.2.0-alpha15
cd build && cmake .. && cmake --build . --target tarball -j8
```

Gebruik voor een installeerbaar OpenCPN-pakket expliciet de `tarball`-target. Alleen de normale build van `liblegend_pi.dylib` is niet voldoende om een actuele importeerbare `.tar.gz` te garanderen.

Bij een geslaagde build wordt in `build/` een OpenCPN-pluginpakket (`.tar.gz`) aangemaakt.

De CMake-template kan tijdens het bouwen waarschuwingen tonen over oudere CMake-policies. Deze meldingen zijn in de huidige ontwikkelomgeving niet fataal zolang de configuratie en compilatie verder slagen.

## Installeren in OpenCPN

De gegenereerde `.tar.gz` kan in OpenCPN worden geïmporteerd via:

```text
Opties → Plugins → Import Plugin
```

Een nieuwe build kan direct over de bestaande versie worden geïmporteerd. Uitschakelen vooraf mag ook; dat gaat sinds `v0.2.0-alpha11` zonder crash.

Diagnosemeldingen van de plugin staan in het logboek van OpenCPN en beginnen met `Legend:`:

```bash
grep "Legend:" ~/Library/Logs/opencpn.log | tail -20
```

Na installatie verschijnt de Legend-knop in de OpenCPN-toolbar.

## Projectstructuur

Belangrijke onderdelen:

```text
Legend4OpenCPN/
├── data/
│   ├── legend_toolbar.png
│   └── s57-nl.txt         # Nederlandse namen van kaartobjecten
├── src/
│   ├── legend_pi.cpp        # plugin, venster en dynamische legenda
│   ├── legend_pi.h
│   ├── chart_symbols.cpp    # S-52-symbolen en S-57-catalogus van OpenCPN
│   ├── chart_symbols.h
│   ├── ocharts_source.cpp   # kaartobjecten via de o-charts-plugin
│   ├── ocharts_source.h
│   ├── s57_source.cpp       # kaartobjecten uit ENC-cellen (S-57)
│   ├── s57_source.h
│   ├── display_settings.cpp # wat de kaart tekent: navraag bij OpenCPN
│   ├── display_settings.h
│   ├── chart_names.cpp      # namen van kaartobjecten (NL/EN)
│   ├── chart_names.h
│   ├── light_character.cpp # lichtkarakters: lezen, woorden, ritme
│   ├── light_character.h
│   ├── vhf_areas.cpp        # marifoongebieden uit de lagen
│   └── vhf_areas.h
├── third_party/
│   └── md4c/              # Git-submodule
├── opencpn-libs/          # Git-submodule
├── Plugin.cmake
├── CMakeLists.txt
└── README.md
```

De map `build/` bevat gegenereerde buildbestanden en hoort niet bij de broncode.

## Richting voor verdere ontwikkeling

Voor volgende versies zijn onder andere interessant:

- een instelbare locatie voor de Legend-map;
- een eenvoudige Markdown-notitie-editor in het Legend-venster;
- een eenvoudige pakketstandaard voor legenda's, notities en bijbehorende media;
- import van bestaande notitieformaten via een aparte conversielaag;
- verder verfijnen van de Markdown-weergave;
- toetsenbordbediening eventueel verder uitbreiden, bijvoorbeeld met `Page Up`, `Page Down`, `Home` en `End`;
- betere integratie met OpenCPN-kaartlagen.

Voor de dynamische legenda in het bijzonder:

- betekenissen bewerken vanuit de legenda zelf;
- gegevens van het gekozen object tonen, zoals doorvaarthoogte en telefoon;
- bruggen en kabels markeren waar de boot niet onder past;
- een licht herkennen aan het ritme dat je ziet;
- dezelfde boei uit een laag en uit de kaart als één rij;
- een Engelse vertaling van de bediening.

Een belangrijk ontwerpdoel is dat kaartmakers of andere aanbieders later standaard een bijbehorende legenda of informatiesnippet kunnen meeleveren, terwijl gebruikers op dezelfde manier hun eigen notities en routebeschrijvingen kunnen toevoegen.

## Licentie

Legend4OpenCPN is vrije software, uitgebracht onder de
[GNU General Public License, versie 2](LICENSE), of naar keuze een latere
versie (SPDX: `GPL-2.0-or-later`).

Externe onderdelen behouden hun eigen licenties. MD4C wordt bijvoorbeeld onder
de MIT-licentie uitgebracht. De buildbestanden die uit de OpenCPN-plugin-sjabloon
komen (zoals `CMakeLists.txt` en de map `cmake/`) behouden hun eigen
licentiekoppen.

## Ontwikkeling

Dit project is in actieve ontwikkeling. De huidige versie is bedoeld als werkende basis waarop de bestandsstructuur, Markdown-ondersteuning, notitiefuncties en distributie verder worden ontwikkeld.
