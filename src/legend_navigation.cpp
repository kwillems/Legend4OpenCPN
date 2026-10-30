// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, underway: the boat's position, Uitkijk, Licht herkennen and
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

    // The fan of the Uitkijk follows the boat; once more when it goes away.
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

// How far the Uitkijk looks: the sea miles of the setting
// "Uitkijkbundel: lengte", whatever the speed.
double legend_pi::AheadRange() const
{
    return m_ahead_miles;
}

wxString legend_pi::AheadTimeText(double miles) const
{
    if (!m_ahead_sailing)
        return wxEmptyString;
    return wxString::FromUTF8("\u00b1 ") +
           FormatDuration(miles / m_ship_sog * 60.0, m_dutch_names);
}

namespace {

// Cells the Uitkijk reads at most, the most detailed first: o-charts cells
// stay open in memory, plain cells are kept parsed.
const size_t kAheadOchartsCells = 12;
const size_t kAheadS57Cells = 24;

// Overview cells (band 1) carry only the largest lights; coarser cells
// than this are not read for the Uitkijk.
const int kAheadMaxCellScale = 700000;

} // namespace

// The groups of the Uitkijk for a fan of this range: its area read again
// when the fan leaves it, and grouped again when the legend was collected
// again (another scale, other settings) or the area was read again.
void legend_pi::EnsureAheadGroups(double range)
{
    double latMin = m_ship_lat;
    double latMax = m_ship_lat;
    double lonMin = m_ship_lon;
    double lonMax = m_ship_lon;
    for (const auto &corner : FanOutline(m_ship_lat, m_ship_lon, m_ship_cog, range,
                                         m_ahead_angle, kAheadMinHalfWidth)) {
        latMin = std::min(latMin, corner.first);
        latMax = std::max(latMax, corner.first);
        lonMin = std::min(lonMin, corner.second);
        lonMax = std::max(lonMax, corner.second);
    }

    const bool inside = m_ahead_area_valid && latMin >= m_ahead_area.lat_min &&
                        latMax <= m_ahead_area.lat_max && lonMin >= m_ahead_area.lon_min &&
                        lonMax <= m_ahead_area.lon_max;
    if (!inside) {
        // Room round the fan, half its length and half a mile more on every
        // side, so a turn or some sailing stays inside the area.
        const double kPi = 3.14159265358979323846;
        const double margin = (range / 2.0 + 0.5) / 60.0;
        const double cosLat = std::max(0.2, std::cos(m_ship_lat * kPi / 180.0));
        PlugIn_ViewPort area = m_viewport;
        area.lat_min = latMin - margin;
        area.lat_max = latMax + margin;
        area.lon_min = lonMin - margin / cosLat;
        area.lon_max = lonMax + margin / cosLat;
        area.clat = (area.lat_min + area.lat_max) / 2.0;
        area.clon = (area.lon_min + area.lon_max) / 2.0;
        ReadAheadArea(area);
    }

    if (!m_ahead_groups_key.IsEmpty() || !m_viewport_valid)
        return;
    m_ahead_groups_key = "grouped";

    // As the legend: what the chart does not draw at the scale of the view
    // only with the setting for those, and then in grey.
    const PlugIn_ViewPort &vp = m_viewport;
    const double scaleForLimits = m_displayed_scale > 0.0 ? m_displayed_scale : vp.chart_scale;
    std::map<wxString, DynamicGroup> groups;
    LoadMeanings();
    CollectWaypointGroups(m_ahead_area, scaleForLimits, &groups, true);
    EnsureChartSymbols();
    if (m_chart_symbols.IsLoaded()) {
        size_t probes = 0;
        int unanswered = 0;
        GroupChartObjects(m_ahead_objects, vp, &groups, true, &probes, &unanswered);
    }
    m_ahead_groups = OrderGroups(groups, m_ship_lat, m_ship_lon);
}

