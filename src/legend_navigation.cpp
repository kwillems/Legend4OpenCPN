// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, underway: the boat's position, Vooruit, Licht herkennen and
// the VHF areas.

#include "legend_pi.h"

#include "geo.h"

#include "legend_html_window.h"
#include "legend_shared.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <set>

// A position from OpenCPN (GPS). The banner is redrawn only when the
// areas the boat is in change.
void legend_pi::SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix)
{
    if (std::isnan(pfix.Lat) || std::isnan(pfix.Lon) ||
        (pfix.Lat == 0.0 && pfix.Lon == 0.0))
        return;

    m_ship_lat = pfix.Lat;
    m_ship_lon = pfix.Lon;
    m_ship_cog = pfix.Cog;
    m_ship_sog = pfix.Sog;
    m_ship_time = wxGetUTCTimeMillis();

    // Also while the legend is closed.
    CheckVhfAreaChange();
    CheckNotePlaces();

    // The fan of Vooruit follows the boat; once more when it goes away.
    const bool fan = AheadFanVisible();
    if (fan || m_ahead_fan_was_visible)
        RefreshAllCanvases();
    m_ahead_fan_was_visible = fan;

    if (!IsDynamicLegendVisible() || m_loading || m_showing_loading_page ||
        m_reorder_mode)
        return;

    // VhfAreasHere() keeps the key of what it found.
    const wxString previous = m_vhf_shown_key;
    bool fromShip = false;
    bool layerShown = false;
    VhfAreasHere(&fromShip, &layerShown);

    // What lies ahead, and the distances from the boat, change with every
    // fix while sailing; the list is redrawn at most every five seconds for
    // them.
    ComputeAhead();
    const bool waited = (wxGetUTCTimeMillis() - m_ahead_rendered) >= 5000;
    const bool aheadChanged = m_ahead_key != m_ahead_drawn_key && waited;
    const bool distanceChanged =
        m_show_distance && waited && DistanceKey() != m_distance_drawn_key;

    if (m_vhf_shown_key != previous || aheadChanged || distanceChanged)
        RenderDynamicLegend(true);
}

bool legend_pi::DistanceFromBoatShown() const
{
    return m_show_distance && BoatPositionKnown();
}

// "0,3 NM · 045°" to the object of the row nearest to the boat; empty when
// the setting is off or the boat's position is not known.
wxString legend_pi::DistanceText(const DynamicGroup &group) const
{
    if (!DistanceFromBoatShown() || group.positions.empty())
        return wxString();

    const std::pair<double, double> *nearest = nullptr;
    double best = 0.0;
    for (const auto &position : group.positions) {
        const double miles = SeaMiles(m_ship_lat, m_ship_lon, position.first, position.second);
        if (!nearest || miles < best) {
            nearest = &position;
            best = miles;
        }
    }
    return FormatDistance(best) + wxString::FromUTF8(" \u00b7 ") +
           FormatBearing(BearingDegrees(m_ship_lat, m_ship_lon, nearest->first,
                                        nearest->second));
}

wxString legend_pi::DistanceKey() const
{
    wxString key;
    for (const DynamicGroup &group : m_dynamic_groups)
        key << DistanceText(group) << '|';
    return key;
}

void legend_pi::SortFromBoat(DynamicGroup &group) const
{
    std::stable_sort(group.positions.begin(), group.positions.end(),
                     [this](const std::pair<double, double> &a,
                            const std::pair<double, double> &b) {
                         return SeaMiles(m_ship_lat, m_ship_lon, a.first, a.second) <
                                SeaMiles(m_ship_lat, m_ship_lon, b.first, b.second);
                     });
}

// How far Vooruit looks: as far as the boat goes in half an hour, at least
// 0.3 and at most 8 sea miles.
double legend_pi::AheadRange() const
{
    return std::min(8.0, std::max(0.3, m_ship_sog * 0.5));
}

