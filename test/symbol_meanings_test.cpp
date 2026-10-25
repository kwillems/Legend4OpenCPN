// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of symbolen.txt: headings, lines, patterns with '*', the two
// languages, and which line an icon gets.

#include "checks.h"
#include "symbol_meanings.h"

#include <vector>

#include <wx/log.h>

namespace {

// The meaning of an icon, or "(none)" when no line fits.
wxString MeaningOf(const SymbolMeanings &meanings, const wxString &key, bool dutch = true)
{
    wxString meaning;
    if (!meanings.Find(key, dutch, &meaning))
        return "(none)";
    return meaning;
}

SymbolMeanings Example()
{
    SymbolMeanings meanings;
    meanings.Parse({
        "# Betekenis en indeling van symbolen",
        "",
        "los = Voor elk kopje",
        "[Marifoon]",
        "vhf_b11b_*    = Meldplicht marifoon, kanaal *",
        "vhf_*         =",
        "  [ Betonning | Buoyage ]  ",
        "011           = Groene spitse ton | Green conical buoy",
        "012=Rode stompe ton|Red can buoy",
        "013           = | Yellow buoy",
        "014           = Gele ton |",
        "*_wrak        = Wrak (*)",
        "geen regel zonder isteken",
        " = zonder naam",
        "011           = Tweede regel voor 011",
        "[Overig]",
        "haven*        = Haven *",
    });
    return meanings;
}

// ---------------------------------------------------------------------------

void TestParse()
{
    const SymbolMeanings meanings = Example();

    // Headings, trimmed, in file order.
    CHECK(meanings.Sections().size() == 3);
    if (meanings.Sections().size() == 3) {
        CHECK_EQ(meanings.Sections()[0], "Marifoon");
        CHECK_EQ(meanings.Sections()[1], "Betonning | Buoyage");
        CHECK_EQ(meanings.Sections()[2], "Overig");
    }

    // Comments, empty lines, lines without '=' and without a name are
    // skipped; a second line for the same icon is kept but not used.
    CHECK(meanings.Lines().size() == 10);

    const SymbolMeaning *entry = nullptr;
    wxString meaning;
    entry = meanings.Find("los", true, &meaning);
    CHECK(entry && entry->section == -1 && entry->order == 0);
    entry = meanings.Find("011", true, &meaning);
    CHECK(entry && entry->section == 1 && entry->order == 3);
    entry = meanings.Find("haventje", true, &meaning);
    CHECK(entry && entry->section == 2 && entry->order == 9);
}

void TestFind()
{
    const SymbolMeanings meanings = Example();

    // Exact names.
    CHECK_EQ(MeaningOf(meanings, "011"), "Groene spitse ton");
    CHECK_EQ(MeaningOf(meanings, "los"), "Voor elk kopje");
    // The first line of a name counts.
    CHECK_EQ(MeaningOf(meanings, "011", true), "Groene spitse ton");

    // Patterns: the '*' of the meaning filled in from the icon name.
    CHECK_EQ(MeaningOf(meanings, "vhf_b11b_78"), "Meldplicht marifoon, kanaal 78");
    CHECK_EQ(MeaningOf(meanings, "steiger_wrak"), "Wrak (steiger)");
    CHECK_EQ(MeaningOf(meanings, "haven"), "Haven ");
    // The first pattern that fits, in file order.
    CHECK_EQ(MeaningOf(meanings, "vhf_b11b_"), "Meldplicht marifoon, kanaal ");
    // A pattern with an empty meaning: it fits, without text.
    CHECK_EQ(MeaningOf(meanings, "vhf_unknown_78"), "");
    wxString meaning = "oud";
    const SymbolMeaning *entry = meanings.Find("vhf_unknown_78", true, &meaning);
    CHECK(entry && entry->section == 0);
    CHECK(meaning.IsEmpty());

    // No line.
    CHECK_EQ(MeaningOf(meanings, "020"), "(none)");
    CHECK_EQ(MeaningOf(meanings, "vh"), "(none)");
    meaning = "oud";
    CHECK(!meanings.Find("020", true, &meaning));
    CHECK(meaning.IsEmpty());

    // An exact name goes before a pattern, also one earlier in the file.
    SymbolMeanings both;
    both.Parse({"vhf_* = Marifoon *", "vhf_78 = Kanaal achtenzeventig"});
    CHECK_EQ(MeaningOf(both, "vhf_78"), "Kanaal achtenzeventig");
    CHECK_EQ(MeaningOf(both, "vhf_18"), "Marifoon 18");
}

void TestLanguages()
{
    const SymbolMeanings meanings = Example();

    CHECK_EQ(MeaningOf(meanings, "011", false), "Green conical buoy");
    // Without spaces around the bar.
    CHECK_EQ(MeaningOf(meanings, "012", true), "Rode stompe ton");
    CHECK_EQ(MeaningOf(meanings, "012", false), "Red can buoy");
    // One part empty: the other one, in both languages.
    CHECK_EQ(MeaningOf(meanings, "013", true), "Yellow buoy");
    CHECK_EQ(MeaningOf(meanings, "013", false), "Yellow buoy");
    CHECK_EQ(MeaningOf(meanings, "014", true), "Gele ton");
    CHECK_EQ(MeaningOf(meanings, "014", false), "Gele ton");
    // Only one language: the same in both.
    CHECK_EQ(MeaningOf(meanings, "los", false), "Voor elk kopje");

    CHECK_EQ(TextInLanguage("Betonning | Buoyage", true), "Betonning");
    CHECK_EQ(TextInLanguage("Betonning | Buoyage", false), "Buoyage");
    CHECK_EQ(TextInLanguage("Marifoon", false), "Marifoon");
    CHECK_EQ(TextInLanguage("", false), "");
}

void TestLoad()
{
    TempFolder folder;
    // Windows line ends and a letter outside ASCII.
    const wxString path = folder.Write(
        "symbolen.txt", U("[Water | Water]\r\nski = Waterskiën | Water skiing\r\n"));

    SymbolMeanings meanings;
    CHECK(meanings.Load(path));
    CHECK(meanings.Sections().size() == 1);
    CHECK_EQ(MeaningOf(meanings, "ski"), U("Waterskiën"));
    CHECK_EQ(MeaningOf(meanings, "ski", false), "Water skiing");

    // No file: nothing left from before.
    {
        wxLogNull quiet;
        CHECK(!meanings.Load(folder.Path() + wxFileName::GetPathSeparator() + "weg.txt"));
    }
    CHECK(meanings.Sections().empty());
    CHECK(meanings.Lines().empty());
    CHECK_EQ(MeaningOf(meanings, "ski"), "(none)");
}

} // namespace

void TestSymbolMeanings()
{
    TestParse();
    TestFind();
    TestLanguages();
    TestLoad();
}
