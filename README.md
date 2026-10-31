# Legend4OpenCPN

English | [Nederlands](README.nl.md)

Legend4OpenCPN is a plugin for [OpenCPN](https://opencpn.org) that explains
what you see on the chart. It keeps a legend of exactly the buoys, beacons,
signs and other symbols on your screen at that moment, and it changes as you
pan or zoom the chart. It also tells you what the lights do and keeps a
lookout for you: what lies on your course and how far away it is. It shows
your own legends, notes and route descriptions too.

> Status: alpha. The plugin is developed and tested on macOS (Apple Silicon)
> with OpenCPN 5.14.

> **Use at your own risk.** Legend4OpenCPN is an aid, not a means of
> navigation. The explanations, light characteristics, channels and other
> information may be incomplete, out of date or wrong. Never rely on it
> alone: use official charts and notices, and keep a proper lookout
> yourself. The author is not liable for any damage or accidents caused by
> using this plugin.

> **Language.** The plugin's interface is in Dutch for now; an English
> interface is planned. Symbol meanings can already be shown in English.
> This README gives each menu item and button as it appears in the plugin,
> followed by an English translation in parentheses.

## At a glance

**Chart legend**

- **Exactly what is on your screen now:** every kind of buoy, beacon,
  topmark, light, mooring post and other symbol in view, one row per kind
  with its count. Pan, zoom or rotate the chart, or switch a layer on or
  off, and the list follows by itself.
- **Even when you zoom far out:** symbols the chart leaves out at that
  scale can stay in the list in grey, with the scale at which they appear.
  That way you know what lies in the area; one click zooms there.
- From waypoint layers (such as Nautin), o-charts and ENC charts, with their
  meaning in Dutch or English, using the terms of INT 1 (the Dutch edition
  is called *Kaart 1*).
- Click a row and the plugin points out the symbol on the chart; click the
  chart and the plugin points out the row. The counter ◎ points out all
  instances at once.
- Under way, optionally the **distance and bearing from the boat** to the
  nearest instance above each row's meaning, such as *0.3 NM · 045°*.
- Group the rows under your own headings, fold and reorder them, and use
  the search field.
- Your own meanings in a single text file; icons as PNG or SVG.

**Lights**

- Every light characteristic in plain words with a drawing of its rhythm,
  under the buoy or beacon that carries the light; sector lights with their
  sectors in words.
- Make a light shine on the chart in its own rhythm, or all lights in view
  at once.
- **Night preview:** the chart goes dark and only the lights shine, as you
  would see it from the water at night.

**Identify a light**

- See a light and don't know which one it is? Tap along with its rhythm, on
  a large pad or the space bar, and the plugin names the lights in view that
  fit best. Missing a period because of the swell is no problem.

**Lookout**

- While sailing: what you will meet on your course within the next 1, 2, 5,
  10 or 20 nautical miles, or any distance you enter (your choice), in order
  of arrival, with when you get there, which side it lies on, the distance
  and its light, and the next VHF area. Also what you are passing right now.
- The lookout beam is drawn faintly on the chart, so you can see where the
  lookout is looking: narrow, medium or wide, in a colour of your choice,
  with cross lines at every nautical mile and its reach at the end.
  Everything inside the beam is in the list.

**VHF**

- The channel for the area you are sailing in, with what you have to do and
  the phone number.
- Clicking a VHF icon or right-clicking the chart highlights the whole area
  and names the channel; when you sail into a new area, a notice appears
  with beeps.

**Your own legends and notes**

- Notes in Markdown from a fixed folder, to page through. A loose PNG in
  that folder becomes a note by itself.
- Write and edit notes in the window itself, as in a word processor:
  headings, bold, lists, checklists, links and images. On disk it stays
  plain Markdown.

**Notes on the chart**

- Pin a note to a place: it shows as a yellow sticky note on the chart,
  under *Notities* (Notes) in the legend and in the *Uitkijk* (Lookout), and
  you get a notice when you come near.
- Create a new note by right-clicking the chart, pinned to that spot right
  away, or under way at the boat's position.
- Drag a sticky note to another place; moved it by accident? One click puts
  it back.
- A link to a place in the text: one click takes the chart there.

**Display**

- Follows OpenCPN's day, dusk and night colour schemes, sharp on a Retina
  display.

## Installing

1. Download or build the package, for example
   `Legend-v0.2.0-alpha27_darwin-wx32-10.13.6-arm64.tar.gz`.
2. In OpenCPN, choose **Options → Plugins → Import Plugin** and select the
   package.
3. Enable the plugin. The Legend button appears in the toolbar.

You can import a new version over the old one.

## The window

The Legend button opens and closes the window. At the top is a drop-down
list of all items, with the position next to it (`2 van 12`, 2 of 12). The
first item is always the **Kaartlegenda** (Chart legend); your notes follow
in alphabetical order.

- Page through with **Vorige** (Previous) and **Volgende** (Next), or with
  `←` and `→`.
- When the drop-down list has focus, `↑` and `↓` work too.
- `Esc` closes the window.
- The last item shown and the window's position and size are remembered.
  The scroll position of a Markdown document is kept while OpenCPN is
  running.

## The chart legend

The chart legend shows what is in view now, one row per symbol with its
count (`×7`):

- the symbols of **waypoints** from GPX files and GPX layers, with their own
  icon;
- the **chart symbols** from o-charts and ENC charts (buoys, beacons,
  topmarks, mooring posts and so on), with the picture OpenCPN draws on the
  chart;
- the **light characteristics**, as an indented row under the buoy or
  beacon that carries the light.

The list updates by itself as soon as the chart comes to rest after
panning, zooming or rotating, and also when you switch a layer on or off.

### Pointing out

- **Click a row** and the nearest instance is marked on the chart with a
  ring. Another click moves to the next one (`2/7`). If it is not well in
  view, the chart scrolls to it. With *Afstand en peiling vanaf de boot*
  (Distance and bearing from the boat) on, that is the instance nearest to
  the boat (see below).
- **Click the counter** (`×7 ◎`) and all instances in view are pointed out
  at once.
- **Click a symbol on the chart** while the legend is open, or choose
  **Toon in legenda** (Show in legend) in the chart's right-click menu. The
  plugin opens the right heading, scrolls to the row and highlights it.
- Everything you can click has a tooltip that says what a click does.

### Header and settings

At the top is **Kaartlegenda** with two buttons on the right:

- the **gear** opens the *Instellingen* (Settings) menu;
- the **arrow** (*Ververs*, Refresh) reads all waypoints, chart cells and
  VHF areas again. Use it when you have changed a waypoint while the chart
  is at rest.

In the Settings menu:

| Menu item | What it does |
|---|---|
| Lichten (Lights) | Show or hide light rows |
| Ook op deze schaal verborgen tonen (Also show hidden at this scale) | Also symbols the chart does not draw at this scale (see below) |
| Afstand en peiling vanaf de boot (Distance and bearing from the boat) | Above each row's meaning, the distance and bearing to the nearest instance (see below); off by default |
| Engelse namen (English names) | Meanings in English instead of Dutch |
| Alle lichten laten branden (Make all lights shine) | All lights in view shine on the chart |
| Nachtvoorbeeld (Night preview) | The chart as you see it at night |
| Licht herkennen (Identify a light) | Identify a light by its rhythm |
| Marifoongebied blijft oplichten (VHF area stays highlighted) | A clicked VHF area stays red until the next click on the chart, instead of fading after six seconds |
| Melding bij nieuw marifoongebied (Notice on new VHF area) | A notice with beeps when the boat enters a new VHF area |
| Melding bij notitie in de buurt (Notice when near a note) | A notice with beeps when the boat comes within the radius of a pinned note |
| Uitkijkbundel op de kaart (Lookout beam on the chart) | The lookout beam drawn faintly on the chart; on by default |
| Uitkijkbundel: breedte ▸ (Lookout beam: width) | Narrow (15°), Medium (30°) or Wide (45°) on either side of the course |
| Uitkijkbundel: lengte ▸ (Lookout beam: length) | 1, 2, 5, 10 or 20 nautical miles, or *Anders…* (Other…) to enter a length yourself (0.5 to 50 nautical miles); 2 by default. While sailing, each choice shows how long it takes you |
| Uitkijkbundel: kleur ▸ (Lookout beam: colour) | Amber (default), Orange or Grey |
| Alles inklappen / uitklappen (Fold / unfold all) | All headings at once |
| Volgorde wijzigen… (Change order…) | Put the headings in a different order |

How many symbols, waypoints, chart objects and light characteristics are in
view is shown at the bottom of the list.

### Searching

Above the list is a search field (`Cmd+F`). Only rows containing all the
typed words remain, including those under folded headings. Case and accents
don't matter: `waterskien` finds *Waterskiën*. A buoy and its light are
found together. `Esc` first clears the search field and only then closes
the window.

### Headings

The list is divided into headings, which you create yourself in
`symbolen.txt` (see *Your own meanings*).

- Click a heading to fold or unfold it. A folded heading still shows the
  count and a strip with the first icons. The plugin remembers which
  headings are folded.
- **Volgorde wijzigen** (Change order) shows only the headings, with large
  ▲ and ▼ buttons that also work well on a touch screen. The new order is
  saved to `symbolen.txt` right away.
- **Kaartsymbolen** (Chart symbols) and **Lichtkarakters** (Light
  characteristics) are the plugin's own headings: they hold chart symbols
  without a meaning of their own and lights that stand on nothing in the
  list. They can be moved just like your own headings.
- Symbols under no heading at all are at the bottom under **Overig**
  (Other).

### Hidden at this scale

Some symbols are drawn by the chart only when you zoom in far enough. With
**Ook op deze schaal verborgen tonen** (Also show hidden at this scale) they
get a grey row. Above the meaning, in small print, are a closed eye and the
scale at which they appear (`1:200k`). A click zooms in until the nearest
instance is visible. What you have switched off yourself under *User
Standard* in OpenCPN stays out.

### Distance and bearing from the boat

With **Afstand en peiling vanaf de boot** (Distance and bearing from the
boat), each row shows above its meaning, small and grey, how far the
nearest instance is from the boat and in which direction: *0.3 NM · 045°*
(true bearing), or in metres below a tenth of a nautical mile, such as
*150 m*. In the Lookout it is shown for every object.

- A click on a row then goes to the instance nearest to the boat; another
  click to the next one, further and further away. Click another row first
  and it starts again at the nearest one.
- It only works with a GPS position from the last minute; otherwise the
  numbers stay away and the centre of the chart is used, as always.
- Under way the numbers are updated at most once every five seconds.
- You can try it at home with the ShipDriver plugin.

## Lights

The plugin reads the light characteristic from the chart and from
waypoints. In a waypoint it follows `Lichtkarakter:` in the description, or
stands on a line of its own in the name, as in the Nautin layer.

- **In words.** Each light gets a lamp in its colour, the abbreviation as on
  the chart and its meaning in the terms of INT 1, for example
  *Fl(2+1)G.10s — composite group-flashing green light: 2+1 flashes every
  10 seconds*. Below it, the rhythm is drawn.
- **Make it shine.** Clicking a light row makes that light shine on the
  chart in its rhythm, with a glow and rays. Period and groups come from the
  chart; how long a single flash lasts is a common value.
- **Alle lichten laten branden** (Make all lights shine) switches on all
  lights in view, each in its own rhythm, until you switch it off again.
  They move along with the chart.
- **Sector lights** become one light with their sectors in words, such as
  *red 090°–115°, white 115°–120°* (bearings towards the light). When the
  light shines on the chart, the sectors are drawn around it as coloured
  wedges.

### Night preview

**Nachtvoorbeeld** (Night preview) lays a dark blue layer over the chart,
through which the coast and fairways just show. On top of it all lights in
view shine in their rhythm. This way you can see in advance what a night
arrival looks like. Your own ship and AIS targets stay visible above it, and
the legend window switches to the night colours too.

### Identify a light

If you see a light and don't know which one it is, choose **Licht
herkennen** (Identify a light). A frame with a large pad appears at the top
of the list.

1. Hold down the pad or the space bar while the light is on, and let go
   when it is dark. Do this for two or three periods. The pad lights up
   while you press, and a line counts the taps and the seconds of light or
   dark.
2. Optionally click the colour of the light.
3. The plugin shows your rhythm as a drawing and in words, plus the five
   lights in view that fit best, with how well they fit. A click makes such
   a light shine on the chart.

Missing a period, for example because of the swell, is no problem. Only
after a minute without taps does the plugin start over; **Tikken wissen**
(Clear taps) does so right away. **Sluiten** (Close) removes the frame.

## Under way

### VHF

The **Marifoon** (VHF) heading is at the top of the list and shows the
channel for the area the boat is in: channel, call name, type of station,
what you have to do (listen, report or check in) and the phone number. The
data comes from the *VHFinfo Nederland* layer of
[VHF2OpenCPN](https://github.com/kwillems/VHF2OpenCPN). Without a recent GPS
position the centre of the chart is used; it then says so. When the layer
is off, the heading disappears.

A VHF area can be highlighted on the chart, in light red as on
[vhfinfo.org](https://vhfinfo.org/map.html):

- **Click a VHF icon** of the layer on the chart, its row in the legend or a
  line of the VHF heading: the whole area lights up and a small window with
  the channel appears at the top of the chart. If an area consists of
  several parts, they all light up. A row's counter ◎ highlights all areas
  of that icon in view.
- **Welk marifoonkanaal hier?** (Which VHF channel here?) in the chart's
  right-click menu highlights all areas at that spot and names their
  channels in a small window. This works even when the layer is switched off
  in OpenCPN: the plugin reads the areas from the GPX file itself.
- An area lights up for six seconds and then fades, or stays until you click
  the chart again (setting *Marifoongebied blijft oplichten*, VHF area stays
  highlighted).

**Notice on a new area.** When you sail with GPS into a VHF area you were
not in a moment ago, it lights up and a small window with the channel
appears at the top of the chart, for example *kanaal 18 · Ketelbrug ·
verkeerspost · melden* (channel 18 · Ketelbrug · traffic post · report), with
six beeps. The window disappears after twelve seconds, with **Sluiten**
(Close) or with a click outside it. This also works when the legend window
is closed, and can be switched off with *Melding bij nieuw marifoongebied*
(Notice on new VHF area).

### Lookout

The **Uitkijk** (Lookout) heading shows what you will meet on your course.
It appears as soon as the boat is moving: a recent GPS position and at
least half a knot. Next to the heading it says where the lookout is
looking, for example *tot 2 NM, ± 20 min · breedte 30°* (up to 2 NM, ±
20 min · width 30°), and *niets in de bundel* (nothing in the beam) when
nothing is there. The time is an estimate at your current speed. When you
fold the Lookout, the icons of what you will meet first are shown to the
right of the heading, in order of arrival (VHF areas and notes too), with
the count of the rest after them, such as *+4*.

Each line starts small with when and where, with the description below:

- All objects from the legend inside the beam, in order of arrival, for
  example *over 4 min · stuurboord · 0,6 NM · 038°* (in 4 min · starboard ·
  0.6 NM · 038°) with *Rode spitse ton* (red conical buoy) and the light
  characteristic below. Distance and bearing are shown when *Afstand en
  peiling vanaf de boot* is on.
- With *Ook op deze schaal verborgen tonen*, buoys the chart does not draw
  yet at this scale are listed too, in grey, with the closed eye and the
  scale, just as in the rest of the legend. A click zooms there.
- Also what you are passing right now: anything within 0.12 nautical miles
  (about 220 metres) beside you or just behind you is at the top as *nu*
  (now), until it falls out of that circle behind you.
- Up to two VHF areas your course enters, at their place in time, for
  example *over 12 min* with *Marifoongebied kanaal 22 · Houtribsluizen ·
  melden* (VHF area channel 22 · Houtribsluizen · report) below. A click
  highlights the area. The signs of the *VHFinfo Nederland* layer itself
  don't count as objects; real VHF signs along the water (B11, E23) do.
- The pinned notes on your course, for example *over 6 min · stuurboord*
  with *Notitie Ondiepte bij de Ketelbrug* (Note Shallows near the
  Ketelbrug) below. A click opens the note; if it is hidden at this scale,
  the click zooms there.
- A click on a line points out the object on the chart, or makes its light
  shine.

**The lookout beam.** The lookout looks from a circle of 0.12 nautical miles
around the boat, in a fan ahead: close by at least 0.12 nautical miles wide
on each side, wider further on. The end is round: every point of the edge
is the same distance from the boat. You choose how far it reaches with
**Uitkijkbundel: lengte** (Lookout beam: length): 1, 2 (default), 5, 10 or
20 nautical miles, or with *Anders…* (Other…) a length of your own from 0.5
to 50 nautical miles. The length does not depend on your speed; in the menu
each choice shows how long it takes you at your current speed. On busy
waters a short length keeps the list manageable. You choose how wide the
fan opens with **Uitkijkbundel: breedte** (Lookout beam: width): narrow
(15°), medium (30°, default) or wide (45°) on either side of the course.

With **Uitkijkbundel op de kaart** (Lookout beam on the chart, on by
default) the beam is drawn very faintly on the chart, while the legend
window is open and the boat is moving, even while you look at a note in it.
That way you can see why a buoy is or isn't in the list. Switching it off
hides only the drawing; the list stays. You choose the colour with
**Uitkijkbundel: kleur** (Lookout beam: colour): amber (default), orange or
grey. Colours that mean something on the chart (yellow, red, green, purple)
are deliberately left out. At dusk and night the beam is dimmed but stays
visible; grey then becomes light grey, because the dark grey of daytime
would disappear on the dark chart.

The beam shows how far it reaches:

- At the starboard corner of the end, the length is shown with the sailing
  time, such as *3 NM, ± 20 min*. That keeps the middle, straight ahead,
  clear.
- Thin dotted lines across the beam mark the nautical miles: at every
  nautical mile up to 5 NM, at every 5 nautical miles up to 25 NM and every
  10 beyond that. So you can see at once whether a buoy lies before or after
  a mile line. No line is drawn just before the end; a 1 NM beam only has
  the label.
- When the lines come too close together on screen, they disappear; when
  the whole beam is small on screen, the label disappears too.

You can test without sailing with the ShipDriver plugin.

## Your own meanings: `symbolen.txt`

On first use the plugin creates `symbolen.txt` in the Legend folder, with an
explanation in comments. In it you give symbols a meaning and group them
under headings:

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

- One `icon name = meaning` per line. The icon name is the file name of the
  icon without `.png` or `.svg`, or for a chart symbol the S-52 name (shown
  in the row's tooltip).
- A `*` in the name stands for any text; a `*` in the meaning is replaced by
  that text. Exact names take precedence.
- `[Heading]` starts a group. Headings and lines appear in the order of the
  file.
- Two languages: `betekenis | meaning` and `[Kopje | Heading]`. If one side
  is empty, such as `| Yellow buoy`, the other side is used in both
  languages.
- An empty meaning (`vhf_* =`) only puts the symbol under a heading.
- Lines starting with `#` are skipped.

If a symbol has no line, the plugin looks further: the first line of the
description if all waypoints with that icon share it, then the description
OpenCPN knows for the icon, and otherwise the icon name in grey. That way
you can see which symbols still need a meaning.

Chart symbols get a name from their object class and attributes, such as
*Lateral buoy: port, red, can*. The Dutch terms come from Kaart 1 and the
BPR, the Dutch inland navigation police regulations (`data/s57-nl.txt`).
With your own `s57-nl.txt` in the Legend folder you can add to them or
correct them.

## Your own legends and notes

All Markdown files in the Legend folder appear in the drop-down list:

```text
~/Documents/OpenCPN/Legend/
├── VHF-Nederland.md
├── Route-IJsselmeer.md
├── Checklist.md
├── symbolen.txt
├── s57-nl.txt        (optional)
└── images/
    ├── brug.png
    ├── sluis.png
    └── VHF-Nederland.png
```

- **Markdown** (`.md`, `.markdown`) is shown formatted, with tables, local
  images and clickable links. If the first line is a heading `# Title`, that
  becomes the title in the drop-down list; otherwise the file name.
- An image wider than the window is scaled down to fit, also when you make
  the window narrower; smaller ones keep their own size.
- **A loose PNG** in the Legend folder, such as a scanned legend, becomes a
  note by itself: the image moves to `images/` and a note with the same name
  shows it. You can then edit, rename, delete and pin it like any other
  note. A PNG that is already used in a note stays where it is.

An example:

```markdown
# Route IJsselmeer

## Departure

- Check the weather and the route.
- Set the VHF to the right channel.

| Object | VHF | Remark |
|---|---:|---|
| Ketelbrug | 18 | Listen on channel 18 |
| Houtribsluizen | 22 | Report before passage |

![Bridge](images/brug.png)
```

Markdown is an open text format: you can also write notes with any other
editor and use them outside OpenCPN.

### Writing notes

Next to the drop-down list are four buttons. The pencil, the name and the
bin appear only with a note:

| Button | What it does |
|---|---|
| Pencil | Edit the note |
| Text box with cursor | Give it another title: the file and the `# Title` at the top change along |
| Bin | Delete, after confirmation. The file goes to the `Verwijderd` (Deleted) folder in the Legend folder; move it back to use it again |
| Plus | A new note: you give a title and start writing right away |

While editing you see the note as it looks, without Markdown characters. At
the top is a row of buttons:

- **H** chooses the heading level: heading 1 to 6, or plain text;
- **B** and *I* for bold and italic (also `Cmd+B` and `Cmd+I`);
- a bulleted list, a numbered list or a checklist; click a box to tick it;
- a link from the selected text, and an image. An image from outside the
  Legend folder is first copied to `images/`;
- the **pin** for a place on the chart (see *Notes on the chart*).

Enter after a heading gives plain text again, Enter on an empty list line
ends the list and `Shift+Enter` starts a new line within the same paragraph.
Everything is saved by itself, a second and a half after you stop typing;
the bottom shows when. **Opslaan** (Save), at the top next to the drop-down
list, or `Esc` saves and takes you back to the normal view.

On disk a note stays plain Markdown. While editing, a table is shown as
Markdown text in a fixed-width font; that way you can edit it and it is
saved exactly as is.

### Notes on the chart

A note can be connected to the chart in two ways.

**A link to a place in the text.** Click it in the note and the chart goes
there and points out the place. You create such a link with the pin button
(*Link naar het midden van de kaart in de tekst*, Link to the centre of the
chart in the text), or by right-clicking the chart: *Notitie ▸ Link naar
deze plek in de tekst* (Note ▸ Link to this place in the text). If text is
selected, it becomes the link; otherwise the coordinates go into the text.
In the file such a link reads `[Ketelbrug](geo:52.58030,5.75120)`, a
standard other chart programs understand too.

**The whole note pinned to a place.** There are three ways to do this:

- by right-clicking the chart: *Notitie ▸ Nieuwe notitie hier* (Note ▸ New
  note here). You give a title, and the new note opens right away, pinned to
  that spot, even if the legend window was closed;
- while editing, with a right-click: *Notitie ▸ Hier vastpinnen op de kaart*
  (Note ▸ Pin here on the chart);
- with the pin button: *Notitie vastpinnen op het midden van de kaart* (Pin
  note to the centre of the chart).

**Where the boat is now.** With a GPS position from the last minute, you can
also use the boat's position, wherever you click:

- the plus button in the window then asks *Nieuwe notitie* (New note) or
  *Nieuwe notitie bij de boot* (New note at the boat); with a right-click:
  *Notitie ▸ Nieuwe notitie bij de boot*;
- while editing, via the pin button or *Notitie ▸* in the right-click menu:
  *Vastpinnen op de positie van de boot* (Pin to the boat's position), or
  *Positie van de boot in de tekst* (Boat's position in the text). The
  latter puts the position with the time as a link in the note, for example
  *52°34,818′N 5°45,072′E (14:32)*: handy for a logbook.

Above the text it then says *Vastgepind op 52°34,818′N 5°45,072′E · straal
0,5 NM* (Pinned at … · radius 0.5 NM). A click on it shows the place; with
**Straal** (Radius) you adjust the distance for the notice, with
**Losmaken** (Unpin) you unpin the note again. In the normal view the same
appears in a coloured bar above the note.

A pinned note:

- shows as a **yellow sticky note** on the chart. Hover over it to see the
  title; a click opens the note and shows the radius as a dotted line.
  Right-clicking the sticky note gives *Notitie ▸ "…" openen* (open) and
  *"…" verwijderen* (delete). **Drag** the sticky note to move the note: the
  radius moves along, and when you let go the new place is written into the
  note. A notice then offers **Terugzetten** (Put back) and **Sluiten**
  (Close); once it is gone, *Notitie ▸ "…" terugzetten op de oude plek* (put
  back in the old place) is still in the right-click menu, for the most
  recently moved note. Like buoys and beacons, the sticky note disappears
  when you zoom out beyond 1:200,000;
- is listed under the **Notities** (Notes) heading in the legend while the
  place is in view, nearest first: the distance in small print, below it
  the title and the first line of text. With *Ook op deze schaal verborgen
  tonen* a note beyond its scale limit stays there in grey, with a closed
  eye and the scale; a click zooms there;
- appears in the **Lookout** when your course passes it;
- gives a **notice** with beeps under way as soon as you come within the
  radius. A click on the notice opens the note. A note you have just pinned
  where the boat is, or that you are editing, doesn't announce itself; only
  once you sail away and come back. Can be switched off with *Melding bij
  notitie in de buurt* (Notice when near a note).

The place is stored at the top of the file, in the block Obsidian uses too:

```markdown
---
location: [52.58030, 5.75120]
straal: 0.5
---
# Shallows near the Ketelbrug
```

The Obsidian plugin *Map View* writes `location` the same way, so the note
shows up on its map as well. `straal` (radius) is in nautical miles. With a
line `schaal: 1:50000` (scale) you give a note a different scale limit than
1:200,000. Other lines you put in the block, such as `tags`, are kept.

## Display

- The legend follows OpenCPN's day, dusk and night colour schemes: window,
  text, icons and the marker dim along. In daytime the window follows the
  light or dark appearance of macOS.
- Icons follow the scale factor for chart objects in OpenCPN's settings, so
  they are the same size as on the chart.
- On a Retina display, icons, lamps and rhythms stay sharp.
- Waypoint icons may be PNG or SVG. If `UserIcons` contains an SVG with the
  same name as a PNG, the legend uses the SVG just like OpenCPN does, at its
  own size and, on a Retina display, redrawn at double size instead of
  enlarged.
- In a narrow window the rhythm drawings get narrower.

## Where the data comes from

- **Layer waypoints** are read by the plugin itself from the GPX files in
  OpenCPN's `layers` folder; other waypoints it asks OpenCPN for. The first
  read happens shortly after start-up, with a progress bar.
- **o-charts charts** (`.oesu`) are read through the o-charts plugin itself,
  the way OpenCPN does for *Chart Object Query*. The o-charts plugin handles
  decryption and the licence check; Legend4OpenCPN stores no chart data and
  exports nothing.
- **ENC charts** (S-57, `.000` with updates), such as the free inland charts
  of Rijkswaterstaat, are read by the plugin with a reader of its own.
- Where charts cover the same water, an object counts once.
- The plugin shows the same objects as the chart: it asks OpenCPN which
  object classes are being drawn at that moment, with the display category,
  the classes switched on and off, and the scale limits.
- **VHF areas** are read from the GPX layers in OpenCPN's `layers` folder:
  the closed tracks with a `VHF-kanaal:` line in their description. The
  plugin sees a new version of such a layer after OpenCPN is restarted.

Settings and caches are kept in OpenCPN's private folder:

```text
~/Library/Preferences/opencpn/Legend/
```

That holds `legend.ini` (last item, window, folded headings and the choices
from Settings) and `ocharts-cells.txt` and `s57-cells.txt` with the coverage
of the chart cells, so that later start-ups are fast.

## Limitations

- Only point objects from o-charts and S-57 charts; lines and areas
  (coastline, depth areas) and other chart formats (CM93, raster charts,
  encrypted S-63) are not included.
- The same buoy can appear twice in the list: as a waypoint from a layer and
  as a chart symbol.
- Sectors come only from charts, not from waypoint layers.
- Lights the chart does not draw at this scale don't shine.
- The lookout reads its own area from the chart cells, including what lies
  out of view. While cells are still being opened, or when part of the beam
  has no chart, it says so next to the heading. What the chart does not draw
  at this scale is only included with *Ook op deze schaal verborgen tonen*.
- The lines and the label on the lookout beam are drawn on top of the chart,
  not beneath it; that is why they are faint.
- The marker on the chart has been tested with **OpenGL on**.
- Icons are read from OpenCPN's SVG and PNG files. If the file is missing,
  the icon may stay blank on macOS.
- The Legend folder is not configurable yet.
- The note editor has no spell checker. A heading inside a quote becomes a
  normal heading when saved.

## Development

### Source code

The repository uses Git submodules (`opencpn-libs` and `third_party/md4c`):

```bash
git clone --recurse-submodules https://github.com/kwillems/Legend4OpenCPN.git
```

Already cloned without submodules:

```bash
git submodule update --init --recursive
```

### Building on macOS

The development environment is macOS on Apple Silicon with OpenCPN 5.14.2,
wxWidgets 3.2.9, CMake and Apple Clang. The plugin must be built against the
same wxWidgets as OpenCPN, so specify `wx-config` explicitly:

```bash
mkdir build
cd build
cmake -DwxWidgets_CONFIG_EXECUTABLE=/opt/wxwidgets-3.2.9/bin/wx-config ..
cmake --build . --target tarball -j8
```

After that `cmake ..` is enough; the choice is kept in `CMakeCache.txt`.
The message that `wxWidgets_CONFIG_EXECUTABLE` was not used is wrong and
can be ignored. Warnings about CMake policies are harmless too.

The package (`.tar.gz`) ends up in `build/`. Its name comes from the git tag
on the current commit, for example `Legend-v0.2.0-alpha27_…tar.gz`; without
a tag it follows from `Plugin.cmake` with the date and commit id. Run
`cmake ..` again after a new tag.

The plugin's messages are in OpenCPN's log and start with `Legend:`:

```bash
grep "Legend:" ~/Library/Logs/opencpn.log | tail -20
```

### Tests

The parts that work without OpenCPN have unit tests in `test/`: light
characteristics, VHF areas, notes with a place, `symbolen.txt`, reading
layer waypoints, and distances, bearings and the lookout beam. Build and run
in the `build/` folder:

```bash
make legend-tests
ctest --output-on-failure
```

`./legend-tests` shows each failed check with the expected and the actual
result. In CircleCI the tests run after the build.

### Project structure

```text
Legend4OpenCPN/
├── data/
│   ├── legend_toolbar.png
│   └── s57-nl.txt           # Dutch names of chart objects
├── src/
│   ├── legend_pi.cpp/.h        # the plugin: start-up, settings, legend files
│   ├── legend_window.cpp       # the window, clicks, keys, settings menu
│   ├── legend_collect.cpp      # collecting waypoints, meanings and chart objects
│   ├── legend_render.cpp       # the chart legend page, block by block
│   ├── legend_chart.cpp        # clicks, menu and drawing on the chart
│   ├── legend_notes.cpp        # writing notes and notes on the chart
│   ├── legend_navigation.cpp   # under way: position, lookout, identify a light, VHF
│   ├── legend_html_window.*    # the legend's HTML window
│   ├── legend_icons.*          # reading and drawing icons
│   ├── legend_shared.*         # colours and helpers all parts use
│   ├── symbol_meanings.*       # reading and looking up symbolen.txt
│   ├── geo.*                   # distance, bearing and the lookout beam
│   ├── chart_symbols.*         # OpenCPN's S-52 symbols and S-57 catalogue
│   ├── ocharts_source.*        # chart objects through the o-charts plugin
│   ├── s57_source.*            # chart objects from ENC cells (S-57)
│   ├── display_settings.*      # what the chart is drawing now
│   ├── chart_names.*           # names of chart objects (NL/EN)
│   ├── layer_waypoints.*       # waypoints from the GPX layers
│   ├── light_character.*       # light characteristics: reading, words, rhythm, identifying
│   ├── shining_lights.*        # making lights shine on the chart
│   ├── note_editor.*           # editing notes: Markdown in and out
│   ├── note_places.*           # notes with a place: the block at the top
│   ├── vhf_areas.*             # VHF areas from the layers
│   ├── vhf_highlight.*         # highlighting a VHF area
│   └── chart_notice.*          # the small window at the top of the chart
├── test/                    # unit tests (make legend-tests, ctest)
├── third_party/md4c/        # Markdown processing (submodule)
├── opencpn-libs/            # OpenCPN plugin API (submodule)
├── Plugin.cmake
└── CMakeLists.txt
```

## Plans

Ideas for future versions:

- marking bridges the boat does not fit under;
- showing the range of a light in the night preview;
- editing meanings in the window itself;
- a configurable Legend folder;
- builds for Windows and Linux, and an English translation of the interface.

## License

Legend4OpenCPN is free software under the
[GNU General Public License, version 2](LICENSE) or, at your option, any
later version (SPDX: `GPL-2.0-or-later`). As that license says, the plugin
comes without any warranty; you use it entirely at your own risk.

External components keep their own license; MD4C, for example, is under the
MIT license. The build files from the OpenCPN plugin template (such as
`CMakeLists.txt` and the `cmake/` folder) keep their own license headers.
