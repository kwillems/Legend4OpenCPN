// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of the VHF areas: reading them from the GPX files of a layers
// folder, which areas a position lies in, the parts of one area, cutting
// them into triangles, and the line about an area in a notice.

#include "checks.h"
#include "vhf_areas.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace {

using Outline = std::vector<std::pair<double, double>>; // lat, lon

// A closed outline from its corners.
Outline Closed(Outline corners)
{
    corners.push_back(corners.front());
    return corners;
}

// A rectangle, counter-clockwise.
Outline Box(double latMin, double lonMin, double latMax, double lonMax)
{
    return Closed({{latMin, lonMin}, {latMin, lonMax}, {latMax, lonMax}, {latMax, lonMin}});
}

// A U lying on its side: a base along latitude 0 to 1 with two arms up to
// latitude 3, and a notch between them (lon 1 to 2). Seven square units.
Outline UShape()
{
    return Closed({{0, 0}, {0, 3}, {3, 3}, {3, 2}, {1, 2}, {1, 1}, {3, 1}, {3, 0}});
}

wxString Track(const wxString &name, const wxString &description, const Outline &points)
{
    wxString xml = "  <trk>\n    <name>" + name + "</name>\n    <desc>" + description +
                   "</desc>\n    <trkseg>\n";
    for (const auto &p : points)
        xml << wxString::Format("      <trkpt lat=\"%s\" lon=\"%s\"/>\n",
                                wxString::FromCDouble(p.first, 6),
                                wxString::FromCDouble(p.second, 6));
    xml << "    </trkseg>\n  </trk>\n";
    return xml;
}

wxString Waypoint(double lat, double lon, const wxString &icon)
{
    return wxString::Format("  <wpt lat=\"%s\" lon=\"%s\"><sym>%s</sym></wpt>\n",
                            wxString::FromCDouble(lat, 6), wxString::FromCDouble(lon, 6), icon);
}

wxString Gpx(const wxString &content)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<gpx version=\"1.1\" creator=\"test\">\n" +
           content + "</gpx>\n";
}

wxString Description(const wxString &callName, const wxString &type, const wxString &channel,
                     const wxString &usage)
{
    return "Oproepnaam: " + callName + "\nType: " + type + "\nVHF-kanaal: " + channel +
           "\nGebruik: " + usage + "\nTelefoon: +31887970800";
}

double TriangleArea(const Outline &polygon, int a, int b, int c)
{
    const double cross = (polygon[b].second - polygon[a].second) *
                             (polygon[c].first - polygon[a].first) -
                         (polygon[b].first - polygon[a].first) *
                             (polygon[c].second - polygon[a].second);
    return std::fabs(cross) / 2.0;
}

// The triangles of an area cover it exactly: as many as corners less two,
// together as large as the area, each one inside it.
void CheckTriangles(const VhfArea &area, double expectedArea)
{
    const int corners = static_cast<int>(area.polygon.size()) - 1;
    CHECK(static_cast<int>(area.triangles.size()) == 3 * (corners - 2));
    double total = 0.0;
    for (size_t i = 0; i + 2 < area.triangles.size(); i += 3) {
        const int a = area.triangles[i];
        const int b = area.triangles[i + 1];
        const int c = area.triangles[i + 2];
        CHECK(a >= 0 && a < corners && b >= 0 && b < corners && c >= 0 && c < corners);
        total += TriangleArea(area.polygon, a, b, c);
        const double lat = (area.polygon[a].first + area.polygon[b].first +
                            area.polygon[c].first) / 3.0;
        const double lon = (area.polygon[a].second + area.polygon[b].second +
                            area.polygon[c].second) / 3.0;
        CHECK(area.Contains(lat, lon));
    }
    CHECK_NEAR(total, expectedArea, 1e-6);
}

// ---------------------------------------------------------------------------

