// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "vhf_areas.h"

#include <algorithm>

#include <wx/arrstr.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/xml/xml.h>

namespace {

// The value after "Key:" on a line of a description, or empty.
wxString DescriptionValue(const wxString &description, const wxString &key)
{
    wxString text = description;
    text.Replace("\r", "\n");
    for (wxString line : wxSplit(text, '\n', '\0')) {
        line.Trim(true).Trim(false);
        if (line.Lower().StartsWith(key.Lower() + ":")) {
            wxString value = line.Mid(key.length() + 1);
            return value.Trim(true).Trim(false);
        }
    }
    return wxString();
}

wxString ChildText(const wxXmlNode *node, const wxString &name)
{
    for (const wxXmlNode *child = node->GetChildren(); child; child = child->GetNext()) {
        if (child->GetName() == name)
            return child->GetNodeContent();
    }
    return wxString();
}

int TypeRank(const wxString &type)
{
    const wxString t = type.Lower();
    if (t == "vts")
        return 0;
    if (t == "lock")
        return 1;
    if (t == "bridge")
        return 2;
    if (t == "marina")
        return 3;
    return 4;
}

// The polygon cut into triangles by clipping ears, so it can be filled in
// OpenGL, which fills only convex shapes. Areas are small, so latitude and
// longitude serve as flat coordinates.
std::vector<int> Triangulate(const std::vector<std::pair<double, double>> &polygon)
{
    std::vector<int> triangles;
    // The outline is closed: the last point repeats the first.
    const int count = static_cast<int>(polygon.size()) - 1;
    if (count < 3)
        return triangles;

    auto x = [&polygon](int i) { return polygon[i].second; };
    auto y = [&polygon](int i) { return polygon[i].first; };
    auto cross = [&](int a, int b, int c) {
        return (x(b) - x(a)) * (y(c) - y(a)) - (y(b) - y(a)) * (x(c) - x(a));
    };

    double area = 0.0;
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        area += x(i) * y(j) - x(j) * y(i);
    }
    std::vector<int> left;
    for (int i = 0; i < count; ++i)
        left.push_back(area > 0 ? i : count - 1 - i); // counter-clockwise

    auto inside = [&](int p, int a, int b, int c) {
        return cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0;
    };

    int guard = 0;
    while (left.size() > 3 && guard < count * count) {
        ++guard;
        bool clipped = false;
        const int n = static_cast<int>(left.size());
        for (int k = 0; k < n; ++k) {
            const int a = left[(k + n - 1) % n];
            const int b = left[k];
            const int c = left[(k + 1) % n];
            if (cross(a, b, c) <= 0)
                continue; // not convex here
            bool ear = true;
            for (int m = 0; m < n && ear; ++m) {
                const int p = left[m];
                if (p != a && p != b && p != c && inside(p, a, b, c))
                    ear = false;
            }
            if (!ear)
                continue;
            triangles.insert(triangles.end(), {a, b, c});
            left.erase(left.begin() + k);
            clipped = true;
            break;
        }
        // An outline that crosses itself has no ears left: a fan for the rest.
        if (!clipped) {
            for (size_t k = 1; k + 1 < left.size(); ++k)
                triangles.insert(triangles.end(), {left[0], left[k], left[k + 1]});
            left.clear();
        }
    }
    if (left.size() == 3)
        triangles.insert(triangles.end(), {left[0], left[1], left[2]});
    return triangles;
}

} // namespace

