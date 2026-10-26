// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "geo.h"

#include <algorithm>
#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;

// The second position seen from the first, in sea miles east and north.
void Offset(double lat1, double lon1, double lat2, double lon2, double *east,
            double *north)
{
    double dlon = lon2 - lon1;
    while (dlon > 180.0)
        dlon -= 360.0;
    while (dlon < -180.0)
        dlon += 360.0;
    *north = (lat2 - lat1) * 60.0;
    *east = dlon * 60.0 * std::cos((lat1 + lat2) / 2.0 * kPi / 180.0);
}

} // namespace

double SeaMiles(double lat1, double lon1, double lat2, double lon2)
{
    double east = 0.0;
    double north = 0.0;
    Offset(lat1, lon1, lat2, lon2, &east, &north);
    return std::sqrt(east * east + north * north);
}

double BearingDegrees(double lat1, double lon1, double lat2, double lon2)
{
    double east = 0.0;
    double north = 0.0;
    Offset(lat1, lon1, lat2, lon2, &east, &north);
    double bearing = std::atan2(east, north) * 180.0 / kPi;
    if (bearing < 0.0)
        bearing += 360.0;
    return bearing;
}

wxString FormatDistance(double seaMiles)
{
    if (seaMiles < 0.1) {
        const long metres = std::lround(seaMiles * 1852.0 / 10.0) * 10;
        return wxString::Format("%ld m", metres);
    }
    wxString text = seaMiles < 9.95 ? wxString::Format("%.1f NM", seaMiles)
                                    : wxString::Format("%.0f NM", seaMiles);
    text.Replace(".", ",");
    return text;
}

void AlongAndAcross(double lat0, double lon0, double course, double lat, double lon,
                    double *along, double *across)
{
    double east = 0.0;
    double north = 0.0;
    Offset(lat0, lon0, lat, lon, &east, &north);
    const double c = course * kPi / 180.0;
    *along = east * std::sin(c) + north * std::cos(c);
    *across = east * std::cos(c) - north * std::sin(c);
}

void PositionAt(double lat0, double lon0, double course, double along, double across,
                double *lat, double *lon)
{
    const double c = course * kPi / 180.0;
    const double east = along * std::sin(c) + across * std::cos(c);
    const double north = along * std::cos(c) - across * std::sin(c);
    *lat = lat0 + north / 60.0;
    // At the middle latitude, as Offset() measures it.
    *lon = lon0 + east / (60.0 * std::cos((lat0 + *lat) / 2.0 * kPi / 180.0));
}

bool InFan(double along, double across, double range, double halfAngle, double minHalfWidth)
{
    if (along * along + across * across <= minHalfWidth * minHalfWidth)
        return true;
    const double widen = std::tan(halfAngle * kPi / 180.0);
    return along >= 0.0 && along <= range &&
           std::fabs(across) <= std::max(minHalfWidth, along * widen);
}

std::vector<std::pair<double, double>> FanOutline(double lat0, double lon0, double course,
                                                  double range, double halfAngle,
                                                  double minHalfWidth)
{
    // Along and across: the boat's end, where the fan starts to widen
    // (when that is before its far end), and the far end.
    const double widen = std::tan(halfAngle * kPi / 180.0);
    const double bend = widen > 0.0 ? minHalfWidth / widen : range;
    const double farHalfWidth = std::max(minHalfWidth, range * widen);
    std::vector<std::pair<double, double>> corners = {{0.0, -minHalfWidth}};
    if (bend < range)
        corners.push_back({bend, -minHalfWidth});
    corners.push_back({range, -farHalfWidth});
    corners.push_back({range, farHalfWidth});
    if (bend < range)
        corners.push_back({bend, minHalfWidth});
    corners.push_back({0.0, minHalfWidth});

    // The half circle behind the boat, from starboard round to port.
    const int kArcSteps = 24;
    for (int step = 1; step < kArcSteps; ++step) {
        const double angle = kPi / 2.0 + kPi * step / kArcSteps;
        corners.push_back({minHalfWidth * std::cos(angle), minHalfWidth * std::sin(angle)});
    }

    std::vector<std::pair<double, double>> outline;
    for (const auto &corner : corners) {
        double lat = 0.0;
        double lon = 0.0;
        PositionAt(lat0, lon0, course, corner.first, corner.second, &lat, &lon);
        outline.emplace_back(lat, lon);
    }
    return outline;
}

wxString FormatBearing(double degrees)
{
    long whole = std::lround(degrees) % 360;
    if (whole < 0)
        whole += 360;
    return wxString::Format("%03ld", whole) + wxString::FromUTF8("°");
}
