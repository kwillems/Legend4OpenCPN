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

bool VhfHighlight::DrawGL(PlugIn_ViewPort *vp, double lineScale, bool dim) const
{
    wxColour fill;
    wxColour line;
    HighlightColours(m_alpha, dim, &fill, &line);
    const double s = std::max(1.0, lineScale);

    glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_HINT_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    for (const VhfArea &area : m_areas) {
        std::vector<wxPoint> points(area.polygon.size());
        for (size_t i = 0; i < area.polygon.size(); ++i)
            GetCanvasPixLL(vp, &points[i], area.polygon[i].first, area.polygon[i].second);

        glColor4ub(fill.Red(), fill.Green(), fill.Blue(), fill.Alpha());
        glBegin(GL_TRIANGLES);
        for (int index : area.triangles)
            glVertex2d(points[index].x, points[index].y);
        glEnd();

        glColor4ub(line.Red(), line.Green(), line.Blue(), line.Alpha());
        glLineWidth(static_cast<float>(2.0 * s));
        glBegin(GL_LINE_LOOP);
        for (size_t i = 0; i + 1 < points.size(); ++i)
            glVertex2d(points[i].x, points[i].y);
        glEnd();
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();
    return true;
}
