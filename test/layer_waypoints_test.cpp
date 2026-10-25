// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of reading the waypoints of the layer files: what is read of
// a waypoint, which files are skipped, and that a file is read again only
// when it changed.

#include "checks.h"
#include "layer_waypoints.h"

#include <wx/datetime.h>

namespace {

wxString Gpx(const wxString &content)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<gpx version=\"1.1\" creator=\"test\" "
           "xmlns:opencpn=\"http://www.opencpn.org\">\n" +
           content + "</gpx>\n";
}

const LayerFile *FileNamed(const LayerWaypoints &layers, const wxString &name)
{
    for (const LayerFile &file : layers.Files()) {
        if (wxFileName(file.path).GetFullName() == name)
            return &file;
    }
    return nullptr;
}

const wxString kBuoys =
    "  <wpt lat=\"51.712345\" lon=\"4.412345\">\n"
    "    <name>HD 12</name>\n"
    "    <desc>Groene spitse ton\nLichtkarakter: Fl.G.5s</desc>\n"
    "    <sym>011</sym>\n"
    "    <extensions>\n"
    "      <opencpn:scale_min_max UseScale=\"true\" ScaleMin=\"50000\" ScaleMax=\"0\"/>\n"
    "    </extensions>\n"
    "  </wpt>\n"
    "  <wpt lat=\"-33.5\" lon=\"-70.25\">\n"
    "    <sym>012</sym>\n"
    "    <extensions>\n"
    "      <opencpn:scale_min_max UseScale=\"false\" ScaleMin=\"25000\" ScaleMax=\"0\"/>\n"
    "    </extensions>\n"
    "  </wpt>\n"
    "  <wpt lat=\"52\" lon=\"5\"/>\n";

// ---------------------------------------------------------------------------

void TestRead()
{
    TempFolder folder;
    folder.Write("betonning.gpx", Gpx(kBuoys));
    // Only a track: no waypoints, so not kept.
    folder.Write("route.gpx", Gpx("  <trk><name>Route</name></trk>\n"));
    // Not XML, a waypoint without a position, and a file that is not GPX.
    folder.Write("kapot.gpx", "<gpx><wpt lat=");
    folder.Write("zonder-positie.gpx",
                 Gpx("  <wpt lat=\"52\" lon=\"5\"><sym>a</sym></wpt>\n"
                     "  <wpt lat=\"noord\" lon=\"5\"><sym>b</sym></wpt>\n"));
    folder.Write("betonning.txt", Gpx(kBuoys));

    LayerWaypoints layers;
    layers.Load(folder.Path());
    CHECK(layers.Files().size() == 1);

    const LayerFile *file = FileNamed(layers, "betonning.gpx");
    CHECK(file && file->points.size() == 3);
    if (!file || file->points.size() != 3)
        return;
    CHECK(file->modified.IsValid());

    const LayerWaypoint &first = file->points[0];
    CHECK_NEAR(first.lat, 51.712345, 1e-9);
    CHECK_NEAR(first.lon, 4.412345, 1e-9);
    CHECK_EQ(first.icon, "011");
    CHECK_EQ(first.name, "HD 12");
    CHECK_EQ(first.description, "Groene spitse ton\nLichtkarakter: Fl.G.5s");
    CHECK(first.useScamin);
    CHECK_NEAR(first.scamin, 50000, 1e-9);

    // South and west; a scale limit that is switched off.
    const LayerWaypoint &second = file->points[1];
    CHECK_NEAR(second.lat, -33.5, 1e-9);
    CHECK_NEAR(second.lon, -70.25, 1e-9);
    CHECK_EQ(second.icon, "012");
    CHECK(second.name.IsEmpty());
    CHECK(!second.useScamin);
    CHECK_NEAR(second.scamin, 25000, 1e-9);

    // Only a position.
    const LayerWaypoint &third = file->points[2];
    CHECK(third.icon.IsEmpty());
    CHECK(!third.useScamin);
    CHECK_NEAR(third.scamin, 0.0, 1e-9);
}

void TestReadAgain()
{
    TempFolder folder;
    const wxString path = folder.Write("betonning.gpx", Gpx(kBuoys));
    const wxDateTime before(1, wxDateTime::Jan, 2026, 12, 0, 0);
    wxFileName(path).SetTimes(nullptr, &before, nullptr);

    LayerWaypoints layers;
    layers.Load(folder.Path());
    CHECK(layers.Files().size() == 1);

    // Another content with the same time: the file is not read again.
    folder.Write("betonning.gpx", Gpx("  <wpt lat=\"52\" lon=\"5\"><sym>x</sym></wpt>\n"));
    wxFileName(path).SetTimes(nullptr, &before, nullptr);
    layers.Load(folder.Path());
    CHECK(layers.Files().size() == 1 && layers.Files()[0].points.size() == 3);

    // A later time: read again.
    const wxDateTime after(2, wxDateTime::Jan, 2026, 12, 0, 0);
    wxFileName(path).SetTimes(nullptr, &after, nullptr);
    layers.Load(folder.Path());
    CHECK(layers.Files().size() == 1 && layers.Files()[0].points.size() == 1);
    if (!layers.Files().empty() && !layers.Files()[0].points.empty())
        CHECK_EQ(layers.Files()[0].points[0].icon, "x");

    // A new file is read, a removed one is gone.
    folder.Write("havens.gpx", Gpx("  <wpt lat=\"53\" lon=\"6\"><sym>haven</sym></wpt>\n"));
    layers.Load(folder.Path());
    CHECK(layers.Files().size() == 2);
    wxRemoveFile(path);
    layers.Load(folder.Path());
    CHECK(layers.Files().size() == 1 && FileNamed(layers, "havens.gpx") != nullptr);

    // A folder that is not there: no files.
    layers.Load(folder.Path() + "-niet");
    CHECK(layers.Files().empty());
}

} // namespace

void TestLayerWaypoints()
{
    TestRead();
    TestReadAgain();
}