// The objects of the list in a fan ahead of the boat: a strip that widens
// with the distance (at least 0.12 sea mile on either side, widening at the
// angle of the setting), as far as AheadRange(), with a circle of 0.12 sea
// mile round the boat at its foot for what lies beside it or was just
// passed (minutes below zero).
void legend_pi::ComputeAhead()
{
    m_ahead.clear();

    const bool recent = m_ship_time > 0 && (wxGetUTCTimeMillis() - m_ship_time) < 60000;
    m_ahead_sailing = recent && !std::isnan(m_ship_cog) && !std::isnan(m_ship_sog) &&
                      m_ship_sog >= 0.5;
    if (!m_ahead_sailing) {
        m_ahead_key = "still";
        return;
    }

    const double range = AheadRange();
    EnsureVhfAreas();

    struct Found
    {
        AheadItem item;
        double along;
        double lat;
        double lon;
    };
    std::vector<Found> found;

    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        const DynamicGroup &group = m_dynamic_groups[i];
        // Lights under a row come with their row; flares double a buoy.
        if (!group.parentKey.IsEmpty() || group.lightSymbol)
            continue;
        // The signs of the VHF layer mark an area, not something in the
        // water; Vooruit tells the areas themselves apart.
        if (!group.chartSymbol &&
            m_vhf_areas.Icons().count(group.iconName.IsEmpty() ? group.key : group.iconName) > 0)
            continue;

        for (size_t j = 0; j < group.positions.size(); ++j) {
            const double lat = group.positions[j].first;
            const double lon = group.positions[j].second;
            double along = 0.0;
            double cross = 0.0;
            AlongAndAcross(m_ship_lat, m_ship_lon, m_ship_cog, lat, lon, &along, &cross);
            if (!InFan(along, cross, range, m_ahead_angle, kAheadMinHalfWidth))
                continue;

            Found f;
            f.item.group = i;
            f.item.position = j;
            f.item.minutes = along / m_ship_sog * 60.0;
            f.item.cross = cross;
            f.along = along;
            f.lat = lat;
            f.lon = lon;

            if (group.lightCharacter) {
                f.item.light = group.lightText;
            }
            else {
                for (size_t k = i + 1; k < m_dynamic_groups.size() &&
                                       m_dynamic_groups[k].parentKey == group.key;
                     ++k) {
                    for (const auto &p : m_dynamic_groups[k].positions) {
                        if (std::fabs(p.first - lat) < 0.0003 &&
                            std::fabs(p.second - lon) < 0.0005) {
                            f.item.light = m_dynamic_groups[k].lightText;
                            break;
                        }
                    }
                    if (!f.item.light.IsEmpty())
                        break;
                }
            }
            found.push_back(f);
        }
    }

    std::sort(found.begin(), found.end(),
              [](const Found &a, const Found &b) { return a.along < b.along; });

    // The same buoy from two sources (a waypoint and a chart symbol) once;
    // the first, or the one that knows its light.
    const size_t kMaxItems = 8;
    wxString key;
    for (const Found &f : found) {
        bool duplicate = false;
        for (AheadItem &kept : m_ahead) {
            const auto &p = m_dynamic_groups[kept.group].positions[kept.position];
            if (std::fabs(p.first - f.lat) < 0.0002 && std::fabs(p.second - f.lon) < 0.0003) {
                if (kept.light.IsEmpty() && !f.item.light.IsEmpty())
                    kept = f.item;
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (m_ahead.size() >= kMaxItems)
            break;
        m_ahead.push_back(f.item);
    }

    for (const AheadItem &item : m_ahead) {
        key << m_dynamic_groups[item.group].key << '@' << item.position << '/'
            << static_cast<int>(item.minutes) << (item.cross >= 0.0 ? 's' : 'p') << ';';
    }

    // Notes whose place lies in the same fan, up to three.
    m_ahead_notes.clear();
    for (const NotePlace &place : m_note_places) {
        double along = 0.0;
        double cross = 0.0;
        AlongAndAcross(m_ship_lat, m_ship_lon, m_ship_cog, place.lat, place.lon, &along,
                       &cross);
        // Its circle reaching into the fan is enough.
        if (!InFan(along, std::max(0.0, std::fabs(cross) - place.radius), range,
                   m_ahead_angle, kAheadMinHalfWidth))
            continue;
        AheadNote note;
        note.minutes = along / m_ship_sog * 60.0;
        note.cross = cross;
        note.title = place.title;
        note.path = place.path;
        m_ahead_notes.push_back(note);
    }
    std::sort(m_ahead_notes.begin(), m_ahead_notes.end(),
              [](const AheadNote &a, const AheadNote &b) { return a.minutes < b.minutes; });
    if (m_ahead_notes.size() > 3)
        m_ahead_notes.resize(3);
    for (const AheadNote &note : m_ahead_notes)
        key << "note:" << note.path << '/' << static_cast<int>(note.minutes) << ';';

    // The VHF areas the course enters in the same half hour, up to two:
    // points along the course line, a few hundred metres apart.
    m_ahead_vhf.clear();
    EnsureVhfAreas();
    if (!m_vhf_areas.IsEmpty() && VhfLayerShown()) {
        std::set<wxString> seen;
        for (const VhfArea *area : m_vhf_areas.At(m_ship_lat, m_ship_lon))
            seen.insert(area->name + "|" + area->description);
        const int steps = 120;
        for (int step = 1; step <= steps && m_ahead_vhf.size() < 2; ++step) {
            const double along = range * step / steps;
            double lat = 0.0;
            double lon = 0.0;
            PositionAt(m_ship_lat, m_ship_lon, m_ship_cog, along, 0.0, &lat, &lon);
            for (const VhfArea *area : m_vhf_areas.At(lat, lon)) {
                if (!seen.insert(area->name + "|" + area->description).second)
                    continue;
                AheadVhf next;
                next.minutes = along / m_ship_sog * 60.0;
                next.lat = lat;
                next.lon = lon;
                next.area = *area;
                m_ahead_vhf.push_back(next);
                key << "vhf:" << area->name << '/' << static_cast<int>(next.minutes) << ';';
                if (m_ahead_vhf.size() >= 2)
                    break;
            }
        }
    }
    m_ahead_key = key.IsEmpty() ? wxString("nothing") : key;
}

// What a button of "Licht herkennen" does: "reset", "stop" or
// "colour-W" (R, G, Y, or "any").
void legend_pi::RunIdentifyCommand(const wxString &what)
{
    if (what == "stop") {
        SetIdentify(false);
        return;
    }
    if (what == "reset")
        m_taps.clear();
    else if (what.StartsWith("colour-"))
        m_identify_colour = what.Mid(7) == "any" ? wxString() : what.Mid(7);
    RenderDynamicLegend(true);
}

void legend_pi::SetIdentify(bool on)
{
    m_identify_on = on;
    m_taps.clear();
    m_tap_down = false;
    if (on)
        m_tap_clock_timer.Start(100);
    else
        m_tap_clock_timer.Stop();
    if (on) {
        // Open the heading and keep the space bar for tapping.
        m_collapsed_sections.erase("@herkennen");
        if (m_markdown_view)
            m_markdown_view->SetFocus();
    }
    RenderDynamicLegend(!on);
}

void legend_pi::TapDown()
{
    const wxLongLong now = wxGetUTCTimeMillis();
    // A pause of a minute starts a new rhythm. Shorter pauses happen when
    // the light is out of sight for a while, in a swell for instance.
    if (!m_taps.empty() && (now - m_tap_last) > 60000)
        m_taps.clear();
    if (m_taps.empty())
        m_tap_origin = now;
    m_tap_start = (now - m_tap_origin).ToDouble() / 1000.0;
    m_tap_down = true;

    // The pad lights up in the colour of the light while it is held, so it
    // is clear the press counts. A red or green light lights it in its
    // colour; a white one, or any, in a warm yellow that shows on white.
    if (m_markdown_view) {
        const wxString code =
            m_identify_colour == "R" || m_identify_colour == "G" ? m_identify_colour
                                                                 : wxString("Y");
        static_cast<LegendHtmlWindow *>(m_markdown_view)
            ->SetCellBackground(kLegendTapIdPrefix + "0", LightDisplayColour(code, SchemeDim()));
    }
    UpdateTapClock();
}

// "4 tikken · licht 1,2 s", "4 tikken · donker 3,4 s": how long the light
// has been held or let go, so a long dark spell shows the count still runs,
// and when a new rhythm is about to begin.
wxString legend_pi::TapClockText() const
{
    const bool nl = m_dutch_names;
    if (m_taps.empty() && !m_tap_down)
        return nl ? "nog niet getikt" : "no taps yet";

    const wxLongLong now = wxGetUTCTimeMillis();
    wxString text = wxString::Format(nl ? "%zu tikken" : "%zu taps", m_taps.size());
    if (m_tap_down) {
        const double lit = (now - m_tap_origin).ToDouble() / 1000.0 - m_tap_start;
        text << wxString::Format(nl ? " | licht %.1f s" : " | light %.1f s", lit);
    }
    else {
        const double dark = (now - m_tap_last).ToDouble() / 1000.0;
        if (dark > 60.0)
            text << (nl ? " | donker > 1 min: de volgende tik begint opnieuw"
                        : " | dark > 1 min: the next tap starts again");
        else if (dark > 40.0)
            text << wxString::Format(nl ? " | donker %.0f s | over %.0f s begint het opnieuw"
                                        : " | dark %.0f s | starts again in %.0f s",
                                     std::floor(dark), std::ceil(60.0 - dark));
        else
            text << wxString::Format(nl ? " | donker %.1f s" : " | dark %.1f s", dark);
    }
    if (nl)
        text.Replace(".", ",");
    text.Replace(" | ", wxString::FromUTF8(" \u00b7 "));
    return text;
}

void legend_pi::UpdateTapClock()
{
    if (!m_identify_on || !m_markdown_view)
        return;
    const wxString text = TapClockText();
    if (text == g_tap_clock_text)
        return;
    g_tap_clock_text = text;
    static_cast<LegendHtmlWindow *>(m_markdown_view)->RefreshCell(kTapClockId);
}

void legend_pi::TapUp()
{
    if (!m_tap_down)
        return;
    m_tap_down = false;
    const wxLongLong now = wxGetUTCTimeMillis();
    m_tap_last = now;
    m_taps.emplace_back(m_tap_start, (now - m_tap_origin).ToDouble() / 1000.0);
    if (m_taps.size() > 60)
        m_taps.erase(m_taps.begin());

    // Drawn after the release, so the page is not replaced under a press.
    if (m_markdown_view)
        m_markdown_view->CallAfter([this]() { RenderDynamicLegend(true); });
}

// A click on a line of the "Vooruit" block: that object on the chart.
void legend_pi::ShowAheadItem(size_t index)
{
    if (index >= m_ahead.size())
        return;
    const AheadItem item = m_ahead[index];
    if (item.group >= m_dynamic_groups.size())
        return;
    DynamicGroup &group = m_dynamic_groups[item.group];
    if (item.position >= group.positions.size())
        return;

    group.current = static_cast<int>(item.position);
    m_active_group_key = group.key;
    m_active_light_key.Clear();

    const auto position = group.positions[item.position];
    LightCharacter light;
    if (!item.light.IsEmpty() && light.Parse(item.light))
        ShowLightMarkers(light, {position});
    else
        ShowMarker(position.first, position.second);

    RenderDynamicLegend(true);
}

void legend_pi::EnsureVhfAreas()
{
    if (m_vhf_loaded)
        return;
    m_vhf_loaded = true;
    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (privateDir && !privateDir->IsEmpty()) {
        const size_t count =
            m_vhf_areas.Load(*privateDir + wxFileName::GetPathSeparator() + "layers");
        wxLogMessage("Legend: %zu VHF areas from the layers", count);
    }
}

// A click on the chart: on the icon of a VHF post, its whole area lights up
// for a few seconds; elsewhere, a lit area goes out.
bool legend_pi::HighlightVhfAreaAt(const wxPoint &pixel, double tolerancePx)
{
    EnsureVhfAreas();
    if (m_vhf_areas.IsEmpty() || !m_viewport_valid)
        return false;

    PlugIn_ViewPort vp = m_viewport;
    const CachedWaypoint *hit = nullptr;
    double best = tolerancePx * tolerancePx;
    for (const CachedWaypoint &wp : m_waypoint_cache) {
        if (!wp.visible || m_vhf_areas.Icons().count(wp.icon) == 0)
            continue;
        wxPoint at;
        GetCanvasPixLL(&vp, &at, wp.lat, wp.lon);
        const double dx = at.x - pixel.x;
        const double dy = at.y - pixel.y;
        if (dx * dx + dy * dy <= best) {
            best = dx * dx + dy * dy;
            hit = &wp;
        }
    }

    if (!hit) {
        if (!m_vhf_highlight.IsEmpty())
            m_vhf_highlight.Stop();
        return false;
    }

    return HighlightVhfAreas(hit->icon, {std::make_pair(hit->lat, hit->lon)});
}

// Lights up the areas of VHF icons at these positions: from a click on the
// chart, on a row of the legend, or on its counter for all of them.
bool legend_pi::HighlightVhfAreas(const wxString &icon,
                                  const std::vector<std::pair<double, double>> &positions)
{
    EnsureVhfAreas();
    if (m_vhf_areas.Icons().count(icon) == 0)
        return false;

    // "vhf_unknown_78": the channel is the last part of the icon name.
    const wxString channel = icon.AfterLast('_');
    std::set<const VhfArea *> taken;
    std::vector<VhfArea> lit;
    for (const auto &position : positions) {
        for (const VhfArea *part :
             m_vhf_areas.AreaOfWaypoint(position.first, position.second, channel)) {
            if (taken.insert(part).second)
                lit.push_back(*part);
        }
    }
    if (lit.empty())
        return false;

    ShowVhfAreas(lit);
    return true;
}

// Lights up these areas, with a notice of their channels, as "Welk
// marifoonkanaal hier?" does.
void legend_pi::ShowVhfAreas(const std::vector<VhfArea> &areas)
{
    if (areas.empty())
        return;
    wxString text;
    std::set<wxString> said;
    for (const VhfArea &area : areas) {
        const wxString line = VhfAreaLine(area, m_dutch_names);
        if (said.insert(line).second)
            text << (text.IsEmpty() ? "" : "\n") << line;
    }
    ShowNotice(said.size() > 1 ? "Marifoongebieden" : "Marifoongebied", text, false);
    m_vhf_highlight.Show(areas);
}

// "Welk marifoonkanaal hier?" from the chart's menu.
void legend_pi::ShowVhfHere(double lat, double lon)
{
    EnsureVhfAreas();
    const std::vector<VhfArea> parts = m_vhf_areas.WithPartsAt(lat, lon);
    wxString text;
    std::set<wxString> said;
    for (const VhfArea *area : m_vhf_areas.At(lat, lon)) {
        const wxString line = VhfAreaLine(*area, m_dutch_names);
        if (said.insert(line).second)
            text << (text.IsEmpty() ? "" : "\n") << line;
    }
    if (m_vhf_areas.IsEmpty())
        text = "Geen marifoongebieden geladen (laag VHFinfo Nederland).";
    else if (text.IsEmpty())
        text = "Hier geldt geen marifoongebied uit VHFinfo Nederland.";
    ShowNotice("Marifoonkanaal op deze locatie", text, false);
    if (!parts.empty())
        m_vhf_highlight.Show(parts);
}

// Whether the layer with the VHF areas is shown in OpenCPN: one of its
// waypoints is visible.
bool legend_pi::VhfLayerShown() const
{
    for (const CachedWaypoint &wp : m_waypoint_cache) {
        if (wp.visible && m_vhf_areas.Icons().count(wp.icon) > 0)
            return true;
    }
    return false;
}

// With every position fix: has the boat sailed into a VHF area it was not
// in before? Then that area lights up, with a notice and a sound.
void legend_pi::CheckVhfAreaChange()
{
    EnsureVhfAreas();
    if (m_vhf_areas.IsEmpty() || m_waypoint_cache.empty())
        return;

    std::set<wxString> now;
    std::vector<const VhfArea *> entered;
    const bool shown = VhfLayerShown();
    if (shown) {
        for (const VhfArea *area : m_vhf_areas.At(m_ship_lat, m_ship_lon)) {
            const wxString key = area->name + "|" + area->description;
            if (!now.insert(key).second)
                continue;
            if (m_vhf_areas_known && m_vhf_areas_here.count(key) == 0)
                entered.push_back(area);
        }
    }
    m_vhf_areas_here = now;
    m_vhf_areas_known = shown;

    if (entered.empty() || !m_vhf_notice_on)
        return;

    wxString text;
    for (const VhfArea *area : entered)
        text << (text.IsEmpty() ? "" : "\n") << VhfAreaLine(*area, m_dutch_names);
    ShowNotice("Nieuw marifoongebied", text, true);
    m_vhf_highlight.Show(m_vhf_areas.WithPartsAt(m_ship_lat, m_ship_lon));
}

void legend_pi::ShowNotice(const wxString &title, const wxString &text, bool sound,
                           const wxColour &titleColour, std::function<void()> action,
                           const wxString &actionLabel)
{
    const LegendPalette palette = GetLegendPalette();
    NoticeColours colours;
    colours.title = titleColour;
    colours.background = palette.background;
    colours.text = palette.text;
    colours.frame = palette.headerRule;
    colours.accent = palette.active;
    m_notice.Show(title, text, sound, colours, action, actionLabel);
}

std::vector<const VhfArea *> legend_pi::VhfAreasHere(bool *fromShip, bool *layerShown)
{
    EnsureVhfAreas();

    // Shown only while a layer with these areas is shown in OpenCPN: one
    // of its waypoints is visible.
    *layerShown = VhfLayerShown();

    // A fix of the last minute counts as the boat's position.
    *fromShip = m_ship_time > 0 && (wxGetUTCTimeMillis() - m_ship_time) < 60000;
    const double lat = *fromShip ? m_ship_lat : m_viewport.clat;
    const double lon = *fromShip ? m_ship_lon : m_viewport.clon;

    std::vector<const VhfArea *> areas;
    if (*layerShown)
        areas = m_vhf_areas.At(lat, lon);

    m_vhf_shown_key = *layerShown ? "shown" : "hidden";
    m_vhf_shown_key << (*fromShip ? "|ship" : "|chart");
    for (const VhfArea *area : areas)
        m_vhf_shown_key << '|' << area->name;
    return areas;
}
