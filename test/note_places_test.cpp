// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of notes tied to a place: reading the block at the top of a
// note (place, radius, scale), writing it back, and finding the notes with
// a place among the files of the Legend folder.

#include "checks.h"
#include "note_places.h"

#include <vector>

#include <wx/log.h>

namespace {

void TestNoBlock()
{
    const wxString text = "# Haven\n\nTankplek aan de kade.\n";
    const NoteFrontMatter front = SplitFrontMatter(text);
    CHECK(front.block.IsEmpty());
    CHECK_EQ(front.body, text);
    CHECK(!front.hasPlace);
    CHECK_NEAR(front.radius, 0.5, 1e-9);
    CHECK(front.scamin == 200000);

    // Not closed: no block either.
    const wxString open = "---\nlocation: [52.5, 5.7]\n# Haven\n";
    CHECK(SplitFrontMatter(open).block.IsEmpty());
    CHECK(!SplitFrontMatter(open).hasPlace);

    // A line "---" further down is a rule, not a block.
    const wxString rule = "# Haven\n---\nlocation: [52.5, 5.7]\n---\n";
    CHECK(!SplitFrontMatter(rule).hasPlace);
}

void TestPlace()
{
    const wxString text =
        "---\nlocation: [52.58030, 5.75120]\nstraal: 0.5\n---\n# Haven\n";
    const NoteFrontMatter front = SplitFrontMatter(text);
    CHECK(front.hasPlace);
    CHECK_NEAR(front.lat, 52.58030, 1e-9);
    CHECK_NEAR(front.lon, 5.75120, 1e-9);
    CHECK_NEAR(front.radius, 0.5, 1e-9);
    CHECK(front.scamin == 200000);
    CHECK_EQ(front.block, "---\nlocation: [52.58030, 5.75120]\nstraal: 0.5\n---\n");
    CHECK_EQ(front.body, "# Haven\n");

    // "plek" without brackets, "radius" with a decimal comma and a unit,
    // other lines kept in the block.
    const NoteFrontMatter plek = SplitFrontMatter(
        "---\ntags: haven\nplek: 51.9, 4.45\nradius: 0,25 NM\n---\nTekst\n");
    CHECK(plek.hasPlace);
    CHECK_NEAR(plek.lat, 51.9, 1e-9);
    CHECK_NEAR(plek.lon, 4.45, 1e-9);
    CHECK_NEAR(plek.radius, 0.25, 1e-9);
    CHECK(plek.block.Contains("tags: haven"));
    CHECK_EQ(plek.body, "Tekst\n");

    // Capitals in the key, spaces around the value.
    CHECK(SplitFrontMatter("---\nLocation :  [52.5, 5.7]  \n---\n").hasPlace);

    // Windows line ends, and a byte order mark before the block.
    const NoteFrontMatter crlf =
        SplitFrontMatter("---\r\nlocation: [52.5, 5.7]\r\nstraal: 1\r\n---\r\nTekst\r\n");
    CHECK(crlf.hasPlace);
    CHECK_NEAR(crlf.radius, 1.0, 1e-9);
    CHECK_EQ(crlf.body, "Tekst\r\n");
    const NoteFrontMatter bom =
        SplitFrontMatter(wxString(wxUniChar(0xFEFF)) + "---\nlocation: [52.5, 5.7]\n---\n");
    CHECK(bom.hasPlace);

    // The closing line as the very last line, without a line end.
    const NoteFrontMatter last = SplitFrontMatter("---\nlocation: [52.5, 5.7]\n---");
    CHECK(last.hasPlace);
    CHECK(last.body.IsEmpty());
}

void TestBadPlace()
{
    // A decimal comma makes four numbers out of two.
    CHECK(!SplitFrontMatter("---\nlocation: [52,58030, 5,75120]\n---\n").hasPlace);
    // Out of range, not numbers, only one.
    CHECK(!SplitFrontMatter("---\nlocation: [95.0, 5.7]\n---\n").hasPlace);
    CHECK(!SplitFrontMatter("---\nlocation: [52.5, 190]\n---\n").hasPlace);
    CHECK(!SplitFrontMatter("---\nlocation: [noord, oost]\n---\n").hasPlace);
    CHECK(!SplitFrontMatter("---\nlocation: 52.5\n---\n").hasPlace);

    // A radius that is no radius leaves the default.
    CHECK_NEAR(SplitFrontMatter("---\nstraal: veel\n---\n").radius, 0.5, 1e-9);
    CHECK_NEAR(SplitFrontMatter("---\nstraal: 0\n---\n").radius, 0.5, 1e-9);
    CHECK_NEAR(SplitFrontMatter("---\nstraal: -1\n---\n").radius, 0.5, 1e-9);
    CHECK_NEAR(SplitFrontMatter("---\nstraal: 500\n---\n").radius, 0.5, 1e-9);
}

void TestScale()
{
    CHECK(SplitFrontMatter("---\nschaal: 1:50000\n---\n").scamin == 50000);
    CHECK(SplitFrontMatter("---\nschaal: 1:50.000\n---\n").scamin == 50000);
    CHECK(SplitFrontMatter("---\nschaal: 1:50k\n---\n").scamin == 50000);
    CHECK(SplitFrontMatter("---\nschaal: 25000\n---\n").scamin == 25000);
    CHECK(SplitFrontMatter("---\nscamin: 1:1 000 000\n---\n").scamin == 1000000);
    // Not a scale: the default stays.
    CHECK(SplitFrontMatter("---\nschaal: groot\n---\n").scamin == 200000);
    CHECK(SplitFrontMatter("---\nschaal: 1:500\n---\n").scamin == 200000);
}

void TestBlockWithPlace()
{
    // A new block.
    CHECK_EQ(BlockWithPlace("", true, 52.5803, 5.7512, 0.5),
             "---\nlocation: [52.58030, 5.75120]\nstraal: 0.5\n---\n");

    // The radius without trailing zeros.
    CHECK(BlockWithPlace("", true, 52.5, 5.7, 2.0).Contains("straal: 2\n"));
    CHECK(BlockWithPlace("", true, 52.5, 5.7, 1.25).Contains("straal: 1.25\n"));
    CHECK(BlockWithPlace("", true, 52.5, 5.7, 0.10).Contains("straal: 0.1\n"));

    // Another place: the old lines go, other lines stay, in their order.
    const wxString block = "---\ntags: haven\nplek: 51.9, 4.45\nradius: 0.3\nschaal: 1:50000\n---\n";
    CHECK_EQ(BlockWithPlace(block, true, 52.0, 5.0, 1.0),
             "---\ntags: haven\nschaal: 1:50000\nlocation: [52.00000, 5.00000]\nstraal: 1\n---\n");

    // The place removed: the other lines stay; nothing left, no block.
    CHECK_EQ(BlockWithPlace(block, false, 0, 0, 0), "---\ntags: haven\nschaal: 1:50000\n---\n");
    CHECK_EQ(BlockWithPlace("---\nlocation: [52.5, 5.7]\nstraal: 0.5\n---\n", false, 0, 0, 0),
             "");

    // Written and read again: the same place.
    const NoteFrontMatter again =
        SplitFrontMatter(BlockWithPlace(block, true, 53.123456, 4.987654, 0.75) + "# Notitie\n");
    CHECK(again.hasPlace);
    CHECK_NEAR(again.lat, 53.12346, 1e-9);
    CHECK_NEAR(again.lon, 4.98765, 1e-9);
    CHECK_NEAR(again.radius, 0.75, 1e-9);
    CHECK(again.scamin == 50000);
    CHECK_EQ(again.body, "# Notitie\n");
}

void TestReadNotePlaces()
{
    TempFolder folder;
    std::vector<wxString> paths;
    paths.push_back(folder.Write("Haven.md",
                                 "---\nlocation: [52.5, 5.7]\nstraal: 0.3\nschaal: 1:25000\n---\n"
                                 "# Jachthaven De Vaart\n\nTankplek aan de **kade**.\n"));
    paths.push_back(folder.Write("Zonder plek.md", "# Boodschappen\n\nBrood en melk.\n"));
    paths.push_back(folder.Write("Brug.md",
                                 "---\nlocation: [51.9, 4.45]\n---\n\n## Bediening\n"
                                 "- [ ] Bellen naar de brugwachter\n"));
    paths.push_back(folder.Path() + wxFileName::GetPathSeparator() + "Bestaat niet.md");

    std::vector<NotePlace> places;
    {
        wxLogNull quiet; // the missing file
        places = ReadNotePlaces(paths);
    }
    CHECK(places.size() == 2);
    if (places.size() != 2)
        return;

    CHECK_EQ(places[0].path, paths[0]);
    CHECK_EQ(places[0].title, "Jachthaven De Vaart");
    CHECK_EQ(places[0].snippet, "Tankplek aan de kade.");
    CHECK_NEAR(places[0].lat, 52.5, 1e-9);
    CHECK_NEAR(places[0].lon, 5.7, 1e-9);
    CHECK_NEAR(places[0].radius, 0.3, 1e-9);
    CHECK(places[0].scamin == 25000);

    // No "# Title": the file name; headings skipped, the task as text.
    CHECK_EQ(places[1].title, "Brug");
    CHECK_EQ(places[1].snippet, "Bellen naar de brugwachter");
    CHECK_NEAR(places[1].radius, 0.5, 1e-9);
}

} // namespace

void TestNotePlaces()
{
    TestNoBlock();
    TestPlace();
    TestBadPlace();
    TestScale();
    TestBlockWithPlace();
    TestReadNotePlaces();
}
