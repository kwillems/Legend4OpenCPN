// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _CHART_NAMES_H_
#define _CHART_NAMES_H_

#include "chart_symbols.h"

#include <map>
#include <vector>

#include <wx/string.h>

// Names of chart objects built from their class and a few attributes, for
// instance "Laterale boei: bakboord, rood, stompe ton" or, in English,
// "Buoy, lateral: port-hand lateral mark, red, can (cylindrical)".
//
// The English texts come from OpenCPN's S-57 catalogue; the Dutch ones and
// the choice of attributes per class come from s57-nl.txt (shipped with the
// plugin, extended or corrected by a file of the same name in the user's
// legend folder).
class ChartNames
{
public:
    // Adds the lines of a file; later files override earlier lines.
    bool Load(const wxString &path);
    void Clear();
    bool IsLoaded() const { return !m_classes.empty(); }

    // The name of an object; empty when its class is unknown.
    wxString Compose(
        const ChartObject &obj,
        const ChartSymbols &catalogue,
        bool dutch) const;

private:
    wxString ValueText(
        const wxString &acronym,
        const wxString &value,
        const ChartSymbols &catalogue,
        bool dutch) const;

    std::map<wxString, wxString> m_classes;               // class -> name
    std::map<wxString, std::vector<wxString>> m_build;    // class -> attributes
    std::map<wxString, std::map<wxString, wxString>> m_values; // attr -> value -> text
};

#endif
