// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, the page: the HTML of the dynamic legend, block by block.

#include "legend_pi.h"

#include "geo.h"

#include "legend_html_window.h"
#include "legend_icons.h"
#include "legend_shared.h"

#include <wx/filesys.h>
#include <wx/fs_mem.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/settings.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <set>

// Width of the rhythm image under a light: 180 pixels, less in a window
// too narrow for that beside the icon, the lamp and the counter of a light
// row under its buoy (about 210 pixels together, with the margins).
int legend_pi::RhythmWidth() const
{
    if (!m_markdown_view)
        return 180;
    const int page = m_markdown_view->GetClientSize().GetWidth() -
                     wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, m_markdown_view) - 24;
    return std::max(60, std::min(180, page - 210));
}

namespace {

wxString EscapeHtml(wxString text)
{
    text.Replace("&", "&amp;");
    text.Replace("<", "&lt;");
    text.Replace(">", "&gt;");
    return text;
}

// "×12", or "3/12" once a click went to the third one.
wxString CountText(const DynamicGroup &group)
{
    const size_t count = group.positions.size();
    if (group.current >= 0 && count > 1)
        return wxString::Format("%d/%zu", group.current + 1, count);
    return wxString::Format("&times;%zu", count);
}

// Icons of a heading strip; more than this are counted instead.
const size_t kStripIcons = 6;

// The largest square a row icon gets; larger icons are scaled down.
const int kMaxIconBox = 64;

} // namespace

// The dynamic legend while its page is built: the colours, which rows are
// shown and how, and their icons.
struct legend_pi::DynamicPage
{
    LegendPalette palette;
    wxString html;

    // A search leaves out the rows that do not match.
    bool filtering = false;
    // Headings only when symbolen.txt defines them.
    bool showHeadings = false;

    // By group: the row a light sub-row belongs to (-1: none), whether it
    // is shown, whether its heading is folded, and its stripe.
    std::vector<int> parentOf;
    std::vector<bool> shown;
    std::vector<bool> collapsed;
    std::vector<size_t> stripeIndex;

    size_t mainRows = 0;  // rows that are not lights under a row
    size_t matches = 0;   // of those, the ones shown
    size_t lightRows = 0;
    bool lightsShown = false;

    // By group: the icon, the icon at twice the size, the row colour.
    std::vector<wxImage> icons;
    std::vector<wxImage> icons2;
    std::vector<wxColour> rowColours;
    int iconBox = 16;     // the square every row icon gets
    int tilePadding = 0;

    int IconCell() const { return iconBox + 2 * tilePadding; }
};

void legend_pi::RenderDynamicLegend(bool keepScroll)
{
    if (!m_markdown_view)
        return;

    if (!g_memoryHandler) {
        g_memoryHandler = new wxMemoryFSHandler;
        wxFileSystem::AddHandler(g_memoryHandler);
    }

    m_rhythm_width = RhythmWidth();

    int scrollX = 0;
    int scrollY = 0;
    if (keepScroll)
        m_markdown_view->GetViewStart(&scrollX, &scrollY);

    const wxString html = BuildDynamicPage();
    static_cast<LegendHtmlWindow *>(m_markdown_view)->SetDynamicPage(html);
    m_showing_loading_page = false;

    if (m_scroll_to_row >= 0) {
        const int row = m_scroll_to_row;
        m_scroll_to_row = -1;
        m_markdown_view->CallAfter([this, row]() {
            if (m_markdown_view && m_current_legend_path == kDynamicGpxId)
                static_cast<LegendHtmlWindow *>(m_markdown_view)->ScrollToRow(row);
        });
    }
    else if (keepScroll) {
        m_markdown_view->CallAfter(
            [this, scrollX, scrollY]() {
                if (m_markdown_view &&
                    m_current_legend_path == kDynamicGpxId) {
                    m_markdown_view->Scroll(scrollX, scrollY);
                }
            });
    }
}

// The page of the dynamic legend: the title bar, the blocks at the top
// (Marifoon, Uitkijk, Notities, Licht herkennen), then the rows under
// their headings and the counts at the foot.
wxString legend_pi::BuildDynamicPage()
{
    ClearDynamicImages();
    ++m_dynamic_generation;

    DynamicPage page;
    page.palette = GetLegendPalette();
    PrepareDynamicRows(page);
    LoadRowIcons(page);

    page.html << "<html>" << LegendBodyTag(true);
    RenderTitleBar(page);

    // While Ververs spins, only the title: the list comes back when done.
    if (m_refresh_state == RefreshIconState::Spinning) {
        if (m_refresh_page_height > 0)
            page.html << "<legendspacer height=\"" << m_refresh_page_height << "\">";
        page.html << "</body></html>";
        return page.html;
    }

    // Marifoon and Uitkijk: headings at the top of the list that fold like
    // the others; folded, the heading still tells the channel or the next
    // object. Not during a search or the order edit mode.
    m_rendered_headings.clear();
    if (m_viewport_valid && !m_reorder_mode && !page.filtering) {
        RenderVhfBlock(page);
        RenderAheadBlock(page);
        RenderNotesBlock(page);
        RenderIdentifyBlock(page);
    }

    const wxColour &muted = page.palette.muted;
    if (page.showHeadings && m_reorder_mode) {
        RenderReorderList(page);
    }
    else if (m_viewport_valid && m_dynamic_groups.empty()) {
        page.html << "<p><font color=\"" << HtmlColour(muted) << "\">"
                  << "Geen zichtbare waypoints in dit kaartbeeld.</font></p>";
    }
    else if (m_viewport_valid && page.matches == 0) {
        RenderNoMatches(page);
    }
    else if (m_viewport_valid) {
        RenderRows(page);
        RenderFooter(page);
    }

    page.html << "</body></html>";
    return page.html;
}

// Which rows are shown, how they are striped and which headings are
// folded.
void legend_pi::PrepareDynamicRows(DynamicPage &page)
{
    // A search leaves out the rows that do not match; their headings go
    // too when nothing under them matches. Matching rows are shown even
    // under a collapsed heading.
    page.filtering = !m_filter_words.empty() && !m_reorder_mode;
    const bool filtering = page.filtering;
    const size_t groupCount = m_dynamic_groups.size();

    // The row a light sub-row belongs to: the row it follows.
    std::vector<int> &parentOf = page.parentOf;
    parentOf.assign(groupCount, -1);
    for (size_t i = 0; i < groupCount; ++i) {
        const wxString &parentKey = m_dynamic_groups[i].parentKey;
        if (parentKey.IsEmpty())
            continue;
        int p = static_cast<int>(i) - 1;
        while (p >= 0 && !m_dynamic_groups[p].parentKey.IsEmpty())
            --p;
        if (p >= 0 && m_dynamic_groups[p].key == parentKey)
            parentOf[i] = p;
    }

    // A row and its lights are found together: a matching row shows all
    // its lights, a matching light shows its row.
    std::vector<bool> &shown = page.shown;
    shown.assign(groupCount, true);
    if (filtering) {
        for (size_t i = 0; i < groupCount; ++i)
            shown[i] = GroupMatchesFilter(m_dynamic_groups[i]);
        const std::vector<bool> direct = shown;
        for (size_t i = 0; i < groupCount; ++i) {
            if (parentOf[i] < 0)
                continue;
            if (direct[parentOf[i]])
                shown[i] = true;
            if (direct[i])
                shown[parentOf[i]] = true;
        }
    }

    for (size_t i = 0; i < groupCount; ++i) {
        if (parentOf[i] < 0) {
            ++page.mainRows;
            if (shown[i])
                ++page.matches;
        }
        if (m_dynamic_groups[i].lightCharacter) {
            ++page.lightRows;
            if (shown[i])
                page.lightsShown = true;
        }
    }

    // Headings only when symbolen.txt defines them; a collapsed heading
    // hides its rows.
    page.showHeadings = !m_symbol_meanings.Sections().empty();
    page.collapsed.assign(groupCount, false);
    for (size_t i = 0; i < groupCount; ++i) {
        // In the order edit mode only the headings are shown.
        page.collapsed[i] =
            page.showHeadings && !filtering &&
            (m_reorder_mode ||
             m_collapsed_sections.count(
                 SectionName(m_dynamic_groups[i].section)) > 0);
    }

    // Stripes start again under every heading and skip rows left out; the
    // lights under a row share its stripe.
    std::vector<size_t> &stripeIndex = page.stripeIndex;
    stripeIndex.assign(groupCount, 0);
    for (size_t i = 0, n = 0; i < groupCount; ++i) {
        if (i > 0 &&
            m_dynamic_groups[i].section != m_dynamic_groups[i - 1].section)
            n = 0;
        if (parentOf[i] >= 0) {
            stripeIndex[i] = stripeIndex[parentOf[i]];
            continue;
        }
        stripeIndex[i] = n;
        if (shown[i])
            ++n;
    }
}

