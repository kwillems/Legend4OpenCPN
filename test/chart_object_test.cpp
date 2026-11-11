// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of when two chart objects count as one: the same object in
// overlapping cells once, every sector of a sector light on its own.

#include "checks.h"
#include "chart_object.h"

#include <set>

namespace {

ChartObject Light(const char *colour, const char *from, const char *to)
{
    ChartObject light;
    light.className = "LIGHTS";
    light.lat = 52.4632;
    light.lon = 4.5541;
    light.attributes = {{"COLOUR", colour}, {"LITCHR", "7"}, {"SIGGRP", "(1)"},
                        {"SIGPER", "4"},    {"SECTR1", from}, {"SECTR2", to}};
    return light;
}

void TestSectorLights()
{
    // A white, red and green sector at one position: three lights.
    std::set<wxString> keys;
    keys.insert(ChartObjectKey(Light("1", "30", "120")));
    keys.insert(ChartObjectKey(Light("3", "120", "200")));
    keys.insert(ChartObjectKey(Light("4", "200", "30")));
    CHECK(keys.size() == 3);

    // The same sector from an overlapping cell: once.
    CHECK(!keys.insert(ChartObjectKey(Light("3", "120", "200"))).second);

    // Same colour, other sector: a light of its own.
    CHECK(keys.insert(ChartObjectKey(Light("1", "250", "280"))).second);
}

void TestOtherObjects()
{
    ChartObject buoy;
    buoy.className = "BOYLAT";
    buoy.lat = 52.4632;
    buoy.lon = 4.5541;
    buoy.attributes = {{"COLOUR", "3"}};

    // Another buoy at the same position, whatever its attributes, is the
    // same buoy from another cell.
    ChartObject other = buoy;
    other.attributes = {{"COLOUR", "4"}};
    CHECK_EQ(ChartObjectKey(other), ChartObjectKey(buoy));

    // Another class at the same position is another object.
    other.className = "BCNLAT";
    CHECK(ChartObjectKey(other) != ChartObjectKey(buoy));

    // A few metres away is another object as well.
    other = buoy;
    other.lat += 0.0001;
    CHECK(ChartObjectKey(other) != ChartObjectKey(buoy));
}

}  // namespace

void TestChartObjects()
{
    TestSectorLights();
    TestOtherObjects();
}
