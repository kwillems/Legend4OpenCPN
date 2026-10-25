// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of the light characters: reading them from text, from S-57
// and from waypoints, telling them in words, their rhythm, and recognising
// a tapped rhythm.

#include "checks.h"
#include "light_character.h"

namespace {

// The abbreviation of a parsed text, or "(invalid)".
wxString Read(const wxString &text)
{
    LightCharacter light;
    if (!light.Parse(text))
        return "(invalid)";
    return light.Abbreviation();
}

wxString InWords(const wxString &text, bool dutch)
{
    LightCharacter light;
    if (!light.Parse(text))
        return "(invalid)";
    return light.Words(dutch);
}

std::vector<LightPhase> PhasesOf(const wxString &text)
{
    LightCharacter light;
    light.Parse(text);
    return light.Phases();
}

double Length(const std::vector<LightPhase> &phases)
{
    double total = 0.0;
    for (const LightPhase &phase : phases)
        total += phase.seconds;
    return total;
}

int LitCount(const std::vector<LightPhase> &phases)
{
    int lit = 0;
    for (const LightPhase &phase : phases) {
        if (phase.colour >= 0)
            ++lit;
    }
    return lit;
}

// Taps of a light: one (start, end) per flash.
using Taps = std::vector<std::pair<double, double>>;

// ---------------------------------------------------------------------------

void TestParse()
{
    // As charts write them.
    CHECK_EQ(Read("Fl(2+1)G.10s"), "Fl(2+1)G.10s");
    CHECK_EQ(Read("Iso.G.4s"), "Iso.G.4s");
    CHECK_EQ(Read("Q(6)+LFl.15s"), "Q(6)+LFl.15s");
    CHECK_EQ(Read("Oc(2)R.8s"), "Oc(2)R.8s");
    CHECK_EQ(Read("Mo(U)W.15s"), "Mo(U)W.15s");
    CHECK_EQ(Read("VQ(3)W.5s"), "VQ(3)W.5s");
    CHECK_EQ(Read("Al.WR"), "Al.WR");
    CHECK_EQ(Read("F.R"), "F.R");
    CHECK_EQ(Read("Q"), "Q");

    // "FFl" is one kind, not "F" followed by "Fl".
    CHECK_EQ(Read("FFl.R.6s"), "FFl.R.6s");

    // Spaces, letter case and a trailing dot do not matter.
    CHECK_EQ(Read("Fl. (5) Y 20s"), "Fl(5)Y.20s");
    CHECK_EQ(Read("LFL.R.6s"), "LFl.R.6s");
    CHECK_EQ(Read("iso.g.4s"), "Iso.G.4s");
    CHECK_EQ(Read("Iso.G.4s."), "Iso.G.4s");

    // A group of one is no group.
    CHECK_EQ(Read("Fl(1)G.4s"), "Fl.G.4s");

    // A decimal comma, and seconds written out.
    CHECK_EQ(Read("Fl.Y.2,5s"), "Fl.Y.2.5s");
    CHECK_EQ(Read("Fl.Y.2.5s"), "Fl.Y.2.5s");
    CHECK_EQ(Read("Fl.G.5sec"), "Fl.G.5s");

    // The range in sea miles is not part of the character.
    CHECK_EQ(Read("Fl.G.5s4M"), "Fl.G.5s");
    CHECK_EQ(Read("Iso.W.2s 8sm"), "Iso.W.2s");

    // A colour after the period.
    CHECK_EQ(Read("Fl.4s Y"), "Fl.Y.4s");

    // Sector lights with several colours.
    CHECK_EQ(Read("Iso.WRG.4s"), "Iso.WRG.4s");
    CHECK_EQ(Read("Fl.Bu.3s"), "Fl.Bu.3s");

    // Not a light character.
    CHECK_EQ(Read(""), "(invalid)");
    CHECK_EQ(Read("Boei 13"), "(invalid)");
    CHECK_EQ(Read("W"), "(invalid)");        // a colour alone
    CHECK_EQ(Read("WR"), "(invalid)");       // colours alone, not alternating
    CHECK_EQ(Read("Fl(2"), "(invalid)");     // group not closed
    CHECK_EQ(Read("Fl(a)G.4s"), "(invalid)"); // letters only for Morse
    CHECK_EQ(Read("Mo"), "(invalid)");       // Morse without its letter
    CHECK_EQ(Read("Fl+X"), "(invalid)");     // nothing after the '+'
    CHECK_EQ(Read("Fl.G.700s"), "(invalid)"); // period too long
    CHECK_EQ(Read("Fl.G.4s5s"), "(invalid)"); // two periods
    CHECK_EQ(Read("Fl.G.4x"), "(invalid)");  // not seconds
    CHECK_EQ(Read("Fl.G.4s extra"), "(invalid)");
}

void TestColours()
{
    LightCharacter light;
    CHECK(light.Parse("Fl.5s"));
    CHECK(light.Colours() == std::vector<wxString>({"W"})); // white unless said

    CHECK(light.Parse("Iso.WRG.4s"));
    CHECK(light.Colours() == std::vector<wxString>({"W", "R", "G"}));
    CHECK_EQ(light.RhythmAbbreviation(), "Iso.4s");

    CHECK(light.Parse("Fl(2+1)G.10s"));
    CHECK_EQ(light.RhythmAbbreviation(), "Fl(2+1)10s");

    // The parts of a sector light are joined with their colours.
    CHECK(light.Parse("Iso.W.4s"));
    light.SetColours({"W", "R"});
    CHECK_EQ(light.Abbreviation(), "Iso.WR.4s");
}

void TestFromS57()
{
    LightCharacter light;

    // LITCHR, SIGGRP, SIGPER, COLOUR.
    CHECK(light.FromS57("2", "(2+1)", "10", "4"));
    CHECK_EQ(light.Abbreviation(), "Fl(2+1)G.10s");

    CHECK(light.FromS57("7", "", "4", "1,3,4"));
    CHECK_EQ(light.Abbreviation(), "Iso.WRG.4s");

    CHECK(light.FromS57("25", "(6)()", "15", "1"));
    CHECK_EQ(light.Abbreviation(), "Q(6)+LFl.W.15s");

    CHECK(light.FromS57("12", "(U)", "15", "1"));
    CHECK_EQ(light.Abbreviation(), "Mo(U)W.15s");

    CHECK(light.FromS57("28", "", "", "3,4")); // alternating, no rhythm
    CHECK_EQ(light.Abbreviation(), "Al.RG");

    CHECK(light.FromS57("2,3", "(1)", " 5 ", "3")); // first LITCHR value counts
    CHECK_EQ(light.Abbreviation(), "Fl.R.5s");

    CHECK(!light.FromS57("99", "", "5", "1"));    // unknown kind
    CHECK(!light.FromS57("", "", "5", "1"));
    CHECK(!light.FromS57("12", "()", "8", "1"));  // Morse without its letter
    CHECK(!light.FromS57("2", "(A)", "5", "3"));  // letters only for Morse
    CHECK(!light.IsValid());
}

void TestFindInWaypoint()
{
    LightCharacter light;

    CHECK(light.FindInWaypoint("Boei", "groen # Lichtkarakter: Iso.G.4s"));
    CHECK_EQ(light.Abbreviation(), "Iso.G.4s");

    CHECK(light.FindInWaypoint("Buoy", "Light: Fl.R.5s\nsecond line"));
    CHECK_EQ(light.Abbreviation(), "Fl.R.5s");

    // A line of the name after the first that holds nothing else.
    CHECK(light.FindInWaypoint("13\nIso.G.8s", ""));
    CHECK_EQ(light.Abbreviation(), "Iso.G.8s");

    // The first line of the name is the name itself.
    CHECK(!light.FindInWaypoint("Iso.G.8s", ""));
    CHECK(!light.FindInWaypoint("Boei 13", "groen"));
    CHECK(!light.IsValid());

    // A marker with something unreadable: the name is tried next.
    CHECK(light.FindInWaypoint("KM 12\nOc.R.6s", "Lichtkarakter: onbekend"));
    CHECK_EQ(light.Abbreviation(), "Oc.R.6s");
}

void TestWords()
{
    CHECK_EQ(InWords("Fl(2+1)G.10s", true),
             "groen groepschitterlicht met samengestelde groep: 2+1 schitteringen, "
             "elke 10 seconden");
    CHECK_EQ(InWords("Fl(2+1)G.10s", false),
             "green composite group-flashing light: 2+1 flashes, every 10 seconds");
    CHECK_EQ(InWords("Iso.G.4s", true),
             "groen isofase licht: 2 seconden licht en 2 seconden donker");
    CHECK_EQ(InWords("Iso.WRG.4s", true),
             "wit, rood en groen isofase licht (sectorlicht): 2 seconden licht en "
             "2 seconden donker");
    CHECK_EQ(InWords("Q", true), "wit flikkerlicht: ongeveer 60 per minuut");
    CHECK_EQ(InWords("VQ(3)W.5s", true),
             "wit groep snel flikkerlicht: 3 flikkeringen, elke 5 seconden");
    CHECK_EQ(InWords("Q(6)+LFl.15s", true),
             "wit groepflikkerlicht: 6 flikkeringen en een lange schittering, "
             "elke 15 seconden");
    CHECK_EQ(InWords("Oc(2)R.8s", true),
             "rood groeponderbroken licht: 2 verduisteringen, elke 8 seconden");
    CHECK_EQ(InWords("Mo(U)W.15s", true),
             U("wit Morse-Code licht: de letter U (· · –), elke 15 seconden"));
    CHECK_EQ(InWords("Al.WR", true), "wit en rood alternerend licht");
    CHECK_EQ(InWords("Fl.Y.2,5s", true), "geel schitterlicht: elke 2,5 seconden");
    CHECK_EQ(InWords("Fl.Y.2,5s", false), "yellow flashing light: every 2.5 seconds");
    CHECK_EQ(InWords("Fl.1s", true), "wit schitterlicht: elke seconde");
}

void TestPhases()
{
    // With a period, the rhythm takes exactly that long.
    const std::vector<std::pair<const char *, double>> periods = {
        {"Fl(2+1)G.10s", 10.0}, {"Iso.G.4s", 4.0},   {"Oc(2)R.8s", 8.0},
        {"Q(6)+LFl.15s", 15.0}, {"Mo(U)W.15s", 15.0}, {"LFl.W.10s", 10.0},
        {"VQ(3)W.5s", 5.0},     {"Fl.G.5s", 5.0},     {"FFl.R.6s", 6.0}};
    for (const auto &light : periods)
        CheckNear(Length(PhasesOf(light.first)), light.second, 1e-9, light.first, __FILE__,
                  __LINE__);

    // Isophase: as long light as dark.
    std::vector<LightPhase> phases = PhasesOf("Iso.G.4s");
    CHECK(phases.size() == 2);
    CHECK_NEAR(phases[0].seconds, 2.0, 1e-9);
    CHECK(phases[0].colour == 0);
    CHECK(phases[1].colour == -1);

    // Groups of flashes: as many as the groups hold.
    CHECK(LitCount(PhasesOf("Fl(2+1)G.10s")) == 3);
    CHECK(LitCount(PhasesOf("Oc(2)R.8s")) == 2); // light, eclipse, light, eclipse

    // Six quick flashes and one long one, the long one last.
    phases = PhasesOf("Q(6)+LFl.15s");
    CHECK(LitCount(phases) == 7);
    double lastLit = 0.0;
    for (const LightPhase &phase : phases) {
        if (phase.colour >= 0)
            lastLit = phase.seconds;
    }
    CHECK_NEAR(lastLit, 2.0, 1e-9);

    // Morse A: a dot, a dash.
    phases = PhasesOf("Mo(A)W.8s");
    CHECK(LitCount(phases) == 2);
    CHECK_NEAR(phases[0].seconds, 0.5, 1e-9);
    CHECK_NEAR(phases[2].seconds, 1.5, 1e-9);

    // Fixed and flashing: a steady part around the flash.
    phases = PhasesOf("FFl.R.6s");
    CHECK(phases.size() == 3);
    CHECK(phases[0].dim && !phases[1].dim && phases[2].dim);

    // An alternating light lit once per period takes its colours in turn.
    phases = PhasesOf("Al.Fl.WR.5s");
    std::vector<int> colours;
    for (const LightPhase &phase : phases) {
        if (phase.colour >= 0)
            colours.push_back(phase.colour);
    }
    CHECK(colours == std::vector<int>({0, 1}));

    // Nothing to show for nothing read.
    CHECK(LightCharacter().Phases().empty());
}

void TestSectorWords()
{
    const std::vector<LightSector> sectors = {{"R", 120.0, 150.0}, {"W", 90.0, 120.0}};
    CHECK_EQ(SectorWords(sectors, true),
             U("sectoren (peiling naar het licht): wit 090°–120°, rood 120°–150°"));
    CHECK_EQ(SectorWords(sectors, false),
             U("sectors (bearing towards the light): white 090°–120°, red 120°–150°"));
    CHECK_EQ(SectorWords({}, true), "");
}

void TestTaps()
{
    // Bounces of a few hundredths of a second are not flashes.
    CHECK(!AnalyseTaps({{0.0, 0.01}, {1.0, 1.02}}).valid);

    // Fl(2) every 6 seconds, four periods.
    Taps fl2;
    for (int p = 0; p < 4; ++p) {
        fl2.push_back({6.0 * p, 6.0 * p + 0.5});
        fl2.push_back({6.0 * p + 1.5, 6.0 * p + 2.0});
    }
    TapPattern pattern = AnalyseTaps(fl2);
    CHECK(pattern.valid);
    CHECK_EQ(pattern.kind, "Fl");
    CHECK(pattern.groups == std::vector<int>({2}));
    CHECK_NEAR(pattern.period, 6.0, 0.05);
    CHECK_EQ(pattern.Words(true), "groepen van 2 schitteringen, elke ~6.0 s");

    LightCharacter light;
    light.Parse("Fl(2)W.6s");
    const double right = light.Match(pattern);
    CHECK_NEAR(right, 1.0, 1e-9);
    light.Parse("Fl(3)W.6s");
    CHECK(light.Match(pattern) < right);
    light.Parse("Iso.W.6s");
    CHECK(light.Match(pattern) < right);

    // Isophase every 4 seconds.
    Taps iso;
    for (int p = 0; p < 4; ++p)
        iso.push_back({4.0 * p, 4.0 * p + 2.0});
    pattern = AnalyseTaps(iso);
    CHECK_EQ(pattern.kind, "Iso");
    CHECK_NEAR(pattern.period, 4.0, 0.05);

    // A continuous quick light: no groups, no period.
    Taps quick;
    for (int i = 0; i < 10; ++i)
        quick.push_back({1.0 * i, 1.0 * i + 0.4});
    pattern = AnalyseTaps(quick);
    CHECK_EQ(pattern.kind, "Q");
    CHECK(pattern.groups.empty());
    CHECK(pattern.period == 0.0);
    CHECK_EQ(pattern.Words(true), "flikkerlicht");

    // Q(6)+LFl every 15 seconds: six quick flashes and a long one.
    Taps cardinal;
    for (int p = 0; p < 3; ++p) {
        const double t = 15.0 * p;
        for (int i = 0; i < 6; ++i)
            cardinal.push_back({t + i, t + i + 0.4});
        cardinal.push_back({t + 6.0, t + 8.0});
    }
    pattern = AnalyseTaps(cardinal);
    CHECK_EQ(pattern.kind, "Q");
    CHECK(pattern.groups == std::vector<int>({6}));
    CHECK(pattern.longAfter);
    CHECK_NEAR(pattern.period, 15.0, 0.05);
    CHECK_EQ(pattern.Words(true),
             "groepen van 6 flikkeringen en een lange schittering, elke ~15 s");
    light.Parse("Q(6)+LFl.15s");
    CHECK_NEAR(light.Match(pattern), 1.0, 1e-9);
}

} // namespace

void TestLightCharacters()
{
    TestParse();
    TestColours();
    TestFromS57();
    TestFindInWaypoint();
    TestWords();
    TestPhases();
    TestSectorWords();
    TestTaps();
}