// The colour and the icon of every row shown. All icons are loaded first:
// the largest one sets the square every icon gets, so all rows are equally
// high without shrinking any icon. Very large icons are limited to
// kMaxIconBox.
void legend_pi::LoadRowIcons(DynamicPage &page)
{
    const LegendPalette &palette = page.palette;
    const wxColour &background = palette.background;
    const wxColour &stripe = palette.stripe;
    const wxColour &active = palette.active;
    const wxColour &muted = palette.muted;

    page.tilePadding = palette.iconTiles ? 3 : 0;

    // Row icons follow OpenCPN's chart object scale factor, so they look as
    // large as on the chart. Text does not: that is a matter of the user
    // interface, not of chart objects.
    const double rawScale = GetOCPNChartScaleFactor_Plugin();
    double iconScale = rawScale > 0.0 ? rawScale : 1.0;
    iconScale = std::min(4.0, std::max(0.5, iconScale));

    if (std::fabs(iconScale - m_rendered_icon_scale) > 1e-3) {
        wxLogMessage(
            "Legend: chart object scale factor %.2f (OpenCPN: %.2f)",
            iconScale,
            rawScale);
    }
    m_rendered_icon_scale = iconScale;

    page.icons.assign(m_dynamic_groups.size(), wxImage());
    page.icons2.assign(m_dynamic_groups.size(), wxImage());
    page.rowColours.assign(m_dynamic_groups.size(), wxColour());

    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        if (!page.shown[i])
            continue;

        const DynamicGroup &group = m_dynamic_groups[i];

        const bool isActive = IsActiveRow(group);

        wxColour &rowColour = page.rowColours[i];
        rowColour =
            isActive ? active
                     : (page.stripeIndex[i] % 2 == 1 ? stripe : background);

        // The row just found from the chart: lit in the marker's colour,
        // fading to the highlight colour over the next second. Its light
        // lights up along with it.
        if (!m_flash_key.IsEmpty() &&
            (group.key == m_flash_key ||
             (!m_flash_light_key.IsEmpty() && group.key == m_flash_light_key))) {
            wxColour outline;
            wxColour highlight;
            MarkerColours(outline, highlight);
            const wxColour lit = BlendColour(background, highlight, 0.5);
            const double remaining =
                std::max(0.0, 1.0 - m_flash_phase / static_cast<double>(kFadeSteps));
            rowColour = BlendColour(active, lit, remaining * remaining);
        }

        // Transparency is resolved against what ends up behind the icon:
        // the tile in dark mode, otherwise the row itself. Icons of a
        // collapsed group appear small next to the heading instead.
        if (page.collapsed[i])
            rowColour = background;

        // The lamp of a light under a row sits on the row itself, no tile.
        const wxColour iconBackground =
            (palette.iconTiles && page.parentOf[i] < 0) ? palette.tile : rowColour;

        wxImage image;
        wxImage doubled; // drawn at twice the size, for a Retina screen

        if (group.lightCharacter) {
            // A lamp in the colours of the light.
            LightCharacter light;
            if (light.Parse(group.lightText)) {
                image = LightLampImage(light.Colours(), muted, iconBackground, SchemeDim());
                doubled = LightLampImage(light.Colours(), muted, iconBackground, SchemeDim(), 36);
            }
        }
        else if (group.chartSymbol) {
            // Chart symbols come from OpenCPN's symbol sheet of the current
            // colour scheme, which already has dusk and night versions.
            const wxImage picture =
                m_chart_symbols.SymbolImage(group.symbol, LegendScheme());
            if (picture.IsOk())
                image = FlattenImage(picture, iconBackground);
        }
        else {
            // First choice: the icon's own SVG or PNG file. Fall back to
            // asking OpenCPN for the bitmap. In dusk and night the icon is
            // dimmed, the background it sits on is not.
            const wxString key =
                group.iconName.IsEmpty() ? group.key : group.iconName;
            auto flatten = [&key](const wxColour &colour) {
                wxImage drawn = LoadIconFromFile(key, colour);

                if (!drawn.IsOk()) {
                    wxBitmap bitmap = GetIcon_PlugIn(key);

                    if (bitmap.IsOk())
                        drawn = FlattenIcon(bitmap, colour, key);
                }
                return drawn;
            };

            image = FlattenDimmed(flatten, iconBackground, SchemeDim());

            // An SVG icon is drawn again at twice the size, not enlarged.
            if (image.IsOk() && !SvgIconPath(key).IsEmpty()) {
                auto flatten2 = [&key](const wxColour &colour) {
                    return LoadIconFromFile(key, colour, 2);
                };
                doubled = FlattenDimmed(flatten2, iconBackground, SchemeDim());
            }
        }

        // The version at twice the size comes from the image as loaded, the
        // file often being larger than it is shown.
        auto scaled = [](const wxImage &source, double factor) {
            const int width = std::max(
                1, static_cast<int>(std::lround(source.GetWidth() * factor)));
            const int height = std::max(
                1, static_cast<int>(std::lround(source.GetHeight() * factor)));
            return source.Scale(width, height, wxIMAGE_QUALITY_HIGH);
        };
        if (image.IsOk()) {
            page.icons2[i] = doubled.IsOk() ? (std::fabs(iconScale - 1.0) > 1e-3
                                                   ? scaled(doubled, iconScale)
                                                   : doubled)
                                            : scaled(image, 2.0 * iconScale);
        }
        if (image.IsOk() && std::fabs(iconScale - 1.0) > 1e-3)
            image = scaled(image, iconScale);

        if (image.IsOk() && !page.collapsed[i] && page.shown[i]) {
            page.iconBox = std::max(
                page.iconBox,
                std::min(kMaxIconBox,
                         std::max(image.GetWidth(), image.GetHeight())));
        }

        page.icons[i] = image;
    }
}

// An image of the list under "name.png", and when given its version at
// twice the size under "name@2x.png", which wxHTML on macOS takes on a
// Retina screen, so it stays sharp there.
wxString legend_pi::AddDynamicImage(const wxString &name, const wxImage &image,
                                    const wxImage &doubled)
{
    wxMemoryFSHandler::AddFile(name, image, wxBITMAP_TYPE_PNG);
    m_dynamic_image_names.push_back(name);
    if (doubled.IsOk() && name.EndsWith(".png")) {
        const wxString hiDpi = name.Left(name.length() - 4) + "@2x.png";
        wxMemoryFSHandler::AddFile(hiDpi, doubled, wxBITMAP_TYPE_PNG);
        m_dynamic_image_names.push_back(hiDpi);
    }
    return name;
}

bool legend_pi::IsActiveRow(const DynamicGroup &group) const
{
    return (!m_active_group_key.IsEmpty() && group.key == m_active_group_key) ||
           (!m_active_light_key.IsEmpty() && group.key == m_active_light_key);
}

// A light character as on the chart, in words, with its rhythm below.
wxString legend_pi::LightRowLabel(const DynamicPage &page, const DynamicGroup &group,
                                  size_t index, const wxColour &rowColour)
{
    wxString label =
        "<b>" + EscapeHtml(group.lightText) + "</b>&nbsp; " + EscapeHtml(group.label);

    // A sector light: its sectors, of the light the counter is on (the
    // nearest one until a click went to another).
    if (!group.positions.empty()) {
        const size_t at =
            group.current >= 0 ? static_cast<size_t>(group.current) : 0;
        const std::vector<LightSector> sectors =
            m_light_sectors.At(group.positions[at].first, group.positions[at].second,
                               RhythmOf(group.lightText));
        if (!sectors.empty())
            label << "<br>" << EscapeHtml(SectorWords(sectors, m_dutch_names));
    }
    LightCharacter light;
    if (light.Parse(group.lightText)) {
        const int width = m_rhythm_width;
        const wxColour &muted = page.palette.muted;
        const wxImage rhythm =
            LightRhythmImage(light, width, 14, muted, rowColour, SchemeDim());
        if (rhythm.IsOk()) {
            const wxString name = AddDynamicImage(
                wxString::Format("legend_dyn_%d_rhythm_%zu.png", m_dynamic_generation, index),
                rhythm,
                LightRhythmImage(light, 2 * width, 28, muted, rowColour, SchemeDim(), 2.0));
            label << "<br><img src=\"memory:" << name << "\">";
        }
    }
    return label;
}

namespace {
const int kRefreshIconSize = 30;     // pixels, as the settings icon
const int kRefreshSpinSteps = 16;    // per turn
const long kRefreshMinSpinMs = 900;  // so a quick Ververs is still seen
const long kRefreshCheckMs = 1500;
const long kRefreshMessageMs = 4000; // a check with words, to read them
} // namespace

// Ververs clicked: the list empties and the arrow starts to turn at once,
// before the work.
void legend_pi::StartRefreshSpin()
{
    m_refresh_state = RefreshIconState::Spinning;
    m_refresh_state_since = wxGetLocalTimeMillis();
    m_refresh_turn = 0;
    m_refresh_message.Clear();
    m_refresh_rows_before.clear();
    for (const DynamicGroup &group : m_dynamic_groups)
        m_refresh_rows_before[group.key] = {group.label, group.section};
    m_refresh_icon_timer.Start(50);
    if (IsDynamicLegendVisible() && !m_showing_loading_page) {
        // With a scroll bar, the empty page keeps the height of the list:
        // a scroll bar that went away would make the title bar wider.
        // Without one, it gets none either.
        const int height = m_markdown_view->GetVirtualSize().y;
        m_refresh_page_height =
            height > m_markdown_view->GetClientSize().y ? height : 0;
        RenderDynamicLegend(false);
        m_markdown_view->Update();
    }
}