// Reads the chart objects of an area from the cells already open; the
// others are opened one by one in the background, after which the area is
// read again. Where cells of different scales overlap, an object of a
// coarser cell gives way to the same kind of object near it in a more
// detailed one: the same buoy, drawn a little apart.
void legend_pi::ReadAheadArea(const PlugIn_ViewPort &area)
{
    m_ahead_area = area;
    m_ahead_area_valid = true;
    m_ahead_groups_key.Clear();
    m_ahead_cells_used.clear();

    // Points spread over the area: every one gets the most detailed cell
    // that covers it, whatever the number of small harbour cells.
    std::vector<std::pair<double, double>> samples;
    const int kSteps = 8;
    for (int i = 0; i <= kSteps; ++i)
        for (int j = 0; j <= kSteps; ++j)
            samples.emplace_back(area.lat_min + (area.lat_max - area.lat_min) * i / kSteps,
                                 area.lon_min + (area.lon_max - area.lon_min) * j / kSteps);

    std::vector<ChartObject> objects;
    std::vector<wxString> unopened;
    std::vector<wxString> unread;
    if (m_ocharts.Available())
        objects = m_ocharts.PointObjectsInArea(area, kAheadMaxCellScale, kAheadOchartsCells,
                                               samples, &unopened, &m_ahead_cells_used);
    const std::vector<ChartObject> enc = m_s57.PointObjectsInArea(
        area, kAheadMaxCellScale, kAheadS57Cells, samples, &unread, &m_ahead_cells_used);
    objects.insert(objects.end(), enc.begin(), enc.end());

    std::stable_sort(objects.begin(), objects.end(),
                     [](const ChartObject &a, const ChartObject &b) {
                         return a.cellScale < b.cellScale;
                     });
    const double kPi = 3.14159265358979323846;
    const double cosLat = std::max(0.2, std::cos(area.clat * kPi / 180.0));
    const double kBucket = 0.02; // degrees, more than the largest tolerance
    std::map<std::pair<long, long>, std::vector<size_t>> buckets;
    m_ahead_objects.clear();
    for (const ChartObject &object : objects) {
        // Half a millimetre on the coarser chart, at least 50 metres.
        const double tolerance = std::max(50.0, object.cellScale * 0.0005) / 1852.0 / 60.0;
        const long bLat = static_cast<long>(std::floor(object.lat / kBucket));
        const long bLon = static_cast<long>(std::floor(object.lon / kBucket));
        bool duplicate = false;
        for (long dLat = -1; dLat <= 1 && !duplicate; ++dLat) {
            for (long dLon = -1; dLon <= 1 && !duplicate; ++dLon) {
                auto bucket = buckets.find(std::make_pair(bLat + dLat, bLon + dLon));
                if (bucket == buckets.end())
                    continue;
                for (size_t index : bucket->second) {
                    const ChartObject &kept = m_ahead_objects[index];
                    if (kept.cellScale >= object.cellScale ||
                        kept.className != object.className)
                        continue;
                    const double dlat = kept.lat - object.lat;
                    const double dlon = (kept.lon - object.lon) * cosLat;
                    if (dlat * dlat + dlon * dlon <= tolerance * tolerance) {
                        duplicate = true;
                        break;
                    }
                }
            }
        }
        if (duplicate)
            continue;
        buckets[std::make_pair(bLat, bLon)].push_back(m_ahead_objects.size());
        m_ahead_objects.push_back(object);
    }

    m_ahead_cells_to_open.clear();
    for (const wxString &path : unopened)
        if (m_ahead_cells_tried.count(path) == 0)
            m_ahead_cells_to_open.emplace_back(true, path);
    for (const wxString &path : unread)
        if (m_ahead_cells_tried.count(path) == 0)
            m_ahead_cells_to_open.emplace_back(false, path);
    m_ahead_reading = !m_ahead_cells_to_open.empty();
    wxLogMessage("Legend: lookout area %.3f..%.3f N, %.3f..%.3f E: %zu cells read, "
                 "%zu to open, %zu objects",
                 area.lat_min, area.lat_max, area.lon_min, area.lon_max,
                 m_ahead_cells_used.size(), m_ahead_cells_to_open.size(),
                 m_ahead_objects.size());
    if (m_ahead_reading && !m_ahead_cell_timer.IsRunning())
        m_ahead_cell_timer.StartOnce(50);
}

// One cell of the Uitkijk's area opened; after the last, the area is read
// again and the list follows.
void legend_pi::OpenNextAheadCell()
{
    if (m_ahead_cells_to_open.empty())
        return;
    // The legend's own reading of the view goes first.
    if (m_cell_timer.IsRunning()) {
        m_ahead_cell_timer.StartOnce(200);
        return;
    }
    const auto cell = m_ahead_cells_to_open.front();
    m_ahead_cells_to_open.erase(m_ahead_cells_to_open.begin());
    m_ahead_cells_tried.insert(cell.second);
    if (cell.first)
        m_ocharts.OpenCell(cell.second);
    else
        m_s57.ReadCell(cell.second);

    if (!m_ahead_cells_to_open.empty()) {
        m_ahead_cell_timer.StartOnce(50);
        return;
    }
    m_ahead_area_valid = false;
    m_ahead_reading = false;
    if (IsDynamicLegendVisible())
        RenderDynamicLegend(true);
}