bool VhfArea::Contains(double lat, double lon) const
{
    if (lat < latMin || lat > latMax || lon < lonMin || lon > lonMax)
        return false;

    // Ray casting along the latitude.
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const double yi = polygon[i].first, xi = polygon[i].second;
        const double yj = polygon[j].first, xj = polygon[j].second;
        if ((yi > lat) != (yj > lat) &&
            lon < (xj - xi) * (lat - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

size_t VhfAreas::Load(const wxString &layersDir)
{
    m_areas.clear();
    m_icons.clear();

    if (!wxDirExists(layersDir))
        return 0;

    wxArrayString files;
    wxDir::GetAllFiles(layersDir, &files, "*.gpx", wxDIR_FILES);

    for (const wxString &path : files) {
        wxXmlDocument document;
        {
            wxLogNull quiet;
            if (!document.Load(path) || !document.GetRoot())
                continue;
        }

        std::vector<VhfArea> found;
        std::set<wxString> icons;

        for (const wxXmlNode *node = document.GetRoot()->GetChildren(); node;
             node = node->GetNext()) {
            if (node->GetName() == "wpt") {
                const wxString icon = ChildText(node, "sym");
                if (!icon.IsEmpty())
                    icons.insert(icon);
                continue;
            }
            if (node->GetName() != "trk")
                continue;

            const wxString description = ChildText(node, "desc");
            VhfArea area;
            area.channel = DescriptionValue(description, "VHF-kanaal");
            if (area.channel.IsEmpty())
                continue;
            area.name = ChildText(node, "name");
            area.callName = DescriptionValue(description, "Oproepnaam");
            area.type = DescriptionValue(description, "Type").Lower();
            area.usage = DescriptionValue(description, "Gebruik").Lower();
            area.phone = DescriptionValue(description, "Telefoon");
            area.description = description;

            for (const wxXmlNode *segment = node->GetChildren(); segment;
                 segment = segment->GetNext()) {
                if (segment->GetName() != "trkseg")
                    continue;
                for (const wxXmlNode *point = segment->GetChildren(); point;
                     point = point->GetNext()) {
                    double lat = 0.0;
                    double lon = 0.0;
                    if (point->GetName() == "trkpt" &&
                        point->GetAttribute("lat").ToCDouble(&lat) &&
                        point->GetAttribute("lon").ToCDouble(&lon))
                        area.polygon.emplace_back(lat, lon);
                }
            }

            // Only closed outlines are areas.
            if (area.polygon.size() < 4 || area.polygon.front() != area.polygon.back())
                continue;

            area.latMin = area.latMax = area.polygon.front().first;
            area.lonMin = area.lonMax = area.polygon.front().second;
            for (const auto &p : area.polygon) {
                area.latMin = std::min(area.latMin, p.first);
                area.latMax = std::max(area.latMax, p.first);
                area.lonMin = std::min(area.lonMin, p.second);
                area.lonMax = std::max(area.lonMax, p.second);
            }
            area.triangles = Triangulate(area.polygon);
            found.push_back(area);
        }

        if (found.empty())
            continue;
        m_areas.insert(m_areas.end(), found.begin(), found.end());
        m_icons.insert(icons.begin(), icons.end());
    }

    return m_areas.size();
}

std::vector<const VhfArea *> VhfAreas::At(double lat, double lon) const
{
    std::vector<const VhfArea *> here;
    for (const VhfArea &area : m_areas) {
        if (area.Contains(lat, lon))
            here.push_back(&area);
    }
    std::stable_sort(here.begin(), here.end(), [](const VhfArea *a, const VhfArea *b) {
        return TypeRank(a->type) < TypeRank(b->type);
    });
    return here;
}

std::vector<const VhfArea *> VhfAreas::AreaOfWaypoint(double lat, double lon,
                                                       const wxString &channel) const
{
    std::vector<const VhfArea *> parts;
    const VhfArea *own = nullptr;
    for (const VhfArea &area : m_areas) {
        if (area.channel == channel && area.Contains(lat, lon)) {
            own = &area;
            break;
        }
    }
    // An icon whose name gives no channel: the area it lies in.
    if (!own) {
        for (const VhfArea &area : m_areas) {
            if (area.Contains(lat, lon)) {
                own = &area;
                break;
            }
        }
    }
    if (!own)
        return parts;
    for (const VhfArea &area : m_areas) {
        if (&area == own || area.description == own->description)
            parts.push_back(&area);
    }
    return parts;
}

std::vector<VhfArea> VhfAreas::PartsOf(const wxString &description) const
{
    std::vector<VhfArea> parts;
    for (const VhfArea &part : m_areas)
        if (part.description == description)
            parts.push_back(part);
    return parts;
}

std::vector<VhfArea> VhfAreas::WithPartsAt(double lat, double lon) const
{
    std::vector<VhfArea> result;
    std::set<const VhfArea *> taken;
    for (const VhfArea *area : At(lat, lon)) {
        for (const VhfArea &part : m_areas) {
            if (part.description == area->description && taken.insert(&part).second)
                result.push_back(part);
        }
    }
    return result;
}

wxString VhfTypeWord(const wxString &type, bool dutch)
{
    if (type == "vts")
        return dutch ? "verkeerspost" : "traffic post";
    if (type == "lock")
        return dutch ? "sluis" : "lock";
    if (type == "bridge")
        return dutch ? "brug" : "bridge";
    if (type == "marina")
        return dutch ? "jachthaven" : "marina";
    if (type == "area")
        return dutch ? "gebied" : "area";
    return type;
}

wxString VhfUsageWord(const wxString &usage, bool dutch)
{
    if (usage == "listen")
        return dutch ? "uitluisteren" : "listen";
    if (usage == "report")
        return dutch ? "melden" : "report";
    if (usage == "announce")
        return dutch ? "aanmelden" : "announce";
    return usage;
}

wxString VhfAreaLine(const VhfArea &area, bool dutch)
{
    wxString line = (dutch ? "Kanaal " : "Channel ") + area.channel;
    const wxString who = area.callName.IsEmpty() ? area.name : area.callName;
    if (!who.IsEmpty())
        line << wxString::FromUTF8(" \u00b7 ") << who;
    if (!area.type.IsEmpty())
        line << wxString::FromUTF8(" \u00b7 ") << VhfTypeWord(area.type, dutch);
    if (!area.usage.IsEmpty())
        line << wxString::FromUTF8(" \u00b7 ") << VhfUsageWord(area.usage, dutch);
    return line;
}