// The spin goes on while waypoints load or chart cells open; then the list
// comes back with a check mark that shows for a while, then the arrow
// again.
void legend_pi::OnRefreshIconTimer()
{
    const long elapsed =
        static_cast<long>((wxGetLocalTimeMillis() - m_refresh_state_since).GetValue());

    if (m_refresh_state == RefreshIconState::Spinning) {
        // Only whole turns: the arrow stops where it always stands.
        if (elapsed >= kRefreshMinSpinMs && !m_loading && !m_cell_timer.IsRunning() &&
            m_refresh_turn % kRefreshSpinSteps == 0) {
            m_refresh_state = RefreshIconState::Done;
            m_refresh_state_since = wxGetLocalTimeMillis();
            m_refresh_page_height = 0;
            m_refresh_message = RefreshChangeMessage();
            if (IsDynamicLegendVisible() && !m_showing_loading_page) {
                RenderDynamicLegend(false);
                return;
            }
        }
        else {
            ++m_refresh_turn;
        }
    }
    else if (m_refresh_state == RefreshIconState::Done &&
             elapsed >= (m_refresh_message.IsEmpty() ? kRefreshCheckMs : kRefreshMessageMs)) {
        m_refresh_state = RefreshIconState::Idle;
        m_refresh_icon_timer.Stop();
        if (!m_refresh_message.IsEmpty()) {
            m_refresh_message.Clear();
            if (IsDynamicLegendVisible() && !m_showing_loading_page) {
                RenderDynamicLegend(true);
                return;
            }
        }
    }

    UpdateRefreshIcon();
    if (m_markdown_view && IsDynamicLegendVisible())
        static_cast<LegendHtmlWindow *>(m_markdown_view)->RefreshCell(kRefreshIconId);
}

// Rows that came, went or changed their text or heading with Ververs (an
// edited symbolen.txt), in a few words; empty when the list stayed the
// same.
wxString legend_pi::RefreshChangeMessage() const
{
    int added = 0;
    int changed = 0;
    std::set<wxString> after;
    for (const DynamicGroup &group : m_dynamic_groups) {
        after.insert(group.key);
        auto before = m_refresh_rows_before.find(group.key);
        if (before == m_refresh_rows_before.end())
            ++added;
        else if (before->second != std::make_pair(group.label, group.section))
            ++changed;
    }
    int gone = 0;
    for (const auto &before : m_refresh_rows_before)
        gone += after.count(before.first) == 0 ? 1 : 0;

    wxString message;
    auto part = [&message](int count, const char *what) {
        if (count > 0)
            message << (message.IsEmpty() ? "" : ", ") << count << " " << what;
    };
    part(added, "nieuw");
    part(changed, "gewijzigd");
    part(gone, "weg");
    return message;
}

// The picture of the Ververs cell for the present state, sharp on a Retina
// screen.
void legend_pi::UpdateRefreshIcon()
{
    const LegendPalette palette = GetLegendPalette();
    const double scale = m_markdown_view ? m_markdown_view->GetContentScaleFactor() : 1.0;
    const int pixels = static_cast<int>(kRefreshIconSize * scale + 0.5);

    wxImage image;
    if (m_refresh_state == RefreshIconState::Done) {
        image = CheckIcon(palette.text, palette.header, pixels);
    }
    else {
        const double turn = 2.0 * 3.14159265358979323846 * (m_refresh_turn % kRefreshSpinSteps) /
                            kRefreshSpinSteps;
        image = HeaderIcon(false, palette.text, palette.header, pixels, turn);
    }
    g_refresh_icon = wxBitmap(image, -1, scale);
}

// The title, with the icons of the settings menu and Ververs on the right
// (their words in tooltips); one line high. The counts are at the foot of
// the list. A line under the title only to say the chart position is
// unknown, or how many rows match a search.
void legend_pi::RenderTitleBar(DynamicPage &page)
{
    const LegendPalette &palette = page.palette;
    const wxColour &muted = palette.muted;
    wxString &html = page.html;

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" "
         << "bgcolor=\"" << HtmlColour(palette.header) << "\"><tr><td valign=\"middle\">"
         << "<font size=\"+1\"><b>Kaartlegenda</b></font>";

    if (!m_viewport_valid) {
        html << "<br><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
             << "De kaartpositie is nog niet bekend. Beweeg de kaart "
                "een klein stukje en klik daarna op Ververs.</font>";
    }
    else if (page.filtering) {
        html << "<br><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
             << static_cast<int>(page.matches) << " van de "
             << static_cast<int>(page.mainRows)
             << " symbolen in beeld passen bij &lsquo;" << EscapeHtml(m_filter)
             << "&rsquo;</font>";
    }

    html << "</td><td align=\"right\" valign=\"middle\" nowrap>";
    if (page.showHeadings && m_reorder_mode) {
        html << "<a href=\"legend-reorder-done:\"><b>Klaar</b></a>";
    }
    else {
        auto icon = [&](bool gear) {
            const wxString base = wxString::Format(
                "legend_dyn_%d_%s", m_dynamic_generation, gear ? "settings" : "refresh");
            wxMemoryFSHandler::AddFile(
                base + ".png", HeaderIcon(gear, palette.text, palette.header, 30),
                wxBITMAP_TYPE_PNG);
            m_dynamic_image_names.push_back(base + ".png");
            // wxHTML on macOS takes "name@2x.png" on a Retina screen: drawn
            // at twice the size, the icon stays sharp there.
            wxMemoryFSHandler::AddFile(
                base + "@2x.png", HeaderIcon(gear, palette.text, palette.header, 60),
                wxBITMAP_TYPE_PNG);
            m_dynamic_image_names.push_back(base + "@2x.png");
            return "<img src=\"memory:" + base + ".png\" align=\"absmiddle\">";
        };
        // Cells with an id carry the tooltip; the link does the work.
        html << "<table cellspacing=\"0\" cellpadding=\"2\"><tr>";
        // What Ververs changed, beside the check mark while it shows.
        if (m_refresh_state == RefreshIconState::Done && !m_refresh_message.IsEmpty()) {
            html << "<td valign=\"middle\"><font size=\"-1\" color=\"" << HtmlColour(muted)
                 << "\">" << EscapeHtml(m_refresh_message) << "</font></td>"
                 << "<td width=\"10\"></td>";
        }
        if (m_viewport_valid) {
            html << "<td id=\"" << kLegendTipIdPrefix << "0\"><a href=\"legend-settings:\">"
                 << icon(true) << "</a></td><td width=\"10\"></td>";
        }
        UpdateRefreshIcon();
        html << "<td id=\"" << kLegendTipIdPrefix << "1\"><a href=\"legend-refresh:\">"
             << "<legendrefresh id=\"" << kRefreshIconId << "\" size=\"" << kRefreshIconSize << "\">"
             << "</a></td></tr></table>";
    }
    html << "</td></tr></table>";

    // What the settings menu offers depends on the list.
    m_menu_has_headings = page.showHeadings && !m_dynamic_groups.empty() && !page.filtering;
    m_menu_any_open = false;
    for (const DynamicGroup &group : m_dynamic_groups) {
        if (m_collapsed_sections.count(SectionName(group.section)) == 0) {
            m_menu_any_open = true;
            break;
        }
    }
    m_menu_has_lights = page.lightRows > 0;

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
         << "<tr><td bgcolor=\"" << HtmlColour(palette.headerRule)
         << "\" height=\"2\"></td></tr>"
         << "<tr><td height=\"6\"></td></tr>"
         << "</table>";
}

// A heading of a block at the top of the list (Marifoon, Uitkijk, ...),
// with a summary after it; true when it is open.
bool legend_pi::RenderExtraHeading(DynamicPage &page, const wxString &key,
                                   const wxString &title, const wxString &summary)
{
    const bool folded = m_collapsed_sections.count(key) > 0;
    const wxString sectionId = wxString::Format(
        "%s%zu", kLegendSectionIdPrefix, m_rendered_headings.size());
    m_rendered_headings.push_back(key);
    page.html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
              << "<tr><td height=\"6\"></td></tr>"
              << "<tr><td id=\"" << sectionId << "\" valign=\"middle\">"
              << "<font size=\"+3\">" << (folded ? "&#9654;" : "&#9660;")
              << "</font>&nbsp;<b>" << title << "</b>";
    if (!summary.IsEmpty()) {
        page.html << " <font color=\"" << HtmlColour(page.palette.muted) << "\">&middot; "
                  << summary << "</font>";
    }
    page.html << "</td></tr>"
              << "<tr><td height=\"4\"></td></tr>"
              << "<tr><td bgcolor=\"" << HtmlColour(page.palette.rule)
              << "\" height=\"1\"></td></tr></table>";
    return !folded;
}

