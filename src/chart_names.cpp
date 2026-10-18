// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "chart_names.h"

#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

bool ChartNames::Load(const wxString &path)
{
    if (!wxFileName::FileExists(path))
        return false;

    wxTextFile file;
    if (!file.Open(path, wxConvUTF8))
        return false;

    // Sections: [klassen], [opbouw] or the acronym of an attribute.
    wxString section;

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        wxString line = file[i];

        // Everything after '#' is a comment (the source marks).
        const int hash = line.Find('#');
        if (hash != wxNOT_FOUND)
            line = line.Left(hash);
        line.Trim(true).Trim(false);
        if (line.IsEmpty())
            continue;

        if (line.StartsWith("[") && line.EndsWith("]")) {
            section = line.Mid(1, line.length() - 2);
            section.Trim(true).Trim(false);
            continue;
        }

        const int equals = line.Find('=');
        if (equals == wxNOT_FOUND || section.IsEmpty())
            continue;

        wxString key = line.Left(equals);
        wxString value = line.Mid(equals + 1);
        key.Trim(true).Trim(false);
        value.Trim(true).Trim(false);
        if (key.IsEmpty())
            continue;

        if (section == "klassen") {
            m_classes[key] = value;
        }
        else if (section == "opbouw") {
            std::vector<wxString> attributes;
            wxStringTokenizer words(value, " \t,", wxTOKEN_STRTOK);
            while (words.HasMoreTokens())
                attributes.push_back(words.GetNextToken());
            m_build[key] = attributes;
        }
        else {
            m_values[section][key] = value;
        }
    }

    return true;
}

void ChartNames::Clear()
{
    m_classes.clear();
    m_build.clear();
    m_values.clear();
}

// One value of an attribute; lists ("3,4,3") translated part by part. A
// Dutch text that is missing falls back to the English one, so nothing is
// lost.
wxString ChartNames::ValueText(
    const wxString &acronym,
    const wxString &value,
    const ChartSymbols &catalogue,
    bool dutch) const
{
    // Colours of one object read as one colour scheme: "rood-groen-rood".
    const wxString separator = acronym == "COLOUR" ? "-" : ", ";

    auto translations = m_values.find(acronym);

    wxString result;
    wxStringTokenizer parts(value, ",", wxTOKEN_STRTOK);
    while (parts.HasMoreTokens()) {
        wxString part = parts.GetNextToken();
        part.Trim(true).Trim(false);
        if (part.IsEmpty())
            continue;

        wxString text;
        if (dutch && translations != m_values.end()) {
            auto found = translations->second.find(part);
            if (found != translations->second.end())
                text = found->second;
        }
        if (text.IsEmpty())
            text = catalogue.AttributeValueMeaning(acronym, part);
        if (text.IsEmpty())
            continue;

        if (!result.IsEmpty())
            result << separator;
        result << text;
    }
    return result;
}

wxString ChartNames::Compose(
    const ChartObject &obj,
    const ChartSymbols &catalogue,
    bool dutch) const
{
    wxString name;
    if (dutch) {
        auto found = m_classes.find(obj.className);
        if (found != m_classes.end())
            name = found->second;
    }
    if (name.IsEmpty())
        name = catalogue.ClassDescription(obj.className);
    if (name.IsEmpty())
        return wxString();

    wxString details;
    auto build = m_build.find(obj.className);
    if (build != m_build.end()) {
        for (const wxString &acronym : build->second) {
            const wxString *value = obj.Attribute(acronym);
            if (!value || value->IsEmpty())
                continue;
            const wxString text = ValueText(acronym, *value, catalogue, dutch);
            if (text.IsEmpty())
                continue;
            if (!details.IsEmpty())
                details << ", ";
            details << text;
        }
    }

    return details.IsEmpty() ? name : name + ": " + details;
}
