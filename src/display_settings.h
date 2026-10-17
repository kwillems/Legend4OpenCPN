// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _DISPLAY_SETTINGS_H_
#define _DISPLAY_SETTINGS_H_

#include <map>

#include <wx/datetime.h>
#include <wx/string.h>

// The vector chart display settings of OpenCPN that decide which objects
// the chart draws: the display category (base, standard, all, mariner's
// standard) and, for mariner's standard, the list of object classes the
// user switched on. The plugin API does not expose them; OpenCPN writes
// them to its configuration file, which is read here.
class DisplaySettings
{
public:
    void SetFile(const wxString &path) { m_path = path; }

    // Reads the file again when it changed (or when forced). True when the
    // settings that matter here are different from before.
    bool Refresh(bool force = false);

    // Whether the chart shows objects of this class and S-52 display
    // category ("Displaybase", "Standard", "Other", "Mariners").
    bool Shows(const wxString &className, const wxString &displayCat) const;

    // The category as OpenCPN stores it: 'D', 'S', 'O' or 'M'.
    wxChar Category() const { return m_category; }

private:
    wxString m_path;
    wxDateTime m_modified;
    wxChar m_category = 'S';
    std::map<wxString, bool> m_classVisible; // upper-case acronym -> shown
};

#endif