// Marifoon: the VHF areas the boat is in (or the middle of the chart),
// while the VHFinfo layer is shown.
void legend_pi::RenderVhfBlock(DynamicPage &page)
{
    const bool nl = m_dutch_names;
    const wxColour &background = page.palette.background;
    const wxColour &stripe = page.palette.stripe;
    wxString &html = page.html;

    bool fromShip = false;
    bool layerShown = false;
    const std::vector<const VhfArea *> areas = VhfAreasHere(&fromShip, &layerShown);
    if (!layerShown)
        return;

    wxString summary;
    if (areas.empty()) {
        summary = nl ? "geen VHFinfo-gebied hier" : "no VHFinfo area here";
    }
    else {
        const VhfArea *first = areas.front();
        const wxString who = first->callName.IsEmpty() ? first->name : first->callName;
        summary << (nl ? "kanaal " : "channel ") << EscapeHtml(first->channel);
        if (!who.IsEmpty())
            summary << ", " << EscapeHtml(who);
        if (areas.size() > 1)
            summary << wxString::Format(" (+%zu)", areas.size() - 1);
    }
    if (!fromShip)
        summary << (nl ? " &middot; midden van de kaart, geen GPS"
                       : " &middot; middle of the chart, no GPS");

    if (!RenderExtraHeading(page, "@marifoon", nl ? "Marifoon" : "VHF", summary) ||
        areas.empty())
        return;

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";
    m_vhf_rows.clear();
    for (size_t k = 0; k < areas.size(); ++k) {
        const VhfArea *area = areas[k];
        m_vhf_rows.push_back(*area);
        const wxString who = area->callName.IsEmpty() ? area->name : area->callName;
        html << "<tr bgcolor=\"" << HtmlColour(k % 2 == 1 ? stripe : background)
             << "\"><td id=\"" << kLegendVhfAreaIdPrefix << k << "\"><b>"
             << (nl ? "Kanaal " : "Channel ")
             << EscapeHtml(area->channel) << "</b>";
        if (!who.IsEmpty())
            html << " &middot; " << EscapeHtml(who);
        if (!area->type.IsEmpty())
            html << " &middot; " << EscapeHtml(VhfTypeWord(area->type, nl));
        if (!area->usage.IsEmpty())
            html << " &middot; " << EscapeHtml(VhfUsageWord(area->usage, nl));
        if (!area->phone.IsEmpty()) {
            html << " &middot; <a href=\"tel:" << EscapeHtml(area->phone) << "\">"
                 << EscapeHtml(area->phone) << "</a>";
        }
        html << "</td></tr>";
    }
    html << "</table>";
}

// Uitkijk: while sailing, what the boat meets in the coming minutes, in
// order of arrival.
void legend_pi::RenderAheadBlock(DynamicPage &page)
{
    const bool nl = m_dutch_names;
    const wxColour &background = page.palette.background;
    const wxColour &stripe = page.palette.stripe;
    wxString &html = page.html;

    ComputeAhead();
    m_ahead_drawn_key = m_ahead_key;
    m_distance_drawn_key = DistanceKey();
    m_ahead_rendered = wxGetUTCTimeMillis();

    if (!m_ahead_sailing)
        return;

    auto when = [nl](const AheadItem &item) {
        if (item.minutes < 1.0)
            return wxString(nl ? "nu" : "now");
        return wxString::Format(nl ? "over %d min" : "in %d min",
                                static_cast<int>(std::lround(item.minutes)));
    };
    auto side = [nl](const AheadItem &item) {
        // In the circle round the boat an object can lie behind it, just
        // passed.
        if (std::fabs(item.cross) < 0.02 && item.minutes < 0.0)
            return wxString(nl ? "recht achter" : "right astern");
        if (std::fabs(item.cross) < 0.02)
            return wxString(nl ? "recht vooruit" : "dead ahead");
        if (item.cross > 0.0)
            return wxString(nl ? "stuurboord" : "starboard");
        return wxString(nl ? "bakboord" : "port");
    };
    auto name = [&](const AheadItem &item) {
        const DynamicGroup &group = m_dynamic_groups[item.group];
        return EscapeHtml(group.lightCharacter ? group.lightText : group.label);
    };

    auto whenMinutes = [nl](double minutes) {
        if (minutes < 1.0)
            return wxString(nl ? "nu" : "now");
        return wxString::Format(nl ? "over %d min" : "in %d min",
                                static_cast<int>(std::lround(minutes)));
    };
    auto vhfText = [&](const AheadVhf &next) {
        const VhfArea &area = next.area;
        wxString text = (nl ? "kanaal " : "channel ") + EscapeHtml(area.channel);
        const wxString who = area.callName.IsEmpty() ? area.name : area.callName;
        if (!who.IsEmpty())
            text << " &middot; " << EscapeHtml(who);
        if (!area.usage.IsEmpty())
            text << " &middot; " << EscapeHtml(VhfUsageWord(area.usage, nl));
        return text;
    };

    // Beside the heading, what the Uitkijk covers: the minutes of the
    // setting at this speed (more at the nearest 0.3 sea mile) and the
    // width of the setting; and when nothing lies in it, that too.
    const double range = AheadRange();
    const int minutes = static_cast<int>(std::lround(range / m_ship_sog * 60.0));
    wxString summary =
        wxString::Format(nl ? "komende %d minuten, tot %s" : "next %d minutes, up to %s",
                         minutes, FormatDistance(range)) +
        " &middot; " + (nl ? "breedte " : "width ") +
        wxString::Format("%d&deg;", m_ahead_angle);
    if (m_ahead.empty() && m_ahead_vhf.empty() && m_ahead_notes.empty())
        summary << " &middot; " << (nl ? "niets in de bundel" : "nothing in the beam");

    // The key "@vooruit" is the block's name before; the folded state is
    // kept under it.
    if (!RenderExtraHeading(page, "@vooruit", nl ? "Uitkijk" : "Lookout", summary))
        return;

    // The icons as large as in the rows of the legend: each its own size,
    // in a square as large as the largest of them, on a tile when the
    // rows have one.
    int box = page.iconBox;
    for (const AheadItem &item : m_ahead) {
        const wxImage &icon = page.icons[item.group];
        if (icon.IsOk())
            box = std::max(box, std::min(kMaxIconBox,
                                         std::max(icon.GetWidth(), icon.GetHeight())));
    }
    const wxColour *tile = page.palette.iconTiles ? &page.palette.tile : nullptr;
    const int padding = page.tilePadding;
    const int cell = box + 2 * padding + 10;

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";
    size_t row = 0;
    size_t nextVhf = 0;
    // A VHF area ahead, in its place in time.
    auto vhfRow = [&](size_t v) {
        const AheadVhf &next = m_ahead_vhf[v];
        const wxString id = wxString::Format("%s%zu", kLegendAheadVhfIdPrefix, v);
        const wxColour &rowColour = row++ % 2 == 1 ? stripe : background;
        const wxString image = AddDynamicImage(
            wxString::Format("legend_dyn_%d_aheadvhf_%zu.png", m_dynamic_generation, v),
            VhfBadgeImage(next.area.channel, rowColour, box),
            VhfBadgeImage(next.area.channel, rowColour, 2 * box));
        html << "<tr bgcolor=\"" << HtmlColour(rowColour) << "\"><td id=\"" << id
             << "\" width=\"" << cell << "\" align=\"center\" valign=\"middle\"><img src=\"memory:"
             << image << "\"></td><td id=\"" << id << "\" valign=\"middle\"><b>"
             << whenMinutes(next.minutes) << "</b> &middot; "
             << (nl ? "marifoongebied " : "VHF area ") << vhfText(next)
             << "</td></tr>";
    };
    // A note ahead.
    auto noteRow = [&](size_t n) {
        const AheadNote &note = m_ahead_notes[n];
        const wxString id = wxString::Format("%s%zu", kLegendAheadNoteIdPrefix, n);
        const wxColour &rowColour = row++ % 2 == 1 ? stripe : background;
        const wxString image = AddDynamicImage(
            wxString::Format("legend_dyn_%d_aheadnote_%zu.png", m_dynamic_generation, n),
            NoteSignImage(rowColour, box), NoteSignImage(rowColour, 2 * box));
        wxString where = std::fabs(note.cross) < 0.02
                             ? wxString(nl ? "recht vooruit" : "dead ahead")
                             : note.cross > 0.0 ? wxString(nl ? "stuurboord" : "starboard")
                                                : wxString(nl ? "bakboord" : "port");
        html << "<tr bgcolor=\"" << HtmlColour(rowColour) << "\"><td id=\"" << id
             << "\" width=\"" << cell
             << "\" align=\"center\" valign=\"middle\"><img src=\"memory:" << image
             << "\"></td><td id=\"" << id << "\" valign=\"middle\"><b>"
             << whenMinutes(note.minutes) << "</b> &middot; " << where << " &middot; "
             << (nl ? "notitie " : "note ") << EscapeHtml(note.title) << "</td></tr>";
    };
    size_t nextNote = 0;
    for (size_t k = 0; k < m_ahead.size(); ++k) {
        while (true) {
            const double v = nextVhf < m_ahead_vhf.size()
                                 ? m_ahead_vhf[nextVhf].minutes
                                 : 1e9;
            const double n = nextNote < m_ahead_notes.size()
                                 ? m_ahead_notes[nextNote].minutes
                                 : 1e9;
            if (v <= m_ahead[k].minutes && v <= n)
                vhfRow(nextVhf++);
            else if (n <= m_ahead[k].minutes)
                noteRow(nextNote++);
            else
                break;
        }
        const AheadItem &item = m_ahead[k];
        const DynamicGroup &group = m_dynamic_groups[item.group];
        const wxString id = wxString::Format("%s%zu", kLegendAheadIdPrefix, k);
        const wxColour &rowColour = row++ % 2 == 1 ? stripe : background;

        html << "<tr bgcolor=\"" << HtmlColour(rowColour) << "\"><td id=\"" << id
             << "\" width=\"" << cell << "\" align=\"center\" valign=\"middle\">";
        if (page.icons[item.group].IsOk()) {
            const wxString image = AddDynamicImage(
                wxString::Format("legend_dyn_%d_ahead_%zu.png",
                                 m_dynamic_generation, k),
                NormalizeIcon(page.icons[item.group], rowColour, box, tile, padding),
                NormalizeIcon(page.icons2[item.group], rowColour, 2 * box, tile, 2 * padding));
            html << "<img src=\"memory:" << image << "\">";
        }
        html << "</td><td id=\"" << id << "\" valign=\"middle\">"
             << "<b>" << when(item) << "</b> &middot; " << side(item);
        // Setting "Afstand en peiling vanaf de boot": to this object.
        if (DistanceFromBoatShown() && item.position < group.positions.size()) {
            const auto &at = group.positions[item.position];
            html << " &middot; "
                 << FormatDistance(SeaMiles(m_ship_lat, m_ship_lon, at.first, at.second))
                 << " &middot; "
                 << FormatBearing(BearingDegrees(m_ship_lat, m_ship_lon, at.first, at.second));
        }
        html << " &middot; " << name(item);
        if (!item.light.IsEmpty() && !group.lightCharacter)
            html << " &middot; <b>" << EscapeHtml(item.light) << "</b>";
        html << "</td></tr>";
    }
    while (nextVhf < m_ahead_vhf.size() || nextNote < m_ahead_notes.size()) {
        const double v = nextVhf < m_ahead_vhf.size() ? m_ahead_vhf[nextVhf].minutes
                                                      : 1e9;
        const double n = nextNote < m_ahead_notes.size()
                             ? m_ahead_notes[nextNote].minutes
                             : 1e9;
        if (v <= n)
            vhfRow(nextVhf++);
        else
            noteRow(nextNote++);
    }
    html << "</table>";
}