// Whether every part of the fan lies in a cell the Uitkijk read: points
// along the course line and its edges. Without readable charts at all
// there is nothing to say.
bool legend_pi::AheadCovered(double range) const
{
    if (!m_chart_sources_available)
        return true;
    const double widen = std::tan(m_ahead_angle * 3.14159265358979323846 / 180.0);
    for (int step = 0; step <= 8; ++step) {
        const double along = range * step / 8.0;
        const double half = std::max(kAheadMinHalfWidth, along * widen);
        for (double across : {-half, 0.0, half}) {
            double lat = 0.0;
            double lon = 0.0;
            PositionAt(m_ship_lat, m_ship_lon, m_ship_cog, along, across, &lat, &lon);
            bool covered = false;
            for (const CellExtent &cell : m_ahead_cells_used) {
                if (lat >= cell.latMin && lat <= cell.latMax && lon >= cell.lonMin &&
                    lon <= cell.lonMax) {
                    covered = true;
                    break;
                }
            }
            if (!covered)
                return false;
        }
    }
    return true;
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
    // Not only what is in view: the Uitkijk reads the fan's own area.
    EnsureAheadGroups(range);
    m_ahead_uncovered = !m_ahead_reading && !AheadCovered(range);

    struct Found
    {
        AheadItem item;
        double along;
        double lat;
        double lon;
    };
    std::vector<Found> found;

    for (size_t i = 0; i < m_ahead_groups.size(); ++i) {
        const DynamicGroup &group = m_ahead_groups[i];
        // Lights under a row come with their row; flares double a buoy.
        if (!group.parentKey.IsEmpty() || group.lightSymbol)
            continue;
        // The signs of the VHF layer mark an area, not something in the
        // water; the Uitkijk tells the areas themselves apart.
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
                for (size_t k = i + 1; k < m_ahead_groups.size() &&
                                       m_ahead_groups[k].parentKey == group.key;
                     ++k) {
                    for (const auto &p : m_ahead_groups[k].positions) {
                        if (std::fabs(p.first - lat) < 0.0003 &&
                            std::fabs(p.second - lon) < 0.0005) {
                            f.item.light = m_ahead_groups[k].lightText;
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
    // the first, or the one that knows its light. All of the fan is listed:
    // what lies in the beam on the chart is in the list too.
    wxString key;
    for (const Found &f : found) {
        bool duplicate = false;
        for (AheadItem &kept : m_ahead) {
            const auto &p = m_ahead_groups[kept.group].positions[kept.position];
            if (std::fabs(p.first - f.lat) < 0.0002 && std::fabs(p.second - f.lon) < 0.0003) {
                if (kept.light.IsEmpty() && !f.item.light.IsEmpty())
                    kept = f.item;
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        m_ahead.push_back(f.item);
    }

    for (const AheadItem &item : m_ahead) {
        key << m_ahead_groups[item.group].key << '@' << item.position << '/'
            << static_cast<int>(item.minutes) << (item.cross >= 0.0 ? 's' : 'p')
            << (m_ahead_groups[item.group].hiddenAtScale ? "h" : "") << ';';
    }
    if (m_ahead_reading)
        key << "reading;";
    if (m_ahead_uncovered)
        key << "uncovered;";

    // Notes whose place lies in the same fan.
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
        // As in the list: a sign the chart does not draw at this scale
        // only with the setting for those, and then in grey.
        const bool hidden = NoteHiddenAtScale(place);
        if (hidden && !m_show_hidden_at_scale)
            continue;
        AheadNote note;
        note.hidden = hidden;
        note.scamin = place.scamin;
        note.minutes = along / m_ship_sog * 60.0;
        note.cross = cross;
        note.title = place.title;
        note.path = place.path;
        m_ahead_notes.push_back(note);
    }
    std::sort(m_ahead_notes.begin(), m_ahead_notes.end(),
              [](const AheadNote &a, const AheadNote &b) { return a.minutes < b.minutes; });
    for (const AheadNote &note : m_ahead_notes)
        key << "note:" << note.path << '/' << static_cast<int>(note.minutes)
            << (note.hidden ? "h" : "") << ';';

    // The VHF areas the course enters as far ahead, up to two:
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

// A click on a line of the "Uitkijk" block: that object on the chart; when
// it is hidden at this scale, zoomed in to just past its limit first, as
// for a grey row of the list.
void legend_pi::ShowAheadItem(size_t index)
{
    if (index >= m_ahead.size())
        return;
    const AheadItem item = m_ahead[index];
    if (item.group >= m_ahead_groups.size())
        return;
    const DynamicGroup &group = m_ahead_groups[item.group];
    if (item.position >= group.positions.size())
        return;

    // Its row in the legend, when it has one in view, is lit.
    m_active_group_key = group.key;
    m_active_light_key.Clear();

    // The Uitkijk also lists what lies beyond the view: the chart goes
    // there, at the same scale.
    const auto position = group.positions[item.position];
    const bool inView = m_viewport_valid && position.first >= m_viewport.lat_min &&
                        position.first <= m_viewport.lat_max &&
                        position.second >= m_viewport.lon_min &&
                        position.second <= m_viewport.lon_max;
    if (group.hiddenAtScale) {
        const double zoom = ViewScaleForLimit(group.visibleFrom);
        if (zoom > 0.0)
            JumpTo(position.first, position.second, zoom);
    }
    else if (!inView && m_viewport_valid) {
        JumpTo(position.first, position.second, m_viewport.view_scale_ppm);
    }
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
