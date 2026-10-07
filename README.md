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
- automatisch aanmaken van de benodigde gebruikersmappen.

Bij nul of één item wordt overbodige navigatie niet getoond of uitgeschakeld. Bij meerdere items verschijnt compact een keuzelijst met daarnaast de positie, bijvoorbeeld `2 van 5`.

Markdown wordt verwerkt met [MD4C](https://github.com/mity/md4c).

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
└── images/
    ├── brug.png
    └── sluis.png
```

De plugin maakt de hoofdmap automatisch aan wanneer dat nodig is.

PNG-bestanden in de hoofdmap worden als afzonderlijke legenda-items getoond. Afbeeldingen die alleen in Markdown-documenten worden gebruikt, kunnen daarom het best in een submap zoals `images/` worden geplaatst.

De interne configuratie van de plugin wordt apart opgeslagen onder:

```text
~/Library/Application Support/OpenCPN/Legend/
```

Daar staat onder andere `legend.ini`. Daarin worden bijvoorbeeld het laatst gekozen item en de vensterpositie en -grootte bewaard.

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

Voor de huidige build wordt expliciet wxWidgets 3.2 gebruikt:

```bash
cd ~/Development/Legend4OpenCPN

rm -rf build
mkdir build
cd build

export WX_CONFIG=/opt/homebrew/bin/wx-config-3.2

cmake ..
cmake --build . --target tarball -j8
```

Gebruik voor een installeerbaar OpenCPN-pakket expliciet de `tarball`-target. Alleen de normale build van `liblegend_pi.dylib` is niet voldoende om een actuele importeerbare `.tar.gz` te garanderen.

Bij een geslaagde build wordt in `build/` een OpenCPN-pluginpakket (`.tar.gz`) aangemaakt.

De CMake-template kan tijdens het bouwen waarschuwingen tonen over oudere CMake-policies. Deze meldingen zijn in de huidige ontwikkelomgeving niet fataal zolang de configuratie en compilatie verder slagen.

## Installeren in OpenCPN

De gegenereerde `.tar.gz` kan in OpenCPN worden geïmporteerd via:

```text
Opties → Plugins → Import Plugin
```

Voor testen van een nieuwe build is het verstandig de bestaande Legend-plugin eerst uit te schakelen, het nieuwe pakket te importeren en de plugin daarna weer in te schakelen.

Na installatie verschijnt de Legend-knop in de OpenCPN-toolbar.

## Projectstructuur

Belangrijke onderdelen:

```text
Legend4OpenCPN/
├── data/
│   └── legend_toolbar.png
├── src/
│   ├── legend_pi.cpp
│   └── legend_pi.h
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

Een belangrijk ontwerpdoel is dat kaartmakers of andere aanbieders later standaard een bijbehorende legenda of informatiesnippet kunnen meeleveren, terwijl gebruikers op dezelfde manier hun eigen notities en routebeschrijvingen kunnen toevoegen.

## Licentie

Voor Legend4OpenCPN zelf is nog geen definitieve licentie vastgelegd.

Externe dependencies behouden hun eigen licenties. MD4C wordt bijvoorbeeld onder de MIT-licentie uitgebracht.

## Ontwikkeling

Dit project is in actieve ontwikkeling. De huidige versie is bedoeld als werkende basis waarop de bestandsstructuur, Markdown-ondersteuning, notitiefuncties en distributie verder worden ontwikkeld.