// Notities: notes whose place is in view, nearest first.
void legend_pi::RenderNotesBlock(DynamicPage &page)
{
    const bool nl = m_dutch_names;
    const wxColour &background = page.palette.background;
    const wxColour &stripe = page.palette.stripe;
    const wxColour &muted = page.palette.muted;
    wxString &html = page.html;

    m_note_rows.clear();

    const PlugIn_ViewPort &view = m_viewport;
    const bool fromShip =
        m_ship_time > 0 && (wxGetUTCTimeMillis() - m_ship_time) < 60000;
    const double hereLat = fromShip ? m_ship_lat : view.clat;
    const double hereLon = fromShip ? m_ship_lon : view.clon;
    // Notes beyond their scale limit are left out, or shown in grey
    // with the setting for symbols hidden at this scale.
    std::vector<std::pair<double, size_t>> inView;
    size_t shownCount = 0;
    for (size_t i = 0; i < m_note_places.size(); ++i) {
        const NotePlace &place = m_note_places[i];
        bool lonIn = false;
        for (double offset : {0.0, 360.0, -360.0})
            lonIn = lonIn || (place.lon + offset >= view.lon_min &&
                              place.lon + offset <= view.lon_max);
        if (place.lat < view.lat_min || place.lat > view.lat_max || !lonIn)
            continue;
        const bool hidden = NoteHiddenAtScale(place);
        if (hidden && !m_show_hidden_at_scale)
            continue;
        if (!hidden)
            ++shownCount;
        // Hidden ones after the shown ones.
        inView.emplace_back(SeaMiles(hereLat, hereLon, place.lat, place.lon) +
                                (hidden ? 1e6 : 0.0),
                            i);
    }
    std::sort(inView.begin(), inView.end());
    auto miles = [](double value) {
        wxString text = wxString::Format("%.1f NM", value);
        text.Replace(".", ",");
        return text;
    };

    if (inView.empty())
        return;

    const NotePlace &nearest = m_note_places[inView.front().second];
    wxString summary;
    if (shownCount > 0) {
        summary = EscapeHtml(nearest.title) + ", " + miles(inView.front().first);
        if (shownCount > 1)
            summary << wxString::Format(" (+%zu)", shownCount - 1);
    }
    else {
        summary = wxString::Format(nl ? "%zu op deze schaal verborgen"
                                      : "%zu hidden at this scale",
                                   inView.size());
    }
    if (!RenderExtraHeading(page, "@notities", nl ? "Notities" : "Notes", summary))
        return;

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";
    for (size_t k = 0; k < inView.size(); ++k) {
        const NotePlace &place = m_note_places[inView[k].second];
        const bool hidden = NoteHiddenAtScale(place);
        const double distance = inView[k].first - (hidden ? 1e6 : 0.0);
        m_note_rows.push_back(inView[k].second);
        const wxString id = wxString::Format("%s%zu", kLegendNoteIdPrefix, k);
        const wxColour &rowColour = k % 2 == 1 ? stripe : background;
        wxImage sign = NoteSignImage(rowColour, 24);
        wxImage sign2 = NoteSignImage(rowColour, 48);
        if (hidden) {
            sign = FadeImage(sign, rowColour, 0.55);
            sign2 = FadeImage(sign2, rowColour, 0.55);
        }
        const wxString image = AddDynamicImage(
            wxString::Format("legend_dyn_%d_note_%zu.png", m_dynamic_generation, k),
            sign, sign2);
        wxString title = EscapeHtml(place.title);
        if (hidden) {
            // Not drawn at this scale: grey, a closed eye and the
            // scale at which it appears; a click zooms in.
            const wxString eye = AddDynamicImage(
                wxString::Format("legend_dyn_%d_noteeye_%zu.png",
                                 m_dynamic_generation, k),
                EyeIcon(false, muted, rowColour), EyeIcon(false, muted, rowColour, 2));
            title = "<font color=\"" + HtmlColour(muted) + "\">" + title +
                    "</font>&nbsp; <img src=\"memory:" + eye + "\"> <font size=\"-1\" color=\"" +
                    HtmlColour(muted) + "\">" + FormatScaleShort(place.scamin) + "</font>";
        }
        html << "<tr bgcolor=\"" << HtmlColour(rowColour) << "\"><td id=\"" << id
             << "\" width=\"34\" align=\"center\" valign=\"top\"><img src=\"memory:"
             << image << "\"></td><td id=\"" << id << "\">" << title
             << "<br><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
             << miles(distance);
        if (!place.snippet.IsEmpty()) {
            wxString snippet = place.snippet;
            if (snippet.length() > 70)
                snippet = snippet.Left(68) + wxString::FromUTF8("\u2026");
            html << " &middot; " << EscapeHtml(snippet);
        }
        html << "</font></td></tr>";
    }
    html << "</table>";
}

