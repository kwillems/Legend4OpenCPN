// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of distances and bearings from the boat, and how they are
// written in the legend.

#include "checks.h"
#include "geo.h"

namespace {

void TestDistance()
{
    // One minute of latitude is one sea mile.
    CHECK_NEAR(SeaMiles(52.0, 5.0, 52.0 + 1.0 / 60.0, 5.0), 1.0, 1e-9);
    CHECK_NEAR(SeaMiles(52.0, 5.0, 52.0, 5.0), 0.0, 1e-12);
    // A minute of longitude is shorter away from the equator.
    CHECK_NEAR(SeaMiles(0.0, 5.0, 0.0, 5.0 + 1.0 / 60.0), 1.0, 1e-9);
    CHECK_NEAR(SeaMiles(60.0, 5.0, 60.0, 5.0 + 1.0 / 60.0), 0.5, 1e-3);
    // Both ways the same.
    CHECK_NEAR(SeaMiles(52.1, 5.2, 52.3, 5.6), SeaMiles(52.3, 5.6, 52.1, 5.2), 1e-12);
    // Across the date line: the short way round.
    CHECK_NEAR(SeaMiles(0.0, 179.99, 0.0, -179.99), 1.2, 1e-6);
}

void TestBearing()
{
    const double lat = 52.0;
    const double lon = 5.0;
    const double step = 1.0 / 60.0;
    CHECK_NEAR(BearingDegrees(lat, lon, lat + step, lon), 0.0, 1e-9);   // north
    CHECK_NEAR(BearingDegrees(lat, lon, lat, lon + step), 90.0, 1e-9);  // east
    CHECK_NEAR(BearingDegrees(lat, lon, lat - step, lon), 180.0, 1e-9); // south
    CHECK_NEAR(BearingDegrees(lat, lon, lat, lon - step), 270.0, 1e-9); // west

    // North-east, with the shorter minutes of longitude at this latitude.
    const double east = step / std::cos(lat * 3.14159265358979323846 / 180.0);
    CHECK_NEAR(BearingDegrees(lat, lon, lat + step, lon + east), 45.0, 0.01);
    CHECK_NEAR(BearingDegrees(lat, lon, lat - step, lon - east), 225.0, 0.01);

    // Never 360 or below 0.
    const double justWest = BearingDegrees(lat, lon, lat + step, lon - 1e-9);
    CHECK(justWest >= 0.0 && justWest < 360.0);
    // Across the date line: east, not west.
    CHECK_NEAR(BearingDegrees(0.0, 179.99, 0.0, -179.99), 90.0, 1e-6);
}

void TestFormat()
{
    CHECK_EQ(FormatDistance(0.0), "0 m");
    CHECK_EQ(FormatDistance(0.05), "90 m");     // 92.6 m
    CHECK_EQ(FormatDistance(0.099), "180 m");   // 183.3 m
    CHECK_EQ(FormatDistance(0.1), "0,1 NM");
    CHECK_EQ(FormatDistance(0.34), "0,3 NM");
    CHECK_EQ(FormatDistance(2.06), "2,1 NM");
    CHECK_EQ(FormatDistance(9.94), "9,9 NM");
    CHECK_EQ(FormatDistance(9.96), "10 NM");
    CHECK_EQ(FormatDistance(12.4), "12 NM");

    CHECK_EQ(FormatBearing(0.0), U("000°"));
    CHECK_EQ(FormatBearing(45.2), U("045°"));
    CHECK_EQ(FormatBearing(180.0), U("180°"));
    CHECK_EQ(FormatBearing(359.4), U("359°"));
    CHECK_EQ(FormatBearing(359.6), U("000°"));
}

} // namespace

void TestGeo()
{
    TestDistance();
    TestBearing();
    TestFormat();
}
