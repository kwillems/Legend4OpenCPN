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

#include "shining_lights.h"

#include <cstdint>  // ocpn_plugin.h (api-18) uses uint64_t without it
#include "ocpn_plugin.h"

#include <wx/dc.h>
#include <wx/glcanvas.h>
#include <wx/graphics.h>
#include "wx_compat.h"
#include <wx/time.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

const double kPi = 3.14159265358979323846;

// Grid of the sectors of sector lights, about two metres.
const double kSectorCell = 2e-5;

// The look of a light on the chart. While it shines, a large glow with
// rays in the colour of the light; the steady part of a fixed and flashing
// light is a smaller, paler glow without rays. Sizes follow the pixel
// density of the screen.
struct LightMarkerLook
{
    double scale = 1.0;
    double ring = 0.0;     // radius of the ring
    double glow = 0.0;     // radius of the glow
    double core = 0.0;     // radius of the bright centre
    double rayFrom = 0.0;  // rays run from here...
    double rayTo = 0.0;    // ...to here
    int rays = 8;
};

LightMarkerLook MakeLightMarkerLook(double pixelsPerPoint, bool dimPart)
{
    LightMarkerLook look;
    look.scale = std::max(1.0, pixelsPerPoint);
    look.ring = 26.0 * look.scale;
    look.glow = (dimPart ? 16.0 : 34.0) * look.scale;
    look.core = (dimPart ? 7.0 : 11.0) * look.scale;
    look.rayFrom = 15.0 * look.scale;
    look.rayTo = 31.0 * look.scale;
    return look;
}

// The colour a light has in a state, or false while it is dark.
bool StateColour(
    int state,
    const std::vector<wxString> &colours,
    double dim,
    wxColour *colour,
    bool *dimPart)
{
    if (state < 0 || colours.empty())
        return false;
    *dimPart = state >= kDimLight;
    *colour = LightDisplayColour(
        colours[static_cast<size_t>(state % kDimLight) % colours.size()], dim);
    return true;
}

// Screen angle (radians, y down) of a bearing at a position: the chart can
// be turned, so north is found by projecting a point a little to the north.
double ScreenAngle(PlugIn_ViewPort *vp, double lat, double lon, double bearing)
{
    wxPoint centre;
    wxPoint north;
    GetCanvasPixLL(vp, &centre, lat, lon);
    GetCanvasPixLL(vp, &north, lat + 0.01, lon);
    const double up = std::atan2(static_cast<double>(north.y - centre.y),
                                 static_cast<double>(north.x - centre.x));
    return up + bearing * kPi / 180.0;
}

// The sectors of a sector light as coloured wedges around it. Sector limits
// are bearings from seaward towards the light; the wedge lies on the other
// side, where the ship sees that colour. Bright while the light shines,
// faint while it is dark.
void DrawSectorsGC(
    wxGraphicsContext *gc,
    PlugIn_ViewPort *vp,
    double lat,
    double lon,
    double cx,
    double cy,
    const std::vector<LightSector> &sectors,
    double radius,
    bool lit,
    const wxColour &outline,
    double dim)
{
    for (const LightSector &sector : sectors) {
        double sweep = std::fmod(sector.to - sector.from + 360.0, 360.0);
        if (sweep <= 0.0)
            sweep = 360.0;
        const double start = ScreenAngle(vp, lat, lon, sector.from + 180.0);
        const wxColour colour = LightDisplayColour(sector.colour, dim);

        wxGraphicsPath wedge = gc->CreatePath();
        wedge.MoveToPoint(cx, cy);
        wedge.AddArc(cx, cy, radius, start, start + sweep * kPi / 180.0, true);
        wedge.CloseSubpath();
        gc->SetBrush(wxBrush(
            wxColour(colour.Red(), colour.Green(), colour.Blue(), lit ? 120 : 45)));
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(
            wxColour(outline.Red(), outline.Green(), outline.Blue(), lit ? 200 : 90))
                                     .Width(1.2)));
        gc->DrawPath(wedge);
    }
}

