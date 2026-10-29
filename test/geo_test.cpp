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

    CHECK_EQ(FormatMiles(2.0), "2 NM");
    CHECK_EQ(FormatMiles(0.5), "0,5 NM");
    CHECK_EQ(FormatMiles(12.5), "12,5 NM");
    CHECK_EQ(FormatMiles(20.2), "20 NM");

    CHECK_EQ(FormatDuration(0.2, true), "1 min");
    CHECK_EQ(FormatDuration(7.4, true), "7 min");
    CHECK_EQ(FormatDuration(9.6, true), "10 min");
    CHECK_EQ(FormatDuration(23.0, true), "25 min");
    CHECK_EQ(FormatDuration(58.0, true), "1 u");
    CHECK_EQ(FormatDuration(64.0, true), "1 u 05");
    CHECK_EQ(FormatDuration(100.0, false), "1 h 40");
    CHECK_EQ(FormatDuration(120.0, false), "2 h");

    CHECK_EQ(FormatBearing(0.0), U("000°"));
    CHECK_EQ(FormatBearing(45.2), U("045°"));
    CHECK_EQ(FormatBearing(180.0), U("180°"));
    CHECK_EQ(FormatBearing(359.4), U("359°"));
    CHECK_EQ(FormatBearing(359.6), U("000°"));
}

void TestAlongAndAcross()
{
    const double lat = 52.6;
    const double lon = 5.5;
    double along = 0.0;
    double across = 0.0;

    // Sailing north: a point one mile north is ahead, one mile east is to
    // starboard.
    AlongAndAcross(lat, lon, 0.0, lat + 1.0 / 60.0, lon, &along, &across);
    CHECK_NEAR(along, 1.0, 1e-9);
    CHECK_NEAR(across, 0.0, 1e-9);
    double east = 0.0;
    double ignored = 0.0;
    PositionAt(lat, lon, 90.0, 1.0, 0.0, &ignored, &east);
    AlongAndAcross(lat, lon, 0.0, lat, east, &along, &across);
    CHECK_NEAR(along, 0.0, 1e-9);
    CHECK_NEAR(across, 1.0, 1e-6);

    // Sailing south (188°, as on the IJsselmeer test): west is starboard.
    AlongAndAcross(lat, lon, 188.0, lat - 1.5 / 60.0, lon - 0.012, &along, &across);
    CHECK(along > 1.4 && along < 1.6);
    CHECK(across > 0.0);

    // There and back again.
    for (double course : {0.0, 45.0, 188.0, 300.0}) {
        double at = 0.0;
        double ao = 0.0;
        PositionAt(lat, lon, course, 2.5, -0.7, &at, &ao);
        AlongAndAcross(lat, lon, course, at, ao, &along, &across);
        CHECK_NEAR(along, 2.5, 1e-9);
        CHECK_NEAR(across, -0.7, 1e-9);
    }
}

void TestFan()
{
    // Near the boat at least 0.12 mile wide on either side.
    CHECK(InFan(0.1, 0.11, 5.0, 15.0, 0.12));
    CHECK(InFan(0.1, -0.11, 5.0, 15.0, 0.12));
    CHECK(!InFan(0.1, 0.13, 5.0, 15.0, 0.12));
    // The circle round the boat: beside it and just passed count too.
    CHECK(InFan(0.0, 0.0, 5.0, 15.0, 0.12));
    CHECK(InFan(0.0, 0.11, 5.0, 15.0, 0.12));
    CHECK(InFan(-0.1, -0.05, 5.0, 15.0, 0.12));
    CHECK(!InFan(-0.1, -0.1, 5.0, 15.0, 0.12));
    // Not further behind the boat, not beyond the range.
    CHECK(!InFan(-1.0, 0.0, 5.0, 15.0, 0.12));
    CHECK(!InFan(5.1, 0.0, 5.0, 15.0, 0.12));
    // Further away the angle decides: a buoy 26° off the bow, as on the
    // test with ShipDriver, falls outside 15° and inside 30°.
    CHECK(!InFan(1.54, 0.53, 5.0, 15.0, 0.12));
    CHECK(InFan(1.54, 0.53, 5.0, 30.0, 0.12));
    CHECK(InFan(1.54, -0.53, 5.0, 30.0, 0.12));
    CHECK(InFan(1.0, 0.99, 5.0, 45.0, 0.12));
    // The far end is round: as far from the boat in every direction.
    CHECK(InFan(1.4, 1.3, 2.0, 45.0, 0.12));    // 1.91 mile off
    CHECK(!InFan(1.9, 1.8, 2.0, 45.0, 0.12));   // 2.62 mile off
    CHECK(InFan(1.99, 0.0, 2.0, 45.0, 0.12));

    // The outline: four corners when it widens inside its range, between
    // them 25 points of the arc at the far end, from 30° to port round to
    // 30° to starboard, then 23 points of the half circle behind the boat.
    const double kDeg = 3.14159265358979323846 / 180.0;
    const double lat = 52.6;
    const double lon = 5.5;
    auto outline = FanOutline(lat, lon, 0.0, 4.0, 30.0, 0.12);
    CHECK(outline.size() == 4 + 25 + 23);
    double along = 0.0;
    double across = 0.0;
    if (outline.size() == 4 + 25 + 23) {
        AlongAndAcross(lat, lon, 0.0, outline[0].first, outline[0].second, &along, &across);
        CHECK_NEAR(along, 0.0, 1e-9);
        CHECK_NEAR(across, -0.12, 1e-9);
        AlongAndAcross(lat, lon, 0.0, outline[2].first, outline[2].second, &along, &across);
        CHECK_NEAR(along, 4.0 * std::cos(30.0 * kDeg), 1e-9);
        CHECK_NEAR(across, -4.0 * std::sin(30.0 * kDeg), 1e-9);
        AlongAndAcross(lat, lon, 0.0, outline[14].first, outline[14].second, &along, &across);
        CHECK_NEAR(along, 4.0, 1e-9);
        CHECK_NEAR(across, 0.0, 1e-9);
        for (size_t i = 2; i <= 26; ++i) {
            AlongAndAcross(lat, lon, 0.0, outline[i].first, outline[i].second, &along, &across);
            CHECK_NEAR(std::sqrt(along * along + across * across), 4.0, 1e-6);
        }
    }
    // Every point of the half circle lies behind the boat, 0.12 mile off.
    for (size_t i = 4 + 25; i < outline.size(); ++i) {
        AlongAndAcross(lat, lon, 0.0, outline[i].first, outline[i].second, &along, &across);
        CHECK(along < 0.0);
        CHECK_NEAR(std::sqrt(along * along + across * across), 0.12, 1e-6);
    }
    // Too short to widen: a strip, its two corners at the boat, the arc
    // where the strip meets it and the half circle.
    auto strip = FanOutline(lat, lon, 0.0, 0.2, 15.0, 0.12);
    CHECK(strip.size() == 2 + 25 + 23);
    if (strip.size() == 2 + 25 + 23) {
        AlongAndAcross(lat, lon, 0.0, strip[1].first, strip[1].second, &along, &across);
        CHECK_NEAR(across, -0.12, 1e-6);
        CHECK_NEAR(along, std::sqrt(0.2 * 0.2 - 0.12 * 0.12), 1e-6);
    }
}

} // namespace

void TestGeo()
{
    TestDistance();
    TestBearing();
    TestFormat();
    TestAlongAndAcross();
    TestFan();
}