void TestContains()
{
    VhfArea area;
    area.polygon = UShape();
    area.latMin = 0;
    area.latMax = 3;
    area.lonMin = 0;
    area.lonMax = 3;

    CHECK(area.Contains(0.5, 1.5));   // the base
    CHECK(area.Contains(2.0, 0.5));   // the left arm
    CHECK(area.Contains(2.0, 2.5));   // the right arm
    CHECK(!area.Contains(2.0, 1.5));  // the notch between the arms
    CHECK(!area.Contains(-0.5, 1.5)); // below
    CHECK(!area.Contains(1.5, 3.5));  // beside
    CHECK(!area.Contains(4.0, 4.0));  // outside the bounding box
}

void TestLoad()
{
    TempFolder folder;

    // The lock comes first in the file; at a place in both, the traffic post
    // must still come first.
    const wxString post = Description("Volkeraksluizen Zuid", "VTS", "78", "announce");
    const wxString river = Description("Hollands Diep", "area", "10", "listen");
    wxString vhf;
    vhf << Waypoint(51.71, 4.41, "vhf_vts_78") << Waypoint(51.61, 4.41, "vhf_unknown");
    // Lower-case key: also read.
    vhf << Track("VHF 18 - Sluis Noord",
                 "Oproepnaam: Sluis Noord\nType: lock\nvhf-kanaal: 18\nGebruik: report",
                 Box(51.71, 4.42, 51.73, 4.46));
    vhf << Track("VHF 78 - Volkeraksluizen", post, Box(51.70, 4.40, 51.72, 4.44));
    // One area drawn as two outlines with the same description.
    vhf << Track("VHF 10 - Hollands Diep", river, Box(51.60, 4.40, 51.62, 4.42));
    vhf << Track("VHF 10 - Hollands Diep", river, Box(51.60, 4.50, 51.62, 4.52));
    // Not areas: no channel, an open outline, too few points.
    vhf << Track("Vaarroute", "Gewoon een route", Box(51.50, 4.40, 51.52, 4.42));
    vhf << Track("VHF 20 - Open", Description("Open", "area", "20", "listen"),
                 {{51.40, 4.40}, {51.40, 4.42}, {51.42, 4.42}});
    vhf << Track("VHF 21 - Kort", Description("Kort", "area", "21", "listen"),
                 {{51.30, 4.40}, {51.30, 4.42}, {51.30, 4.40}});
    folder.Write("vhfinfo.gpx", Gpx(vhf));

    // A layer without areas: its waypoint icons do not count.
    folder.Write("havens.gpx", Gpx(Waypoint(51.0, 4.0, "harbour") +
                                   Track("Route", "Geen kanaal", Box(51.0, 4.0, 51.1, 4.1))));
    // Not XML at all, and an area in a file that is not GPX.
    folder.Write("kapot.gpx", "<gpx><trk><name>half");
    folder.Write("gebied.txt", Gpx(Track("VHF 30 - Txt", Description("Txt", "area", "30",
                                                                       "listen"),
                                         Box(52.0, 5.0, 52.1, 5.1))));

    VhfAreas areas;
    CHECK(areas.Load(folder.Path()) == 4);
    CHECK(areas.All().size() == 4);
    CHECK(!areas.IsEmpty());

    CHECK(areas.Icons().count("vhf_vts_78") == 1);
    CHECK(areas.Icons().count("vhf_unknown") == 1);
    CHECK(areas.Icons().count("harbour") == 0);

    // The fields of the traffic post, its type in lower case.
    const VhfArea *found = nullptr;
    for (const VhfArea &area : areas.All()) {
        if (area.channel == "78")
            found = &area;
    }
    CHECK(found != nullptr);
    if (found) {
        CHECK_EQ(found->name, "VHF 78 - Volkeraksluizen");
        CHECK_EQ(found->callName, "Volkeraksluizen Zuid");
        CHECK_EQ(found->type, "vts");
        CHECK_EQ(found->usage, "announce");
        CHECK_EQ(found->phone, "+31887970800");
        CHECK_NEAR(found->latMin, 51.70, 1e-9);
        CHECK_NEAR(found->latMax, 51.72, 1e-9);
        CHECK_NEAR(found->lonMin, 4.40, 1e-9);
        CHECK_NEAR(found->lonMax, 4.44, 1e-9);
        CheckTriangles(*found, 0.02 * 0.04);
    }

    // Where the lock and the post overlap: the post first.
    std::vector<const VhfArea *> here = areas.At(51.715, 4.43);
    CHECK(here.size() == 2);
    if (here.size() == 2) {
        CHECK_EQ(here[0]->channel, "78");
        CHECK_EQ(here[1]->channel, "18");
    }
    here = areas.At(51.705, 4.41);
    CHECK(here.size() == 1 && here[0]->channel == "78");
    CHECK(areas.At(51.80, 4.41).empty());

    // Both parts of the river area.
    const wxString riverDescription = areas.At(51.61, 4.41).front()->description;
    CHECK(areas.PartsOf(riverDescription).size() == 2);
    CHECK(areas.WithPartsAt(51.61, 4.41).size() == 2);
    CHECK(areas.WithPartsAt(51.715, 4.43).size() == 2); // post and lock
    CHECK(areas.WithPartsAt(51.80, 4.41).empty());

    // The area of a waypoint: by its channel, even where an area listed
    // earlier lies over it.
    std::vector<const VhfArea *> own = areas.AreaOfWaypoint(51.715, 4.43, "78");
    CHECK(own.size() == 1 && own[0]->channel == "78");
    own = areas.AreaOfWaypoint(51.715, 4.43, "18");
    CHECK(own.size() == 1 && own[0]->channel == "18");
    // No channel in the icon name: the area it lies in, with all its parts.
    own = areas.AreaOfWaypoint(51.61, 4.41, "");
    CHECK(own.size() == 2);
    CHECK(areas.AreaOfWaypoint(51.90, 4.00, "78").empty());

    // A folder that is not there: nothing, and nothing left from before.
    CHECK(areas.Load(folder.Path() + "-niet") == 0);
    CHECK(areas.IsEmpty());
    CHECK(areas.Icons().empty());
}