void DrawSectorsGL(
    PlugIn_ViewPort *vp,
    double lat,
    double lon,
    double cx,
    double cy,
    const std::vector<LightSector> &sectors,
    double radius,
    bool lit,
    const wxColour &outline,
    double dim)
{
    for (const LightSector &sector : sectors) {
        double sweep = std::fmod(sector.to - sector.from + 360.0, 360.0);
        if (sweep <= 0.0)
            sweep = 360.0;
        const double start = ScreenAngle(vp, lat, lon, sector.from + 180.0);
        const double end = start + sweep * kPi / 180.0;
        const int steps = std::max(4, static_cast<int>(sweep / 4.0));
        const wxColour colour = LightDisplayColour(sector.colour, dim);

        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), lit ? 120 : 45);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2d(cx, cy);
        for (int i = 0; i <= steps; ++i) {
            const double angle = start + (end - start) * i / steps;
            glVertex2d(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
        }
        glEnd();

        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), lit ? 200 : 90);
        glLineWidth(1.2f);
        glBegin(GL_LINE_LOOP);
        glVertex2d(cx, cy);
        for (int i = 0; i <= steps; ++i) {
            const double angle = start + (end - start) * i / steps;
            glVertex2d(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
        }
        glEnd();
    }
}

// Night blue, dark enough that only lights stand out, light enough that
// coast and fairways stay faintly visible.
const wxColour kNightShade(4, 8, 22, 222);

} // namespace

int PhaseStateAt(const std::vector<LightPhase> &phases, double seconds)
{
    double end = 0.0;
    for (const LightPhase &phase : phases) {
        end += phase.seconds;
        if (seconds < end) {
            if (phase.colour < 0)
                return -1;
            return phase.colour + (phase.dim ? kDimLight : 0);
        }
    }
    return -1;
}

std::vector<wxPoint> MarkerCentres(
    PlugIn_ViewPort *vp,
    const std::vector<std::pair<double, double>> &positions)
{
    std::vector<wxPoint> centres;

    for (const auto &position : positions) {
        wxPoint centre;
        GetCanvasPixLL(vp, &centre, position.first, position.second);

        if (centre.x < -60 || centre.y < -60 ||
            centre.x > vp->pix_width + 60 || centre.y > vp->pix_height + 60)
            continue;

        centres.push_back(centre);
    }

    return centres;
}

// --- LightSectors ---

std::pair<long, long> LightSectors::Cell(double lat, double lon)
{
    return std::make_pair(static_cast<long>(std::floor(lat / kSectorCell)),
                          static_cast<long>(std::floor(lon / kSectorCell)));
}

void LightSectors::Add(double lat, double lon, const wxString &rhythm,
                       const LightSector &sector)
{
    m_sectors[Cell(lat, lon)].push_back(std::make_pair(rhythm, sector));
}

std::vector<LightSector> LightSectors::At(
    double lat, double lon, const wxString &rhythm) const
{
    std::vector<LightSector> sectors;
    if (rhythm.IsEmpty())
        return sectors;

    const std::pair<long, long> cell = Cell(lat, lon);
    for (long dLat = -1; dLat <= 1; ++dLat) {
        for (long dLon = -1; dLon <= 1; ++dLon) {
            auto found = m_sectors.find(
                std::make_pair(cell.first + dLat, cell.second + dLon));
            if (found == m_sectors.end())
                continue;
            for (const auto &entry : found->second) {
                if (entry.first == rhythm)
                    sectors.push_back(entry.second);
            }
        }
    }
    return sectors;
}

// --- ChosenLight ---

ChosenLight::ChosenLight(std::function<void()> refresh)
    : m_refresh(std::move(refresh))
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OnTimer(); });
}

// The light shines for a few periods, at least a quarter of a minute and
// at most a minute. Only a change between light and dark repaints the
// chart.
bool ChosenLight::Show(
    const LightCharacter &light,
    const std::vector<std::pair<double, double>> &positions)
{
    std::vector<LightPhase> phases = light.Phases();
    double cycle = 0.0;
    for (const LightPhase &phase : phases)
        cycle += phase.seconds;
    if (cycle <= 0.0)
        return false;

    m_positions = positions;
    m_rhythm = light.RhythmAbbreviation();
    m_phases = phases;
    m_colours = light.Colours();
    m_cycle = cycle;
    m_duration = std::min(60.0, std::max(15.0, 3.0 * cycle));
    m_started = wxGetUTCTimeMillis();
    m_state = -2; // nothing drawn yet
    m_active = true;

    m_timer.Stop();
    m_timer.Start(40);
    OnTimer();
    return true;
}

