// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _CHART_OBJECT_H_
#define _CHART_OBJECT_H_

#include <utility>
#include <vector>

#include <wx/string.h>

// One object of a vector chart, independent of where it came from: the
// o-charts plugin or an S-57 file read by this plugin.
struct ChartObject
{
    enum Primitive { Point = 0, Line = 1, Area = 2 };

    wxString className;   // S-57 acronym, e.g. BOYLAT
    int primitive = Point;
    double lat = 0.0;
    double lon = 0.0;
    int scamin = 0;       // 0 = no scale limit
    int cellScale = 0;    // compilation scale of its cell, 0 = unknown

    // Acronym and value as text, in chart order. List values are written
    // as "3,4,3", the way the S-52 lookup tables write them.
    std::vector<std::pair<wxString, wxString>> attributes;

    // Value of an attribute, or nullptr when the object does not have it.
    const wxString *Attribute(const wxString &acronym) const;
};

// What makes two chart objects the same object, for counting an object
// that overlapping cells both hold only once: its class and position, and
// for a light also its character, colour and sector. A sector light is
// several LIGHTS objects at one position, and each sector counts.
wxString ChartObjectKey(const ChartObject &object);

#endif