// Licht herkennen: tap along with a light, see which lights fit.
void legend_pi::RenderIdentifyBlock(DynamicPage &page)
{
    const bool nl = m_dutch_names;
    const LegendPalette &palette = page.palette;
    const wxColour &background = palette.background;
    const wxColour &stripe = palette.stripe;
    const wxColour &muted = palette.muted;
    wxString &html = page.html;

    m_identify_matches.clear();
    if (!m_identify_on)
        return;

    const TapPattern pattern = AnalyseTaps(m_taps);

    // Each light character in view once, best fits first.
    std::vector<std::pair<double, size_t>> scored;
    std::set<wxString> seen;
    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        const DynamicGroup &group = m_dynamic_groups[i];
        if (!group.lightCharacter || group.hiddenAtScale ||
            !seen.insert(group.lightText).second)
            continue;
        LightCharacter light;
        if (!light.Parse(group.lightText))
            continue;
        if (!m_identify_colour.IsEmpty()) {
            const std::vector<wxString> colours = light.Colours();
            if (std::find(colours.begin(), colours.end(), m_identify_colour) ==
                colours.end())
                continue;
        }
        const double score = pattern.valid ? light.Match(pattern) : 0.0;
        scored.emplace_back(score, i);
    }
    std::sort(scored.begin(), scored.end(),
              [](const std::pair<double, size_t> &a, const std::pair<double, size_t> &b) {
                  return a.first > b.first;
              });

    wxString summary;
    if (!pattern.valid)
        summary = nl ? "tik mee met het licht" : "tap along with the light";
    else if (!scored.empty() && scored.front().first >= 0.45)
        summary = (nl ? "lijkt op " : "looks like ") +
                  EscapeHtml(m_dynamic_groups[scored.front().second].lightText);
    else
        summary = EscapeHtml(pattern.Words(nl));

    if (!RenderExtraHeading(page, "@herkennen", nl ? "Licht herkennen" : "Identify a light",
                            summary))
        return;

    // A frame sets the block apart from the list below it: a
    // coloured border around a striped panel.
    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"2\" bgcolor=\""
         << HtmlColour(palette.headerRule) << "\"><tr><td>"
         << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\" bgcolor=\""
         << HtmlColour(stripe) << "\">"
         << "<tr><td colspan=\"2\"><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
         << (nl ? "Houd het vlak of de spatiebalk ingedrukt zolang het licht "
                  "brandt en laat los als het donker is, twee of drie perioden lang."
                : "Hold the pad or the space bar while the light shines and let go "
                  "while it is dark, for two or three periods.")
         << "</font></td></tr>"
         << "<tr><td colspan=\"2\" id=\"" << kLegendTapIdPrefix << "0\" bgcolor=\""
         << HtmlColour(palette.active) << "\" align=\"center\" valign=\"middle\" height=\"64\">"
         << "<font size=\"+1\"><b>"
         << (nl ? "Indrukken = licht aan" : "Press = light on")
         << "</b></font><br><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
         << "<legendclock id=\"" << kTapClockId << "\"></font></td></tr>";
    g_tap_clock_text = TapClockText();

    // Colour of the light seen: a lamp to click for each colour,
    // the chosen one on a highlighted tile.
    html << "<tr><td colspan=\"2\" align=\"center\"><font size=\"-1\">"
         << (nl ? "Klik op de kleur van het licht:" : "Click the colour of the light:")
         << "</font><br><table align=\"center\" cellspacing=\"2\" cellpadding=\"4\"><tr>";
    const char *codes[] = {"", "W", "R", "G", "Y"};
    const char *namesNl[] = {"elke", "wit", "rood", "groen", "geel"};
    const char *namesEn[] = {"any", "white", "red", "green", "yellow"};
    for (int c = 0; c < 5; ++c) {
        const bool chosen = m_identify_colour == codes[c];
        const wxColour tile = chosen ? palette.active : stripe;
        const std::vector<wxString> colours =
            c == 0 ? std::vector<wxString>{"W", "R", "G", "Y"}
                   : std::vector<wxString>{codes[c]};
        const wxString lamp = AddDynamicImage(
            wxString::Format("legend_dyn_%d_idcolour_%d.png", m_dynamic_generation, c),
            LightLampImage(colours, muted, tile, SchemeDim(), 18),
            LightLampImage(colours, muted, tile, SchemeDim(), 36));
        const wxString label = nl ? namesNl[c] : namesEn[c];
        html << "<td id=\"" << kLegendButtonIdPrefix << 10 + c << "\" align=\"center\" bgcolor=\""
             << HtmlColour(tile) << "\"><img src=\"memory:" << lamp << "\"><br><font size=\"-1\">"
             << (chosen ? "<b>" + label + "</b>" : label) << "</font></td>";
    }
    html << "</tr></table></td></tr>";

    if (!m_taps.empty()) {
        const wxString name = AddDynamicImage(
            wxString::Format("legend_dyn_%d_taps.png", m_dynamic_generation),
            TapRhythmImage(m_taps, m_rhythm_width + 20, 16, muted, palette.text, stripe),
            TapRhythmImage(m_taps, 2 * (m_rhythm_width + 20), 32, muted, palette.text,
                           stripe, 2.0));
        html << "<tr><td colspan=\"2\"><img src=\"memory:" << name << "\">";
        if (pattern.valid)
            html << "<br><font size=\"-1\">" << EscapeHtml(pattern.Words(nl)) << "</font>";
        html << "</td></tr>";
    }

    if (scored.empty()) {
        html << "<tr><td colspan=\"2\"><font color=\"" << HtmlColour(muted) << "\">"
             << (nl ? "Geen lichten in beeld om mee te vergelijken."
                    : "No lights in view to compare with.")
             << "</font></td></tr>";
    }
    else if (pattern.valid) {
        const size_t kShown = 5;
        for (size_t k = 0; k < scored.size() && k < kShown; ++k) {
            if (scored[k].first < 0.45)
                break;
            const DynamicGroup &group = m_dynamic_groups[scored[k].second];
            const wxString id =
                wxString::Format("%s%zu", kLegendMatchIdPrefix, m_identify_matches.size());
            m_identify_matches.push_back(scored[k].second);
            const int percent = static_cast<int>(std::lround(scored[k].first * 100));
            const wxString fit =
                scored[k].first >= 0.85 ? (nl ? "past goed" : "good fit")
                : scored[k].first >= 0.65 ? (nl ? "past redelijk" : "fair fit")
                                         : (nl ? "past matig" : "weak fit");
            html << "<tr bgcolor=\"" << HtmlColour(k % 2 == 0 ? background : stripe) << "\">"
                 << "<td colspan=\"2\" id=\"" << id << "\"><b>" << EscapeHtml(group.lightText)
                 << "</b>&nbsp; " << EscapeHtml(group.label) << "<br><font size=\"-1\" color=\""
                 << HtmlColour(muted) << "\">" << fit << " (" << percent << "%) &middot; &times;"
                 << group.positions.size() << "</font></td></tr>";
        }
        if (m_identify_matches.empty()) {
            html << "<tr><td colspan=\"2\"><font color=\"" << HtmlColour(muted) << "\">"
                 << (nl ? "Geen licht in beeld past bij dit ritme."
                        : "No light in view fits this rhythm.")
                 << "</font></td></tr>";
        }
    }

    // Two buttons at the foot: erase the taps and start again,
    // or close the block (the settings menu opens it again).
    auto button = [&](int number, const wxString &label) {
        html << "<td><table cellspacing=\"0\" cellpadding=\"1\" bgcolor=\""
             << HtmlColour(palette.headerRule) << "\"><tr><td>"
             << "<table cellspacing=\"0\" cellpadding=\"5\" bgcolor=\"" << HtmlColour(background)
             << "\"><tr><td id=\"" << kLegendButtonIdPrefix << number << "\" nowrap>"
             << label << "</td></tr></table></td></tr></table></td>";
    };
    html << "<tr><td colspan=\"2\" align=\"right\">"
         << "<table align=\"right\" cellspacing=\"0\" cellpadding=\"0\"><tr>";
    if (!m_taps.empty()) {
        button(0, nl ? "Tikken wissen" : "Clear taps");
        html << "<td width=\"10\"></td>";
    }
    button(1, nl ? "Sluiten" : "Close");
    html << "</tr></table></td></tr></table></td></tr></table>";

    // Room between the frame and the list.
    const wxString space = wxString::Format("legend_dyn_%d_idspace.png",
                                            m_dynamic_generation);
    wxImage spaceImage(1, 12);
    spaceImage.SetRGB(wxRect(0, 0, 1, 12), background.Red(), background.Green(),
                      background.Blue());
    AddDynamicImage(space, spaceImage, wxImage());
    html << "<img src=\"memory:" << space << "\"><br>";
}

// The order edit mode: every heading of symbolen.txt, also those with
// nothing in view, with large buttons that also suit a finger on a
// tablet.
void legend_pi::RenderReorderList(DynamicPage &page)
{
    const LegendPalette &palette = page.palette;
    const wxColour &background = palette.background;
    const wxColour &stripe = palette.stripe;
    const wxColour &muted = palette.muted;
    const wxColour &rule = palette.rule;
    wxString &html = page.html;

    const int count = static_cast<int>(m_symbol_meanings.Sections().size());

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\">";

    for (int section = 0; section < count; ++section) {
        const wxColour rowColour = section % 2 == 1 ? stripe : background;
        size_t symbols = 0;
        size_t waypoints = 0;
        wxString strip;

        for (size_t j = 0; j < m_dynamic_groups.size(); ++j) {
            if (m_dynamic_groups[j].section != section)
                continue;

            ++symbols;
            waypoints += m_dynamic_groups[j].positions.size();

            if (symbols > kStripIcons || !page.icons[j].IsOk())
                continue;

            const wxImage icon1x = NormalizeIcon(
                page.icons[j],
                rowColour,
                20,
                palette.iconTiles ? &palette.tile : nullptr,
                palette.iconTiles ? 2 : 0);

            const wxString name = AddDynamicImage(
                wxString::Format("legend_dyn_%d_order_%zu.png", m_dynamic_generation, j),
                icon1x,
                NormalizeIcon(page.icons2[j], rowColour, 40,
                              palette.iconTiles ? &palette.tile : nullptr,
                              palette.iconTiles ? 4 : 0));
            strip << "<img src=\"memory:" << name << "\"> ";
        }

        if (symbols > kStripIcons) {
            strip << "<font color=\"" << HtmlColour(muted) << "\">+"
                  << symbols - kStripIcons << "</font>";
        }

        auto button = [&](const wxString &prefix, const char *arrow,
                          bool enabled) {
            wxString cell;
            cell << "<td width=\"44\" align=\"center\" valign=\"middle\"";
            if (enabled)
                cell << " id=\"" << prefix << section << "\"";
            cell << "><font size=\"+3\"";
            if (!enabled)
                cell << " color=\"" << HtmlColour(rule) << "\"";
            cell << ">" << arrow << "</font></td>";
            return cell;
        };

        html << "<tr bgcolor=\"" << HtmlColour(rowColour) << "\">"
             << "<td valign=\"middle\"><b>"
             << EscapeHtml(SectionTitle(section))
             << "</b><br><font size=\"-1\" color=\"" << HtmlColour(muted)
             << "\">";

        if (symbols == 0)
            html << "niets in beeld";
        else
            html << symbols << (symbols == 1 ? " symbool, " : " symbolen, ")
                 << waypoints << " in beeld";

        html << "</font></td><td align=\"right\" valign=\"middle\">"
             << strip << "</td>"
             << button(kLegendMoveUpIdPrefix, "&#9650;", section > 0)
             << button(kLegendMoveDownIdPrefix, "&#9660;",
                       section < count - 1)
             << "</tr>";
    }

    html << "</table>"
         << "<p><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
         << "Tik of klik op &#9650; of &#9660; om een kopje te verplaatsen. "
            "De volgorde wordt in symbolen.txt bewaard. 'Overig' staat "
            "altijd onderaan.</font></p>";
}