void ChosenLight::Stop()
{
    m_timer.Stop();

    if (m_active) {
        m_active = false;
        m_refresh();
    }
}

void ChosenLight::Clear()
{
    m_timer.Stop();
    m_active = false;
}

void ChosenLight::OnTimer()
{
    if (!m_active) {
        m_timer.Stop();
        return;
    }

    const double elapsed = (wxGetUTCTimeMillis() - m_started).ToDouble() / 1000.0;
    if (elapsed > m_duration) {
        Stop();
        return;
    }

    const int state = PhaseStateAt(m_phases, std::fmod(elapsed, m_cycle));
    if (state != m_state) {
        m_state = state;
        m_refresh();
    }
}

bool ChosenLight::Draw(wxDC &dc, PlugIn_ViewPort *vp, const LightStyle &style,
                       const LightSectors &sectors) const
{
    const std::vector<wxPoint> centres = MarkerCentres(vp, m_positions);
    if (centres.empty() || m_colours.empty())
        return false;

    const wxColour &outline = style.outline;
    const wxColour &highlight = style.highlight;

    wxColour colour;
    bool dimPart = false;
    const bool lit = StateColour(m_state, m_colours, style.dim, &colour, &dimPart);
    const LightMarkerLook look = MakeLightMarkerLook(style.pixelsPerPoint, dimPart);
    const double s = look.scale;

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));

    // Sectors first, under the light.
    if (gc) {
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        for (const auto &position : m_positions) {
            const std::vector<LightSector> here =
                sectors.At(position.first, position.second, m_rhythm);
            if (here.empty())
                continue;
            wxPoint centre;
            GetCanvasPixLL(vp, &centre, position.first, position.second);
            DrawSectorsGC(gc.get(), vp, position.first, position.second, centre.x,
                          centre.y, here, 90.0 * look.scale, lit, outline, style.dim);
        }
    }

    for (const wxPoint &centre : centres) {
        const double cx = centre.x;
        const double cy = centre.y;

        if (!gc) {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(outline, static_cast<int>(6 * s)));
            dc.DrawCircle(centre, static_cast<int>(look.ring));
            dc.SetPen(wxPen(highlight, static_cast<int>(3 * s)));
            dc.DrawCircle(centre, static_cast<int>(look.ring));
            if (lit) {
                dc.SetPen(wxPen(outline, static_cast<int>(2 * s)));
                dc.SetBrush(wxBrush(colour));
                dc.DrawCircle(centre, static_cast<int>(look.core * 1.6));
            }
            continue;
        }

        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        // The glow under everything else.
        if (lit) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(gc->CreateRadialGradientBrush(
                cx, cy, cx, cy, look.glow,
                wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 120 : 210),
                wxColour(colour.Red(), colour.Green(), colour.Blue(), 0)));
            gc->DrawEllipse(cx - look.glow, cy - look.glow, 2 * look.glow, 2 * look.glow);
        }

        // The ring with ticks, as the marker.
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        const double tick = 12.0 * s;
        for (int pass = 0; pass < 2; ++pass) {
            gc->SetPen(gc->CreatePen(
                wxGraphicsPenInfo(pass == 0 ? outline : highlight).Width((pass == 0 ? 6.0 : 3.0) * s)));
            gc->DrawEllipse(cx - look.ring, cy - look.ring, 2 * look.ring, 2 * look.ring);
            gc->StrokeLine(cx - look.ring - tick, cy, cx - look.ring + 2 * s, cy);
            gc->StrokeLine(cx + look.ring - 2 * s, cy, cx + look.ring + tick, cy);
            gc->StrokeLine(cx, cy - look.ring - tick, cx, cy - look.ring + 2 * s);
            gc->StrokeLine(cx, cy + look.ring - 2 * s, cx, cy + look.ring + tick);
        }

        if (!lit)
            continue;

        // Rays, outlined so that white and yellow show on a light chart.
        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                gc->SetPen(gc->CreatePen(
                    wxGraphicsPenInfo(pass == 0 ? outline : colour)
                        .Width((pass == 0 ? 5.0 : 3.0) * s)
                        .Cap(wxCAP_ROUND)));
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    gc->StrokeLine(cx + look.rayFrom * std::cos(angle),
                                   cy + look.rayFrom * std::sin(angle),
                                   cx + look.rayTo * std::cos(angle),
                                   cy + look.rayTo * std::sin(angle));
                }
            }
        }

        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(outline).Width(2.0 * s)));
        gc->SetBrush(wxBrush(
            wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255)));
        gc->DrawEllipse(cx - look.core, cy - look.core, 2 * look.core, 2 * look.core);
    }

    return true;
}

