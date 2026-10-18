// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _LIGHT_CHARACTER_H_
#define _LIGHT_CHARACTER_H_

#include <utility>
#include <vector>

#include <wx/colour.h>
#include <wx/image.h>
#include <wx/string.h>

// One step of a light's rhythm: lit in a colour, or dark.
struct LightPhase
{
    double seconds = 0.0;
    int colour = -1;     // index in LightCharacter::Colours(), -1 = dark
    bool dim = false;    // the steady part of a fixed and flashing light
};

// What someone tapped while watching a light, read as a light character:
// the kind, the groups and the period. Tapping means holding a key or a pad
// while the light shines and letting go while it is dark.
struct TapPattern
{
    bool valid = false;
    wxString kind;           // Fl, LFl, Q, VQ, Iso, Oc or Mo
    std::vector<int> groups; // {2, 1} for 2+1; empty for a continuous quick light
    bool longAfter = false;  // each group ends in a long flash: Q(6)+LFl
    wxString letter;         // Morse code letter
    double period = 0.0;     // seconds, 0 = not known yet

    // "groepen van 2+1 schitteringen, elke ~10 s".
    wxString Words(bool dutch) const;
};

// The moments a light shone (start, end in seconds), as tapped.
TapPattern AnalyseTaps(const std::vector<std::pair<double, double>> &shines);

// The tapped moments drawn like a rhythm: bars where the light shone.
wxImage TapRhythmImage(
    const std::vector<std::pair<double, double>> &shines,
    int width,
    int height,
    const wxColour &line,
    const wxColour &light,
    const wxColour &background,
    double scale = 1.0);

// The character of a light as charts write it, such as "Fl(2+1)G.10s" or
// "Iso.G.4s": read from text or from the attributes of an S-57 light, and
// told in words in Dutch (Kaart 1, P 10) or English (INT 1, P 10).
class LightCharacter
{
public:
    // Reads a characteristic; false when the whole text is not one. Spaces
    // and letter case of the kinds do not matter: "Fl. (5) Y 20s" and
    // "LFL.R.6s" are read.
    bool Parse(const wxString &text);

    // From the S-57 attributes LITCHR, SIGGRP, SIGPER and COLOUR.
    bool FromS57(
        const wxString &litchr,
        const wxString &siggrp,
        const wxString &sigper,
        const wxString &colour);

    // A light character in the texts of a waypoint: after "Lichtkarakter:"
    // (or "Light:") in the description, or a line of the name that holds
    // nothing else, such as the second line of "13\nIso.G.8s".
    bool FindInWaypoint(const wxString &name, const wxString &description);

    bool IsValid() const { return !m_parts.empty() || m_alternating; }

    // As charts write it: "Fl(2+1)G.10s", "Q(6)+LFl.15s", "Al.WR".
    wxString Abbreviation() const;

    // In words, starting in lower case, for example "groen groepschitter-
    // licht met samengestelde groep: 2+1 schitteringen, elke 10 seconden".
    wxString Words(bool dutch) const;

    // Colour codes of the light ("W", "R", "G", "Y", "Bu", ...); white when
    // the characteristic names no colour.
    std::vector<wxString> Colours() const;

    // The colours as written ("W", "R", ...), for the parts of a sector
    // light that are joined into one ("Iso.WRG.4s").
    void SetColours(const std::vector<wxString> &colours) { m_colours = colours; }

    // The abbreviation without colours: the rhythm the parts of a sector
    // light share.
    wxString RhythmAbbreviation() const;

    // How well a tapped rhythm fits this light, from 0 to 1: the kind, the
    // groups and the period count.
    double Match(const TapPattern &pattern) const;

    // The rhythm, drawn to scale but schematic: the lengths of flashes and
    // eclipses are typical values, not taken from a chart. One period, or
    // as many as an alternating light needs to show each of its colours.
    std::vector<LightPhase> Phases() const;

private:
    struct Part
    {
        wxString kind;  // F, Fl, LFl, Q, VQ, UQ, IQ, IVQ, IUQ, Iso, Oc, Mo, FFl
        wxString group; // "3", "2+1", a Morse letter, or empty
    };

    void Clear();
    double PeriodForDrawing(double sequenceLength) const;
    std::vector<LightPhase> OnePeriod() const;

    bool m_alternating = false;
    std::vector<Part> m_parts;      // two for "Q(6)+LFl"
    std::vector<wxString> m_colours;
    double m_period = 0.0;          // seconds, 0 = not given
};

// One sector of a sector light: its colour and its limits as charts give
// them, bearings in degrees from seaward towards the light, clockwise from
// the first to the second.
struct LightSector
{
    wxString colour;
    double from = 0.0;
    double to = 0.0;
};

// The sectors in words: "sectoren (peiling naar het licht): wit 090°–120°,
// rood 120°–150°".
wxString SectorWords(const std::vector<LightSector> &sectors, bool dutch);

// Colour of a light on screen, by its code ("W", "R", "G", ...); dim darkens
// it for dusk and night.
wxColour LightDisplayColour(const wxString &code, double dim);

// A round lamp in the colours of a light, as the icon of its row; a light
// with several colours is divided like a pie. dim darkens the light colours
// for dusk and night.
wxImage LightLampImage(
    const std::vector<wxString> &colours,
    const wxColour &outline,
    const wxColour &background,
    double dim,
    int size = 18);

// The rhythm of a light: a thin line for the dark, bars in the colour of
// the light where it shines and a low bar for the steady part of a fixed
// and flashing light. Ticks mark the start and the end.
wxImage LightRhythmImage(
    const LightCharacter &light,
    int width,
    int height,
    const wxColour &line,
    const wxColour &background,
    double dim,
    double scale = 1.0); // line widths and margins, 2 for a Retina version

#endif
