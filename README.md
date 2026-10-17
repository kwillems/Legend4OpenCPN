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
- een dynamische legenda van de waypoint-symbolen die in beeld zijn, met
  betekenissen, kopjes en een markering op de kaart (zie hieronder);
- een lichte en een donkere weergave die macOS volgt.

Bij nul of één item wordt overbodige navigatie niet getoond of uitgeschakeld. Bij meerdere items verschijnt compact een keuzelijst met daarnaast de positie, bijvoorbeeld `2 van 5`.

Markdown wordt verwerkt met [MD4C](https://github.com/mity/md4c).

## Dynamische legenda (symbolen in beeld)

Het eerste item in de keuzelijst is altijd de dynamische legenda. Die toont
de symbolen die op dat moment in het kaartbeeld zichtbaar zijn, gegroepeerd
per symbool met het aantal (`×7`):

- de waypoint-symbolen, waaronder die uit GPX-bestanden en GPX-lagen, met
  hun eigen icoon;
- de kaartsymbolen van o-charts-kaarten (boeien, bakens, lichten, toptekens,
  meerpalen enzovoort), met het plaatje zoals OpenCPN het op de kaart tekent.
  Zie *Kaartsymbolen* hieronder.

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
- **Ververs** laadt alle waypoints opnieuw en inventariseert de kaartcellen
  opnieuw. Gebruik dit na het verplaatsen van een waypoint of het wijzigen
  van een icoon zonder dat de kaart beweegt.

### Kopjes, in- en uitklappen en volgorde

Met kopjes in `symbolen.txt` (zie hieronder) wordt de lijst in groepen
verdeeld.

- Klik op een kopje (▼/▶) om de groep in of uit te klappen. Een ingeklapte
  groep toont het aantal symbolen en waypoints in beeld en een strook met de
  eerste iconen.
- Bovenaan staat één link die wisselt tussen **Alles inklappen** en **Alles
  uitklappen**, afhankelijk van wat er nog openstaat.
- Welke kopjes ingeklapt zijn, wordt per kopjesnaam onthouden, ook na het
  herstarten van OpenCPN.
- **Volgorde wijzigen** toont alleen de kopjes, met grote knoppen ▲ en ▼ die
  ook op een aanraakscherm goed werken. Een verplaatsing wordt direct in
  `symbolen.txt` bewaard. **Klaar** brengt je terug naar de lijst.
- Symbolen die onder geen kopje vallen, staan onderaan onder **Overig**.

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
  **Kaartsymbolen**, vóór **Overig**. De tekst is dan de Engelse
  S-52-omschrijving, bijvoorbeeld "Pillar buoy, lateral-r". In `symbolen.txt`
  gebruik je de symboolnaam, bijvoorbeeld `BOYPIL60 = Rode pilaarton` of
  `LIGHTS* = Licht`; de tooltip van een rij toont die naam.
- De link **Kaartsymbolen uit** / **aan** in het kopblok schakelt dit deel
  van de legenda uit of in; de keuze wordt bewaard.

De o-charts-bron werkt alleen als de o-charts-plugin geladen is. Versleutelde
S-63-cellen worden niet gelezen.

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

[Betonning]
022           = Groene spitse ton
```

- Per regel: `icoonnaam = betekenis`. De icoonnaam is de bestandsnaam van het
  icoon zonder `.png`.
- Een `*` in de icoonnaam staat voor een willekeurig stuk tekst; een `*` in de
  betekenis wordt vervangen door dat stuk. Exacte namen gaan voor, daarna telt
  het eerste passende patroon.
- `[Kopje]` begint een groep. Kopjes en regels verschijnen in de volgorde van
  het bestand.
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

Dat is de map `Legend` in de privémap van OpenCPN. Daar staan `legend.ini`, met het laatst gekozen item, de vensterpositie en -grootte, de ingeklapte kopjes en de schakelaar voor kaartsymbolen, en de caches `ocharts-cells.txt` en `s57-cells.txt` met de dekking van de kaartcellen.

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
│   └── legend_toolbar.png
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
│   └── display_settings.h
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
- gegevens van het gekozen object tonen, zoals lichtkarakter, doorvaarthoogte en marifoonkanaal;
- instelbaar ook symbolen tonen die op de huidige schaal verborgen zijn;
- vanaf een klik op de kaart naar de rij in de legenda;
- een Engelse vertaling van het venster.

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