bool ChosenLight::DrawGL(PlugIn_ViewPort *vp, const LightStyle &style,
                         const LightSectors &sectors) const
{
#ifndef __ANDROID__
    const std::vector<wxPoint> centres = MarkerCentres(vp, m_positions);
    if (centres.empty() || m_colours.empty())
        return false;

    const wxColour &outline = style.outline;
    const wxColour &highlight = style.highlight;

    wxColour colour;
    bool dimPart = false;
    const bool lit = StateColour(m_state, m_colours, style.dim, &colour, &dimPart);
    const LightMarkerLook look = MakeLightMarkerLook(style.pixelsPerPoint, dimPart);
    const double s = look.scale;

    // GL_CURRENT_BIT included: the drawing colour set here would otherwise
    // stay in effect for whatever OpenCPN draws next, and chart symbols
    // are modulated by it (black symbols in the night scheme).
    // GL_LIGHTING_BIT keeps the shade model, set to smooth for the glow.
    glPushAttrib(
        GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
        GL_HINT_BIT | GL_LIGHTING_BIT);

    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    const int segments = 64;

    auto circle = [&](double x, double y, double r) {
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    // A disc that fades from the centre colour to transparent at its edge.
    auto glowDisc = [&](double x, double y, double r, unsigned char alpha) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), alpha);
        glVertex2d(x, y);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 0);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    auto disc = [&](double x, double y, double r) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2d(x, y);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    auto ticks = [&](double x, double y, double r, double length) {
        glBegin(GL_LINES);
        glVertex2d(x - r - length, y);
        glVertex2d(x - r + 2 * s, y);
        glVertex2d(x + r - 2 * s, y);
        glVertex2d(x + r + length, y);
        glVertex2d(x, y - r - length);
        glVertex2d(x, y - r + 2 * s);
        glVertex2d(x, y + r - 2 * s);
        glVertex2d(x, y + r + length);
        glEnd();
    };

    // Sectors first, under the light.
    for (const auto &position : m_positions) {
        const std::vector<LightSector> here =
            sectors.At(position.first, position.second, m_rhythm);
        if (here.empty())
            continue;
        wxPoint centre;
        GetCanvasPixLL(vp, &centre, position.first, position.second);
        DrawSectorsGL(vp, position.first, position.second, centre.x, centre.y,
                      here, 90.0 * s, lit, outline, style.dim);
    }

    for (const wxPoint &centre : centres) {
        const double cx = centre.x;
        const double cy = centre.y;

        if (lit)
            glowDisc(cx, cy, look.glow, dimPart ? 120 : 210);

        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(static_cast<float>(6.0 * s));
        circle(cx, cy, look.ring);
        ticks(cx, cy, look.ring, 12.0 * s);
        glColor4ub(highlight.Red(), highlight.Green(), highlight.Blue(), 255);
        glLineWidth(static_cast<float>(3.0 * s));
        circle(cx, cy, look.ring);
        ticks(cx, cy, look.ring, 10.0 * s);

        if (!lit)
            continue;

        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0)
                    glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
                else
                    glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 255);
                glLineWidth(static_cast<float>((pass == 0 ? 5.0 : 3.0) * s));
                glBegin(GL_LINES);
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    glVertex2d(cx + look.rayFrom * std::cos(angle),
                               cy + look.rayFrom * std::sin(angle));
                    glVertex2d(cx + look.rayTo * std::cos(angle),
                               cy + look.rayTo * std::sin(angle));
                }
                glEnd();
            }
        }

        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255);
        disc(cx, cy, look.core);
        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(static_cast<float>(2.0 * s));
        circle(cx, cy, look.core);
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();

    return true;
#else
    // Android has OpenGL ES only, without these drawing calls.
    return false;
#endif
}