// A search that matches nothing in view.
void legend_pi::RenderNoMatches(DynamicPage &page)
{
    const wxColour &muted = page.palette.muted;
    wxString &html = page.html;

    html << "<p>Geen symbolen in beeld die passen bij &lsquo;"
         << EscapeHtml(m_filter) << "&rsquo;.</p>"
         << "<p><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
         << "Er wordt gezocht in de namen in de lijst, de kopjes en de "
            "icoonnamen, alleen van wat nu in beeld is. Verschuif de "
            "kaart, zoom uit, of maak het zoekveld leeg.";
    if (!m_show_hidden_at_scale)
        html << " Symbolen die op deze schaal verborgen zijn, tellen pas "
                "mee met &lsquo;Ook verborgen tonen&rsquo;.";
    html << "</font></p>";
}

// The rows of the list under their headings, each with the lights it
// carries.
void legend_pi::RenderRows(DynamicPage &page)
{
    page.html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";

    // Section of the last row drawn; a row of another section starts a
    // new heading. Rows left out by a search do not count.
    bool firstRow = true;
    int previousSection = 0;

    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        DynamicGroup &group = m_dynamic_groups[i];

        if (!page.shown[i]) {
            group.imageName.Clear();
            continue;
        }

        const bool newHeading =
            firstRow || group.section != previousSection;
        const bool atTop = firstRow;
        firstRow = false;
        previousSection = group.section;

        if (page.showHeadings && newHeading)
            RenderSectionHeading(page, i, atTop);

        if (page.collapsed[i]) {
            group.imageName.Clear();
            continue;
        }

        // Lights under a row are drawn with that row, below.
        if (page.parentOf[i] >= 0)
            continue;

        RenderRow(page, i);
        RenderLightsOfRow(page, i);
    }

    page.html << "</table>";
}

// The heading above the rows of a section, starting at row i: its title,
// the counts, and the icons of its rows when it is folded.
void legend_pi::RenderSectionHeading(DynamicPage &page, size_t i, bool atTop)
{
    const LegendPalette &palette = page.palette;
    const wxColour &background = palette.background;
    const wxColour &muted = palette.muted;
    const wxColour &rule = palette.rule;
    const DynamicGroup &group = m_dynamic_groups[i];

    const wxString heading = SectionName(group.section);
    const wxString headingTitle = SectionTitle(group.section);
    const bool folded = page.collapsed[i];

    // Symbols and waypoints of this group, and the icons shown
    // next to a collapsed heading.
    size_t symbols = 0;
    size_t waypoints = 0;
    wxString strip;

    for (size_t j = i; j < m_dynamic_groups.size() &&
                       m_dynamic_groups[j].section == group.section;
         ++j) {
        // Lights under a row are part of that row's symbol.
        if (!page.shown[j] || page.parentOf[j] >= 0)
            continue;

        ++symbols;
        waypoints += m_dynamic_groups[j].positions.size();

        if (!folded || symbols > kStripIcons || !page.icons[j].IsOk())
            continue;

        const wxImage icon1x = NormalizeIcon(
            page.icons[j],
            background,
            20,
            palette.iconTiles ? &palette.tile : nullptr,
            palette.iconTiles ? 2 : 0);

        const wxString name = AddDynamicImage(
            wxString::Format("legend_dyn_%d_strip_%zu.png", m_dynamic_generation, j),
            icon1x,
            NormalizeIcon(page.icons2[j], background, 40,
                          palette.iconTiles ? &palette.tile : nullptr,
                          palette.iconTiles ? 4 : 0));
        strip << "<img src=\"memory:" << name << "\"> ";
    }

    if (folded && symbols > kStripIcons) {
        strip << "<font color=\"" << HtmlColour(muted) << "\">+"
              << symbols - kStripIcons << "</font>";
    }

    // Every cell of the heading carries its id, so a click
    // anywhere on it folds the group in or out. While searching
    // nothing folds, so the heading is not a button then.
    const wxString sectionId = wxString::Format(
        "%s%zu", kLegendSectionIdPrefix, m_rendered_headings.size());
    m_rendered_headings.push_back(heading);
    const wxString idAttribute =
        page.filtering ? wxString() : " id=\"" + sectionId + "\"";

    // Every group gets its own table; between them the heading
    // with a thin line under it. Inside the row table the
    // padding would turn the line into a thick bar.
    page.html << "</table>"
              << "<table width=\"100%\" cellspacing=\"0\" "
                 "cellpadding=\"0\">"
              << "<tr><td colspan=\"2\" height=\"" << (atTop ? 6 : 14)
              << "\"></td></tr>"
              << "<tr><td" << idAttribute << " valign=\"middle\">"
              << "<font size=\"+3\">"
              << (folded ? "&#9654;" : "&#9660;") << "</font>&nbsp;<b>"
              << EscapeHtml(headingTitle) << "</b> <font color=\"" << HtmlColour(muted)
              << "\">&middot; " << symbols
              << (symbols == 1 ? " symbool, " : " symbolen, ")
              << waypoints << " in beeld</font></td>"
              << "<td" << idAttribute
              << " align=\"right\" valign=\"middle\">"
              << strip << "</td></tr>"
              << "<tr><td colspan=\"2\" height=\"4\"></td></tr>"
              << "<tr><td colspan=\"2\" bgcolor=\"" << HtmlColour(rule)
              << "\" height=\"1\"></td></tr></table>"
              << "<table width=\"100%\" cellspacing=\"0\" "
                 "cellpadding=\"5\">";
}

// One row of the list: its icon, its name and the counter.
void legend_pi::RenderRow(DynamicPage &page, size_t i)
{
    const LegendPalette &palette = page.palette;
    const wxColour &foreground = palette.text;
    const wxColour &muted = palette.muted;
    DynamicGroup &group = m_dynamic_groups[i];

    group.imageName.Clear();

    const bool isActive = IsActiveRow(group);

    const wxColour &rowColour = page.rowColours[i];
    wxImage image = page.icons[i];
    wxImage doubled = page.icons2[i];

    if (image.IsOk()) {
        image = NormalizeIcon(
            image,
            rowColour,
            page.iconBox,
            palette.iconTiles ? &palette.tile : nullptr,
            page.tilePadding);
        if (doubled.IsOk()) {
            doubled = NormalizeIcon(
                doubled,
                rowColour,
                2 * page.iconBox,
                palette.iconTiles ? &palette.tile : nullptr,
                2 * page.tilePadding);
        }

        // The chart does not draw these at this scale: faded.
        if (group.hiddenAtScale) {
            image = FadeImage(image, rowColour, 0.55);
            if (doubled.IsOk())
                doubled = FadeImage(doubled, rowColour, 0.55);
        }
    }

    if (image.IsOk()) {
        group.imageName = AddDynamicImage(
            wxString::Format("legend_dyn_%d_%zu.png", m_dynamic_generation, i),
            image,
            doubled);
    }

    wxString label = group.lightCharacter
                         ? LightRowLabel(page, group, i, rowColour)
                         : EscapeHtml(group.label);
    if (group.labelIsName) {
        // No meaning known yet: the icon name, in grey, as it must
        // be written in symbolen.txt.
        label = "<font color=\"" + HtmlColour(muted) + "\">" + label +
                "</font>";
    }
    if (group.hiddenAtScale) {
        // Not drawn on the chart at this scale: a closed eye and the
        // scale at which it appears; a click zooms in.
        label = "<font color=\"" + HtmlColour(muted) + "\">" + label +
                "</font>";

        const wxString eyeName = AddDynamicImage(
            wxString::Format("legend_dyn_%d_eye_%zu.png", m_dynamic_generation, i),
            EyeIcon(false, muted, rowColour),
            EyeIcon(false, muted, rowColour, 2));

        label << "&nbsp; <img src=\"memory:" << eyeName << "\">";
        if (group.visibleFrom > 0) {
            label << " <font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
                  << FormatScaleShort(group.visibleFrom) << "</font>";
        }
    }
    if (isActive)
        label = "<b>" + label + "</b>";

    // Setting "Afstand en peiling vanaf de boot": the nearest one, small,
    // under the text.
    const wxString distance = DistanceText(group);
    if (!distance.IsEmpty()) {
        label << "<br><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
              << distance << "</font>";
    }

    // Every cell of the row carries the row id, so a click anywhere
    // in the row can be traced back to its symbol. The counter is a
    // button of its own: it marks all objects of the symbol at once.
    const wxString rowId =
        wxString::Format("%s%zu", kLegendRowIdPrefix, i);
    const wxString allId =
        wxString::Format("%s%zu", kLegendAllIdPrefix, i);
    const int iconCell = page.IconCell();

    page.html << "<tr bgcolor=\"" << HtmlColour(rowColour) << "\">"
              << "<td id=\"" << rowId << "\" width=\"" << iconCell + 10
              << "\" height=\"" << iconCell + 10
              << "\" align=\"center\" valign=\"middle\">"
              << "<a name=\"legend-row-" << i << "\"></a>";
    if (!group.imageName.IsEmpty())
        page.html << "<img src=\"memory:" << group.imageName << "\">";
    page.html << "</td><td id=\"" << rowId << "\" valign=\"middle\">"
              << label << "</td>"
              << "<td id=\"" << allId
              << "\" align=\"right\" valign=\"middle\" nowrap>"
              << "<font color=\"" << HtmlColour(isActive ? foreground : muted)
              << "\">" << CountText(group) << "&nbsp;<font size=\"+1\">&#9678;"
              << "</font></font></td></tr>";
}

