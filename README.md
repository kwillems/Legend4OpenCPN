# Legend4OpenCPN

Legend4OpenCPN is een OpenCPN-plugin voor het tonen van legenda's, notities en andere compacte informatie naast de kaart.

De plugin is begonnen als een manier om een legenda bij kaartlagen te tonen, maar is bewust breder opgezet. Naast afbeeldingen kan Legend4OpenCPN ook Markdown-bestanden tonen. Daardoor kan dezelfde plugin later ook worden gebruikt voor bijvoorbeeld routebeschrijvingen, vaarinstructies, checklists en eigen aantekeningen.

> Status: vroege ontwikkelversie / alpha.

## Huidige mogelijkheden

Op dit moment ondersteunt Legend4OpenCPN:

- een eigen knop in de OpenCPN-toolbar;
- een afzonderlijk legenda-/informatievenster;
- PNG-afbeeldingen;
- Markdown (`.md` en `.markdown`);
- bladeren door meerdere items met vorige/volgende;
- alfabetische sortering van beschikbare items;
- onthouden van het laatst getoonde item;
- automatisch aanmaken van de benodigde gebruikersmappen.

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
└── Checklist.md
```

De plugin maakt deze map automatisch aan wanneer dat nodig is.

De interne configuratie van de plugin wordt apart opgeslagen onder:

```text
~/Library/Application Support/OpenCPN/Legend/
```

Daar staat onder andere `legend.ini`, waarin bijvoorbeeld het laatst gekozen item wordt onthouden.

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

De bedoeling is dat gebruikers zulke bestanden gewoon met een teksteditor of notitie-app kunnen maken en daarna in de `Legend`-map plaatsen.

Ondersteuning voor afbeeldingen en andere media *binnen* Markdown is een volgende stap.

## Waarom Markdown?

Markdown is eenvoudig met de hand te schrijven en wordt door veel bestaande programma's gebruikt of ondersteund.

Daardoor kan Legend4OpenCPN op termijn ook informatie ontvangen die afkomstig is uit bijvoorbeeld notitieprogramma's en converters. Als mogelijke toekomstige importlaag wordt onder andere gekeken naar oplossingen zoals Obsidian Importer.

Het uitgangspunt is om Legend4OpenCPN zelf klein te houden: de plugin moet vooral een stabiele weergavelaag zijn voor eenvoudige, uitwisselbare inhoud.

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
cmake --build . -j8
```

Bij een geslaagde build wordt in `build/` onder andere een OpenCPN-pluginpakket (`.tar.gz`) aangemaakt.

De CMake-template kan tijdens het bouwen waarschuwingen tonen over oudere CMake-policies en meldingen dat de buildmap zelf geen Git-repository is. Deze meldingen zijn in de huidige ontwikkelomgeving niet fataal zolang de configuratie en compilatie verder slagen.

## Installeren in OpenCPN

De gegenereerde `.tar.gz` kan in OpenCPN worden geïmporteerd via:

```text
Opties → Plugins → Import Plugin
```

Na installatie verschijnt de Legend-knop in de OpenCPN-toolbar.

Klik op de knop om het legenda-/informatievenster te openen of te sluiten.

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

- automatisch opnieuw inlezen wanneer bestanden in de Legend-map wijzigen;
- positie en grootte van het venster onthouden;
- een instelbare locatie voor de Legend-map;
- afbeeldingen en andere lokale media in Markdown;
- een eenvoudige pakketstandaard voor legenda's en informatie;
- import van bestaande notitieformaten via een aparte conversielaag;
- direct kiezen van een legenda/notitie in plaats van alleen bladeren;
- betere integratie met OpenCPN-kaartlagen.

Een belangrijk ontwerpdoel is dat kaartmakers of andere aanbieders later standaard een bijbehorende legenda of informatiesnippet kunnen meeleveren, terwijl gebruikers op exact dezelfde manier hun eigen notities en routebeschrijvingen kunnen toevoegen.

## Licentie

Voor Legend4OpenCPN zelf is nog geen definitieve licentie vastgelegd.

Externe dependencies behouden hun eigen licenties. MD4C wordt bijvoorbeeld onder de MIT-licentie uitgebracht.

## Ontwikkeling

Dit project is in actieve ontwikkeling. De huidige versie is bedoeld als werkende basis waarop de bestandsstructuur, Markdown-ondersteuning en distributie verder worden ontwikkeld.