// --- ShiningLights ---

ShiningLights::ShiningLights(std::function<void()> refresh)
    : m_refresh(std::move(refresh))
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OnTimer(); });
}

void ShiningLights::SetOn(bool on)
{
    m_on = on;

    if (!on) {
        m_timer.Stop();
        ClearLights();
        m_refresh();
        return;
    }

    m_started = wxGetUTCTimeMillis();
    m_timer.Stop();
    m_timer.Start(50);
    OnTimer();
}

void ShiningLights::Clear()
{
    m_timer.Stop();
    m_on = false;
}

void ShiningLights::ClearLights()
{
    m_lights.clear();
    m_characters.clear();
    m_character_index.clear();
    m_taken.clear();
}

void ShiningLights::AddLight(const wxString &lightText,
                             const std::vector<std::pair<double, double>> &positions)
{
    auto known = m_character_index.find(lightText);
    size_t index = 0;
    if (known != m_character_index.end()) {
        index = known->second;
    }
    else {
        LightCharacter light;
        if (!light.Parse(lightText))
            return;
        Character character;
        character.phases = light.Phases();
        character.colours = light.Colours();
        character.rhythm = light.RhythmAbbreviation();
        for (const LightPhase &phase : character.phases)
            character.cycle += phase.seconds;
        if (character.cycle <= 0.0)
            return;
        index = m_characters.size();
        m_characters.push_back(character);
        m_character_index[lightText] = index;
    }

    for (const auto &position : positions) {
        const auto cell = std::make_pair(
            static_cast<long>(std::floor(position.first / 0.0003)),
            static_cast<long>(std::floor(position.second / 0.0005)));
        if (!m_taken.insert(cell).second)
            continue;

        Light shining;
        shining.lat = position.first;
        shining.lon = position.second;
        shining.character = index;
        // A fixed start within the period, taken from the position, so
        // the lights do not flash in step and keep their start when the
        // list is rebuilt.
        shining.offset = std::fmod(
            std::fabs(position.first * 7919.0 + position.second * 104729.0),
            m_characters[index].cycle);
        m_lights.push_back(shining);
    }
}

void ShiningLights::OnTimer()
{
    if (!m_on) {
        m_timer.Stop();
        return;
    }

    const double elapsed = (wxGetUTCTimeMillis() - m_started).ToDouble() / 1000.0;

    bool changed = false;
    for (Light &light : m_lights) {
        const Character &character = m_characters[light.character];
        const int state = PhaseStateAt(
            character.phases, std::fmod(elapsed + light.offset, character.cycle));
        if (state != light.state) {
            light.state = state;
            changed = true;
        }
    }

    if (changed)
        m_refresh();
}

bool ShiningLights::Draw(wxDC &dc, PlugIn_ViewPort *vp, const LightStyle &style,
                         const LightSectors &sectors) const
{
    if (m_lights.empty())
        return false;

    const wxColour &outline = style.outline;
    const double pixels = style.pixelsPerPoint * 0.65;

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return false;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    bool drew = false;
    for (const Light &light : m_lights) {
        wxColour colour;
        bool dimPart = false;
        if (!StateColour(light.state, m_characters[light.character].colours, style.dim,
                         &colour, &dimPart))
            continue;

        wxPoint centre;
        GetCanvasPixLL(vp, &centre, light.lat, light.lon);
        if (centre.x < -40 || centre.y < -40 || centre.x > vp->pix_width + 40 ||
            centre.y > vp->pix_height + 40)
            continue;

        const LightMarkerLook look = MakeLightMarkerLook(pixels, dimPart);
        const double s = std::max(0.65, pixels);
        const double cx = centre.x;
        const double cy = centre.y;

        const std::vector<LightSector> here =
            sectors.At(light.lat, light.lon, m_characters[light.character].rhythm);
        if (!here.empty())
            DrawSectorsGC(gc.get(), vp, light.lat, light.lon, cx, cy, here, 60.0 * s, true,
                          outline, style.dim);

        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(gc->CreateRadialGradientBrush(
            cx, cy, cx, cy, look.glow,
            wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 120 : 210),
            wxColour(colour.Red(), colour.Green(), colour.Blue(), 0)));
        gc->DrawEllipse(cx - look.glow, cy - look.glow, 2 * look.glow, 2 * look.glow);

        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                gc->SetPen(gc->CreatePen(
                    wxGraphicsPenInfo(pass == 0 ? outline : colour)
                        .Width((pass == 0 ? 4.0 : 2.5) * s)
                        .Cap(wxCAP_ROUND)));
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    gc->StrokeLine(cx + look.rayFrom * std::cos(angle),
                                   cy + look.rayFrom * std::sin(angle),
                                   cx + look.rayTo * std::cos(angle),
                                   cy + look.rayTo * std::sin(angle));
                }
            }
        }

        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(outline).Width(1.5 * s)));
        gc->SetBrush(wxBrush(
            wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255)));
        gc->DrawEllipse(cx - look.core, cy - look.core, 2 * look.core, 2 * look.core);
        drew = true;
    }
    return drew;
}

