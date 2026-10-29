# Legend4OpenCPN

Legend4OpenCPN is een plugin voor [OpenCPN](https://opencpn.org) die naast de
kaart uitlegt wat je ziet. Hij houdt een legenda bij van precies de boeien,
bakens, tekens en andere symbolen die op dat moment op je scherm staan, en
verandert mee als je de kaart verschuift of zoomt. Daarnaast vertelt hij wat
de lichten doen en houdt hij voor je de uitkijk: wat er op je koers ligt en
hoe ver het weg is. Ook toont hij je eigen legenda's, notities en
routebeschrijvingen.

> Status: alpha. De plugin wordt ontwikkeld en getest op macOS (Apple
> Silicon) met OpenCPN 5.14.

> **Gebruik op eigen risico.** Legend4OpenCPN is een hulpmiddel, geen
> navigatiemiddel. De uitleg, lichtkarakters, kanalen en andere informatie
> kunnen onvolledig, verouderd of fout zijn. Vertrouw er nooit alleen op:
> gebruik officiële kaarten en berichten, en blijf zelf goed uitkijken. De
> maker is niet aansprakelijk voor schade of ongelukken door het gebruik van
> deze plugin.

## In het kort

**Kaartlegenda**

- **Precies wat er nu op je scherm staat:** elke soort boei, baken, topteken,
  licht, meerpaal en ander symbool in beeld, één rij per soort met het aantal.
  Verschuif, zoom of draai je de kaart, of zet je een laag aan of uit, dan
  past de lijst zich vanzelf aan.
- **Ook als je ver uitzoomt:** symbolen die de kaart op die schaal weglaat,
  kunnen grijs in de lijst blijven staan, met de schaal waarop ze verschijnen.
  Zo weet je wat er in het gebied ligt; een klik zoomt erheen.
- Uit waypoint-lagen (zoals Nautin), o-charts en ENC-kaarten, met hun
  betekenis in het Nederlands of Engels, volgens de termen van Kaart 1.
- Klik op een rij en de plugin wijst het symbool aan op de kaart; klik op de
  kaart en de plugin wijst de rij aan. De teller ◎ wijst alle exemplaren
  tegelijk aan.
- Onderweg desgewenst boven de betekenis van elke rij de **afstand en
  peiling vanaf de boot** naar het dichtstbijzijnde exemplaar, zoals
  *0,3 NM · 045°*.
- Zelf in te delen onder kopjes, in te klappen en te ordenen, met een
  zoekveld.
- Eigen betekenissen in één tekstbestand; iconen als PNG of SVG.

**Lichten**

- Elk lichtkarakter in gewone woorden met een tekening van zijn ritme,
  onder de ton of het baken waar het licht op staat; sectorlichten met hun
  sectoren in woorden.
- Een licht laten branden op de kaart, in zijn eigen ritme, of alle lichten in
  beeld tegelijk.
- **Nachtvoorbeeld:** de kaart donker, alleen de lichten branden, zoals je het
  's nachts vanaf het water ziet.

**Licht herkennen**

- Zie je een licht en weet je niet welk het is? Tik mee met zijn ritme, op een
  groot vlak of de spatiebalk, en de plugin noemt de lichten in beeld die het
  best passen. Een gemiste periode door de deining is geen probleem.

**Uitkijk**

- Tijdens het varen: wat je de komende 1, 2, 5, 10 of 20 zeemijl op koers
  tegenkomt, of zo ver als je zelf invult (jij kiest), op volgorde van
  aankomst, met wanneer je er bent, de kant waar het ligt, de afstand en zijn
  licht, en het volgende marifoongebied. Ook wat je op dit moment passeert.
- De uitkijkbundel staat licht op de kaart, zodat je ziet waar de uitkijk
  kijkt; smal, middel of breed, in een kleur naar keuze. Alles wat in de
  bundel ligt, staat in de lijst.

**Marifoon**

- Het kanaal van het gebied waar je vaart, met wat je moet doen en het
  telefoonnummer.
- Een klik op een VHF-icoon of rechtsklikken op de kaart laat het hele gebied
  oplichten en noemt het kanaal; vaar je een nieuw gebied in, dan volgt een
  melding met pieptonen.

**Eigen legenda's en notities**

- Notities in Markdown uit een vaste map, om doorheen te bladeren. Een losse
  PNG in die map wordt vanzelf een notitie.
- Notities schrijven en bewerken in het venster zelf, zoals in een
  tekstverwerker: koppen, vet, lijsten, afvinklijsten, links en afbeeldingen.
  Op schijf blijft het gewone Markdown.

**Notities op de kaart**

- Pin een notitie vast op een plek: die staat als geel blaadje op de kaart,
  onder *Notities* in de legenda en in de *Uitkijk*, en je krijgt een
  melding als je in de buurt komt.
- Een nieuwe notitie maak je met een rechtsklik op de kaart, meteen
  vastgepind op die plek, of onderweg op de positie van de boot.
- Sleep een blaadje naar een andere plek; per ongeluk verplaatst zet je met
  één klik terug.
- In de tekst een link naar een plek: een klik laat de kaart erheen gaan.

**Weergave**

- Volgt de kleurenschema's dag, schemer en nacht van OpenCPN, scherp op een
  Retina-scherm.

## Installeren

1. Download of bouw het pakket, bijvoorbeeld
   `Legend-v0.2.0-alpha27_darwin-wx32-10.13.6-arm64.tar.gz`.
2. Kies in OpenCPN **Opties → Plugins → Import Plugin** en kies het pakket.
3. Zet de plugin aan. In de werkbalk verschijnt de Legend-knop.

Een nieuwe versie kun je over de oude heen importeren.

## Het venster

De Legend-knop opent en sluit het venster. Bovenin staat een keuzelijst met
alle items, met daarnaast de positie (`2 van 12`). Het eerste item is altijd
de **Kaartlegenda**; daarna volgen je notities op alfabet.

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
  het niet ruim in beeld, dan schuift de kaart ernaartoe. Staat *Afstand en
  peiling vanaf de boot* aan, dan is dat het exemplaar dat het dichtst bij de
  boot ligt (zie hieronder).
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
| Afstand en peiling vanaf de boot | Boven de betekenis van elke rij de afstand en peiling naar het dichtstbijzijnde exemplaar (zie hieronder); standaard uit |
| Engelse namen | Betekenissen in het Engels in plaats van het Nederlands |
| Alle lichten laten branden | Alle lichten in beeld branden op de kaart |
| Nachtvoorbeeld | De kaart zoals je hem 's nachts ziet |
| Licht herkennen | Een licht herkennen aan zijn ritme |
| Marifoongebied blijft oplichten | Een aangeklikt marifoongebied blijft rood tot de volgende klik op de kaart, in plaats van na zes seconden te vervagen |
| Melding bij nieuw marifoongebied | Een melding met pieptonen als de boot een nieuw marifoongebied binnenvaart |
| Melding bij notitie in de buurt | Een melding met pieptonen als de boot binnen de straal van een vastgepinde notitie komt |
| Uitkijkbundel op de kaart | De uitkijkbundel licht getekend op de kaart; standaard aan |
| Uitkijkbundel: breedte ▸ | Smal (15°), Middel (30°) of Breed (45°) aan weerszijden van de koers |
| Uitkijkbundel: lengte ▸ | 1, 2, 5, 10 of 20 zeemijl, of *Anders…* om zelf een lengte in te vullen (0,5 tot 50 zeemijl); standaard 2. Tijdens het varen staat erachter hoe lang je erover doet |
| Uitkijkbundel: kleur ▸ | Amber (standaard), Geel, Rood, Groen, Paars of Grijs |
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
deze schaal verborgen tonen** krijgen ze een grijze rij. Boven de betekenis
staan klein een gesloten oogje en de schaal waarop ze verschijnen (`1:200k`). Een klik zoomt in tot het
dichtstbijzijnde exemplaar zichtbaar is. Wat je zelf hebt uitgezet bij
*Gebruikers standaard* in OpenCPN blijft weg.

### Afstand en peiling vanaf de boot

Met **Afstand en peiling vanaf de boot** staat boven de betekenis van elke
rij klein en grijs hoe ver het dichtstbijzijnde exemplaar van de boot af ligt
en in welke richting: *0,3 NM · 045°* (ware peiling), of in meters onder een
tiende zeemijl, zoals *150 m*. In de Uitkijk staat het bij elk object.

- Een klik op een rij gaat dan naar het exemplaar dat het dichtst bij de boot
  ligt; nog een klik naar het volgende, steeds verder weg. Klik je eerst een
  andere rij aan, dan begint het weer bij het dichtstbijzijnde.
- Het werkt alleen met een GPS-positie van de laatste minuut; anders blijven
  de getallen weg en geldt het midden van de kaart, zoals altijd.
- Onderweg worden de getallen hooguit eens per vijf seconden bijgewerkt.
- Thuis uitproberen kan met de plugin ShipDriver.

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

Een marifoongebied kan op de kaart oplichten, lichtrood zoals op
[vhfinfo.org](https://vhfinfo.org/map.html):

- **Klik op een VHF-icoon** van de laag op de kaart, op zijn rij in de
  legenda of op een regel van het Marifoon-kopje: het hele gebied licht op en
  bovenaan de kaart verschijnt een venstertje met het kanaal. Bestaat een
  gebied uit meer stukken, dan lichten ze allemaal op. De teller ◎ van een
  rij laat alle gebieden van dat icoon in beeld oplichten.
- **Welk marifoonkanaal hier?** in het rechtsklikmenu van de kaart laat alle
  gebieden op die plek oplichten en noemt hun kanalen in een venstertje. Dit
  werkt ook als de laag in OpenCPN uit staat: de plugin leest de gebieden
  zelf uit het GPX-bestand.
- Een gebied licht zes seconden op en vervaagt dan, of blijft staan tot je
  weer op de kaart klikt (instelling *Marifoongebied blijft oplichten*).

**Melding bij een nieuw gebied.** Vaar je met GPS een marifoongebied binnen
waar je net nog niet was, dan licht het op en verschijnt bovenaan de kaart
een venstertje met het kanaal, bijvoorbeeld *kanaal 18 · Ketelbrug ·
verkeerspost · melden*, met zes pieptonen. Het venstertje verdwijnt na twaalf
seconden, met **Sluiten** of met een klik ernaast. Dit werkt ook als het legendavenster dicht is, en
is uit te zetten met *Melding bij nieuw marifoongebied*.

### Uitkijk

Het kopje **Uitkijk** laat zien wat je op koers tegenkomt. Het verschijnt
zodra de boot vaart: een recente GPS-positie en minstens een halve knoop.
Naast het kopje staat waar de uitkijk naar kijkt, bijvoorbeeld *tot 2 NM,
± 20 min · breedte 30°*, en *niets in de bundel* als er niets ligt. De tijd
is een schatting bij je snelheid van dit moment.

Elke regel begint klein met wanneer en waar, met de beschrijving eronder:

- Alle objecten uit de legenda in de bundel, op volgorde van aankomst,
  bijvoorbeeld *over 4 min · stuurboord · 0,6 NM · 038°* met daaronder *Rode
  spitse ton* en het lichtkarakter. Afstand en peiling staan erbij als
  *Afstand en peiling vanaf de boot* aan staat.
- Met *Ook op deze schaal verborgen tonen* staan ook tonnen die de kaart op
  deze schaal nog niet tekent erin, grijs, met het gesloten oogje en de
  schaal, net als in de rest van de legenda. Een klik zoomt erheen.
- Ook wat je op dit moment passeert: wat binnen 0,12 zeemijl (zo'n 220 meter)
  naast of net achter je ligt, staat bovenaan als *nu*, tot het achter je uit
  die cirkel valt.
- Tot twee marifoongebieden waar de koers in gaat, op hun plek in de tijd,
  bijvoorbeeld *over 12 min* met daaronder *Marifoongebied kanaal 22 ·
  Houtribsluizen · melden*. Een klik laat het gebied oplichten. De tekens van de laag
  *VHFinfo Nederland* zelf tellen niet als object; echte marifoonborden langs
  het water (B11, E23) wel.
- De vastgepinde notities op je koers, bijvoorbeeld *over 6 min ·
  stuurboord* met daaronder *Notitie Ondiepte bij de Ketelbrug*. Een klik
  opent de notitie; is hij op deze schaal verborgen, dan zoomt de klik erheen.
- Een klik op een regel wijst het object aan op de kaart, of laat zijn licht
  branden.

**De uitkijkbundel.** De uitkijk kijkt vanuit een cirkel van 0,12 zeemijl om
de boot in een waaier vooruit: dichtbij minstens 0,12 zeemijl breed aan elke
kant, verderop breder. Het eind is rond: elk punt van de rand ligt even ver
van de boot. Hoe ver hij reikt, kies je met **Uitkijkbundel: lengte**: 1, 2
(standaard), 5, 10 of 20 zeemijl, of met *Anders…* een eigen lengte van 0,5
tot 50 zeemijl. De lengte hangt niet af van je snelheid; in het menu staat
achter elke keuze hoe lang je er bij je snelheid van nu over doet. Op druk
vaarwater houdt een korte lengte de lijst overzichtelijk. Hoe breed de waaier uitloopt, kies je met **Uitkijkbundel:
breedte**: smal (15°), middel (30°, standaard) of breed (45°) aan
weerszijden van de koers.

Met **Uitkijkbundel op de kaart** (standaard aan) staat de bundel heel licht
op de kaart, zolang de legenda open is en de boot vaart. De kleur kies je met
**Uitkijkbundel: kleur**: amber (standaard), geel, rood, groen, paars of
grijs. Zo
zie je waarom een ton wel of niet in de lijst staat. Uitzetten verbergt
alleen de tekening; de lijst blijft. Bij schemer en nacht wordt de bundel
gedimd, maar blijft hij zichtbaar.

Testen zonder te varen kan met de plugin ShipDriver.

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
  icoon zonder `.png` of `.svg`, of voor een kaartsymbool de S-52-naam (die
  staat in de tooltip van de rij).
- Een `*` in de naam staat voor willekeurige tekst; een `*` in de betekenis
  wordt door die tekst vervangen. Exacte namen gaan voor.
- `[Kopje]` begint een groep. Kopjes en regels verschijnen in de volgorde van
  het bestand.
- Twee talen: `betekenis | meaning` en `[Kopje | Heading]`. Is één kant
  leeg, zoals `| Yellow buoy`, dan geldt in beide talen de andere.
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

Alle Markdown-bestanden in de Legend-map verschijnen in de keuzelijst:

```text
~/Documents/OpenCPN/Legend/
├── VHF-Nederland.md
├── Route-IJsselmeer.md
├── Checklist.md
├── symbolen.txt
├── s57-nl.txt        (optioneel)
└── images/
    ├── brug.png
    ├── sluis.png
    └── VHF-Nederland.png
```

- **Markdown** (`.md`, `.markdown`) wordt opgemaakt weergegeven, met
  tabellen, lokale afbeeldingen en klikbare links. Is de eerste regel een kop
  `# Titel`, dan wordt dat de titel in de keuzelijst; anders de bestandsnaam.
- Een afbeelding die breder is dan het venster wordt verkleind tot hij past,
  ook als je het venster smaller maakt; kleinere houden hun eigen maat.
- **Een losse PNG** in de Legend-map, zoals een gescande legenda, wordt
  vanzelf een notitie: de afbeelding verhuist naar `images/` en een notitie
  met dezelfde naam toont hem. Zo kun je hem bewerken, hernoemen,
  verwijderen en vastpinnen als elke andere notitie. Een PNG die al in een
  notitie staat, blijft waar hij is.

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
  buiten de Legend-map wordt eerst naar `images/` gekopieerd;
- de **speld** voor een plek op de kaart (zie *Notities op de kaart*).

Enter na een kop geeft weer gewone tekst, Enter op een lege lijstregel
beëindigt de lijst en `Shift+Enter` begint een nieuwe regel binnen dezelfde
alinea. Alles wordt vanzelf bewaard, anderhalve seconde nadat je stopt met
typen; onderin staat wanneer. **Opslaan**, bovenin naast de keuzelijst, of
`Esc` bewaart en brengt je terug naar de gewone weergave.

Op schijf blijft een notitie gewone Markdown. Een tabel zie je tijdens het
bewerken als Markdown-tekst in een vaste letter; zo kun je hem bewerken en
blijft hij precies bewaard.

### Notities op de kaart

Een notitie kan op twee manieren met de kaart verbonden zijn.

**Een link naar een plek in de tekst.** Klik je er in de notitie op, dan gaat
de kaart erheen en wijst de plek aan. Zo'n link maak je met de speld-knop
(*Link naar het midden van de kaart in de tekst*), of met een rechtsklik op de
kaart: *Notitie ▸ Link naar deze plek in de tekst*. Is er tekst geselecteerd,
dan wordt die de link; anders komen de coördinaten in de tekst. In het
bestand staat zo'n link als `[Ketelbrug](geo:52.58030,5.75120)`, een
standaard die ook andere kaartprogramma's begrijpen.

**De hele notitie vastgepind op een plek.** Dat kan op drie manieren:

- met een rechtsklik op de kaart: *Notitie ▸ Nieuwe notitie hier*. Je geeft
  een titel, en de nieuwe notitie opent meteen vastgepind op die plek, ook als
  het legendavenster dicht was;
- tijdens het bewerken met een rechtsklik: *Notitie ▸ Hier vastpinnen op de
  kaart*;
- met de speld-knop: *Notitie vastpinnen op het midden van de kaart*.

**Waar de boot nu is.** Met een GPS-positie van de laatste minuut kan het ook
op de positie van de boot, waar je ook klikt:

- de plus-knop in het venster vraagt dan *Nieuwe notitie* of *Nieuwe notitie
  bij de boot*; met een rechtsklik: *Notitie ▸ Nieuwe notitie bij de boot*;
- tijdens het bewerken, via de speld-knop of *Notitie ▸* in het
  rechtsklikmenu: *Vastpinnen op de positie van de boot*, of *Positie van de
  boot in de tekst*. Dat laatste zet de positie met de tijd als link in de
  notitie, bijvoorbeeld *52°34,818′N 5°45,072′E (14:32)*: handig voor een
  logboek.

Boven de tekst staat dan *Vastgepind op 52°34,818′N 5°45,072′E · straal
0,5 NM*. Een klik daarop toont de plek; met **Straal** pas je de afstand voor
de melding aan, met **Losmaken** maak je de notitie weer los. In de gewone
weergave staat hetzelfde in een gekleurd balkje boven de notitie.

Een vastgepinde notitie:

- staat als een **geel blaadje** op de kaart. Met de muis erboven zie je de
  titel; een klik opent de notitie en toont de straal als stippellijn. Een
  rechtsklik op het blaadje geeft *Notitie ▸ "…" openen* en *"…"
  verwijderen*. **Sleep** het blaadje om de notitie te verplaatsen: de
  straal schuift mee en bij het loslaten komt de nieuwe plek in de notitie.
  Een melding biedt daarna **Terugzetten** en **Sluiten**; is die weg, dan
  staat *Notitie ▸ "…" terugzetten op de oude plek* nog in het
  rechtsklikmenu, voor de laatst verplaatste notitie. Net als tonnen en bakens verdwijnt het blaadje als je verder
  uitzoomt dan 1:200 000;
- staat onder het kopje **Notities** in de legenda zolang de plek in beeld
  is, de dichtstbijzijnde eerst: klein de afstand, daaronder de titel en de
  eerste regel tekst. Met
  *Ook op deze schaal verborgen tonen* blijft een notitie voorbij zijn
  schaalgrens er grijs staan, met een dicht oogje en de schaal; een klik zoomt
  erheen;
- staat in de **Uitkijk** als je koers erlangs gaat;
- geeft onderweg een **melding** met pieptonen zodra je binnen de straal
  komt. Een klik op de melding opent de notitie. Een notitie die je net
  vastpint waar de boot is, of die je aan het bewerken bent, meldt zich niet;
  pas als je wegvaart en terugkomt. Uit te zetten met *Melding bij notitie in
  de buurt*.

De plek staat bovenaan het bestand, in het blokje dat ook Obsidian gebruikt:

```markdown
---
location: [52.58030, 5.75120]
straal: 0.5
---
# Ondiepte bij de Ketelbrug
```

`location` schrijft de Obsidian-plugin *Map View* ook zo, dus de notitie
verschijnt daar eveneens op de kaart. `straal` is in zeemijl. Met een regel
`schaal: 1:50000` geef je een notitie een andere schaalgrens dan 1:200 000.
Andere regels die je in het blokje zet, zoals `tags`, blijven bewaard.

## Weergave

- De legenda volgt de kleurenschema's dag, schemer en nacht van OpenCPN:
  venster, tekst, iconen en de markering dimmen mee. Overdag
  volgt het venster de lichte of donkere weergave van macOS.
- Iconen volgen de schaalfactor voor kaartobjecten uit de instellingen van
  OpenCPN, zodat ze even groot zijn als op de kaart.
- Op een Retina-scherm blijven iconen, lampjes en ritmes scherp.
- Waypoint-iconen mogen PNG of SVG zijn. Staat er in `UserIcons` een SVG met
  dezelfde naam als een PNG, dan gebruikt de legenda net als OpenCPN de SVG,
  op zijn eigen formaat en op een Retina-scherm opnieuw getekend op dubbele
  grootte in plaats van vergroot.
- In een smal venster worden de ritmetekeningen smaller.

## Waar de gegevens vandaan komen

- **Waypoints van lagen** leest de plugin zelf uit de GPX-bestanden in de
  map `layers` van OpenCPN; andere waypoints vraagt hij aan OpenCPN. Het
  eerste inlezen gebeurt kort na het starten, met een voortgangsbalk.
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
- **Marifoongebieden** leest de plugin uit de GPX-lagen in de map `layers`
  van OpenCPN: de gesloten tracks met een regel `VHF-kanaal:` in de
  beschrijving. Een nieuwe versie van zo'n laag ziet de plugin na een herstart
  van OpenCPN.

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
- De uitkijk kijkt alleen naar wat in beeld is: zoom zo ver uit dat de
  bundel op het scherm past. Wat de kaart op die schaal niet meer tekent,
  staat er alleen in met *Ook op deze schaal verborgen tonen*.
- De markering op de kaart is getest met **OpenGL aan**.
- Iconen worden gelezen uit de SVG- en PNG-bestanden van OpenCPN. Ontbreekt
  het bestand, dan kan het icoon op macOS leeg blijven.
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
huidige commit, bijvoorbeeld `Legend-v0.2.0-alpha27_…tar.gz`; zonder tag
volgt hij uit `Plugin.cmake` met datum en commit-id. Draai na een nieuwe tag
opnieuw `cmake ..`.

Meldingen van de plugin staan in het logboek van OpenCPN en beginnen met
`Legend:`:

```bash
grep "Legend:" ~/Library/Logs/opencpn.log | tail -20
```

### Tests

De onderdelen die zonder OpenCPN werken, hebben unit tests in `test/`:
lichtkarakters, marifoongebieden, notities met een plek, `symbolen.txt`, het
inlezen van laagwaypoints en afstanden, peilingen en de uitkijkbundel.
Bouwen en draaien in de map `build/`:

```bash
make legend-tests
ctest --output-on-failure
```

`./legend-tests` toont elke mislukte controle met het verwachte en het
werkelijke resultaat. In CircleCI draaien de tests mee na het bouwen.

### Projectstructuur

```text
Legend4OpenCPN/
├── data/
│   ├── legend_toolbar.png
│   └── s57-nl.txt           # Nederlandse namen van kaartobjecten
├── src/
│   ├── legend_pi.cpp/.h        # de plugin: starten, instellingen, legendabestanden
│   ├── legend_window.cpp       # het venster, klikken, toetsen, instellingenmenu
│   ├── legend_collect.cpp      # waypoints, betekenissen en kaartobjecten verzamelen
│   ├── legend_render.cpp       # de pagina van de kaartlegenda, blok voor blok
│   ├── legend_chart.cpp        # klikken, menu en tekenen op de kaart
│   ├── legend_notes.cpp        # notities schrijven en op de kaart
│   ├── legend_navigation.cpp   # onderweg: positie, uitkijk, licht herkennen, marifoon
│   ├── legend_html_window.*    # het HTML-venster van de legenda
│   ├── legend_icons.*          # iconen inlezen en tekenen
│   ├── legend_shared.*         # kleuren en hulpjes die alle delen gebruiken
│   ├── symbol_meanings.*       # symbolen.txt lezen en opzoeken
│   ├── geo.*                   # afstand, peiling en de uitkijkbundel
│   ├── chart_symbols.*         # S-52-symbolen en S-57-catalogus van OpenCPN
│   ├── ocharts_source.*        # kaartobjecten via de o-charts-plugin
│   ├── s57_source.*            # kaartobjecten uit ENC-cellen (S-57)
│   ├── display_settings.*      # wat de kaart nu tekent
│   ├── chart_names.*           # namen van kaartobjecten (NL/EN)
│   ├── layer_waypoints.*       # waypoints uit de GPX-lagen
│   ├── light_character.*       # lichtkarakters: lezen, woorden, ritme, herkennen
│   ├── shining_lights.*        # lichten laten branden op de kaart
│   ├── note_editor.*           # notities bewerken: Markdown in en uit
│   ├── note_places.*           # notities met een plek: het blokje bovenaan
│   ├── vhf_areas.*             # marifoongebieden uit de lagen
│   ├── vhf_highlight.*         # een marifoongebied laten oplichten
│   └── chart_notice.*          # het venstertje bovenaan de kaart
├── test/                    # unit tests (make legend-tests, ctest)
├── third_party/md4c/        # Markdown-verwerking (submodule)
├── opencpn-libs/            # OpenCPN-plugin-API (submodule)
├── Plugin.cmake
└── CMakeLists.txt
```

## Plannen

Ideeën voor volgende versies:

- bruggen markeren waar de boot niet onder past;
- het bereik van een licht laten zien in het nachtvoorbeeld;
- betekenissen bewerken in het venster zelf;
- een instelbare Legend-map;
- builds voor Windows en Linux, en een Engelse vertaling van de bediening.

## Licentie

Legend4OpenCPN is vrije software onder de
[GNU General Public License, versie 2](LICENSE) of, naar keuze, een latere
versie (SPDX: `GPL-2.0-or-later`). Zoals die licentie zegt, komt de plugin
zonder enige garantie; je gebruikt hem volledig op eigen risico.

Externe onderdelen houden hun eigen licentie; MD4C valt bijvoorbeeld onder de
MIT-licentie. De buildbestanden uit de OpenCPN-pluginsjabloon (zoals
`CMakeLists.txt` en de map `cmake/`) houden hun eigen licentiekoppen.
