// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _NOTE_PLACES_H_
#define _NOTE_PLACES_H_

#include <vector>

#include <wx/string.h>

// The block of settings at the top of a note, between two lines "---"
// ("front matter", as in Obsidian), and the text after it. A note that
// belongs to a place says so there:
//
//   ---
//   location: [52.58030, 5.75120]
//   straal: 0.5
//   ---
//
// "location" is the way the Obsidian plugin Map View writes it, so the note
// shows on that map too; "plek" is read as well. The radius is in sea
// miles. Other lines of the block are kept as they are.
struct NoteFrontMatter
{
    wxString block;       // the whole block with both "---" lines, or empty
    wxString body;        // the note after the block
    bool hasPlace = false;
    double lat = 0.0;
    double lon = 0.0;
    double radius = 0.5;  // sea miles
};

NoteFrontMatter SplitFrontMatter(const wxString &text);

// The block with the place set (or removed when `place` is false); an empty
// block disappears.
wxString BlockWithPlace(const wxString &block, bool place, double lat, double lon,
                        double radius);

// A note that belongs to a place, as found in the Legend folder.
struct NotePlace
{
    wxString path;
    wxString title;    // the "# Title", or the file name
    wxString snippet;  // the first line of text after the title
    double lat = 0.0;
    double lon = 0.0;
    double radius = 0.5;
};

// The notes with a place among these Markdown files.
std::vector<NotePlace> ReadNotePlaces(const std::vector<wxString> &paths);

#endif
