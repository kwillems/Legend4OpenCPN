// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _GEO_H_
#define _GEO_H_

#include <wx/string.h>

// Distances and bearings over the short distances of the legend: the
// earth as flat around the two positions, which is close enough within a
// few tens of sea miles.

// Distance in sea miles.
double SeaMiles(double lat1, double lon1, double lat2, double lon2);

// True bearing from the first position to the second, 0 up to 360 degrees.
double BearingDegrees(double lat1, double lon1, double lat2, double lon2);

// "150 m" below a tenth of a sea mile, "0,3 NM" below ten, "12 NM" beyond.
wxString FormatDistance(double seaMiles);

// "045°": whole degrees, three digits; 359.6 becomes "000°".
wxString FormatBearing(double degrees);

#endif
