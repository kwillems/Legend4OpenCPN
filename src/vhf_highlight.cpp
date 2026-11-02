// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifdef __WXOSX__
#define GL_SILENCE_DEPRECATION
#endif

#include "vhf_highlight.h"

#include <cstdint>  // ocpn_plugin.h (api-18) uses uint64_t without it
#include "ocpn_plugin.h"

#include <wx/dc.h>
#include <wx/glcanvas.h>
#include <wx/graphics.h>
#include "wx_compat.h"
#include "pidc.h"
#include "pidc_shapes.h"
#include <wx/time.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace {

// Light red, as the selected area on the map of vhfinfo.org (fill #ff6666
// at 42 %, a red edge); softer in dusk and night.
void HighlightColours(double alpha, bool dimScheme, wxColour *fill, wxColour *line)
{
    const double dim = dimScheme ? 0.6 : 1.0;
    *fill = wxColour(255, 102, 102, static_cast<unsigned char>(107 * alpha * dim));
    *line = wxColour(255, 0, 0, static_cast<unsigned char>(200 * alpha * dim));
}

} // namespace

VhfHighlight::VhfHighlight(std::function<void()> refresh)
    : m_refresh(std::move(refresh))
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OnTimer(); });
}

void VhfHighlight::Show(const std::vector<VhfArea> &areas)
{
    m_areas = areas;
    m_alpha = 1.0;
    StartTiming();
    m_refresh();
}

void VhfHighlight::SetKeepLit(bool keepLit)
{
    m_keep_lit = keepLit;
    if (!m_areas.empty())
        StartTiming();
}

void VhfHighlight::StartTiming()
{
    m_started = wxGetUTCTimeMillis();
    if (m_keep_lit)
        m_timer.Stop();
    else
        m_timer.Start(50);
}

void VhfHighlight::Stop()
{
    Clear();
    m_refresh();
}

void VhfHighlight::Clear()
{
    m_timer.Stop();
    m_areas.clear();
}

// Lit for six seconds, then fading out in one and a half.
void VhfHighlight::OnTimer()
{
    if (m_keep_lit) {
        m_timer.Stop();
        return;
    }
    const double elapsed = (wxGetUTCTimeMillis() - m_started).ToDouble() / 1000.0;
    const double hold = 6.0;
    const double fade = 1.5;
    if (elapsed >= hold + fade) {
        Stop();
        return;
    }
    const double alpha = elapsed <= hold ? 1.0 : 1.0 - (elapsed - hold) / fade;
    if (std::fabs(alpha - m_alpha) > 0.02 || alpha == 1.0) {
        const bool changed = alpha != m_alpha;
        m_alpha = alpha;
        if (changed)
            m_refresh();
    }
}

bool VhfHighlight::Draw(wxDC &dc, PlugIn_ViewPort *vp, double lineScale, bool dim) const
{
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return false;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    wxColour fill;
    wxColour line;
    HighlightColours(m_alpha, dim, &fill, &line);
    const double s = std::max(1.0, lineScale);

    for (const VhfArea &area : m_areas) {
        wxGraphicsPath path = gc->CreatePath();
        for (size_t i = 0; i < area.polygon.size(); ++i) {
            wxPoint at;
            GetCanvasPixLL(vp, &at, area.polygon[i].first, area.polygon[i].second);
            if (i == 0)
                path.MoveToPoint(at.x, at.y);
            else
                path.AddLineToPoint(at.x, at.y);
        }
        path.CloseSubpath();
        gc->SetBrush(wxBrush(fill));
        gc->FillPath(path, wxODDEVEN_RULE);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(2.0 * s).Join(wxJOIN_ROUND)));
        gc->StrokePath(path);
    }
    return true;
}

bool VhfHighlight::Draw(piDC &dc, PlugIn_ViewPort *vp, double lineScale, bool dim) const
{
    wxColour fill;
    wxColour line;
    HighlightColours(m_alpha, dim, &fill, &line);
    const double s = std::max(1.0, lineScale);

    for (const VhfArea &area : m_areas) {
        std::vector<wxPoint> points(area.polygon.size());
        for (size_t i = 0; i < area.polygon.size(); ++i)
            GetCanvasPixLL(vp, &points[i], area.polygon[i].first, area.polygon[i].second);

        // The area's own triangles: the outline may be concave.
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(fill));
        for (size_t t = 0; t + 2 < area.triangles.size(); t += 3)
            FillPolygon(dc, {points[area.triangles[t]], points[area.triangles[t + 1]],
                             points[area.triangles[t + 2]]});

        // The last point repeats the first.
        if (points.size() > 1)
            points.pop_back();
        dc.SetPen(wxPen(line, PenWidth(2.0, s)));
        StrokePolyline(dc, points, true);
    }
    return true;
}