void TestTriangles()
{
    TempFolder folder;
    const wxString description = Description("U", "area", "40", "listen");
    Outline reversed = UShape();
    std::reverse(reversed.begin(), reversed.end());
    folder.Write("u.gpx", Gpx(Track("VHF 40 - U", description, UShape()) +
                              Track("VHF 40 - U omgekeerd", description, reversed)));

    VhfAreas areas;
    CHECK(areas.Load(folder.Path()) == 2);
    // Counter-clockwise and clockwise: both cut into six triangles inside
    // the U, none in its notch.
    for (const VhfArea &area : areas.All())
        CheckTriangles(area, 7.0);
}

void TestAreaLine()
{
    VhfArea area;
    area.name = "VHF 78 - Volkeraksluizen";
    area.callName = "Volkeraksluizen Zuid";
    area.channel = "78";
    area.type = "vts";
    area.usage = "announce";
    CHECK_EQ(VhfAreaLine(area, true),
             U("Kanaal 78 · Volkeraksluizen Zuid · verkeerspost · aanmelden"));
    CHECK_EQ(VhfAreaLine(area, false),
             U("Channel 78 · Volkeraksluizen Zuid · traffic post · announce"));

    // Without a call name the name of the track; without type and usage
    // only the channel and the name.
    area.callName.Clear();
    area.type.Clear();
    area.usage.Clear();
    CHECK_EQ(VhfAreaLine(area, true), U("Kanaal 78 · VHF 78 - Volkeraksluizen"));

    CHECK_EQ(VhfTypeWord("lock", true), "sluis");
    CHECK_EQ(VhfTypeWord("bridge", false), "bridge");
    CHECK_EQ(VhfTypeWord("marina", true), "jachthaven");
    CHECK_EQ(VhfTypeWord("ferry", true), "ferry"); // unknown: as it is
    CHECK_EQ(VhfUsageWord("listen", true), "uitluisteren");
    CHECK_EQ(VhfUsageWord("report", true), "melden");
    CHECK_EQ(VhfUsageWord("call", false), "call");
}

} // namespace

void TestVhfAreas()
{
    TestContains();
    TestLoad();
    TestTriangles();
    TestAreaLine();
}
