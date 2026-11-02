// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "pidc_shapes.h"

// Before pidc.h: on Windows GL/gl.h needs windows.h, which wxWidgets
// brings in.
#include <wx/glcanvas.h>
#include "pidc.h"
#ifdef __ANDROID__
#include "gles_picture.h"
#endif

#include <algorithm>
#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;

wxPoint Pixel(double x, double y)
{
    return wxPoint(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
}

} // namespace

std::vector<wxPoint> CirclePoints(double cx, double cy, double r, int segments)
{
    std::vector<wxPoint> points;
    points.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const double angle = 2.0 * kPi * i / segments;
        points.push_back(Pixel(cx + r * std::cos(angle), cy + r * std::sin(angle)));
    }
    return points;
}

std::vector<wxPoint> ArcPoints(double cx, double cy, double r, double start, double end,
                               int segments)
{
    std::vector<wxPoint> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = start + (end - start) * i / segments;
        points.push_back(Pixel(cx + r * std::cos(angle), cy + r * std::sin(angle)));
    }
    return points;
}

void FillPolygon(piDC &dc, const std::vector<wxPoint> &points)
{
    if (points.size() < 3)
        return;
#ifndef __ANDROID__
    // Some of piDC's calls turn blending off when done.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::vector<wxPoint> copy(points);
    dc.DrawPolygonTessellated(static_cast<int>(copy.size()), copy.data());
#else
    FillFan(dc, points.front(),
            std::vector<wxPoint>(points.begin() + 1, points.end()), false);
#endif
}

void FillFan(piDC &dc, const wxPoint &centre, const std::vector<wxPoint> &rim, bool closed)
{
    if (rim.size() < 2)
        return;
#ifndef __ANDROID__
    // The tessellated polygon covers each spot once, also where the rim
    // bends back as seen from the centre.
    std::vector<wxPoint> polygon;
    polygon.reserve(rim.size() + 1);
    if (!closed)
        polygon.push_back(centre);
    polygon.insert(polygon.end(), rim.begin(), rim.end());
    FillPolygon(dc, polygon);
#else
    // piDC's fills are not reliable on Android (see GlesFillTriangles).
    const wxBrush brush = dc.GetBrush();
    if (!brush.IsOk() || brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT)
        return;
    std::vector<wxPoint> corners;
    const size_t count = closed ? rim.size() : rim.size() - 1;
    corners.reserve(count * 3);
    for (size_t i = 0; i < count; ++i) {
        corners.push_back(centre);
        corners.push_back(rim[i]);
        corners.push_back(rim[(i + 1) % rim.size()]);
    }
    GlesFillTriangles(brush.GetColour(), corners);
#endif
}

void StrokePolyline(piDC &dc, const std::vector<wxPoint> &points, bool closed)
{
    for (size_t i = 0; i + 1 < points.size(); ++i)
        dc.DrawLine(points[i].x, points[i].y, points[i + 1].x, points[i + 1].y);
    if (closed && points.size() > 2)
        dc.DrawLine(points.back().x, points.back().y, points.front().x, points.front().y);
}

void StrokeDashed(piDC &dc, const std::vector<wxPoint> &points, double dash, double gap)
{
    // Where along the pattern of dash and gap the next segment starts.
    double phase = 0.0;
    const double period = dash + gap;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const double x0 = points[i].x, y0 = points[i].y;
        const double dx = points[i + 1].x - x0, dy = points[i + 1].y - y0;
        const double length = std::hypot(dx, dy);
        if (length <= 0.0)
            continue;
        double along = 0.0;
        while (along < length) {
            const double inPattern = std::fmod(phase + along, period);
            if (inPattern < dash) {
                const double end = std::min(length, along + dash - inPattern);
                const wxPoint a = Pixel(x0 + dx * along / length, y0 + dy * along / length);
                const wxPoint b = Pixel(x0 + dx * end / length, y0 + dy * end / length);
                dc.DrawLine(a.x, a.y, b.x, b.y);
                along = end;
            }
            else {
                along += period - inPattern;
            }
        }
        phase = std::fmod(phase + length, period);
    }
}

void GlowDisc(piDC &dc, double cx, double cy, double r, const wxColour &colour,
              int centreAlpha)
{
    // Each of the discs adds the same share, so that together they reach
    // centreAlpha in the middle and fade towards the edge.
    const int discs = 6;
    const double share = 1.0 - std::pow(1.0 - centreAlpha / 255.0, 1.0 / discs);
    const int alpha = std::max(1, static_cast<int>(std::lround(share * 255.0)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(colour.Red(), colour.Green(), colour.Blue(), alpha)));
    for (int i = discs; i >= 1; --i)
        FillPolygon(dc, CirclePoints(cx, cy, r * i / discs, 32));
}

void OutlinedDisc(piDC &dc, double cx, double cy, double r, const wxColour &fill,
                  const wxColour &edge, int edgeWidth)
{
    const int segments = std::max(16, static_cast<int>(r * 2));
    const std::vector<wxPoint> circle = CirclePoints(cx, cy, r, segments);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(fill));
    FillPolygon(dc, circle);
    dc.SetPen(wxPen(edge, edgeWidth));
    StrokePolyline(dc, circle, true);
}

int PenWidth(double points, double scale)
{
    return std::max(1, static_cast<int>(std::lround(points * scale)));
}
