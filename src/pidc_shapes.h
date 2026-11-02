// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _PIDC_SHAPES_H_
#define _PIDC_SHAPES_H_

// Shapes drawn with piDC (opencpn-libs/plugin_dc) that look the same with
// desktop OpenGL and with OpenGL ES on Android. They work around what piDC
// does differently there:
// - DrawPolygon draws nothing with desktop OpenGL, where
//   DrawPolygonTessellated fills; on Android the tessellated fill takes a
//   transformation left by an earlier call, and only triangles
//   (DrawPolygon with three points) fill reliably.
// - DrawLines turns blending off on the desktop, so lines that are partly
//   transparent are drawn as separate DrawLine calls.
// - DrawCircle on Android fills without the brush's transparency.
// - piDC has no gradients.

#include <vector>

#include <wx/colour.h>
#include <wx/gdicmn.h>

class piDC;

// Points on a circle, or on an arc from `start` to `end` (radians,
// clockwise on screen).
std::vector<wxPoint> CirclePoints(double cx, double cy, double r, int segments);
std::vector<wxPoint> ArcPoints(double cx, double cy, double r, double start, double end,
                               int segments);

// Fills the polygon with the current brush, transparency included (with
// blending turned on, which some of piDC's calls turn off).
void FillPolygon(piDC &dc, const std::vector<wxPoint> &points);

// The same for a shape that every point of the rim sees from `centre`:
// triangles from the centre (closed: the last point joins the first).
// On Android FillPolygon fills this way from its first point, so it is
// meant for shapes that are convex or seen whole from their first point.
void FillFan(piDC &dc, const wxPoint &centre, const std::vector<wxPoint> &rim, bool closed);

// The line through the points with the current pen, blended.
void StrokePolyline(piDC &dc, const std::vector<wxPoint> &points, bool closed);

// The same in dashes of `dash` pixels with gaps of `gap` pixels.
void StrokeDashed(piDC &dc, const std::vector<wxPoint> &points, double dash, double gap);

// A disc in `colour` with `centreAlpha` in the middle, fading out to its
// edge: a few discs on top of each other.
void GlowDisc(piDC &dc, double cx, double cy, double r, const wxColour &colour,
              int centreAlpha);

// A filled disc with an outline, the fill with its transparency.
void OutlinedDisc(piDC &dc, double cx, double cy, double r, const wxColour &fill,
                  const wxColour &edge, int edgeWidth);

// A pen width of `points` times `scale` in whole pixels, at least one.
int PenWidth(double points, double scale);

#endif
