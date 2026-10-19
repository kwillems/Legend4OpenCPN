# Legend4OpenCPN

Legend4OpenCPN is een plugin voor [OpenCPN](https://opencpn.org) die naast de
kaart uitlegt wat je ziet. Hij laat de betekenis zien van de symbolen die in
beeld zijn, vertelt wat de lichten doen en wat er voor de boeg ligt, en toont
je eigen legenda's, notities en routebeschrijvingen.

> Status: alpha. De plugin wordt ontwikkeld en getest op macOS (Apple
> Silicon) met OpenCPN 5.14.

> **Gebruik op eigen risico.** Legend4OpenCPN is een hulpmiddel, geen
> navigatiemiddel. De uitleg, lichtkarakters, kanalen en andere informatie
> kunnen onvolledig, verouderd of fout zijn. Vertrouw er nooit alleen op:
> gebruik officiële kaarten en berichten, en blijf zelf goed uitkijken. De
> maker is niet aansprakelijk voor schade of ongelukken door het gebruik van
> deze plugin.

## In het kort

- **Kaartlegenda:** alle symbolen in beeld, uit waypoint-lagen en uit de
  vectorkaarten, met hun betekenis in het Nederlands of Engels. Klik op een
  rij en de plugin wijst het symbool aan op de kaart; klik op de kaart en de
  plugin wijst de rij aan.
- **Lichten:** elk lichtkarakter in gewone woorden, met zijn ritme. Lichten
  kunnen op de kaart gaan branden, één of allemaal tegelijk, ook als
  nachtvoorbeeld op een donkere kaart.
- **Licht herkennen:** tik mee met een licht dat je ziet, en de plugin zegt
  welk licht in beeld het best past.
- **Onderweg:** het marifoonkanaal van het gebied waar je vaart, en wat je het
  komende half uur op koers tegenkomt.
- **Eigen legenda's en notities:** PNG-afbeeldingen en Markdown-bestanden uit
  een vaste map, om doorheen te bladeren. Notities schrijf en bewerk je in het
  venster zelf, zoals in een tekstverwerker.

## Installeren

1. Download of bouw het pakket, bijvoorbeeld
   `Legend-v0.2.0-alpha25_darwin-wx32-10.13.6-arm64.tar.gz`.
2. Kies in OpenCPN **Opties → Plugins → Import Plugin** en kies het pakket.
3. Zet de plugin aan. In de werkbalk verschijnt de Legend-knop.

Een nieuwe versie kun je over de oude heen importeren.

## Het venster

De Legend-knop opent en sluit het venster. Bovenin staat een keuzelijst met
alle items, met daarnaast de positie (`2 van 12`). Het eerste item is altijd
de **Kaartlegenda**; daarna volgen je eigen PNG- en Markdown-bestanden op
alfabet.

- Blader met **Vorige** en **Volgende**, of met `←` en `→`.
- Heeft de keuzelijst de focus, dan werken ook `↑` en `↓`.
- `Esc` sluit het venster.
- Het laatst getoonde item, de vensterpositie en -grootte worden onthouden.
  De scrollpositie van een Markdown-document blijft bewaard zolang OpenCPN
  draait.

## De kaartlegenda

De kaartlegenda toont wat er nu in beeld is, per symbool één rij met het
aantal (`×7`):

- de symbolen van **waypoints** uit GPX-bestanden en GPX-lagen, met hun eigen
  icoon;
- de **kaartsymbolen** uit o-charts- en ENC-kaarten (boeien, bakens,
  toptekens, meerpalen enzovoort), met het plaatje dat OpenCPN op de kaart
  tekent;
- de **lichtkarakters**, als ingesprongen rij onder de boei of het baken
  waar het licht op staat.

De lijst past zich vanzelf aan zodra de kaart stilstaat na verschuiven,
zoomen of draaien, en ook als je een laag aan- of uitzet.

### Aanwijzen

- **Klik op een rij** en het dichtstbijzijnde exemplaar wordt op de kaart
  gemarkeerd met een ring. Nog een klik gaat naar het volgende (`2/7`). Staat
  het niet ruim in beeld, dan schuift de kaart ernaartoe.
- **Klik op de teller** (`×7 ◎`) en alle exemplaren in beeld worden tegelijk
  aangewezen.
- **Klik op de kaart** op een symbool terwijl de legenda open is, of kies
  **Toon in legenda** in het rechtsklikmenu van de kaart. De plugin opent het
  juiste kopje, scrolt naar de rij en laat die oplichten.
- Boven alles wat je kunt aanklikken, zegt een tooltip wat een klik doet.

### Kopblok en instellingen

Bovenaan staat **Kaartlegenda** met rechts twee knoppen:

- het **tandwiel** opent het menu Instellingen;
- de **pijl** (Ververs) leest alle waypoints, kaartcellen en marifoongebieden
  opnieuw in. Gebruik dit als je een waypoint hebt veranderd terwijl de kaart
  stilstaat.

In het menu Instellingen:

| Keuze | Wat het doet |
|---|---|
| Lichten | Lichtrijen tonen of verbergen |
| Ook op deze schaal verborgen tonen | Ook symbolen die de kaart op deze schaal niet tekent (zie hieronder) |
| Engelse namen | Betekenissen in het Engels in plaats van het Nederlands |
| Alle lichten laten branden | Alle lichten in beeld branden op de kaart |
| Nachtvoorbeeld | De kaart zoals je hem 's nachts ziet |
| Licht herkennen | Een licht herkennen aan zijn ritme |
| Alles inklappen / uitklappen | Alle kopjes tegelijk |
| Volgorde wijzigen… | De kopjes in een andere volgorde zetten |

Hoeveel symbolen, waypoints, kaartobjecten en lichtkarakters er in beeld zijn,
staat onderaan de lijst.

### Zoeken

Boven de lijst staat een zoekveld (`Cmd+F`). Alleen rijen waarin alle getypte
woorden voorkomen blijven over, ook onder ingeklapte kopjes. Hoofdletters en
accenten tellen niet: `waterskien` vindt *Waterskiën*. Een boei en haar licht
worden samen gevonden. `Esc` maakt eerst het zoekveld leeg en sluit pas daarna
het venster.

### Kopjes

De lijst is verdeeld in kopjes, die je zelf maakt in `symbolen.txt` (zie
*Je eigen betekenissen*).

- Klik op een kopje om het in of uit te klappen. Een ingeklapt kopje toont nog
  het aantal en een strook met de eerste iconen. De plugin onthoudt welke
  kopjes dicht zijn.
- **Volgorde wijzigen** toont alleen de kopjes, met grote knoppen ▲ en ▼ die
  ook op een aanraakscherm goed werken. De nieuwe volgorde wordt meteen in
  `symbolen.txt` bewaard.
- **Kaartsymbolen** en **Lichtkarakters** zijn kopjes van de plugin zelf: daar
  staan kaartsymbolen zonder eigen betekenis en lichten die op niets uit de
  lijst staan. Ze zijn net zo te verplaatsen als je eigen kopjes.
- Symbolen onder geen enkel kopje staan onderaan bij **Overig**.

### Verborgen op deze schaal

Sommige symbolen tekent de kaart pas als je ver genoeg inzoomt. Met **Ook op
deze schaal verborgen tonen** krijgen ze een grijze rij met een gesloten oogje
en de schaal waarop ze verschijnen (`1:200k`). Een klik zoomt in tot het
dichtstbijzijnde exemplaar zichtbaar is. Wat je zelf hebt uitgezet bij
*Gebruikers standaard* in OpenCPN blijft weg.

## Lichten

De plugin leest het lichtkarakter uit de kaart en uit waypoints. In een
waypoint staat het na `Lichtkarakter:` in de beschrijving, of op een eigen
regel van de naam, zoals in de Nautin-laag.

- **In woorden.** Elk licht krijgt een lampje in zijn kleur, de afkorting
  zoals op de kaart en de betekenis met de termen van Kaart 1, bijvoorbeeld
  *Fl(2+1)G.10s — groen groepschitterlicht met samengestelde groep: 2+1
  schitteringen, elke 10 seconden*. Daaronder staat het ritme als tekening.
- **Laten branden.** Een klik op een lichtrij laat dat licht op de kaart
  branden in zijn ritme, met een gloed en stralen. Periode en groepen komen
  uit de kaart; hoe lang één schittering duurt is een gangbare waarde.
- **Alle lichten laten branden** zet alle lichten in beeld aan, elk in zijn
  eigen ritme, tot je het weer uitzet. Ze schuiven mee met de kaart.
- **Sectorlichten** worden één licht met hun sectoren in woorden, zoals
  *rood 090°–115°, wit 115°–120°* (peilingen naar het licht). Brandt het
  licht op de kaart, dan staan de sectoren er als gekleurde taartpunten om.

### Nachtvoorbeeld

**Nachtvoorbeeld** legt een donkerblauwe laag over de kaart, waar kust en
vaarwegen nog net doorheen schemeren. Daarop branden alle lichten in beeld in
hun ritme. Zo zie je vooraf hoe een nachtelijke aankomst eruitziet. Je eigen
schip en AIS-doelen blijven erboven zichtbaar, en het legendavenster gaat
mee in de nachtkleuren.

### Licht herkennen

Zie je een licht en weet je niet welk het is, kies dan **Licht herkennen**.
Bovenaan de lijst verschijnt een kader met een groot vlak.

1. Houd het vlak of de spatiebalk ingedrukt zolang het licht brandt, en laat
   los als het donker is. Doe dat twee of drie perioden lang. Het vlak licht
   op zolang je drukt, en een regel telt de tikken en de seconden licht of
   donker.
2. Klik eventueel op de kleur van het licht.
3. De plugin toont je ritme als tekening en in woorden, en de vijf lichten in
   beeld die het best passen, met hoe goed ze passen. Een klik laat zo'n
   licht op de kaart branden.

Een periode missen, bijvoorbeeld door de deining, is geen probleem. Pas na een
minuut zonder tikken begint de plugin opnieuw; **Tikken wissen** doet dat
meteen. **Sluiten** haalt het kader weg.

## Onderweg

### Marifoon

Het kopje **Marifoon** staat bovenaan de lijst en toont het kanaal van het
gebied waar de boot is: kanaal, oproepnaam, soort post, wat je moet doen
(uitluisteren, melden of aanmelden) en het telefoonnummer. De gegevens komen
uit de laag *VHFinfo Nederland* van
[VHF2OpenCPN](https://github.com/kwillems/VHF2OpenCPN). Zonder recente
GPS-positie geldt het midden van de kaart; dat staat er dan bij. Staat de
laag uit, dan verdwijnt het kopje.

### Vooruit

Het kopje **Vooruit** laat zien wat je de komende dertig minuten op koers
tegenkomt. Het verschijnt zodra de boot vaart: een recente GPS-positie en
minstens een halve knoop.

- Tot acht objecten uit de legenda, op volgorde van aankomst, bijvoorbeeld
  *over 4 min · stuurboord · Rode spitse ton*, met het lichtkarakter erbij.
- De plugin kijkt in een waaier vóór de boot die verder weg breder wordt, tot
  zo ver als je in een half uur komt (hooguit 8 zeemijl).
- Ingeklapt noemt het kopje nog het eerstvolgende object.
- Een klik op een regel wijst het object aan op de kaart, of laat zijn licht
  branden.
- Testen zonder te varen kan met de plugin ShipDriver.

## Je eigen betekenissen: `symbolen.txt`

De plugin maakt bij het eerste gebruik `symbolen.txt` aan in de Legend-map,
met uitleg in commentaar. Daarin geef je symbolen een betekenis en deel je ze
in onder kopjes:

```text
[Marifoon]
vhf_b11b_*    = Meldplicht marifoon, kanaal *
vhf_*         =

[Bruggen en sluizen]
Brug_dicht    =
Sluis         = Sluis

[Betonning | Buoyage]
011           = Groene spitse ton | Green conical buoy
BOYPIL60      = Rode pilaarton | Red pillar buoy
```

- Per regel `icoonnaam = betekenis`. De icoonnaam is de bestandsnaam van het
  icoon zonder `.png`, of voor een kaartsymbool de S-52-naam (die staat in de
  tooltip van de rij).
- Een `*` in de naam staat voor willekeurige tekst; een `*` in de betekenis
  wordt door die tekst vervangen. Exacte namen gaan voor.
- `[Kopje]` begint een groep. Kopjes en regels verschijnen in de volgorde van
  het bestand.
- Twee talen: `betekenis | meaning` en `[Kopje | Heading]`.
- Een lege betekenis (`vhf_* =`) deelt het symbool alleen in.
- Regels die met `#` beginnen worden overgeslagen.

Heeft een symbool geen regel, dan zoekt de plugin verder: de eerste regel van
de beschrijving als alle waypoints met dat icoon die delen, dan de
beschrijving die OpenCPN van het icoon kent, en anders de icoonnaam in grijs.
Zo zie je welke symbolen nog een betekenis nodig hebben.

Kaartsymbolen krijgen een naam uit hun objectsoort en kenmerken, zoals
*Laterale boei: bakboord, rood, stompe ton*. De Nederlandse termen komen uit
Kaart 1 en het BPR (`data/s57-nl.txt`). Met een eigen `s57-nl.txt` in de
Legend-map vul je ze aan of verbeter je ze.

## Eigen legenda's en notities

Alle PNG- en Markdown-bestanden in de Legend-map verschijnen in de
keuzelijst:

```text
~/Documents/OpenCPN/Legend/
├── VHF-Nederland.png
├── Route-IJsselmeer.md
├── Checklist.md
├── symbolen.txt
├── s57-nl.txt        (optioneel)
└── images/
    ├── brug.png
    └── sluis.png
```

- **PNG-afbeeldingen** worden verkleind tot ze in het venster passen.
- **Markdown** (`.md`, `.markdown`) wordt opgemaakt weergegeven, met
  tabellen, lokale afbeeldingen en klikbare links. Is de eerste regel een kop
  `# Titel`, dan wordt dat de titel in de keuzelijst; anders de bestandsnaam.
- Zet afbeeldingen die alleen in Markdown gebruikt worden in een submap
  zoals `images/`, dan verschijnen ze niet als losse items.

Een voorbeeld:

```markdown
# Route IJsselmeer

## Vertrek

- Controleer weer en route.
- Zet de marifoon op het juiste kanaal.

| Object | VHF | Opmerking |
|---|---:|---|
| Ketelbrug | 18 | Luister uit op kanaal 18 |
| Houtribsluizen | 22 | Meld je voor passage |

![Brug](images/brug.png)
```

Markdown is een open tekstformaat: je kunt notities ook met elke andere
editor schrijven en buiten OpenCPN gebruiken.

### Notities schrijven

Naast de keuzelijst staan vier knoppen. Het potlood, de naam en de
prullenbak verschijnen alleen bij een notitie:

| Knop | Wat het doet |
|---|---|
| Potlood | De notitie bewerken |
| Tekstvak met cursor | Een andere titel geven: het bestand en de `# Titel` bovenaan veranderen mee |
| Prullenbak | Verwijderen, na bevestiging. Het bestand gaat naar de map `Verwijderd` in de Legend-map; zet het terug om het weer te gebruiken |
| Plus | Een nieuwe notitie: je geeft een titel en begint meteen te schrijven |

Tijdens het bewerken zie je de notitie zoals hij eruitziet, zonder
Markdown-tekens. Bovenin staat een rij knoppen:

- **H** kiest het kopniveau: kop 1 tot en met 6, of gewone tekst;
- **B** en *I* voor vet en cursief (ook `Cmd+B` en `Cmd+I`);
- een lijst met bolletjes, een genummerde lijst of een afvinklijst; klik op
  een hokje om het af te vinken;
- een link van de geselecteerde tekst, en een afbeelding. Een afbeelding van
  buiten de Legend-map wordt eerst naar `images/` gekopieerd.

Enter na een kop geeft weer gewone tekst, Enter op een lege lijstregel
beëindigt de lijst en `Shift+Enter` begint een nieuwe regel binnen dezelfde
alinea. Alles wordt vanzelf bewaard, anderhalve seconde nadat je stopt met
typen; onderin staat wanneer. **Klaar** of `Esc` brengt je terug naar de
gewone weergave.

Op schijf blijft een notitie gewone Markdown. Een tabel zie je tijdens het
bewerken als Markdown-tekst in een vaste letter; zo kun je hem bewerken en
blijft hij precies bewaard.

## Weergave

- De legenda volgt de kleurenschema's dag, schemer en nacht van OpenCPN:
  venster, tekst, iconen, PNG-legenda's en de markering dimmen mee. Overdag
  volgt het venster de lichte of donkere weergave van macOS.
- Iconen volgen de schaalfactor voor kaartobjecten uit de instellingen van
  OpenCPN, zodat ze even groot zijn als op de kaart.
- Op een Retina-scherm blijven iconen, lampjes en ritmes scherp.
- In een smal venster worden de ritmetekeningen smaller.

## Waar de gegevens vandaan komen

- **Waypoints** vraagt de plugin aan OpenCPN. Het eerste inlezen gebeurt op de
  achtergrond kort na het starten.
- **o-charts-kaarten** (`.oesu`) leest de plugin via de o-charts-plugin zelf,
  zoals OpenCPN dat doet voor *Kaartobjectgegevens*. De o-charts-plugin doet
  de ontsleuteling en de licentiecontrole; Legend4OpenCPN slaat geen
  kaartgegevens op en exporteert niets.
- **ENC-kaarten** (S-57, `.000` met bijwerkingen), zoals de gratis
  binnenvaartkaarten van Rijkswaterstaat, leest de plugin met een eigen lezer.
- Waar kaarten hetzelfde water bedekken, telt een object één keer.
- De plugin toont dezelfde objecten als de kaart: hij vraagt OpenCPN welke
  objectsoorten op dit moment getekend worden, met de weergavecategorie, de
  aan- en uitgezette klassen en de schaalgrenzen.
- **Marifoongebieden** komen uit de GPX-lagen in de map `layers` van
  OpenCPN.

Instellingen en caches staan in de privémap van OpenCPN:

```text
~/Library/Preferences/opencpn/Legend/
```

Daar staan `legend.ini` (laatste item, venster, ingeklapte kopjes en de
keuzes uit Instellingen) en `ocharts-cells.txt` en `s57-cells.txt` met de
dekking van de kaartcellen, zodat latere starts snel zijn.

## Beperkingen

- Alleen puntobjecten uit o-charts- en S-57-kaarten; lijnen en vlakken
  (kustlijn, dieptegebieden) en andere kaartformaten (CM93, rasterkaarten,
  versleutelde S-63) doen niet mee.
- Dezelfde boei kan twee keer in de lijst staan: als waypoint uit een laag en
  als kaartsymbool.
- Sectoren komen alleen uit kaarten, niet uit waypoint-lagen.
- Lichten die de kaart op deze schaal niet tekent, branden niet mee.
- Vooruit kijkt alleen naar wat in beeld is.
- De markering op de kaart is getest met **OpenGL aan**.
- Iconen worden gelezen uit de PNG-bestanden van OpenCPN. Ontbreekt het
  bestand, dan kan het icoon op macOS leeg blijven.
- De Legend-map is nog niet instelbaar.
- De notitie-editor heeft geen spellingcontrole. Een kop binnen een citaat
  wordt bij het bewaren een gewone kop.

## Ontwikkelen

### Broncode

De repository gebruikt Git-submodules (`opencpn-libs` en `third_party/md4c`):

```bash
git clone --recurse-submodules https://github.com/kwillems/Legend4OpenCPN.git
```

Al gekloond zonder submodules:

```bash
git submodule update --init --recursive
```

### Bouwen op macOS

De ontwikkelomgeving is macOS op Apple Silicon met OpenCPN 5.14.2,
wxWidgets 3.2.9, CMake en Apple Clang. De plugin moet gebouwd worden tegen
dezelfde wxWidgets als OpenCPN; geef daarom `wx-config` expliciet op:

```bash
mkdir build
cd build
cmake -DwxWidgets_CONFIG_EXECUTABLE=/opt/wxwidgets-3.2.9/bin/wx-config ..
cmake --build . --target tarball -j8
```

Daarna is `cmake ..` genoeg; de keuze blijft bewaard in `CMakeCache.txt`.
De melding dat `wxWidgets_CONFIG_EXECUTABLE` niet gebruikt is, klopt niet en
mag je negeren. Waarschuwingen over CMake-policies zijn ook onschuldig.

Het pakket (`.tar.gz`) komt in `build/`. De naam komt uit de git-tag op de
huidige commit, bijvoorbeeld `Legend-v0.2.0-alpha25_…tar.gz`; zonder tag
volgt hij uit `Plugin.cmake` met datum en commit-id. Draai na een nieuwe tag
opnieuw `cmake ..`.

Meldingen van de plugin staan in het logboek van OpenCPN en beginnen met
`Legend:`:

```bash
grep "Legend:" ~/Library/Logs/opencpn.log | tail -20
```

### Projectstructuur

```text
Legend4OpenCPN/
├── data/
│   ├── legend_toolbar.png
│   └── s57-nl.txt           # Nederlandse namen van kaartobjecten
├── src/
│   ├── legend_pi.cpp/.h        # plugin, venster en kaartlegenda
│   ├── chart_symbols.cpp/.h    # S-52-symbolen en S-57-catalogus van OpenCPN
│   ├── ocharts_source.cpp/.h   # kaartobjecten via de o-charts-plugin
│   ├── s57_source.cpp/.h       # kaartobjecten uit ENC-cellen (S-57)
│   ├── display_settings.cpp/.h # wat de kaart nu tekent
│   ├── chart_names.cpp/.h      # namen van kaartobjecten (NL/EN)
│   ├── light_character.cpp/.h  # lichtkarakters: lezen, woorden, ritme, herkennen
│   ├── note_editor.cpp/.h      # notities bewerken: Markdown in en uit
│   └── vhf_areas.cpp/.h        # marifoongebieden uit de lagen
├── third_party/md4c/        # Markdown-verwerking (submodule)
├── opencpn-libs/            # OpenCPN-plugin-API (submodule)
├── Plugin.cmake
└── CMakeLists.txt
```

## Plannen

Ideeën voor volgende versies:

- dezelfde boei uit een laag en uit de kaart als één rij;
- bruggen markeren waar de boot niet onder past;
- het bereik van een licht laten zien in het nachtvoorbeeld;
- betekenissen bewerken in het venster zelf;
- notities die bij een plek horen en verschijnen als die in beeld is;
- een instelbare Legend-map;
- builds voor Windows en Linux, en een Engelse vertaling van de bediening.

Het idee op langere termijn: kaartmakers en andere aanbieders kunnen een
legenda of uitleg meeleveren, en gebruikers voegen op dezelfde manier hun
eigen notities en routebeschrijvingen toe.

## Licentie

Legend4OpenCPN is vrije software onder de
[GNU General Public License, versie 2](LICENSE) of, naar keuze, een latere
versie (SPDX: `GPL-2.0-or-later`). Zoals die licentie zegt, komt de plugin
zonder enige garantie; je gebruikt hem volledig op eigen risico.

Externe onderdelen houden hun eigen licentie; MD4C valt bijvoorbeeld onder de
MIT-licentie. De buildbestanden uit de OpenCPN-pluginsjabloon (zoals
`CMakeLists.txt` en de map `cmake/`) houden hun eigen licentiekoppen.
