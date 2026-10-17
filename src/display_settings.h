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

#include "ocpn_plugin.h"

#include <map>

#include <wx/datetime.h>
#include <wx/string.h>

// The vector chart display settings of OpenCPN that decide which objects
// the chart draws: the display category (base, standard, all, mariner's
// standard), the list of object classes the user switched on, and the use
// of scale limits.
//
// The live answer comes from OpenCPN's presentation library, asked the way
// chart plugins ask it for every object they draw: a probe object of the
// class is given a context and checked for rendering. OpenCPN's
// configuration file is the fallback; it is written only now and then.
class DisplaySettings
{
public:
    void SetFile(const wxString &path) { m_path = path; }

    // Whether OpenCPN would draw a point object of this class with this
    // scale limit in the view, according to its current settings. answered
    // is false when the presentation library gave no answer; use Shows()
    // then.
    bool ChartShows(
        const wxString &className,
        int scamin,
        const PlugIn_ViewPort &vp,
        bool *answered) const;

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
