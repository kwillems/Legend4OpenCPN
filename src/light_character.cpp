// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "light_character.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/tokenzr.h>
#include <wx/wxcrt.h>

namespace {

// Kinds of light, longest first so that "FFl" is not read as "F" + "Fl".
const char *const kKinds[] = {
    "FFl", "LFl", "IVQ", "IUQ", "Iso", "IQ", "VQ", "UQ", "Oc", "Fl", "Mo", "Q", "F"};

// Colour codes of lights (INT 1, P 11), two-letter codes first.
const char *const kColours[] = {"Bu", "Vi", "Or", "Am", "W", "R", "G", "Y"};

// Morse code of letters and digits, as dots and dashes.
const std::map<wxUniChar, const char *> &MorseTable()
{
    static const std::map<wxUniChar, const char *> table = {
        {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
        {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
        {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
        {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
        {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
        {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
        {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"},
        {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
        {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}};
    return table;
}

wxString MorseCode(const wxString &letters)
{
    wxString code;
    for (const wxUniChar ch : letters.Upper()) {
        auto found = MorseTable().find(ch);
        if (found == MorseTable().end())
            continue;
        if (!code.IsEmpty())
            code << "  ";
        for (const char *p = found->second; *p; ++p) {
            if (p != found->second)
                code << ' ';
            code << (*p == '.' ? wxString::FromUTF8("·")
                               : wxString::FromUTF8("–"));
        }
    }
    return code;
}

// "4", "2,5", "0,75" (Dutch) or "2.5" (English).
wxString FormatNumber(double value, bool dutch)
{
    if (std::fabs(value - std::round(value)) < 1e-6)
        return wxString::Format("%d", static_cast<int>(std::lround(value)));

    wxString text = wxString::Format("%.2f", value);
    while (text.EndsWith("0"))
        text.RemoveLast();
    if (dutch)
        text.Replace(".", ",");
    return text;
}

wxString Seconds(double value, bool dutch)
{
    const bool one = std::fabs(value - 1.0) < 1e-6;
    return FormatNumber(value, dutch) + " " +
           (dutch ? (one ? "seconde" : "seconden") : (one ? "second" : "seconds"));
}

wxString ColourWord(const wxString &code, bool dutch)
{
    static const std::map<wxString, std::pair<const char *, const char *>> words = {
        {"W", {"wit", "white"}},     {"R", {"rood", "red"}},
        {"G", {"groen", "green"}},   {"Y", {"geel", "yellow"}},
        {"Bu", {"blauw", "blue"}},   {"Or", {"oranje", "orange"}},
        {"Vi", {"violet", "violet"}}, {"Am", {"amber", "amber"}}};
    auto found = words.find(code);
    if (found == words.end())
        return code;
    return dutch ? found->second.first : found->second.second;
}

// "a", "a en b", "a, b en c".
wxString JoinWords(const std::vector<wxString> &items, bool dutch)
{
    wxString text;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            text << (i + 1 == items.size() ? (dutch ? " en " : " and ") : ", ");
        text << items[i];
    }
    return text;
}

bool IsQuick(const wxString &kind)
{
    return kind == "Q" || kind == "VQ" || kind == "UQ" || kind == "IQ" ||
           kind == "IVQ" || kind == "IUQ";
}

// Numbers of a group: "2+1" -> {2, 1}; empty or unreadable -> {1}.
std::vector<int> GroupCounts(const wxString &group)
{
    std::vector<int> counts;
    wxStringTokenizer parts(group, "+", wxTOKEN_STRTOK);
    while (parts.HasMoreTokens()) {
        long n = 0;
        if (parts.GetNextToken().ToLong(&n) && n > 0 && n < 50)
            counts.push_back(static_cast<int>(n));
    }
    if (counts.empty())
        counts.push_back(1);
    return counts;
}

} // namespace

// Colour of a light on screen, by its code in a light character.
wxColour LightDisplayColour(const wxString &code, double dim)
{
    wxColour colour(255, 250, 225); // W
    if (code == "R")
        colour = wxColour(226, 46, 38);
    else if (code == "G")
        colour = wxColour(34, 168, 64);
    else if (code == "Y")
        colour = wxColour(242, 196, 0);
    else if (code == "Bu")
        colour = wxColour(40, 96, 220);
    else if (code == "Or")
        colour = wxColour(245, 140, 0);
    else if (code == "Vi")
        colour = wxColour(150, 70, 200);
    else if (code == "Am")
        colour = wxColour(240, 170, 20);

    auto scale = [dim](unsigned char v) {
        return static_cast<unsigned char>(std::lround(v * dim));
    };
    return wxColour(scale(colour.Red()), scale(colour.Green()), scale(colour.Blue()));
}

void LightCharacter::Clear()
{
    m_alternating = false;
    m_parts.clear();
    m_colours.clear();
    m_period = 0.0;
}

bool LightCharacter::Parse(const wxString &text)
{
    Clear();

    wxString s;
    for (const wxUniChar ch : text) {
        if (ch != ' ' && ch != '\t')
            s << ch;
    }
    while (s.EndsWith("."))
        s.RemoveLast();
    if (s.IsEmpty())
        return false;

    size_t pos = 0;
    auto skipDots = [&]() {
        while (pos < s.length() && s[pos] == '.')
            ++pos;
    };
    auto startsWithNoCase = [&](const wxString &word) {
        return s.Mid(pos, word.length()).CmpNoCase(word) == 0;
    };

    if (startsWithNoCase("Al.")) {
        m_alternating = true;
        pos += 3;
    }

    // Kinds with their groups, joined by '+': "Q(6)+LFl".
    while (pos < s.length()) {
        wxString kind;
        for (const char *candidate : kKinds) {
            if (startsWithNoCase(candidate)) {
                kind = candidate;
                break;
            }
        }
        if (kind.IsEmpty()) {
            if (!m_parts.empty() && s[pos - 1] == '+') {
                Clear();
                return false;
            }
            break;
        }
        pos += kind.length();

        Part part;
        part.kind = kind;

        skipDots();
        if (pos < s.length() && s[pos] == '(') {
            const size_t close = s.find(')', pos);
            if (close == wxString::npos) {
                Clear();
                return false;
            }
            part.group = s.Mid(pos + 1, close - pos - 1);
            pos = close + 1;

            // Letters only for Morse code, numbers otherwise.
            for (const wxUniChar ch : part.group) {
                const bool ok = kind == "Mo" ? wxIsalnum(ch)
                                             : (wxIsdigit(ch) || ch == '+');
                if (!ok) {
                    Clear();
                    return false;
                }
            }
            if (part.group == "1" && kind != "Mo")
                part.group.Clear();
        }
        if (kind == "Mo" && part.group.IsEmpty()) {
            Clear();
            return false;
        }
        m_parts.push_back(part);

        if (pos < s.length() && s[pos] == '+') {
            ++pos;
            continue;
        }
        break;
    }

    if (m_parts.empty() && !m_alternating)
        return false;

    // Colours: "G", "WR", "Bu"; also written in small letters.
    auto readColours = [&]() {
        skipDots();
        while (pos < s.length()) {
            bool matched = false;
            for (const char *code : kColours) {
                const wxString c(code);
                if (startsWithNoCase(c)) {
                    m_colours.push_back(c);
                    pos += c.length();
                    matched = true;
                    break;
                }
            }
            if (!matched)
                break;
        }
    };
    readColours();

    // Period ("10s", "2.5s") and range in sea miles ("4M", "4sm"), which
    // is not part of the character and is left out.
    for (int number = 0; number < 2; ++number) {
        skipDots();
        if (pos >= s.length() || !wxIsdigit(s[pos]))
            break;

        size_t end = pos;
        while (end < s.length() && (wxIsdigit(s[end]) || s[end] == '.' || s[end] == ','))
            ++end;
        wxString digits = s.Mid(pos, end - pos);
        digits.Replace(",", ".");
        double value = 0.0;
        if (!digits.ToCDouble(&value) || value <= 0.0) {
            Clear();
            return false;
        }
        pos = end;

        bool range = false;
        for (const char *unit : {"nm", "sm", "zm", "M"}) {
            if (startsWithNoCase(unit) && pos + wxString(unit).length() == s.length()) {
                pos = s.length();
                range = true;
                break;
            }
        }
        if (range)
            break;

        if (m_period > 0.0 || value > 600.0) {
            Clear();
            return false;
        }
        if (startsWithNoCase("sec"))
            pos += 3;
        else if (pos < s.length() && (s[pos] == 's' || s[pos] == 'S'))
            ++pos;
        else {
            Clear();
            return false;
        }
        m_period = value;

        // A colour after the period ("Fl.4s Y") is read as well.
        if (m_colours.empty())
            readColours();
    }

    if (pos != s.length() || (m_parts.empty() && m_colours.size() < 2)) {
        Clear();
        return false;
    }
    return true;
}

bool LightCharacter::FromS57(
    const wxString &litchr,
    const wxString &siggrp,
    const wxString &sigper,
    const wxString &colour)
{
    Clear();

    long code = 0;
    if (!litchr.BeforeFirst(',').ToLong(&code))
        return false;

    // LITCHR values of the S-57 catalogue.
    std::vector<wxString> kinds;
    switch (code) {
    case 1: case 21: kinds = {"F"}; break;
    case 2: kinds = {"Fl"}; break;
    case 3: kinds = {"LFl"}; break;
    case 4: kinds = {"Q"}; break;
    case 5: kinds = {"VQ"}; break;
    case 6: kinds = {"UQ"}; break;
    case 7: kinds = {"Iso"}; break;
    case 8: kinds = {"Oc"}; break;
    case 9: kinds = {"IQ"}; break;
    case 10: kinds = {"IVQ"}; break;
    case 11: kinds = {"IUQ"}; break;
    case 12: kinds = {"Mo"}; break;
    case 13: kinds = {"FFl"}; break;
    case 14: kinds = {"Fl", "LFl"}; break;
    case 15: kinds = {"Oc", "Fl"}; break;
    case 16: kinds = {"F", "LFl"}; break;
    case 17: m_alternating = true; kinds = {"Oc"}; break;
    case 18: m_alternating = true; kinds = {"LFl"}; break;
    case 19: case 20: m_alternating = true; kinds = {"Fl"}; break;
    case 25: kinds = {"Q", "LFl"}; break;
    case 26: kinds = {"VQ", "LFl"}; break;
    case 27: kinds = {"UQ", "LFl"}; break;
    case 28: m_alternating = true; break;
    case 29: m_alternating = true; kinds = {"FFl"}; break;
    default: return false;
    }

    // SIGGRP: "(2)", "(2+1)", "(6)(1)", "()".
    std::vector<wxString> groups;
    wxString rest = siggrp;
    while (rest.Contains("(")) {
        rest = rest.AfterFirst('(');
        groups.push_back(rest.BeforeFirst(')'));
        rest = rest.AfterFirst(')');
    }

    for (size_t i = 0; i < kinds.size(); ++i) {
        Part part;
        part.kind = kinds[i];
        if (i < groups.size()) {
            part.group = groups[i];
            part.group.Trim(true).Trim(false);
        }

        // Letters only for Morse code, numbers otherwise; a chart that
        // mixes them up is not guessed at.
        for (const wxUniChar ch : part.group) {
            const bool ok = part.kind == "Mo" ? wxIsalnum(ch)
                                              : (wxIsdigit(ch) || ch == '+');
            if (!ok) {
                Clear();
                return false;
            }
        }
        if (part.kind == "Mo" && part.group.IsEmpty()) {
            Clear();
            return false;
        }

        if (part.group == "1" && part.kind != "Mo")
            part.group.Clear();
        m_parts.push_back(part);
    }

    // COLOUR: "1,3" -> white, red.
    static const std::map<long, const char *> colourCodes = {
        {1, "W"}, {3, "R"}, {4, "G"}, {5, "Bu"}, {6, "Y"},
        {9, "Am"}, {10, "Vi"}, {11, "Or"}};
    wxStringTokenizer colours(colour, ",", wxTOKEN_STRTOK);
    while (colours.HasMoreTokens()) {
        long c = 0;
        if (!colours.GetNextToken().ToLong(&c))
            continue;
        auto found = colourCodes.find(c);
        if (found != colourCodes.end())
            m_colours.push_back(found->second);
    }

    double period = 0.0;
    wxString periodText = sigper;
    periodText.Trim(true).Trim(false);
    if (periodText.ToCDouble(&period) && period > 0.0 && period <= 600.0)
        m_period = period;

    if (m_parts.empty() && m_colours.size() < 2) {
        Clear();
        return false;
    }
    return true;
}

bool LightCharacter::FindInWaypoint(const wxString &name, const wxString &description)
{
    // "groen # Lichtkarakter: Iso.G.4s"
    const wxString lower = description.Lower();
    for (const char *marker : {"lichtkarakter:", "light character:", "light:"}) {
        const int at = lower.Find(marker);
        if (at == wxNOT_FOUND)
            continue;

        wxString value = description.Mid(at + wxString(marker).length());
        value = value.BeforeFirst('\n').BeforeFirst('\r').BeforeFirst('#');
        if (Parse(value))
            return true;
    }

    // "13\nIso.G.8s": a line after the first that is only a characteristic.
    wxString text = name;
    text.Replace("\r", "\n");
    const wxArrayString lines = wxSplit(text, '\n', '\0');
    for (size_t i = 1; i < lines.GetCount(); ++i) {
        if (Parse(lines[i]))
            return true;
    }

    Clear();
    return false;
}

wxString LightCharacter::Abbreviation() const
{
    wxString text = m_alternating ? "Al." : "";

    for (size_t i = 0; i < m_parts.size(); ++i) {
        if (i > 0)
            text << '+';
        text << m_parts[i].kind;
        if (!m_parts[i].group.IsEmpty())
            text << '(' << m_parts[i].group << ')';
    }

    if (!m_colours.empty()) {
        if (!text.IsEmpty() && !text.EndsWith(")") && !text.EndsWith("."))
            text << '.';
        for (const wxString &colour : m_colours)
            text << colour;
    }

    if (m_period > 0.0) {
        if (!text.IsEmpty() && !text.EndsWith(")"))
            text << '.';
        text << FormatNumber(m_period, false) << 's';
    }
    return text;
}

std::vector<wxString> LightCharacter::Colours() const
{
    if (m_colours.empty())
        return {"W"};
    return m_colours;
}

wxString LightCharacter::Words(bool dutch) const
{
    if (!IsValid())
        return wxString();

    const std::vector<wxString> colours = Colours();

    // The name of the kind, as in Kaart 1 and INT 1 (P 10).
    auto kindName = [dutch](const Part &part) -> wxString {
        const bool group = !part.group.IsEmpty();
        const bool composite = part.group.Contains("+");
        const wxString &k = part.kind;

        if (k == "F")
            return dutch ? "vast licht" : "fixed light";
        if (k == "FFl")
            return dutch ? "vast- en schitterlicht" : "fixed and flashing light";
        if (k == "Fl") {
            if (composite)
                return dutch ? "groepschitterlicht met samengestelde groep"
                             : "composite group-flashing light";
            if (group)
                return dutch ? "groepschitterlicht" : "group-flashing light";
            return dutch ? "schitterlicht" : "flashing light";
        }
        if (k == "LFl")
            return group ? (dutch ? "groep lang schitterlicht" : "group long-flashing light")
                         : (dutch ? "lang schitterlicht" : "long-flashing light");
        if (k == "Oc") {
            if (composite)
                return dutch ? "groeponderbroken licht met samengestelde groep"
                             : "composite group-occulting light";
            if (group)
                return dutch ? "groeponderbroken licht" : "group-occulting light";
            return dutch ? "onderbroken licht" : "occulting light";
        }
        if (k == "Iso")
            return dutch ? "isofase licht" : "isophase light";
        if (k == "Q")
            return group ? (dutch ? "groepflikkerlicht" : "group quick-flashing light")
                         : (dutch ? "flikkerlicht" : "quick-flashing light");
        if (k == "VQ")
            return group ? (dutch ? "groep snel flikkerlicht" : "group very quick-flashing light")
                         : (dutch ? "snel flikkerlicht" : "very quick-flashing light");
        if (k == "UQ")
            return group ? (dutch ? "groep ultrasnel flikkerlicht" : "group ultra quick-flashing light")
                         : (dutch ? "ultrasnel flikkerlicht" : "ultra quick-flashing light");
        if (k == "IQ")
            return dutch ? "onderbroken flikkerlicht" : "interrupted quick-flashing light";
        if (k == "IVQ")
            return dutch ? "onderbroken snel flikkerlicht"
                         : "interrupted very quick-flashing light";
        if (k == "IUQ")
            return dutch ? "onderbroken ultrasnel flikkerlicht"
                         : "interrupted ultra quick-flashing light";
        if (k == "Mo")
            return dutch ? "Morse-Code licht" : "Morse code light";
        return k;
    };

    // What one part shows: "3 schitteringen", "de letter U (· · –)".
    auto partDetail = [dutch](const Part &part, bool following) -> wxString {
        const wxString &k = part.kind;
        if (k == "Mo") {
            return (dutch ? "de letter " : "the letter ") + part.group.Upper() + " (" +
                   MorseCode(part.group) + ")";
        }
        if (part.group.IsEmpty()) {
            if (!following)
                return wxString();
            if (k == "LFl")
                return dutch ? "een lange schittering" : "a long flash";
            if (k == "Fl")
                return dutch ? "een schittering" : "a flash";
            if (k == "Oc")
                return dutch ? "een verduistering" : "an eclipse";
            if (k == "F")
                return dutch ? "vast licht" : "a steady light";
            return wxString();
        }

        const std::vector<int> counts = GroupCounts(part.group);
        int total = 0;
        for (int n : counts)
            total += n;
        const bool one = counts.size() == 1 && total == 1;

        wxString noun;
        if (k == "Oc")
            noun = dutch ? (one ? "verduistering" : "verduisteringen")
                         : (one ? "eclipse" : "eclipses");
        else if (k == "LFl")
            noun = dutch ? (one ? "lange schittering" : "lange schitteringen")
                         : (one ? "long flash" : "long flashes");
        else if (IsQuick(k))
            noun = dutch ? (one ? "flikkering" : "flikkeringen")
                         : (one ? "flash" : "flashes");
        else
            noun = dutch ? (one ? "schittering" : "schitteringen")
                         : (one ? "flash" : "flashes");
        return part.group + " " + noun;
    };

    std::vector<wxString> colourWords;
    for (const wxString &colour : colours)
        colourWords.push_back(ColourWord(colour, dutch));

    wxString name = m_parts.empty() ? wxString(dutch ? "licht" : "light")
                                    : kindName(m_parts[0]);
    if (m_alternating)
        name = (dutch ? "alternerend " : "alternating ") + name;

    wxString text = JoinWords(colourWords, dutch) + " " + name;
    if (!m_alternating && colours.size() > 1)
        text << (dutch ? " (sectorlicht)" : " (sector light)");

    std::vector<wxString> details;

    std::vector<wxString> partDetails;
    for (size_t i = 0; i < m_parts.size(); ++i) {
        const wxString detail = partDetail(m_parts[i], i > 0);
        if (!detail.IsEmpty())
            partDetails.push_back(detail);
    }
    if (!partDetails.empty())
        details.push_back(JoinWords(partDetails, dutch));

    // Rate of quick lights that are not in groups (Kaart 1, P 10.6-10.8).
    // A continuous one with a period says it already.
    const bool continuousWithPeriod =
        m_period > 0.0 && m_parts.size() == 1 &&
        (m_parts[0].kind == "Q" || m_parts[0].kind == "VQ" || m_parts[0].kind == "UQ");
    if (m_parts.size() == 1 && m_parts[0].group.IsEmpty() && !continuousWithPeriod) {
        const wxString &k = m_parts[0].kind;
        if (k == "Q" || k == "IQ")
            details.push_back(dutch ? "ongeveer 60 per minuut" : "about 60 a minute");
        else if (k == "VQ" || k == "IVQ")
            details.push_back(dutch ? "ongeveer 120 per minuut" : "about 120 a minute");
        else if (k == "UQ" || k == "IUQ")
            details.push_back(dutch ? "240 tot 300 per minuut" : "240 to 300 a minute");
    }

    if (m_period > 0.0) {
        if (m_parts.size() == 1 && m_parts[0].kind == "Iso") {
            const wxString half = Seconds(m_period / 2.0, dutch);
            details.push_back(dutch ? half + " licht en " + half + " donker"
                                    : half + " light and " + half + " dark");
        }
        else if (std::fabs(m_period - 1.0) < 1e-6) {
            details.push_back(dutch ? "elke seconde" : "every second");
        }
        else {
            details.push_back((dutch ? "elke " : "every ") + Seconds(m_period, dutch));
        }
    }

    if (!details.empty()) {
        text << ": ";
        for (size_t i = 0; i < details.size(); ++i)
            text << (i > 0 ? ", " : "") << details[i];
    }
    return text;
}

double LightCharacter::PeriodForDrawing(double sequenceLength) const
{
    if (m_period > 0.0)
        return std::max(m_period, sequenceLength + 0.5);
    return sequenceLength + std::max(1.5, sequenceLength * 0.6);
}

std::vector<LightPhase> LightCharacter::Phases() const
{
    std::vector<LightPhase> phases = OnePeriod();

    // An alternating light that is lit once per period changes colour from
    // one period to the next: repeat the period for every colour.
    const int colourCount = static_cast<int>(Colours().size());
    if (!m_alternating || colourCount < 2 || m_parts.empty())
        return phases;

    int lit = 0;
    for (const LightPhase &phase : phases) {
        if (phase.colour >= 0)
            ++lit;
    }
    if (lit >= colourCount)
        return phases;

    std::vector<LightPhase> repeated;
    for (int r = 0; r < colourCount; ++r) {
        for (LightPhase phase : phases) {
            if (phase.colour >= 0)
                phase.colour = (phase.colour + r) % colourCount;
            repeated.push_back(phase);
        }
    }
    return repeated;
}

std::vector<LightPhase> LightCharacter::OnePeriod() const
{
    std::vector<LightPhase> phases;
    if (!IsValid())
        return phases;

    const int colourCount = static_cast<int>(Colours().size());
    int lit = 0;

    // Alternating lights change colour from one lit phase to the next;
    // other lights are drawn in their first colour.
    auto on = [&](double seconds, bool dim = false) {
        LightPhase phase;
        phase.seconds = seconds;
        phase.colour = m_alternating ? lit % colourCount : 0;
        phase.dim = dim;
        phases.push_back(phase);
        ++lit;
    };
    auto off = [&](double seconds) {
        LightPhase phase;
        phase.seconds = seconds;
        phases.push_back(phase);
    };
    auto length = [&]() {
        double total = 0.0;
        for (const LightPhase &phase : phases)
            total += phase.seconds;
        return total;
    };

    // Alternating without a rhythm: the colours take turns.
    if (m_parts.empty()) {
        const double period = m_period > 0.0 ? m_period : 4.0;
        for (int i = 0; i < colourCount; ++i)
            on(period / colourCount);
        return phases;
    }

    const wxString &first = m_parts[0].kind;
    const bool single = m_parts.size() == 1;

    // Lights that are lit the whole period, or for most of it.
    if (single && (first == "F" || first == "Iso" || first == "FFl" ||
                   ((first == "Q" || first == "VQ" || first == "UQ") &&
                    m_parts[0].group.IsEmpty()))) {
        if (first == "F") {
            on(m_period > 0.0 ? m_period : 3.0);
        }
        else if (first == "Iso") {
            const double period = m_period > 0.0 ? m_period : 4.0;
            on(period / 2.0);
            off(period / 2.0);
        }
        else if (first == "FFl") {
            const double period = m_period > 0.0 ? m_period : 6.0;
            const double flash = std::min(1.0, period * 0.3);
            on((period - flash) / 2.0, true);
            on(flash);
            on((period - flash) / 2.0, true);
        }
        else {
            const double cycle = first == "Q" ? 1.0 : first == "VQ" ? 0.5 : 0.25;
            const double period =
                m_period > 0.0 ? m_period : (first == "Q" ? 6.0 : first == "VQ" ? 4.0 : 3.0);
            const int flashes = std::max(1, static_cast<int>(std::lround(period / cycle)));
            for (int i = 0; i < flashes; ++i) {
                on(cycle * 0.4);
                off(cycle * 0.6);
            }
        }
        return phases;
    }

    // Occulting: light most of the time, with short eclipses at the end.
    if (first == "Oc") {
        std::vector<LightPhase> eclipses;
        auto add = [&eclipses](double seconds, int colour) {
            LightPhase phase;
            phase.seconds = seconds;
            phase.colour = colour;
            eclipses.push_back(phase);
        };
        const std::vector<int> counts = GroupCounts(m_parts[0].group);
        const bool groupLight = !m_parts[0].group.IsEmpty();
        for (size_t g = 0; g < counts.size(); ++g) {
            for (int i = 0; i < counts[g]; ++i) {
                if (g > 0 || i > 0)
                    add(g > 0 && i == 0 ? 1.5 : 0.6, 0);
                add(groupLight ? 0.6 : 1.0, -1);
            }
        }
        double eclipseLength = 0.0;
        for (const LightPhase &phase : eclipses)
            eclipseLength += phase.seconds;

        const double period = m_period > 0.0 ? std::max(m_period, eclipseLength + 1.0)
                                             : eclipseLength + 3.0;
        on(period - eclipseLength);
        for (const LightPhase &phase : eclipses) {
            if (phase.colour < 0)
                off(phase.seconds);
            else
                on(phase.seconds);
        }
        return phases;
    }

    // Flashes and groups of flashes, followed by darkness.
    for (size_t p = 0; p < m_parts.size(); ++p) {
        const Part &part = m_parts[p];
        const wxString &k = part.kind;
        if (p > 0)
            off(1.0);

        if (k == "Fl" || k == "LFl") {
            const double flash = k == "Fl" ? 0.5 : 2.0;
            const double gap = k == "Fl" ? 1.0 : 1.5;
            const std::vector<int> counts = GroupCounts(part.group);
            for (size_t g = 0; g < counts.size(); ++g) {
                if (g > 0)
                    off(gap * 2.0);
                for (int i = 0; i < counts[g]; ++i) {
                    if (i > 0)
                        off(gap);
                    on(flash);
                }
            }
        }
        else if (IsQuick(k)) {
            const double cycle = (k == "Q" || k == "IQ") ? 1.0
                                 : (k == "VQ" || k == "IVQ") ? 0.5
                                                             : 0.25;
            int flashes = 1;
            if (!part.group.IsEmpty()) {
                for (int n : GroupCounts(part.group))
                    flashes += n;
                --flashes;
            }
            else if (k.StartsWith("I")) {
                // Interrupted: quick flashes for most of the period.
                const double period = m_period > 0.0 ? m_period : 10.0;
                flashes = std::max(2, static_cast<int>(period * 0.7 / cycle));
            }
            for (int i = 0; i < flashes; ++i) {
                if (i > 0)
                    off(cycle * 0.6);
                on(cycle * 0.4);
            }
        }
        else if (k == "Mo") {
            const wxString code = MorseCode(part.group);
            bool firstSymbol = true;
            for (const wxUniChar ch : code) {
                if (ch == ' ')
                    continue;
                if (!firstSymbol)
                    off(0.5);
                on(ch == wxUniChar(0x00b7) ? 0.5 : 1.5);
                firstSymbol = false;
            }
        }
        else if (k == "F" || k == "FFl" || k == "Iso") {
            on(2.0);
        }
        else if (k == "Oc") {
            off(1.0);
        }
    }

    const double sequence = length();
    off(std::max(0.8, PeriodForDrawing(sequence) - sequence));
    return phases;
}

// A round lamp in the colours of a light, the icon of its row; a light
// with several colours is divided like a pie.
wxImage LightLampImage(
    const std::vector<wxString> &colours,
    const wxColour &outline,
    const wxColour &background,
    double dim,
    int size)
{
    const double lineScale = size / 18.0;
    wxBitmap bitmap(size, size, 24);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(background));
        dc.Clear();

        wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            const double centre = size / 2.0;
            const double radius = size / 2.0 - 1.5 * lineScale;
            const double kPi = 3.14159265358979323846;
            const size_t count = std::max<size_t>(1, colours.size());

            gc->SetPen(*wxTRANSPARENT_PEN);
            for (size_t i = 0; i < count; ++i) {
                gc->SetBrush(wxBrush(LightDisplayColour(colours[i], dim)));
                if (count == 1) {
                    gc->DrawEllipse(centre - radius, centre - radius, 2 * radius,
                                    2 * radius);
                    break;
                }
                const double start = -kPi / 2.0 + 2.0 * kPi * i / count;
                wxGraphicsPath slice = gc->CreatePath();
                slice.MoveToPoint(centre, centre);
                slice.AddArc(centre, centre, radius, start, start + 2.0 * kPi / count,
                             true);
                slice.CloseSubpath();
                gc->FillPath(slice);
            }

            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(outline).Width(1.2 * lineScale)));
            gc->DrawEllipse(centre - radius, centre - radius, 2 * radius, 2 * radius);
            delete gc;
        }
        dc.SelectObject(wxNullBitmap);
    }
    return bitmap.ConvertToImage();
}

// The rhythm of a light: a thin line for the dark and bars in the colour of
// the light where it shines, a low bar for the steady part of a fixed and
// flashing light. Ticks mark the start and the end of the period.
wxImage LightRhythmImage(
    const LightCharacter &light,
    int width,
    int height,
    const wxColour &line,
    const wxColour &background,
    double dim,
    double scale)
{
    const std::vector<LightPhase> phases = light.Phases();
    const std::vector<wxString> colours = light.Colours();

    double total = 0.0;
    for (const LightPhase &phase : phases)
        total += phase.seconds;
    if (total <= 0.0 || colours.empty())
        return wxImage();

    wxBitmap bitmap(width, height, 24);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(background));
        dc.Clear();

        wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            const double left = 1.5 * scale;
            const double right = width - 1.5 * scale;
            const double baseline = height - 2.5 * scale;
            const double perSecond = (right - left) / total;

            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(1.0 * scale)));
            gc->StrokeLine(left, baseline, right, baseline);
            gc->StrokeLine(left, baseline - 4.0 * scale, left, baseline + 2.0 * scale);
            gc->StrokeLine(right, baseline - 4.0 * scale, right, baseline + 2.0 * scale);

            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(0.8 * scale)));
            double x = left;
            for (const LightPhase &phase : phases) {
                const double w = phase.seconds * perSecond;
                if (phase.colour >= 0) {
                    const wxString &code =
                        colours[static_cast<size_t>(phase.colour) % colours.size()];
                    gc->SetBrush(wxBrush(LightDisplayColour(code, dim)));
                    const double top = phase.dim
                                           ? baseline - (baseline - 1.5 * scale) * 0.45
                                           : 1.5 * scale;
                    gc->DrawRectangle(x, top, std::max(1.5 * scale, w - 0.4 * scale),
                                      baseline - top);
                }
                x += w;
            }
            delete gc;
        }
        dc.SelectObject(wxNullBitmap);
    }
    return bitmap.ConvertToImage();
}

wxString LightCharacter::RhythmAbbreviation() const
{
    LightCharacter rhythm = *this;
    rhythm.m_colours.clear();
    return rhythm.Abbreviation();
}

wxString SectorWords(const std::vector<LightSector> &sectors, bool dutch)
{
    if (sectors.empty())
        return wxString();

    std::vector<LightSector> ordered = sectors;
    std::sort(ordered.begin(), ordered.end(),
              [](const LightSector &a, const LightSector &b) { return a.from < b.from; });

    auto degrees = [](double value) {
        return wxString::Format("%03d", static_cast<int>(std::lround(value)) % 360) +
               wxString::FromUTF8("\u00b0");
    };

    wxString text = dutch ? "sectoren (peiling naar het licht): "
                          : "sectors (bearing towards the light): ";
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i > 0)
            text << ", ";
        text << ColourWord(ordered[i].colour, dutch) << ' ' << degrees(ordered[i].from)
             << wxString::FromUTF8("\u2013") << degrees(ordered[i].to);
    }
    return text;
}
