// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "geo.h"

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

wxString FormatBearing(double degrees)
{
    long whole = std::lround(degrees) % 360;
    if (whole < 0)
        whole += 360;
    return wxString::Format("%03ld", whole) + wxString::FromUTF8("°");
}
