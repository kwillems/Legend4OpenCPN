// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "note_places.h"

#include <cmath>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

namespace {

bool IsFenceLine(wxString line)
{
    line.Trim(true);
    return line == "---";
}

// "[52.58030, 5.75120]" or "52.58030, 5.75120"; a comma inside a number
// ("52,58030") is not allowed here, it separates the two.
bool ParseLatLon(wxString value, double *lat, double *lon)
{
    value.Trim(true).Trim(false);
    if (value.StartsWith("["))
        value = value.Mid(1);
    if (value.EndsWith("]"))
        value.RemoveLast();
    const wxArrayString parts = wxSplit(value, ',', '\0');
    if (parts.GetCount() != 2)
        return false;
    wxString a = parts[0];
    wxString b = parts[1];
    a.Trim(true).Trim(false);
    b.Trim(true).Trim(false);
    return a.ToCDouble(lat) && b.ToCDouble(lon) && std::fabs(*lat) <= 90.0 &&
           std::fabs(*lon) <= 180.0;
}

// "0.5", "0,5" or "0,5 NM".
bool ParseRadius(wxString value, double *radius)
{
    value.Trim(true).Trim(false);
    value = value.BeforeFirst(' ');
    value.Replace(",", ".");
    return value.ToCDouble(radius) && *radius > 0.0 && *radius < 100.0;
}

// "1:50000", "1:50.000", "50000" or "1:50k".
bool ParseScale(wxString value, int *scale)
{
    value.Trim(true).Trim(false);
    if (value.Contains(":"))
        value = value.AfterFirst(':');
    value.Replace(".", "");
    value.Replace(" ", "");
    long factor = 1;
    if (value.EndsWith("k") || value.EndsWith("K")) {
        factor = 1000;
        value.RemoveLast();
    }
    long number = 0;
    if (!value.ToLong(&number) || number <= 0)
        return false;
    *scale = static_cast<int>(number * factor);
    return *scale >= 1000;
}

wxString KeyOf(const wxString &line)
{
    wxString key = line.BeforeFirst(':');
    key.Trim(true).Trim(false);
    return key.Lower();
}

} // namespace

NoteFrontMatter SplitFrontMatter(const wxString &text)
{
    NoteFrontMatter result;
    result.body = text;

    size_t start = 0;
    if (text.StartsWith(wxString(wxUniChar(0xFEFF))))
        start = 1;
    const size_t firstEnd = text.find('\n', start);
    if (firstEnd == wxString::npos || !IsFenceLine(text.Mid(start, firstEnd - start)))
        return result;

    // The closing line.
    size_t lineStart = firstEnd + 1;
    while (lineStart < text.length()) {
        size_t lineEnd = text.find('\n', lineStart);
        const bool last = lineEnd == wxString::npos;
        if (last)
            lineEnd = text.length();
        const wxString line = text.Mid(lineStart, lineEnd - lineStart);
        if (IsFenceLine(line)) {
            const size_t blockEnd = last ? text.length() : lineEnd + 1;
            result.block = text.Mid(0, blockEnd);
            result.body = text.Mid(blockEnd);

            const wxArrayString lines =
                wxSplit(text.Mid(firstEnd + 1, lineStart - firstEnd - 1), '\n', '\0');
            for (const wxString &entry : lines) {
                const wxString key = KeyOf(entry);
                const wxString value = entry.AfterFirst(':');
                if (key == "location" || key == "plek")
                    result.hasPlace = ParseLatLon(value, &result.lat, &result.lon);
                else if (key == "straal" || key == "radius")
                    ParseRadius(value, &result.radius);
                else if (key == "schaal" || key == "scamin")
                    ParseScale(value, &result.scamin);
            }
            return result;
        }
        if (last)
            break;
        lineStart = lineEnd + 1;
    }
    return result;
}

wxString BlockWithPlace(const wxString &block, bool place, double lat, double lon,
                        double radius)
{
    // The lines between the fences, without the place.
    wxArrayString kept;
    if (!block.IsEmpty()) {
        wxArrayString lines = wxSplit(block, '\n', '\0');
        for (size_t i = 1; i < lines.GetCount(); ++i) {
            const wxString &line = lines[i];
            if (IsFenceLine(line))
                break;
            const wxString key = KeyOf(line);
            if (key == "location" || key == "plek" || key == "straal" || key == "radius")
                continue;
            kept.Add(line);
        }
    }

    if (place) {
        kept.Add(wxString::Format("location: [%s, %s]", wxString::FromCDouble(lat, 5),
                                  wxString::FromCDouble(lon, 5)));
        // "0.5", not "0.50".
        wxString text = wxString::FromCDouble(radius, 2);
        while (text.Contains(".") && text.EndsWith("0"))
            text.RemoveLast();
        if (text.EndsWith("."))
            text.RemoveLast();
        kept.Add("straal: " + text);
    }

    // Nothing left: no block at all.
    bool empty = true;
    for (const wxString &line : kept) {
        wxString trimmed = line;
        trimmed.Trim(true).Trim(false);
        if (!trimmed.IsEmpty())
            empty = false;
    }
    if (empty)
        return wxString();

    wxString out = "---\n";
    for (const wxString &line : kept)
        out << line << '\n';
    out << "---\n";
    return out;
}

std::vector<NotePlace> ReadNotePlaces(const std::vector<wxString> &paths)
{
    std::vector<NotePlace> places;
    for (const wxString &path : paths) {
        wxFile file(path);
        wxString text;
        if (!file.IsOpened() || !file.ReadAll(&text, wxConvUTF8))
            continue;
        const NoteFrontMatter front = SplitFrontMatter(text);
        if (!front.hasPlace)
            continue;

        NotePlace place;
        place.path = path;
        place.lat = front.lat;
        place.lon = front.lon;
        place.radius = front.radius;
        place.scamin = front.scamin;
        place.title = wxFileName(path).GetName();

        // The "# Title" and the first line of text after it.
        wxStringTokenizer lines(front.body, "\n");
        bool titleSeen = false;
        while (lines.HasMoreTokens()) {
            wxString line = lines.GetNextToken();
            line.Trim(true).Trim(false);
            if (line.IsEmpty())
                continue;
            if (!titleSeen && line.StartsWith("# ")) {
                place.title = line.Mid(2).Trim(true).Trim(false);
                titleSeen = true;
                continue;
            }
            titleSeen = true;
            if (line.StartsWith("#"))
                continue;
            // Markdown marks out of the way, roughly.
            for (const char *mark : {"**", "__", "`", "- [ ] ", "- [x] ", "- "})
                line.Replace(mark, "");
            place.snippet = line;
            break;
        }
        places.push_back(place);
    }
    return places;
}
