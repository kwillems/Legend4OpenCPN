// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, collecting: the waypoints, meanings and chart objects in view,
// grouped into the rows of the dynamic legend.

#include "legend_pi.h"

#include "legend_html_window.h"
#include "legend_shared.h"

#include <wx/gdicmn.h>
#include <wx/file.h>
#include <wx/fs_mem.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stopwatch.h>
#include <wx/textfile.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <memory>
#include <set>

namespace {

// Chart lights are paired with the buoy or beacon at the same spot through
// a grid of cells of this size in degrees, a few metres.
const double kCarrierCell = 6e-5;

struct PendingLight
{
    LightCharacter light;
    double lat;
    double lon;
    int hiddenFrom; // scale limit when not drawn at this scale, else 0
    bool sector;    // one sector of a sector light
    LightSector part;
};

// An object a light can stand on, and the row it is listed in. Rank 0 for
// buoys and beacons, 1 for other structures.
struct Carrier
{
    double lat;
    double lon;
    wxString groupKey;
    int rank;
};

std::pair<long, long> CarrierCell(double lat, double lon)
{
    return std::make_pair(static_cast<long>(std::floor(lat / kCarrierCell)),
                          static_cast<long>(std::floor(lon / kCarrierCell)));
}

// First non-empty line of a waypoint description, or empty when that line
// cannot be a meaning (too long, or only a link).
wxString FirstDescriptionLine(const wxString &description)
{
    wxString text = description;
    text.Replace("\r", "\n");

    wxString line;
    for (const wxString &part : wxSplit(text, '\n', '\0')) {
        line = part;
        line.Trim(true).Trim(false);
        if (!line.IsEmpty())
            break;
    }

    if (line.length() > 160 || line.StartsWith("http://") ||
        line.StartsWith("https://"))
        return wxString();

    return line;
}

// Written once into the legend folder, so the format explains itself.
const char *kMeaningsTemplate =
    "# Betekenis en indeling van symbolen in de dynamische legenda van\n"
    "# Legend4OpenCPN.\n"
    "#\n"
    "# Per regel: icoonnaam = betekenis\n"
    "# De icoonnaam is de bestandsnaam van het icoon zonder .png of .svg,\n"
    "# zoals de legenda hem in grijs toont zolang er geen betekenis bekend\n"
    "# is.\n"
    "#\n"
    "# Een '*' in de icoonnaam staat voor een willekeurig stuk tekst. Een '*'\n"
    "# in de betekenis wordt vervangen door dat stuk. Exacte namen gaan voor,\n"
    "# daarna telt het eerste passende patroon.\n"
    "#\n"
    "# Een regel [Kopje] begint een groep in de legenda. Kopjes en regels\n"
    "# verschijnen in de volgorde van dit bestand; wat nergens onder valt,\n"
    "# komt onder 'Overig'. Een lege betekenis ('vhf_* =') deelt een icoon\n"
    "# alleen in; de tekst komt dan uit de beschrijving van de waypoints.\n"
    "#\n"
    "# Twee talen: 'betekenis | meaning' en '[Kopje | Heading]'. De legenda\n"
    "# toont het deel van de gekozen taal; ontbreekt dat, dan het andere.\n"
    "#\n"
    "# Regels die met # beginnen worden overgeslagen. Wijzigingen zijn\n"
    "# zichtbaar na Ververs of zodra de kaart verschuift.\n"
    "#\n"
    "# Voorbeeld:\n"
    "# [Marifoon]\n"
    "# vhf_b11b_*    = Meldplicht marifoon, kanaal *\n"
    "# vhf_*         =\n"
    "#\n"
    "# [Betonning | Buoyage]\n"
    "# 011           = Groene spitse ton | Green conical buoy\n"
    "\n";

}

