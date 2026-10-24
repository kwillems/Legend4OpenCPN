// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _SHINING_LIGHTS_H_
#define _SHINING_LIGHTS_H_

#include "light_character.h"

#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/longlong.h>
#include <wx/string.h>
#include <wx/timer.h>

class wxDC;
class PlugIn_ViewPort;

// Added to the colour index of a light that shines dimmed: the steady
// part of a fixed and flashing light.
const int kDimLight = 1000;

// The colour index of a light at a moment in its period, -1 while dark,
// plus kDimLight for the steady part of a fixed and flashing light.
int PhaseStateAt(const std::vector<LightPhase> &phases, double seconds);

// Screen positions of the marked objects on a canvas, leaving out those
// far outside it.
std::vector<wxPoint> MarkerCentres(
    PlugIn_ViewPort *vp,
    const std::vector<std::pair<double, double>> &positions);

// How lights are drawn on the chart at this moment.
struct LightStyle
{
    wxColour outline;            // as the marker
    wxColour highlight;
    double dim = 1.0;            // brightness of the light colours
    double pixelsPerPoint = 1.0; // viewport pixels per window pixel
};

// Sectors of the sector lights in view, by position (grid of about two
// metres), from the chart objects last collected. Each sector with the
// rhythm of its light ("Iso.4s"): two lights at one spot keep their own
// sectors.
class LightSectors
{
public:
    // The grid cell of a position.
    static std::pair<long, long> Cell(double lat, double lon);

    void Clear() { m_sectors.clear(); }
    void Add(double lat, double lon, const wxString &rhythm, const LightSector &sector);
    // The sectors of the light with this rhythm at a position.
    std::vector<LightSector> At(double lat, double lon, const wxString &rhythm) const;

private:
    std::map<std::pair<long, long>, std::vector<std::pair<wxString, LightSector>>> m_sectors;
};

// A light character chosen in the legend shines on the chart in its own
// rhythm for a few periods, at one light or at all in view. A ring with
// ticks, like the marker, stays where the light is, also while it is dark.
class ChosenLight
{
public:
    // refresh redraws the charts, as the light goes on or off.
    explicit ChosenLight(std::function<void()> refresh);

    // False, and nothing changes, when the light has no rhythm to show.
    bool Show(const LightCharacter &light,
              const std::vector<std::pair<double, double>> &positions);
    // Goes out, and the charts are redrawn.
    void Stop();
    // Goes out without a redraw, before the plugin goes.
    void Clear();
    bool IsActive() const { return m_active; }

    bool Draw(wxDC &dc, PlugIn_ViewPort *vp, const LightStyle &style,
              const LightSectors &sectors) const;
    bool DrawGL(PlugIn_ViewPort *vp, const LightStyle &style,
                const LightSectors &sectors) const;

private:
    void OnTimer();

    std::function<void()> m_refresh;
    bool m_active = false;
    std::vector<std::pair<double, double>> m_positions; // lat, lon
    wxString m_rhythm;              // the abbreviation without colours, for its sectors
    std::vector<LightPhase> m_phases;
    std::vector<wxString> m_colours;
    double m_cycle = 0.0;           // seconds of the phases together
    double m_duration = 0.0;        // seconds the light shines on the chart
    wxLongLong m_started = 0;       // UTC milliseconds
    int m_state = -1;               // as PhaseStateAt(), last drawn
    wxTimer m_timer;
};

// All lights in view shining on the chart at once, each in its own rhythm,
// until switched off: glow, rays and centre as for a chosen light, smaller
// and without the ring, so the chart stays readable.
class ShiningLights
{
public:
    // refresh redraws the charts, as lights go on or off.
    explicit ShiningLights(std::function<void()> refresh);

    // On: the lights added so far start to shine. Off: they are dropped.
    void SetOn(bool on);
    bool IsOn() const { return m_on; }
    // Off without a redraw, before the plugin goes.
    void Clear();

    // The lights follow the chart: the list is filled again as it changes.
    // One light that is listed twice (a waypoint and a chart light) shines
    // once.
    void ClearLights();
    void AddLight(const wxString &lightText,
                  const std::vector<std::pair<double, double>> &positions);

    bool Draw(wxDC &dc, PlugIn_ViewPort *vp, const LightStyle &style,
              const LightSectors &sectors) const;
    bool DrawGL(PlugIn_ViewPort *vp, const LightStyle &style,
                const LightSectors &sectors) const;

private:
    struct Character
    {
        std::vector<LightPhase> phases;
        std::vector<wxString> colours;
        double cycle = 0.0;
        wxString rhythm; // the abbreviation without colours, for its sectors
    };
    struct Light
    {
        double lat = 0.0;
        double lon = 0.0;
        size_t character = 0;  // index in m_characters
        double offset = 0.0;   // seconds; lights do not flash in step
        int state = -2;        // as PhaseStateAt(), last drawn
    };

    void OnTimer();

    std::function<void()> m_refresh;
    bool m_on = false;
    std::vector<Character> m_characters;
    std::vector<Light> m_lights;
    std::map<wxString, size_t> m_character_index; // light text -> m_characters
    std::set<std::pair<long, long>> m_taken;      // positions with a light
    wxLongLong m_started = 0;
    wxTimer m_timer;
};

// Nachtvoorbeeld: a dark shade over the chart, through which the coast
// still shows faintly.
void DrawNightShade(wxDC &dc, PlugIn_ViewPort *vp);
void DrawNightShadeGL(PlugIn_ViewPort *vp);

#endif