bool ShiningLights::DrawGL(PlugIn_ViewPort *vp, const LightStyle &style,
                           const LightSectors &sectors) const
{
#ifndef __ANDROID__
    if (m_lights.empty())
        return false;

    const wxColour &outline = style.outline;
    const double pixels = style.pixelsPerPoint * 0.65;
    const double s = std::max(0.65, pixels);
    const int segments = 40;

    glPushAttrib(
        GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
        GL_HINT_BIT | GL_LIGHTING_BIT);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    bool drew = false;
    for (const Light &light : m_lights) {
        wxColour colour;
        bool dimPart = false;
        if (!StateColour(light.state, m_characters[light.character].colours, style.dim,
                         &colour, &dimPart))
            continue;

        wxPoint centre;
        GetCanvasPixLL(vp, &centre, light.lat, light.lon);
        if (centre.x < -40 || centre.y < -40 || centre.x > vp->pix_width + 40 ||
            centre.y > vp->pix_height + 40)
            continue;

        const LightMarkerLook look = MakeLightMarkerLook(pixels, dimPart);
        const double cx = centre.x;
        const double cy = centre.y;

        const std::vector<LightSector> here =
            sectors.At(light.lat, light.lon, m_characters[light.character].rhythm);
        if (!here.empty())
            DrawSectorsGL(vp, light.lat, light.lon, cx, cy, here, 60.0 * s, true, outline,
                          style.dim);

        glBegin(GL_TRIANGLE_FAN);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 120 : 210);
        glVertex2d(cx, cy);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 0);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(cx + look.glow * std::cos(angle), cy + look.glow * std::sin(angle));
        }
        glEnd();

        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0)
                    glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
                else
                    glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 255);
                glLineWidth(static_cast<float>((pass == 0 ? 4.0 : 2.5) * s));
                glBegin(GL_LINES);
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    glVertex2d(cx + look.rayFrom * std::cos(angle),
                               cy + look.rayFrom * std::sin(angle));
                    glVertex2d(cx + look.rayTo * std::cos(angle),
                               cy + look.rayTo * std::sin(angle));
                }
                glEnd();
            }
        }

        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2d(cx, cy);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(cx + look.core * std::cos(angle), cy + look.core * std::sin(angle));
        }
        glEnd();
        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(static_cast<float>(1.5 * s));
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(cx + look.core * std::cos(angle), cy + look.core * std::sin(angle));
        }
        glEnd();
        drew = true;
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();
    return drew;
#else
    // Android has OpenGL ES only, without these drawing calls.
    return false;
#endif
}

// --- Night shade ---

void DrawNightShade(wxDC &dc, PlugIn_ViewPort *vp)
{
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return;
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(kNightShade));
    gc->DrawRectangle(0, 0, vp->pix_width, vp->pix_height);
}

void DrawNightShadeGL(PlugIn_ViewPort *vp)
{
#ifndef __ANDROID__
    glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(kNightShade.Red(), kNightShade.Green(), kNightShade.Blue(),
               kNightShade.Alpha());
    glBegin(GL_QUADS);
    glVertex2d(0, 0);
    glVertex2d(vp->pix_width, 0);
    glVertex2d(vp->pix_width, vp->pix_height);
    glVertex2d(0, vp->pix_height);
    glEnd();
    glColor4ub(255, 255, 255, 255);
    glPopAttrib();
#else
    // Android has OpenGL ES only, without these drawing calls.
#endif
}
