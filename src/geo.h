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

#include <utility>
#include <vector>

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

// A length the user chose, to half a sea mile: "2 NM", "0,5 NM", "12,5 NM".
wxString FormatMiles(double seaMiles);

// The time to sail somewhere, roughly: "7 min" below ten minutes, then to
// five minutes ("25 min", "1 u 05" / "1 h 05"), whole hours as "2 u" / "2 h".
wxString FormatDuration(double minutes, bool dutch);

// "045°": whole degrees, three digits; 359.6 becomes "000°".
wxString FormatBearing(double degrees);

// A position seen from a starting point along a course (degrees true):
// sea miles ahead, and sea miles to starboard (negative: to port).
void AlongAndAcross(double lat0, double lon0, double course, double lat, double lon,
                    double *along, double *across);

// The position that lies those sea miles ahead and to starboard.
void PositionAt(double lat0, double lon0, double course, double along, double across,
                double *lat, double *lon);

// The fan of the Uitkijk: a circle of `minHalfWidth` sea miles around the
// boat, for what lies beside it or was just passed, and ahead of it a strip
// at least `minHalfWidth` wide on either side, widening at `halfAngle`
// degrees on either side of the course, up to `range` sea miles from the
// boat in every direction: its far end is round.
bool InFan(double along, double across, double range, double halfAngle, double minHalfWidth);

// Its outline as positions (lat, lon), in order, not closed: the corners
// and the arc ahead and the half circle behind the boat. Where the strip widens the
// outline bends outward, so the fan is not convex; from the boat every
// point of it is in sight.
std::vector<std::pair<double, double>> FanOutline(double lat0, double lon0, double course,
                                                  double range, double halfAngle,
                                                  double minHalfWidth);

// The distances from the boat of the lines across the fan: every sea mile
// while it reaches 5 or less, every 5 up to 25, every 10 beyond; none
// within half a step of its far end, which has its own label.
std::vector<double> FanMileLines(double range);

// A line across the fan at `distance` sea miles from the boat, as positions
// (lat, lon) from port round to starboard: an arc, as the far end is, from
// side to side of the fan.
std::vector<std::pair<double, double>> FanArc(double lat0, double lon0, double course,
                                              double distance, double halfAngle,
                                              double minHalfWidth);

#endif
