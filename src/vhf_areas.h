// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _VHF_AREAS_H_
#define _VHF_AREAS_H_

#include <set>
#include <utility>
#include <vector>

#include <wx/string.h>

// One VHF area of a layer: a closed track whose description holds the
// channel, as in the VHFinfo Nederland layer of VHF2OpenCPN:
//   Oproepnaam: Volkeraksluizen Zuid
//   Type: vts
//   VHF-kanaal: 78
//   Gebruik: announce
//   Telefoon: +31887970800
struct VhfArea
{
    wxString name;      // name of the track, "VHF 78 - Volkeraksluizen"
    wxString callName;  // Oproepnaam
    wxString type;      // vts, lock, bridge, marina, area
    wxString channel;   // VHF-kanaal
    wxString usage;     // listen, report, announce
    wxString phone;     // Telefoon
    wxString description; // the whole description; the parts of one area share it
    std::vector<std::pair<double, double>> polygon; // lat, lon, closed
    std::vector<int> triangles; // the polygon cut into triangles, three indexes each
    double latMin = 0.0, latMax = 0.0, lonMin = 0.0, lonMax = 0.0;

    bool Contains(double lat, double lon) const;
};

class VhfAreas
{
public:
    // Reads the areas of every GPX file in a folder of layers. Returns the
    // number of areas found.
    size_t Load(const wxString &layersDir);

    bool IsEmpty() const { return m_areas.empty(); }
    const std::vector<VhfArea> &All() const { return m_areas; }

    // The areas a position lies in: traffic posts first, then locks,
    // bridges, marinas and general areas.
    std::vector<const VhfArea *> At(double lat, double lon) const;

    // Waypoint icons of the layers the areas come from, to tell whether
    // such a layer is shown in OpenCPN.
    const std::set<wxString> &Icons() const { return m_icons; }

    // The area of a waypoint of the layer: the one it lies in with its
    // channel, together with the other parts of that area (an area can be
    // drawn as several outlines with the same description).
    std::vector<const VhfArea *> AreaOfWaypoint(double lat, double lon,
                                               const wxString &channel) const;

    // Every part of the area with this description.
    std::vector<VhfArea> PartsOf(const wxString &description) const;

    // The areas a position lies in, with every other part of each of them.
    std::vector<VhfArea> WithPartsAt(double lat, double lon) const;

private:
    std::vector<VhfArea> m_areas;
    std::set<wxString> m_icons;
};

// "verkeerspost", "sluis", ... and "uitluisteren", "melden", ...
wxString VhfTypeWord(const wxString &type, bool dutch);
wxString VhfUsageWord(const wxString &usage, bool dutch);

// One line about an area: "Kanaal 18 · Ketelbrug · verkeerspost · melden".
wxString VhfAreaLine(const VhfArea &area, bool dutch);

#endif
