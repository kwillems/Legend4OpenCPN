// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "display_settings.h"

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/log.h>

bool DisplaySettings::Refresh(bool force)
{
    if (m_path.IsEmpty() || !wxFileName::FileExists(m_path))
        return false;

    const wxDateTime modified = wxFileName(m_path).GetModificationTime();
    if (!force && m_modified.IsValid() && modified.IsValid() &&
        modified == m_modified)
        return false;
    m_modified = modified;

    // Read only; the file belongs to OpenCPN.
    wxLogNull quiet;
    wxFileConfig config(
        wxEmptyString, wxEmptyString, m_path, wxEmptyString,
        wxCONFIG_USE_LOCAL_FILE);

    // [Settings/GlobalState] nDisplayCategory: the ASCII code of 'D', 'S',
    // 'O' or 'M'.
    long category = 0;
    config.SetPath("/Settings/GlobalState");
    config.Read("nDisplayCategory", &category, static_cast<long>('S'));
    const wxChar newCategory =
        (category == 'D' || category == 'S' || category == 'O' || category == 'M')
            ? static_cast<wxChar>(category)
            : wxChar('S');

    // [Settings/ObjectFilter] vizBOYLAT=0: the mariner's standard list.
    std::map<wxString, bool> visible;
    config.SetPath("/Settings/ObjectFilter");
    wxString name;
    long index = 0;
    for (bool more = config.GetFirstEntry(name, index); more;
         more = config.GetNextEntry(name, index)) {
        if (!name.StartsWith("viz"))
            continue;
        long value = 1;
        config.Read(name, &value, 1L);
        visible[name.Mid(3).Upper()] = value != 0;
    }

    const bool changed = newCategory != m_category || visible != m_classVisible;
    m_category = newCategory;
    m_classVisible.swap(visible);

    if (changed) {
        size_t hidden = 0;
        for (const auto &entry : m_classVisible)
            hidden += entry.second ? 0 : 1;
        wxLogMessage(
            "Legend: OpenCPN display category %c, %zu classes in filter, %zu hidden",
            m_category, m_classVisible.size(), hidden);
    }
    return changed;
}

bool DisplaySettings::Shows(const wxString &className, const wxString &displayCat) const
{
    switch (m_category) {
    case 'D':
        return displayCat == "Displaybase";

    case 'O':
        return true;

    case 'M': {
        // Display base is always drawn; the rest follows the user's list.
        // Classes missing from the list are shown.
        if (displayCat == "Displaybase")
            return true;
        auto found = m_classVisible.find(className.Upper());
        return found == m_classVisible.end() || found->second;
    }

    default:
        return displayCat == "Displaybase" || displayCat == "Standard";
    }
}
