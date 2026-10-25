// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "symbol_meanings.h"

#include <wx/textfile.h>

bool SymbolMeanings::Load(const wxString &path)
{
    Clear();

    wxTextFile file;
    if (!file.Open(path, wxConvUTF8))
        return false;

    std::vector<wxString> lines;
    for (size_t i = 0; i < file.GetLineCount(); ++i)
        lines.push_back(file[i]);
    Parse(lines);
    return true;
}

void SymbolMeanings::Parse(const std::vector<wxString> &lines)
{
    Clear();

    int section = -1;

    for (wxString line : lines) {
        line.Trim(true).Trim(false);

        if (line.IsEmpty() || line.StartsWith("#"))
            continue;

        if (line.StartsWith("[") && line.EndsWith("]")) {
            wxString heading = line.Mid(1, line.length() - 2);
            heading.Trim(true).Trim(false);
            m_sections.push_back(heading);
            section = static_cast<int>(m_sections.size()) - 1;
            continue;
        }

        const int equals = line.Find('=');
        if (equals == wxNOT_FOUND)
            continue;

        SymbolMeaning entry;
        entry.name = line.Left(equals);
        entry.meaning = line.Mid(equals + 1);
        entry.name.Trim(true).Trim(false);
        entry.meaning.Trim(true).Trim(false);
        entry.section = section;
        entry.order = static_cast<int>(m_lines.size());

        if (entry.name.IsEmpty())
            continue;

        const size_t index = m_lines.size();
        const bool pattern = entry.name.Find('*') != wxNOT_FOUND;
        const wxString name = entry.name;
        m_lines.push_back(entry);

        if (pattern)
            m_patterns.push_back(index);
        else if (m_exact.find(name) == m_exact.end())
            m_exact[name] = index;
    }
}

void SymbolMeanings::Clear()
{
    m_sections.clear();
    m_lines.clear();
    m_exact.clear();
    m_patterns.clear();
}

const SymbolMeaning *SymbolMeanings::Find(const wxString &key, bool dutch,
                                          wxString *meaning) const
{
    auto exact = m_exact.find(key);
    if (exact != m_exact.end()) {
        const SymbolMeaning *entry = &m_lines[exact->second];
        *meaning = TextInLanguage(entry->meaning, dutch);
        return entry;
    }

    for (size_t index : m_patterns) {
        const SymbolMeaning &pattern = m_lines[index];
        const int star = pattern.name.Find('*');
        const wxString prefix = pattern.name.Left(star);
        const wxString suffix = pattern.name.Mid(star + 1);

        if (key.length() < prefix.length() + suffix.length() ||
            !key.StartsWith(prefix) || !key.EndsWith(suffix))
            continue;

        const wxString part = key.Mid(
            prefix.length(),
            key.length() - prefix.length() - suffix.length());

        *meaning = TextInLanguage(pattern.meaning, dutch);
        meaning->Replace("*", part);
        return &pattern;
    }

    meaning->Clear();
    return nullptr;
}

wxString TextInLanguage(const wxString &text, bool dutch)
{
    // The bar with or without spaces: a line is trimmed when read, so the
    // space before or after an empty part is gone ("= | Yellow buoy").
    const int bar = text.Find('|');
    if (bar == wxNOT_FOUND)
        return text;

    wxString nl = text.Left(bar);
    wxString en = text.Mid(bar + 1);
    nl.Trim(true).Trim(false);
    en.Trim(true).Trim(false);

    if (dutch)
        return nl.IsEmpty() ? en : nl;
    return en.IsEmpty() ? nl : en;
}