void legend_pi::RefreshDynamicLegendKeepingState()
{
    // New chart cells in view: open them first, then come back.
    if (OpenChartCellsFirst([this]() {
            if (IsDynamicLegendVisible())
                RefreshDynamicLegendKeepingState();
        }))
        return;

    const std::vector<DynamicGroup> previous = m_dynamic_groups;

    CollectDynamicLegend();

    auto sortedPositions = [](const DynamicGroup &group) {
        auto positions = group.positions;
        std::sort(positions.begin(), positions.end());
        return positions;
    };

    std::map<wxString, const DynamicGroup *> previousByKey;
    for (const DynamicGroup &group : previous)
        previousByKey[group.key] = &group;

    bool changed = previous.size() != m_dynamic_groups.size();

    for (DynamicGroup &group : m_dynamic_groups) {
        auto found = previousByKey.find(group.key);
        if (found == previousByKey.end()) {
            changed = true;
            continue;
        }

        const DynamicGroup &old = *found->second;

        // A change in symbolen.txt changes the text or place of the row.
        if (old.label != group.label || old.section != group.section ||
            old.order != group.order)
            changed = true;

        // Same waypoints as before: keep the old order and counter, so
        // clicking on continues through the same sequence. Re-sorting
        // around the new centre would send the next click back to the
        // object just visited.
        if (sortedPositions(old) == sortedPositions(group)) {
            group.positions = old.positions;
            group.current = old.current;
            continue;
        }

        changed = true;

        // The set changed; continue from the object shown last if it is
        // still in view.
        if (old.current >= 0) {
            const auto &shown =
                old.positions[static_cast<size_t>(old.current)];

            for (size_t i = 0; i < group.positions.size(); ++i) {
                if (group.positions[i] == shown) {
                    group.current = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // The VHF banner follows the middle of the chart when there is no
    // position fix: a move into another area counts as a change.
    if (!changed) {
        const wxString previous = m_vhf_shown_key;
        bool fromShip = false;
        bool layerShown = false;
        VhfAreasHere(&fromShip, &layerShown);
        changed = m_vhf_shown_key != previous;
    }

    // Nothing to show differently: leave the window alone.
    if (!changed && !m_showing_loading_page) {
        m_dynamic_groups = previous;
        return;
    }

    RenderDynamicLegend(!m_showing_loading_page);
}

void legend_pi::ClearDynamicImages()
{
    for (const wxString &name : m_dynamic_image_names)
        wxMemoryFSHandler::RemoveFile(name);

    m_dynamic_image_names.clear();
}

wxString legend_pi::GetMeaningsFile() const
{
    return GetLegendDirectory() + wxFileName::GetPathSeparator() +
           "symbolen.txt";
}

void legend_pi::LoadMeanings()
{
    const wxString path = GetMeaningsFile();

    if (!wxFileName::FileExists(path)) {
        wxFile file;
        if (file.Create(path, false))
            file.Write(wxString::FromUTF8(kMeaningsTemplate), wxConvUTF8);
    }

    // Read again only when the file changed.
    const wxDateTime modified = wxFileName(path).GetModificationTime();
    if (modified.IsValid() && m_meanings_modified.IsValid() &&
        modified == m_meanings_modified)
        return;

    m_meanings_modified = modified;
    m_meaning_sections.clear();
    m_meanings.clear();
    m_meanings_exact.clear();
    m_meanings_patterns.clear();

    wxTextFile file;
    if (!file.Open(path, wxConvUTF8))
        return;

    int section = -1;

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        wxString line = file[i];
        line.Trim(true).Trim(false);

        if (line.IsEmpty() || line.StartsWith("#"))
            continue;

        if (line.StartsWith("[") && line.EndsWith("]")) {
            wxString heading = line.Mid(1, line.length() - 2);
            heading.Trim(true).Trim(false);
            m_meaning_sections.push_back(heading);
            section = static_cast<int>(m_meaning_sections.size()) - 1;
            continue;
        }

        const int equals = line.Find('=');
        if (equals == wxNOT_FOUND)
            continue;

        SymbolMeaning entry;
        entry.name = line.Left(equals);
        entry.meaning = line.Mid(equals + 1);
        entry.name.Trim(true).Trim(false);
        entry.meaning.Trim(true).Trim(false);
        entry.section = section;
        entry.order = static_cast<int>(m_meanings.size());

        if (entry.name.IsEmpty())
            continue;

        const size_t index = m_meanings.size();
        const bool pattern = entry.name.Find('*') != wxNOT_FOUND;
        const wxString name = entry.name;
        m_meanings.push_back(entry);

        if (pattern)
            m_meanings_patterns.push_back(index);
        else if (m_meanings_exact.find(name) == m_meanings_exact.end())
            m_meanings_exact[name] = index;
    }

    wxLogMessage(
        "Legend: %zu symbol lines, %zu headings from %s",
        m_meanings.size(),
        m_meaning_sections.size(),
        path);
}

void legend_pi::BuildCommonLines()
{
    struct State
    {
        wxString line;
        int count = 0;
        bool mixed = false;
    };

    std::map<wxString, State> states;

    for (const CachedWaypoint &wp : m_waypoint_cache) {
        State &state = states[wp.icon];

        if (state.mixed)
            continue;

        if (wp.firstLine.IsEmpty() ||
            (state.count > 0 && wp.firstLine != state.line)) {
            state.mixed = true;
            continue;
        }

        state.line = wp.firstLine;
        ++state.count;
    }

    // A line shared by a single waypoint says something about that object,
    // not about the symbol. Short lines such as a colour ("rood") are kept
    // as a first indication; symbolen.txt can replace them.
    m_common_lines.clear();
    for (const auto &entry : states) {
        const State &state = entry.second;

        if (!state.mixed && state.count >= 2)
            m_common_lines[entry.first] = state.line;
    }
}

// Text, heading and position of a symbol. The text, best source first:
// 1. symbolen.txt (exact name, then the first matching pattern);
// 2. the first description line shared by all waypoints with this icon;
// 3. OpenCPN's own description of the icon, when it differs from the name;
// 4. the icon name itself (labelIsName is then set).
// A line in symbolen.txt with an empty meaning only sets heading and
// position; the text then comes from 2-4.
void legend_pi::ResolveLabel(
    const wxString &key,
    const wxString &iconDescription,
    DynamicGroup &group) const
{
    group.labelIsName = false;
    group.section = -1;
    group.order = 1 << 30;

    const SymbolMeaning *entry = nullptr;
    wxString meaning;

    auto exact = m_meanings_exact.find(key);
    if (exact != m_meanings_exact.end()) {
        entry = &m_meanings[exact->second];
        meaning = PickLanguage(entry->meaning);
    }
    else {
        for (size_t index : m_meanings_patterns) {
            const SymbolMeaning &pattern = m_meanings[index];
            const int star = pattern.name.Find('*');
            const wxString prefix = pattern.name.Left(star);
            const wxString suffix = pattern.name.Mid(star + 1);

            if (key.length() < prefix.length() + suffix.length() ||
                !key.StartsWith(prefix) || !key.EndsWith(suffix))
                continue;

            const wxString part = key.Mid(
                prefix.length(),
                key.length() - prefix.length() - suffix.length());

            entry = &pattern;
            meaning = PickLanguage(pattern.meaning);
            meaning.Replace("*", part);
            break;
        }
    }

    if (entry) {
        group.section = entry->section;
        group.order = entry->order;
    }

    if (!meaning.IsEmpty()) {
        group.label = meaning;
        return;
    }

    auto common = m_common_lines.find(key);
    if (common != m_common_lines.end()) {
        group.label = common->second;
        return;
    }

    if (!iconDescription.IsEmpty() && iconDescription != key) {
        group.label = iconDescription;
        return;
    }

    group.labelIsName = true;
    group.label = key;
}

namespace {

CachedWaypoint CachedFromWaypoint(const PlugIn_Waypoint_Ex &wp)
{
    CachedWaypoint entry;
    entry.lat = wp.m_lat;
    entry.lon = wp.m_lon;
    entry.icon = wp.IconName;
    entry.description = wp.IconDescription;
    entry.firstLine = FirstDescriptionLine(wp.m_MarkDescription);
    entry.visible = wp.IsVisible;
    entry.useScamin = wp.b_useScamin;
    entry.scamin = wp.scamin;

    LightCharacter light;
    if (light.FindInWaypoint(wp.m_MarkName, wp.m_MarkDescription))
        entry.light = light.Abbreviation();
    return entry;
}

bool SamePoint(const PlugIn_Waypoint_Ex &wp, const LayerWaypoint &point)
{
    return std::fabs(wp.m_lat - point.lat) < 1e-6 && std::fabs(wp.m_lon - point.lon) < 1e-6 &&
           wp.IconName == point.icon;
}

} // namespace

// The waypoints of the layer files, taken from the files themselves. Of
// each layer OpenCPN is asked only its first and last point, which find it
// in OpenCPN's list and tell whether the layer is shown, and one point per
// icon for the icon's description. False when the files do not match
// OpenCPN's list: then everything comes from OpenCPN, as before.
bool legend_pi::PrefillFromLayers(const wxArrayString &guids)
{
    m_loading_prefilled.clear();
    m_loading_have.clear();

    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (!privateDir || privateDir->IsEmpty())
        return false;

    wxStopWatch watch;
    m_layer_waypoints.Load(*privateDir + wxFileName::GetPathSeparator() + "layers");
    const std::vector<LayerFile> &files = m_layer_waypoints.Files();
    if (files.empty())
        return false;
    const long readMs = watch.Time();

    const size_t count = guids.GetCount();
    std::vector<CachedWaypoint> prefilled(count);
    std::vector<char> have(count, 0);
    std::vector<char> inLayer(count, 0);
    std::vector<bool> used(files.size(), false);
    int asked = 0;
    auto fetch = [&](size_t index) {
        ++asked;
        return GetWaypointEx_Plugin(guids[index]);
    };

    size_t pos = 0;
    size_t own = 0;
    size_t layers = 0;
    while (pos < count) {
        std::unique_ptr<PlugIn_Waypoint_Ex> first = fetch(pos);
        int layer = -1;
        if (first) {
            for (size_t f = 0; f < files.size() && layer < 0; ++f) {
                const std::vector<LayerWaypoint> &points = files[f].points;
                if (used[f] || points.size() > count - pos || !SamePoint(*first, points.front()))
                    continue;
                std::unique_ptr<PlugIn_Waypoint_Ex> last = fetch(pos + points.size() - 1);
                if (last && SamePoint(*last, points.back()))
                    layer = static_cast<int>(f);
            }
        }

        // Not where a layer starts: an own waypoint, as OpenCPN gives it.
        if (layer < 0) {
            if (first) {
                prefilled[pos] = CachedFromWaypoint(*first);
                have[pos] = 1;
            }
            ++pos;
            // Many of those: the files do not tell much, so stop asking twice.
            if (++own > 500)
                return false;
            continue;
        }

        used[layer] = true;
        ++layers;
        const std::vector<LayerWaypoint> &points = files[layer].points;
        // Hiding a layer hides all of its waypoints.
        const bool visible = first->IsVisible;
        for (size_t i = 0; i < points.size(); ++i) {
            const LayerWaypoint &point = points[i];
            auto description = m_icon_descriptions.find(point.icon);
            if (description == m_icon_descriptions.end()) {
                wxString text = first->IconDescription;
                if (i > 0) {
                    std::unique_ptr<PlugIn_Waypoint_Ex> wp = fetch(pos + i);
                    text = wp ? wp->IconDescription : wxString();
                }
                description = m_icon_descriptions.emplace(point.icon, text).first;
            }

            CachedWaypoint &entry = prefilled[pos + i];
            entry.lat = point.lat;
            entry.lon = point.lon;
            entry.icon = point.icon;
            entry.description = description->second;
            entry.firstLine = FirstDescriptionLine(point.description);
            entry.visible = visible;
            entry.useScamin = point.useScamin;
            entry.scamin = point.scamin;
            LightCharacter light;
            if (light.FindInWaypoint(point.name, point.description))
                entry.light = light.Abbreviation();
            have[pos + i] = 1;
            inLayer[pos + i] = 1;
        }
        pos += points.size();
    }

    // A check of points spread over the layers against OpenCPN.
    std::vector<size_t> layered;
    for (size_t i = 0; i < count; ++i) {
        if (inLayer[i])
            layered.push_back(i);
    }
    const size_t kChecks = 24;
    for (size_t k = 0; k < kChecks && !layered.empty(); ++k) {
        const size_t index = layered[(k * 7919 + 13) % layered.size()];
        std::unique_ptr<PlugIn_Waypoint_Ex> wp = fetch(index);
        const CachedWaypoint &entry = prefilled[index];
        if (!wp || std::fabs(wp->m_lat - entry.lat) > 1e-6 ||
            std::fabs(wp->m_lon - entry.lon) > 1e-6 || wp->IconName != entry.icon ||
            wp->IsVisible != entry.visible ||
            wp->b_useScamin != entry.useScamin ||
            (entry.useScamin && std::fabs(wp->scamin - entry.scamin) > 0.5) ||
            FirstDescriptionLine(wp->m_MarkDescription) != entry.firstLine) {
            wxLogMessage("Legend: layer files do not match OpenCPN's waypoints at %zu, "
                         "asking OpenCPN for each",
                         index);
            return false;
        }
    }

    m_loading_prefilled.swap(prefilled);
    m_loading_have.swap(have);
    wxLogMessage("Legend: %zu of %zu waypoints from %zu layer files (read in %ld ms), "
                 "%d asked of OpenCPN, %ld ms",
                 layered.size(), count, layers, readMs, asked, watch.Time());
    return true;
}

bool legend_pi::StartWaypointLoading(bool reloadAll)
{
    // A loading run is busy; its end refreshes the legend. Only a request
    // to reload everything restarts it.
    if (m_loading && !(reloadAll && !m_loading_reload))
        return false;

    const wxArrayString guids = GetWaypointGUIDArray();

    // Unchanged set of waypoints: nothing to ask OpenCPN.
    if (!reloadAll && !m_loading && guids == m_waypoint_guids)
        return true;

    m_loading_known.clear();
    if (!reloadAll) {
        for (size_t i = 0; i < m_waypoint_guids.GetCount(); ++i)
            m_loading_known[m_waypoint_guids[i]] = i;
    }

    m_loading = true;
    m_loading_reload = reloadAll;
    m_loading_guids = guids;
    m_loading_cache.clear();
    m_loading_cache.reserve(guids.GetCount());
    m_loading_next = 0;
    m_loading_fetched = 0;
    m_loading_started = wxGetLocalTimeMillis();

    // The first time the layer files are read, half a second at once: in a
    // timer step, so the window can show that it is loading first.
    if (m_waypoint_cache.empty()) {
        m_loading_prefill_pending = true;
        m_loading_timer.Start(10);
        return false;
    }
    m_loading_prefill_pending = false;
    PrefillFromLayers(guids);

    size_t missing = 0;
    for (size_t i = 0; i < guids.GetCount(); ++i) {
        if (i < m_loading_have.size() && m_loading_have[i])
            continue;
        if (m_loading_known.find(guids[i]) == m_loading_known.end())
            ++missing;
    }

    // A handful of new waypoints is quicker to fetch right away.
    if (missing < 500) {
        while (m_loading)
            OnLoadingTimer();
        return true;
    }

    m_loading_timer.Start(10);
    return false;
}

void legend_pi::OnLoadingTimer()
{
    if (!m_loading) {
        m_loading_timer.Stop();
        return;
    }

    if (m_loading_prefill_pending) {
        m_loading_prefill_pending = false;
        PrefillFromLayers(m_loading_guids);
        return;
    }

    // Work for a short moment per step, then give OpenCPN room to paint
    // and handle input.
    wxStopWatch step;

    while (m_loading_next < m_loading_guids.GetCount() &&
           step.Time() < 40) {
        const size_t index = m_loading_next++;
        const wxString &guid = m_loading_guids[index];

        if (index < m_loading_have.size() && m_loading_have[index]) {
            m_loading_cache.push_back(m_loading_prefilled[index]);
            continue;
        }

        auto found = m_loading_known.find(guid);
        if (found != m_loading_known.end()) {
            m_loading_cache.push_back(m_waypoint_cache[found->second]);
            continue;
        }

        CachedWaypoint entry;
        std::unique_ptr<PlugIn_Waypoint_Ex> wp = GetWaypointEx_Plugin(guid);
        ++m_loading_fetched;

        if (wp)
            entry = CachedFromWaypoint(*wp);

        m_loading_cache.push_back(entry);
    }

    if (m_loading_next >= m_loading_guids.GetCount()) {
        FinishWaypointLoading();
        return;
    }

    if (m_showing_loading_page && IsDynamicLegendVisible())
        RenderLoadingPage();
}

void legend_pi::FinishWaypointLoading()
{
    m_loading_timer.Stop();
    m_loading = false;

    m_waypoint_guids = m_loading_guids;
    m_waypoint_cache.swap(m_loading_cache);
    m_loading_cache.clear();
    m_loading_known.clear();
    m_loading_guids.Clear();
    m_loading_prefilled.clear();
    m_loading_have.clear();

    BuildCommonLines();

    wxLogMessage(
        "Legend: waypoint cache %zu waypoints, %d fetched in %lld ms",
        m_waypoint_cache.size(),
        m_loading_fetched,
        static_cast<long long>(
            (wxGetLocalTimeMillis() - m_loading_started).GetValue()));

    if (IsDynamicLegendVisible())
        RefreshDynamicLegendKeepingState();

    if (m_pending_reveal) {
        m_pending_reveal = false;
        RevealObjectAt(m_pending_reveal_lat, m_pending_reveal_lon,
                       m_pending_reveal_tolerance, false);
    }
}

void legend_pi::RenderLoadingPage()
{
    const size_t total = m_loading ? m_loading_guids.GetCount() : 0;
    RenderProgressPage("Waypoints worden geladen", m_loading_next, total);
}

// Opening a chart cell takes a fifth of a second. With more than one to
// open for the view, they are opened one per timer step, so OpenCPN stays
// usable and the legend shows how far it is; then `then` runs, and finds
// every cell open. False when there is nothing worth waiting for.
bool legend_pi::OpenChartCellsFirst(std::function<void()> then)
{
    if (m_cell_timer.IsRunning()) {
        m_after_cells = then;
        return true;
    }
    if (!m_viewport_valid)
        return false;

    m_cells_to_open.clear();
    PlugIn_ViewPort vp = m_viewport;
    if (m_ocharts.Available()) {
        for (const wxString &path : m_ocharts.UnopenedCells(vp))
            m_cells_to_open.emplace_back(true, path);
    }
    for (const wxString &path : m_s57.UnreadCells(vp))
        m_cells_to_open.emplace_back(false, path);
    if (m_cells_to_open.size() < 2)
        return false;

    m_cells_opened = 0;
    m_after_cells = then;
    if (IsDynamicLegendVisible())
        RenderProgressPage("Kaarten worden gelezen", 0, m_cells_to_open.size());
    m_cell_timer.StartOnce(10);
    return true;
}

void legend_pi::OpenNextChartCell()
{
    if (m_cells_opened < m_cells_to_open.size()) {
        const auto &cell = m_cells_to_open[m_cells_opened++];
        if (cell.first)
            m_ocharts.OpenCell(cell.second);
        else
            m_s57.ReadCell(cell.second);
    }
    if (m_cells_opened < m_cells_to_open.size()) {
        if (IsDynamicLegendVisible())
            RenderProgressPage("Kaarten worden gelezen", m_cells_opened,
                               m_cells_to_open.size());
        m_cell_timer.StartOnce(10);
        return;
    }

    m_cells_to_open.clear();
    std::function<void()> then = m_after_cells;
    m_after_cells = std::function<void()>();
    if (then)
        then();
}

// What the legend waits for, with a bar that fills and "1.200 van 17.350".
void legend_pi::RenderProgressPage(const wxString &what, size_t done, size_t total)
{
    if (!m_markdown_view)
        return;

    const LegendPalette palette = GetLegendPalette();

    wxString html;
    html << "<html>" << LegendBodyTag(true)
         << "<p><font size=\"+1\"><b>Kaartlegenda</b></font></p>"
         << "<p>" << what << "&hellip;</p>";

    if (total > 0) {
        const int percent =
            std::max(1, std::min(100, static_cast<int>(done * 100 / total)));
        auto number = [](size_t value) {
            wxString text = wxString::Format("%zu", value);
            for (int at = static_cast<int>(text.length()) - 3; at > 0; at -= 3)
                text.insert(at, ".");
            return text;
        };
        html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" border=\"0\"><tr>"
             << "<td width=\"" << percent << "%\" bgcolor=\"" << HtmlColour(palette.text)
             << "\"><font size=\"1\">&nbsp;</font></td>";
        if (percent < 100)
            html << "<td bgcolor=\"" << HtmlColour(palette.rule)
                 << "\"><font size=\"1\">&nbsp;</font></td>";
        html << "</tr></table>"
             << "<p><font color=\"" << HtmlColour(palette.muted) << "\">" << number(done)
             << " van " << number(total) << "</font></p>";
    }

    html << "</body></html>";

    static_cast<LegendHtmlWindow *>(m_markdown_view)->SetDynamicPage(html);
    m_showing_loading_page = true;
}

double legend_pi::DisplayedScale(const PlugIn_ViewPort &vp) const
{
    if (vp.pix_width <= 0 || vp.view_scale_ppm <= 0.0)
        return 0.0;

    // Pixels per true metre: a short north-south line through the centre.
    PlugIn_ViewPort view = vp;
    const double halfDegrees = 0.005; // about 556 m each way
    wxPoint south;
    wxPoint north;
    GetCanvasPixLL(&view, &south, vp.clat - halfDegrees, vp.clon);
    GetCanvasPixLL(&view, &north, vp.clat + halfDegrees, vp.clon);
    const double pixels = std::hypot(
        static_cast<double>(north.x - south.x),
        static_cast<double>(north.y - south.y));
    const double metres = 2.0 * halfDegrees * 111194.9;
    if (pixels <= 0.0)
        return 0.0;
    const double truePpm = pixels / metres;

    // Physical pixels per metre of the display: logical size in pixels and
    // in millimetres, times the content scale of a Retina display.
    double displayPpm = 0.0;
    const wxSize sizePx = wxGetDisplaySize();
    const wxSize sizeMm = wxGetDisplaySizeMM();
    double content = 1.0;
    if (wxWindow *canvas = GetOCPNCanvasWindow())
        content = canvas->GetContentScaleFactor();
    if (sizePx.x > 0 && sizeMm.x > 0)
        displayPpm = sizePx.x * content / (sizeMm.x / 1000.0);
    if (displayPpm <= 0.0)
        displayPpm = 96.0 / 0.0254 * content;

    return displayPpm / truePpm;
}

void legend_pi::EnsureChartSymbols()
{
    if (m_chart_symbols.IsLoaded() || m_chart_symbols_tried)
        return;
    m_chart_symbols_tried = true;

    wxString *shared = GetpSharedDataLocation();
    wxFileName dir(shared ? *shared : wxString(), "");
    dir.AppendDir("s57data");

    wxStopWatch watch;
    const bool ok = m_chart_symbols.Load(dir.GetPath());
    wxLogMessage("Legend: S-52 tables %s from %s, %ld ms",
                 ok ? "loaded" : "NOT found", dir.GetPath(), watch.Time());

    LoadChartNames();
}

// Dutch names of chart objects: the table shipped with the plugin, then a
// file of the same name in the legend folder, whose lines go first.
void legend_pi::LoadChartNames()
{
    m_chart_names.Clear();

    const wxString shipped = GetPluginDataDir("legend_pi") +
                             wxFileName::GetPathSeparator() + "data" +
                             wxFileName::GetPathSeparator() + "s57-nl.txt";
    const wxString own = GetLegendDirectory() + wxFileName::GetPathSeparator() +
                         "s57-nl.txt";

    const bool shippedOk = m_chart_names.Load(shipped);
    const bool ownOk = m_chart_names.Load(own);
    wxLogMessage("Legend: chart names %s%s", shippedOk ? "loaded" : "NOT found",
                 ownOk ? ", own s57-nl.txt applied" : "");
}

// Adds the chart objects of the o-charts cells in view to the groups, one
// group per S-52 symbol, so the legend shows the symbols as the chart draws
// them. Objects the chart hides at this scale, and the "Other" category
// that OpenCPN hides by default, are counted but not listed.
void legend_pi::CollectChartObjects(
    const PlugIn_ViewPort &vp,
    std::map<wxString, DynamicGroup> *groups)
{
    m_chart_object_total = 0;
    m_chart_objects_hidden = 0;
    m_chart_objects_scale_hidden = 0;

    EnsureChartSymbols();
    if (!m_chart_symbols.IsLoaded())
        return;

    wxStopWatch watch;

    // Two sources: the o-charts plugin for its encrypted cells, and this
    // plugin's own reader for plain ENC cells. Where both cover the same
    // water, an object counts once.
    std::vector<ChartObject> objects;
    m_ocharts_available = m_ocharts.Available();
    if (m_ocharts_available)
        objects = m_ocharts.PointObjectsInView(vp, m_show_hidden_at_scale);

    auto positionKey = [](const ChartObject &object) {
        return wxString::Format(
            "%s|%.5f|%.5f", object.className, object.lat, object.lon);
    };

    std::set<wxString> fromOcharts;
    for (const ChartObject &object : objects)
        fromOcharts.insert(positionKey(object));

    // Overlapping cells hold the same objects. A sector light is several
    // lights at one position, which differ in colour and character, so
    // for lights those count as well.
    std::set<wxString> seenEnc;
    const std::vector<ChartObject> enc = m_s57.PointObjectsInView(vp);
    for (const ChartObject &object : enc) {
        const wxString position = positionKey(object);
        if (fromOcharts.count(position) > 0)
            continue;

        wxString key = position;
        if (object.className == "LIGHTS") {
            for (const char *acronym :
                 {"LITCHR", "SIGGRP", "SIGPER", "COLOUR", "SECTR1", "SECTR2"}) {
                const wxString *value = object.Attribute(acronym);
                key << '|' << (value ? *value : wxString());
            }
        }
        if (seenEnc.insert(key).second)
            objects.push_back(object);
    }

    m_chart_sources_available = m_ocharts_available || m_s57.CellCount() > 0;

    const bool simplified = PI_GetPLIBSymbolStyle() != PI_PAPER_CHART;
    m_display_settings.Refresh();


    // Whether the chart draws objects of a class with a scale limit: asked
    // of OpenCPN once per class and limit, with its configuration file as
    // fallback.
    std::map<wxString, bool> shownCache;
    int unanswered = 0;

    std::vector<PendingLight> lights;
    std::map<std::pair<long, long>, std::vector<Carrier>> carriers;

    for (const ChartObject &object : objects) {
        ChartSymbols::Resolved resolved;
        if (!m_chart_symbols.Resolve(object, simplified, &resolved) ||
            resolved.symbol.IsEmpty())
            continue;

        const wxString shownKey =
            wxString::Format("%s|%d", object.className, object.scamin);
        auto cached = shownCache.find(shownKey);
        bool shown;
        if (cached != shownCache.end()) {
            shown = cached->second;
        }
        else {
            bool answered = false;
            shown = m_display_settings.ChartShows(
                object.className, object.scamin, vp, &answered);
            if (!answered) {
                ++unanswered;
                shown = m_display_settings.Shows(object.className, resolved.displayCat) &&
                        !(object.scamin > 0 && vp.chart_scale > object.scamin);
            }
            shownCache[shownKey] = shown;
        }

        // Not drawn: by its scale limit, or because the class or category
        // is switched off. Only the first kind can get a row of its own.
        bool hiddenByScale = false;
        if (!shown) {
            if (!m_show_hidden_at_scale || object.scamin <= 0) {
                ++m_chart_objects_hidden;
                continue;
            }

            const wxString noScaleKey = object.className + "|0";
            auto cachedNoScale = shownCache.find(noScaleKey);
            bool shownWithoutScale;
            if (cachedNoScale != shownCache.end()) {
                shownWithoutScale = cachedNoScale->second;
            }
            else {
                bool answered = false;
                shownWithoutScale = m_display_settings.ChartShows(
                    object.className, 0, vp, &answered);
                if (!answered) {
                    shownWithoutScale = m_display_settings.Shows(
                        object.className, resolved.displayCat);
                }
                shownCache[noScaleKey] = shownWithoutScale;
            }

            if (!shownWithoutScale) {
                ++m_chart_objects_hidden;
                continue;
            }
            hiddenByScale = true;
        }

        const wxString key =
            hiddenByScale
                ? wxString::Format("s52:%s|hidden|%09d", resolved.symbol, object.scamin)
                : "s52:" + resolved.symbol;
        DynamicGroup &group = (*groups)[key];

        if (group.key.IsEmpty()) {
            group.key = key;
            group.chartSymbol = true;
            group.lightSymbol = object.className == "LIGHTS";
            group.symbol = resolved.symbol;
            group.hiddenAtScale = hiddenByScale;
            group.visibleFrom = hiddenByScale ? object.scamin : 0;

            // The name made of class and attributes, in the chosen
            // language ("Laterale boei: bakboord, rood, stompe ton"). Else
            // the S-52 description of the symbol, or the object class with
            // its colour and shape.
            wxString description =
                m_chart_names.Compose(object, m_chart_symbols, m_dutch_names);

            if (description.IsEmpty())
                description = m_chart_symbols.SymbolDescription(resolved.symbol);

            if (description.IsEmpty()) {
                description = m_chart_symbols.ClassDescription(object.className);

                wxString details;
                for (const char *acronym : {"COLOUR", "BOYSHP", "BCNSHP",
                                            "TOPSHP", "CATLAM", "CATCAM",
                                            "CATSPM"}) {
                    const wxString *value = object.Attribute(acronym);
                    if (!value)
                        continue;
                    const wxString meaning =
                        m_chart_symbols.AttributeValueMeaning(acronym, *value);
                    if (meaning.IsEmpty())
                        continue;
                    if (!details.IsEmpty())
                        details << ", ";
                    details << meaning;
                }

                if (!details.IsEmpty())
                    description << wxString::FromUTF8(" \u00b7 ") << details;
            }

            if (!description.IsEmpty())
                description = description.Left(1).Upper() + description.Mid(1);

            ResolveLabel(resolved.symbol, description, group);

            // Not mentioned in symbolen.txt: under its own heading.
            if (group.order == (1 << 30))
                group.section = BuiltInSection(-2);
        }

        group.positions.emplace_back(object.lat, object.lon);

        if (hiddenByScale)
            ++m_chart_objects_scale_hidden;
        else
            ++m_chart_object_total;

        // Lights are placed under the buoy or beacon they stand on, once all
        // objects are known; hidden ones at this scale too, in grey.
        if (object.className == "LIGHTS") {
            if (!m_show_lights)
                continue;
            auto value = [&object](const char *acronym) {
                const wxString *v = object.Attribute(acronym);
                return v ? *v : wxString();
            };
            LightCharacter light;
            if (light.FromS57(value("LITCHR"), value("SIGGRP"), value("SIGPER"),
                              value("COLOUR")))
            {
                PendingLight pending{light, object.lat, object.lon,
                                     hiddenByScale ? object.scamin : 0, false,
                                     LightSector()};
                double from = 0.0;
                double to = 0.0;
                if (value("SECTR1").ToCDouble(&from) && value("SECTR2").ToCDouble(&to) &&
                    !value("SECTR1").IsEmpty() && !value("SECTR2").IsEmpty()) {
                    pending.sector = true;
                    pending.part.colour = light.Colours().front();
                    pending.part.from = from;
                    pending.part.to = to;
                }
                lights.push_back(pending);
            }
            continue;
        }

        // What a light can stand on: buoys and beacons first, then other
        // structures. Topmarks, fog signals and radio aids share the spot
        // but carry nothing.
        static const std::set<wxString> notCarriers = {
            "TOPMAR", "topmar", "FOGSIG", "RTPBCN", "RADSTA", "RDOSTA",
            "RETRFL", "SOUNDG"};
        if (notCarriers.count(object.className) == 0) {
            const wxString prefix = object.className.Left(3).Upper();
            const int rank =
                (prefix == "BOY" || prefix == "BCN" || prefix == "LIT") ? 0 : 1;
            carriers[CarrierCell(object.lat, object.lon)].push_back(
                {object.lat, object.lon, key, rank});
        }
    }

    // A light belongs to the object at the same spot (within a few metres);
    // in a chart the light and its buoy are separate objects.
    // The parts of a sector light share a position and a rhythm and differ
    // in colour: joined into one light, as charts write it ("Iso.WRG.4s"),
    // with its sectors kept by position.
    m_light_sectors.Clear();
    {
        std::vector<PendingLight> joined;
        std::map<wxString, size_t> sectorLights;
        for (const PendingLight &pending : lights) {
            if (!pending.sector) {
                joined.push_back(pending);
                continue;
            }
            const std::pair<long, long> cell = LightSectors::Cell(pending.lat, pending.lon);
            m_light_sectors.Add(pending.lat, pending.lon, pending.light.RhythmAbbreviation(),
                                pending.part);

            const wxString key = wxString::Format(
                "%ld|%ld|%d|", cell.first, cell.second, pending.hiddenFrom) +
                pending.light.RhythmAbbreviation();
            auto known = sectorLights.find(key);
            if (known == sectorLights.end()) {
                sectorLights[key] = joined.size();
                PendingLight first = pending;
                first.light.SetColours({pending.part.colour});
                joined.push_back(first);
                continue;
            }
            LightCharacter &light = joined[known->second].light;
            std::vector<wxString> colours = light.Colours();
            if (std::find(colours.begin(), colours.end(), pending.part.colour) ==
                colours.end()) {
                colours.push_back(pending.part.colour);
                light.SetColours(colours);
            }
        }
        lights.swap(joined);
    }

    for (const PendingLight &pending : lights) {
        const Carrier *best = nullptr;
        double bestDistance = 0.0;
        const long cellLat = static_cast<long>(std::floor(pending.lat / kCarrierCell));
        const long cellLon = static_cast<long>(std::floor(pending.lon / kCarrierCell));

        for (long dLat = -1; dLat <= 1; ++dLat) {
            for (long dLon = -1; dLon <= 1; ++dLon) {
                auto cell = carriers.find(std::make_pair(cellLat + dLat, cellLon + dLon));
                if (cell == carriers.end())
                    continue;
                for (const Carrier &carrier : cell->second) {
                    const double dlat = carrier.lat - pending.lat;
                    const double dlon = carrier.lon - pending.lon;
                    const double distance = dlat * dlat + dlon * dlon;
                    if (distance > kCarrierCell * kCarrierCell)
                        continue;
                    // A light shown at this scale belongs to a shown object
                    // rather than to a hidden one, and the other way round.
                    auto rankOf = [&pending](const Carrier &c) {
                        const bool hidden = c.groupKey.Contains("|hidden|");
                        return c.rank + (hidden != (pending.hiddenFrom > 0) ? 2 : 0);
                    };
                    if (!best || rankOf(carrier) < rankOf(*best) ||
                        (rankOf(carrier) == rankOf(*best) && distance < bestDistance)) {
                        best = &carrier;
                        bestDistance = distance;
                    }
                }
            }
        }

        AddLight(*groups, pending.light, pending.lat, pending.lon,
                 best ? best->groupKey : wxString(), pending.hiddenFrom);
    }

    // The list is rebuilt on every pause of the chart; log only what changed.
    const wxString summary = wxString::Format(
        "Legend: chart objects: %s; %s; %d listed, %d hidden, %d hidden by scale "
        "listed, %zu class probes%s",
        m_ocharts_available ? m_ocharts.LastDiagnostics() : wxString("no o-charts"),
        m_s57.LastDiagnostics(), m_chart_object_total, m_chart_objects_hidden,
        m_chart_objects_scale_hidden, shownCache.size(),
        unanswered > 0 ? " (PLIB gave no answer)" : "");
    if (summary != m_chart_objects_logged) {
        m_chart_objects_logged = summary;
        wxLogMessage("%s, %ld ms", summary, watch.Time());
    }
}

void legend_pi::AddLight(
    std::map<wxString, DynamicGroup> &groups,
    const LightCharacter &light,
    double lat,
    double lon,
    const wxString &parentKey,
    int hiddenFrom) const
{
    const wxString abbreviation = light.Abbreviation();
    wxString key = parentKey.IsEmpty()
                       ? "light:" + abbreviation
                       : "light:" + parentKey + "|" + abbreviation;
    if (parentKey.IsEmpty() && hiddenFrom > 0)
        key << wxString::Format("|hidden|%09d", hiddenFrom);
    DynamicGroup &group = groups[key];

    if (group.key.IsEmpty()) {
        group.key = key;
        group.lightCharacter = true;
        group.lightText = abbreviation;
        group.label = light.Words(m_dutch_names);
        group.parentKey = parentKey;
        group.section = BuiltInSection(-3);
        group.hiddenAtScale = hiddenFrom > 0;
        group.visibleFrom = hiddenFrom;

        // A sub-row shares the heading of its row.
        auto parent = groups.find(parentKey);
        if (!parentKey.IsEmpty() && parent != groups.end()) {
            group.section = parent->second.section;
            group.order = parent->second.order;
            group.hiddenAtScale = parent->second.hiddenAtScale;
            group.visibleFrom = parent->second.visibleFrom;
        }
    }

    // The same light twice, from overlapping sources or sectors of one
    // light, counts once.
    for (const auto &position : group.positions) {
        if (std::fabs(position.first - lat) < 0.0003 &&
            std::fabs(position.second - lon) < 0.0005)
            return;
    }
    group.positions.emplace_back(lat, lon);
}

void legend_pi::CollectDynamicLegend()
{
    m_dynamic_groups.clear();
    m_dynamic_total = 0;
    m_dynamic_hidden = 0;

    m_collected_viewport = m_viewport;
    m_collected_viewport_valid = m_viewport_valid;

    if (!m_viewport_valid)
        return;

    LoadMeanings();

    const PlugIn_ViewPort vp = m_viewport;
    m_displayed_scale = DisplayedScale(vp);
    const double scaleForLimits =
        m_displayed_scale > 0.0 ? m_displayed_scale : vp.chart_scale;

    // Longitudes may be offset by 360 degrees relative to the viewport.
    auto lonInView = [&vp](double lon) {
        for (double offset : {0.0, 360.0, -360.0}) {
            const double l = lon + offset;
            if (l >= vp.lon_min && l <= vp.lon_max)
                return true;
        }
        return false;
    };

    std::map<wxString, DynamicGroup> groups;

    for (const CachedWaypoint &wp : m_waypoint_cache) {
        if (!wp.visible)
            continue;

        // Waypoints with a scale limit disappear when zoomed out too far;
        // with the setting on they get a row of their own.
        const bool hiddenByScale = wp.useScamin && scaleForLimits > wp.scamin;
        if (hiddenByScale && !m_show_hidden_at_scale)
            continue;

        if (wp.lat < vp.lat_min || wp.lat > vp.lat_max)
            continue;

        if (!lonInView(wp.lon))
            continue;

        wxString icon = wp.icon;
        if (icon.IsEmpty())
            icon = "unknown";

        // Hidden ones get a row per scale limit, so each row states one
        // scale and a click can zoom to exactly that.
        const int limit = hiddenByScale ? static_cast<int>(wp.scamin) : 0;
        const wxString key =
            hiddenByScale ? wxString::Format("%s|hidden|%09d", icon, limit) : icon;
        DynamicGroup &group = groups[key];

        if (group.key.IsEmpty()) {
            group.key = key;
            group.iconName = icon;
            group.hiddenAtScale = hiddenByScale;
            group.visibleFrom = limit;
            ResolveLabel(icon, wp.description, group);
        }

        group.positions.emplace_back(wp.lat, wp.lon);

        if (hiddenByScale)
            ++m_dynamic_hidden;
        else
            ++m_dynamic_total;

        if (m_show_lights && !wp.light.IsEmpty()) {
            LightCharacter light;
            if (light.Parse(wp.light))
                AddLight(groups, light, wp.lat, wp.lon, key);
        }
    }

    m_light_sectors.Clear();
    CollectChartObjects(vp, &groups);

    // Order the positions of every group from near to far, measured from
    // the centre of the screen.
    const double kPi = 3.14159265358979323846;
    const double cosLat = std::cos(vp.clat * kPi / 180.0);

    auto distance = [&](const std::pair<double, double> &p) {
        double dlat = p.first - vp.clat;
        double dlon = p.second - vp.clon;

        while (dlon > 180.0)
            dlon -= 360.0;
        while (dlon < -180.0)
            dlon += 360.0;

        dlon *= cosLat;
        return dlat * dlat + dlon * dlon;
    };

    // Light sub-rows are kept apart and put under their row afterwards.
    std::map<wxString, std::vector<DynamicGroup>> subRows;

    for (auto &entry : groups) {
        DynamicGroup group = entry.second;

        std::sort(
            group.positions.begin(),
            group.positions.end(),
            [&](const std::pair<double, double> &a,
                const std::pair<double, double> &b) {
                return distance(a) < distance(b);
            });

        if (!group.parentKey.IsEmpty()) {
            if (groups.count(group.parentKey) > 0) {
                subRows[group.parentKey].push_back(group);
                continue;
            }
            group.parentKey.Clear();
            group.section = BuiltInSection(-3);
        }

        m_dynamic_groups.push_back(group);
    }

    std::sort(
        m_dynamic_groups.begin(),
        m_dynamic_groups.end(),
        [](const DynamicGroup &a, const DynamicGroup &b) {
            // Headings in file order, then "Kaartsymbolen" (-2),
            // "Lichtkarakters" (-3) and "Overig" (-1); within a heading the
            // order of symbolen.txt, then the icon name.
            auto rank = [](int section) {
                if (section == -2)
                    return (1 << 30) - 2;
                if (section == -3)
                    return (1 << 30) - 1;
                return section < 0 ? 1 << 30 : section;
            };
            const int sa = rank(a.section);
            const int sb = rank(b.section);
            if (sa != sb)
                return sa < sb;
            if (a.order != b.order)
                return a.order < b.order;
            return a.key.CmpNoCase(b.key) < 0;
        });

    // Lights shining on the chart follow the list, once it is complete:
    // the lights under their rows are added below.
    if (subRows.empty()) {
        if (m_all_lights.IsOn())
            UpdateAllLights();
        return;
    }

    // Each row followed by the lights it carries, the most frequent first.
    std::vector<DynamicGroup> ordered;
    ordered.reserve(m_dynamic_groups.size() + groups.size());
    for (DynamicGroup &group : m_dynamic_groups) {
        const wxString key = group.key;
        const int section = group.section;
        ordered.push_back(group);

        auto found = subRows.find(key);
        if (found == subRows.end())
            continue;

        std::vector<DynamicGroup> &rows = found->second;
        std::sort(rows.begin(), rows.end(),
                  [](const DynamicGroup &a, const DynamicGroup &b) {
                      if (a.positions.size() != b.positions.size())
                          return a.positions.size() > b.positions.size();
                      return a.lightText.CmpNoCase(b.lightText) < 0;
                  });
        for (DynamicGroup &row : rows) {
            row.section = section;
            ordered.push_back(row);
        }
    }
    m_dynamic_groups.swap(ordered);

    if (m_all_lights.IsOn())
        UpdateAllLights();
}