// The lights row i carries: a block under its text, smaller, joined to
// the row by a line on the left like a branch.
void legend_pi::RenderLightsOfRow(DynamicPage &page, size_t i)
{
    const LegendPalette &palette = page.palette;
    const wxColour &background = palette.background;
    const wxColour &foreground = palette.text;
    const wxColour &stripe = palette.stripe;
    const wxColour &muted = palette.muted;
    wxString &html = page.html;

    std::vector<size_t> children;
    for (size_t j = i + 1; j < m_dynamic_groups.size() &&
                           page.parentOf[j] == static_cast<int>(i);
         ++j) {
        if (page.shown[j])
            children.push_back(j);
    }
    if (children.empty())
        return;

    const wxColour blockColour =
        page.stripeIndex[i] % 2 == 1 ? stripe : background;
    const wxString branch = HtmlColour(palette.headerRule);
    const int iconCell = page.IconCell();

    // wxHTML ignores the height of an empty cell: a small piece of
    // the line gives the room between two lights its height.
    const wxString gapName = wxString::Format(
        "legend_dyn_%d_gap_%zu.png", m_dynamic_generation, i);
    {
        wxImage gap(3, 7);
        gap.SetRGB(wxRect(0, 0, 3, 7), palette.headerRule.Red(),
                   palette.headerRule.Green(), palette.headerRule.Blue());
        wxMemoryFSHandler::AddFile(gapName, gap, wxBITMAP_TYPE_PNG);
        m_dynamic_image_names.push_back(gapName);
    }

    html << "<tr bgcolor=\"" << HtmlColour(blockColour) << "\">"
         << "<td width=\"" << iconCell + 10 << "\"></td>"
         << "<td colspan=\"2\">"
         << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">";

    for (const size_t j : children) {
        DynamicGroup &light = m_dynamic_groups[j];
        light.imageName.Clear();

        const wxColour &lightColour = page.rowColours[j];
        const bool lightActive = IsActiveRow(light);

        if (page.icons[j].IsOk()) {
            const wxString name = wxString::Format(
                "legend_dyn_%d_%zu.png", m_dynamic_generation, j);
            // Not drawn at this scale: faded, like its row.
            const wxImage lamp = light.hiddenAtScale
                                     ? FadeImage(page.icons[j], lightColour, 0.55)
                                     : page.icons[j];
            wxImage lamp2 = page.icons2[j];
            if (lamp2.IsOk() && light.hiddenAtScale)
                lamp2 = FadeImage(lamp2, lightColour, 0.55);
            light.imageName = AddDynamicImage(name, lamp, lamp2);
        }

        wxString lightText = LightRowLabel(page, light, j, lightColour);
        if (light.hiddenAtScale)
            lightText = "<font color=\"" + HtmlColour(muted) + "\">" + lightText + "</font>";
        if (lightActive)
            lightText = "<b>" + lightText + "</b>";

        const wxString lightRowId =
            wxString::Format("%s%zu", kLegendRowIdPrefix, j);
        const wxString lightAllId =
            wxString::Format("%s%zu", kLegendAllIdPrefix, j);

        html << "<tr bgcolor=\"" << HtmlColour(lightColour) << "\">"
             << "<td width=\"3\" bgcolor=\"" << branch << "\"></td>"
             << "<td id=\"" << lightRowId << "\" width=\"10\">"
             << "<a name=\"legend-row-" << j << "\"></a></td>"
             << "<td id=\"" << lightRowId << "\" width=\"30\" valign=\"middle\">";
        if (!light.imageName.IsEmpty())
            html << "<img src=\"memory:" << light.imageName << "\">";
        html << "</td><td id=\"" << lightRowId << "\" valign=\"middle\">"
             << "<font size=\"-1\">" << lightText << "</font></td>"
             << "<td id=\"" << lightAllId
             << "\" align=\"right\" valign=\"middle\" nowrap>"
             << "<font size=\"-1\" color=\""
             << HtmlColour(lightActive ? foreground : muted) << "\">"
             << CountText(light) << "&nbsp;&#9678;</font></td></tr>"
             // Room between the lights; the line runs on.
             << "<tr bgcolor=\"" << HtmlColour(blockColour) << "\">"
             << "<td bgcolor=\"" << branch << "\"><img src=\"memory:" << gapName
             << "\"></td><td colspan=\"4\"></td></tr>";
    }

    html << "</table></td></tr>";
}

// The counts at the foot of the list, and how the list works.
void legend_pi::RenderFooter(DynamicPage &page)
{
    const wxColour &muted = page.palette.muted;
    wxString &html = page.html;

    html << "<p><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">In beeld: "
         << static_cast<int>(m_dynamic_groups.size() - page.lightRows) << " symbolen"
         << " &middot; " << m_dynamic_total << " waypoints";
    if (m_chart_object_total > 0)
        html << " &middot; " << m_chart_object_total << " kaartobjecten";
    if (page.lightRows > 0)
        html << " &middot; " << static_cast<int>(page.lightRows) << " lichtkarakters";
    if (m_show_hidden_at_scale && m_dynamic_hidden + m_chart_objects_scale_hidden > 0) {
        html << " &middot; " << m_dynamic_hidden + m_chart_objects_scale_hidden
             << " op deze schaal verborgen";
    }
    html << ".</font></p>";

    html << "<p><font size=\"-1\" color=\"" << HtmlColour(muted) << "\">"
         << (DistanceFromBoatShown()
                 ? "Onder elke rij staan de afstand en de ware peiling vanaf de "
                   "boot naar het dichtstbijzijnde exemplaar. Klik op een rij om "
                   "naar het exemplaar te gaan dat het dichtst bij de boot ligt; "
                 : "Klik op een rij om naar het dichtstbijzijnde symbool te gaan; ")
         << "nog een klik gaat naar het volgende. Klik op &#9678; om alle "
            "exemplaren in beeld tegelijk aan te wijzen. De lijst past zich "
            "vanzelf aan als je de kaart verschuift of zoomt.";
    if (page.showHeadings)
        html << " Klik op een kopje om de groep in of uit te klappen.";
    if (m_chart_object_total > 0)
        html << " Kaartsymbolen komen uit de kaarten in beeld.";
    if (page.lightsShown)
        html << " Lichtkarakters staan voluit in woorden; het ritme eronder "
                "is schematisch getekend. Een klik laat het licht op de kaart "
                "branden in dat ritme.";
    if (m_show_hidden_at_scale)
        html << " Grijze rijen zijn op deze schaal verborgen; een klik "
                "zoomt in tot ze zichtbaar zijn.";
    html << "</font></p>";
}

void legend_pi::ShowDynamicLegend(bool reloadWaypoints)
{
    if (m_content_book)
        m_content_book->SetSelection(1);

    if (StartWaypointLoading(reloadWaypoints)) {
        auto show = [this]() {
            if (!IsDynamicLegendVisible())
                return;
            CollectDynamicLegend();
            RenderDynamicLegend(false);

            // A layer may have been hidden or shown while the legend was
            // closed.
            m_visibility_timer.StartOnce(300);
        };
        if (!OpenChartCellsFirst(show))
            show();
        return;
    }

    // Loading runs in small steps; the window shows the progress and fills
    // itself at the end.
    RenderLoadingPage();
}
