// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _SYMBOL_MEANINGS_H_
#define _SYMBOL_MEANINGS_H_

#include <map>
#include <vector>

#include <wx/string.h>

// One line of symbolen.txt: icon name (may hold one '*'), meaning (may be
// empty: the line then only places the icon under its heading).
struct SymbolMeaning
{
    wxString name;
    wxString meaning;
    int section = -1; // index in the list of headings, -1 = before any
    int order = 0;    // position in the file
};

// symbolen.txt: the headings ("[Betonning | Buoyage]") and the lines
// "icon = meaning" under them, in the order of the file.
class SymbolMeanings
{
public:
    // Reads the file; without it, or unreadable, there are no lines.
    bool Load(const wxString &path);

    // Takes the lines of the file, read already.
    void Parse(const std::vector<wxString> &lines);

    void Clear();

    const std::vector<wxString> &Sections() const { return m_sections; }
    const std::vector<SymbolMeaning> &Lines() const { return m_lines; }

    // The line of an icon: its exact name, or else the first pattern that
    // fits. `meaning` gets its text in the chosen language, with the '*'
    // filled in from the icon name. Null when no line fits.
    const SymbolMeaning *Find(const wxString &key, bool dutch, wxString *meaning) const;

private:
    std::vector<wxString> m_sections;
    std::vector<SymbolMeaning> m_lines;
    std::map<wxString, size_t> m_exact;
    std::vector<size_t> m_patterns;
};

// "Nederlands | English" texts of symbolen.txt: the part for the chosen
// language, or the other part when that one is empty.
wxString TextInLanguage(const wxString &text, bool dutch);

#endif
