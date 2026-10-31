// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, the chart: clicks and the menu on the chart, showing an
// object of the legend there, the markers, everything drawn over the
// chart and the night preview.

#ifdef __WXOSX__
#define GL_SILENCE_DEPRECATION
#endif

#include "legend_pi.h"

#include "geo.h"
#include "legend_shared.h"

#include <wx/glcanvas.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/log.h>
#include <wx/menu.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

// The fan of the Uitkijk: by default a soft amber, as the beam of a
// searchlight, just visible over the chart; the setting "Uitkijkbundel:
// kleur" picks another of the colours of kColours. In dusk and night the
// chart is dark: the beam is dimmed less than the other signs and a little
// more opaque, so it still shows, in a warm colour that spares night vision.
//
// Orange is the searchlight: its edge the colour of the mariner's own marks
// in S-52 (own ship's vector, route), apart from the yellow of special marks
// and land; inside, by day, a faint white that lights the water without
// changing the chart's colours below. In dusk and night white would hurt
// night vision: a dim orange inside then.
//
// Grey is black seen through by day; on the dark chart of dusk and night
// black would vanish, so there it turns a light grey, dimmed as the rest.
void AheadFanColours(int colour, wxColour *fill, wxColour *edge)
{
    // Fill and edge: the edge a darker shade of the fill.
    static const unsigned char kColours[][6] = {
        {239, 159, 39, 186, 117, 23},   // amber
        {230, 120, 40, 224, 112, 32},   // orange
        {0, 0, 0, 0, 0, 0},             // grey: black, seen through
    };
    const int count = static_cast<int>(sizeof(kColours) / sizeof(kColours[0]));
    const unsigned char *c = kColours[colour >= 0 && colour < count ? colour : 0];
    double dim = 1.0;
    int fillAlpha = 28;
    int edgeAlpha = 110;
    switch (LegendScheme()) {
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
        dim = 0.8;
        fillAlpha = 40;
        edgeAlpha = 130;
        break;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
        dim = 0.6;
        fillAlpha = 48;
        edgeAlpha = 140;
        break;
    default:
        break;
    }
    auto shade = [dim](int value) { return static_cast<unsigned char>(value * dim); };
    *fill = wxColour(shade(c[0]), shade(c[1]), shade(c[2]), fillAlpha);
    *edge = wxColour(shade(c[3]), shade(c[4]), shade(c[5]), edgeAlpha);
    const bool dark = LegendScheme() == PI_GLOBAL_COLOR_SCHEME_DUSK ||
                      LegendScheme() == PI_GLOBAL_COLOR_SCHEME_NIGHT;
    if (c == kColours[2] && dark) {
        *fill = wxColour(shade(200), shade(200), shade(200), fillAlpha);
        *edge = wxColour(shade(220), shade(220), shade(220), edgeAlpha);
    }
    if (c == kColours[1]) {
        *edge = wxColour(edge->Red(), edge->Green(), edge->Blue(), edgeAlpha + 50);
        if (LegendScheme() == PI_GLOBAL_COLOR_SCHEME_DAY ||
            LegendScheme() == PI_GLOBAL_COLOR_SCHEME_RGB)
            *fill = wxColour(255, 255, 255, 56);
    }
}

// Where the label of the fan goes: beyond its anchor in the direction
// (dx, dy), a few pixels clear of it, whatever side of the text faces it.
wxPoint AheadLabelCorner(const wxPoint &anchor, double dx, double dy, const wxSize &size,
                         double gap)
{
    const double reach = gap + std::fabs(dx) * size.x / 2.0 + std::fabs(dy) * size.y / 2.0;
    return wxPoint(static_cast<int>(std::lround(anchor.x + dx * reach - size.x / 2.0)),
                   static_cast<int>(std::lround(anchor.y + dy * reach - size.y / 2.0)));
}

// Behind the label a pale ground, so it reads over the chart: light by
// day, dark in dusk and night.
wxColour AheadLabelGround()
{
    if (LegendScheme() == PI_GLOBAL_COLOR_SCHEME_DUSK ||
        LegendScheme() == PI_GLOBAL_COLOR_SCHEME_NIGHT)
        return wxColour(0, 0, 0, 110);
    return wxColour(255, 255, 255, 150);
}

// OpenGL has no text: the text drawn white on black in a bitmap, its
// brightness as the opacity of a texture in `colour`, on a ground of
// `ground` a few pixels wider.
void DrawTextGL(const wxString &text, const wxFont &font, const wxColour &colour,
                const wxColour &ground, const wxPoint &anchor, double dx, double dy,
                double gap, int margin)
{
    wxBitmap probe(1, 1, 24);
    wxMemoryDC dc(probe);
    dc.SetFont(font);
    const wxSize size = dc.GetTextExtent(text);
    if (size.x <= 0 || size.y <= 0)
        return;
    wxBitmap bitmap(size.x, size.y, 24);
    dc.SelectObject(bitmap);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();
    dc.SetTextForeground(*wxWHITE);
    dc.DrawText(text, 0, 0);
    dc.SelectObject(wxNullBitmap);

    const wxImage image = bitmap.ConvertToImage();
    const int width = image.GetWidth();
    const int height = image.GetHeight();
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 255);
    const unsigned char *rgb = image.GetData();
    for (int i = 0; i < width * height; ++i)
        pixels[i * 4 + 3] = std::max({rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]});

    const wxPoint at = AheadLabelCorner(
        anchor, dx, dy, wxSize(width + 2 * margin, height + 2 * margin), gap);
    glColor4ub(ground.Red(), ground.Green(), ground.Blue(), ground.Alpha());
    glBegin(GL_QUADS);
    glVertex2i(at.x, at.y);
    glVertex2i(at.x + width + 2 * margin, at.y);
    glVertex2i(at.x + width + 2 * margin, at.y + height + 2 * margin);
    glVertex2i(at.x, at.y + height + 2 * margin);
    glEnd();

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4ub(colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());

    const int x = at.x + margin;
    const int y = at.y + margin;
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2i(x, y);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2i(x + width, y);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2i(x + width, y + height);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2i(x, y + height);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
}

} // namespace

// Viewport pixels per window pixel: the viewport counts physical pixels,
// mouse events logical ones (2 to 1 on a Retina display).
double legend_pi::ViewportPixelsPerWindowPixel() const
{
    wxWindow *canvas = PluginGetFocusCanvas();
    if (!canvas)
        canvas = GetCanvasByIndex(0);
    if (!canvas || canvas->GetClientSize().x <= 0 || !m_viewport_valid)
        return 1.0;
    return static_cast<double>(m_viewport.pix_width) / canvas->GetClientSize().x;
}

// Looks at clicks on the chart; OpenCPN handles every event as usual.
bool legend_pi::MouseEventHook(wxMouseEvent &event)
{
    // Above the sign of a note: its title as a tooltip.
    if (event.Moving() && m_viewport_valid && !m_note_places.empty()) {
        const double ratio = ViewportPixelsPerWindowPixel();
        const wxPoint at(static_cast<int>(std::lround(event.GetX() * ratio)),
                         static_cast<int>(std::lround(event.GetY() * ratio)));
        const int hover = NotePlaceAt(at, 12.0 * ratio);
        if (hover != m_note_hover) {
            m_note_hover = hover;
            wxWindow *canvas = PluginGetFocusCanvas();
            if (!canvas)
                canvas = GetOCPNCanvasWindow();
            if (canvas) {
                if (hover >= 0)
                    canvas->SetToolTip("Notitie: " + m_note_places[hover].title);
                else
                    canvas->UnsetToolTip();
            }
        }
        return false;
    }

    // A sign held down: a click opens its note, a drag moves it. The mouse
    // stays here, so the chart neither pans nor centres meanwhile.
    if (event.LeftDown() && m_viewport_valid) {
        const double ratio = ViewportPixelsPerWindowPixel();
        const wxPoint at(static_cast<int>(std::lround(event.GetX() * ratio)),
                         static_cast<int>(std::lround(event.GetY() * ratio)));
        const int note = NotePlaceAt(at, 12.0 * ratio);
        if (note >= 0) {
            m_note_drag = note;
            m_note_drag_path = m_note_places[note].path;
            m_note_drag_start = event.GetPosition();
            m_note_drag_moved = false;
            m_note_drag_from_lat = m_note_places[note].lat;
            m_note_drag_from_lon = m_note_places[note].lon;
            m_mouse_down_valid = false;
            return true;
        }
    }

    if (m_note_drag >= 0) {
        // The places were read again meanwhile: find the sign by its note.
        if (static_cast<size_t>(m_note_drag) >= m_note_places.size() ||
            m_note_places[m_note_drag].path != m_note_drag_path) {
            m_note_drag = -1;
            for (size_t i = 0; i < m_note_places.size(); ++i) {
                if (m_note_places[i].path == m_note_drag_path)
                    m_note_drag = static_cast<int>(i);
            }
            if (m_note_drag < 0)
                return false;
        }

        if (event.Dragging() && event.LeftIsDown()) {
            const wxPoint now = event.GetPosition();
            if (std::abs(now.x - m_note_drag_start.x) > 4 ||
                std::abs(now.y - m_note_drag_start.y) > 4)
                m_note_drag_moved = true;
            if (m_note_drag_moved) {
                const double ratio = ViewportPixelsPerWindowPixel();
                PlugIn_ViewPort vp = m_viewport;
                double lat = 0.0;
                double lon = 0.0;
                GetCanvasLLPix(&vp,
                               wxPoint(static_cast<int>(std::lround(now.x * ratio)),
                                       static_cast<int>(std::lround(now.y * ratio))),
                               &lat, &lon);
                m_note_places[m_note_drag].lat = lat;
                m_note_places[m_note_drag].lon = lon;
                m_note_selected = m_note_drag;
                RefreshAllCanvases();
            }
            return true;
        }

        if (event.LeftUp()) {
            const NotePlace place = m_note_places[m_note_drag];
            m_note_drag = -1;
            if (m_note_drag_moved) {
                m_undo_move_valid = true;
                m_undo_move_path = place.path;
                m_undo_move_title = place.title;
                m_undo_move_lat = m_note_drag_from_lat;
                m_undo_move_lon = m_note_drag_from_lon;
                MoveNotePlace(place.path, place.lat, place.lon);
                // Moved by mistake, while panning the chart: one click puts
                // it back.
                ShowNotice("Notitie verplaatst",
                           wxString::FromUTF8("\u201C") + place.title +
                               wxString::FromUTF8("\u201D staat op een nieuwe plek."),
                           false, wxColour(186, 117, 23), [this]() { UndoNoteMove(); },
                           "Terugzetten");
                return true;
            }
            // A click: the note opens and its radius shows.
            for (size_t i = 0; i < m_note_places.size(); ++i) {
                if (m_note_places[i].path == place.path)
                    m_note_selected = static_cast<int>(i);
            }
            RefreshAllCanvases();
            const wxString path = place.path;
            if (wxWindow *canvas = GetOCPNCanvasWindow())
                canvas->CallAfter([this, path]() { OpenNote(path); });
            return true;
        }
        return true;
    }

    if (event.LeftDown()) {
        m_mouse_down = event.GetPosition();
        m_mouse_down_valid = true;
        return false;
    }

    if (!event.LeftUp() || !m_mouse_down_valid)
        return false;
    m_mouse_down_valid = false;

    // A drag is not a click.
    const wxPoint up = event.GetPosition();
    if (std::abs(up.x - m_mouse_down.x) > 4 || std::abs(up.y - m_mouse_down.y) > 4)
        return false;

    if (!m_viewport_valid)
        return false;

    const double ratio = ViewportPixelsPerWindowPixel();
    const wxPoint pixel(static_cast<int>(std::lround(up.x * ratio)),
                        static_cast<int>(std::lround(up.y * ratio)));

    // A click beside the signs: no radius shows any more.
    if (m_note_selected >= 0) {
        m_note_selected = -1;
        RefreshAllCanvases();
    }

    HighlightVhfAreaAt(pixel, 14.0 * ratio);

    if (!IsDynamicLegendVisible())
        return false;

    PlugIn_ViewPort vp = m_viewport;
    double lat = 0.0;
    double lon = 0.0;
    GetCanvasLLPix(
        &vp,
        wxPoint(static_cast<int>(std::lround(up.x * ratio)),
                static_cast<int>(std::lround(up.y * ratio))),
        &lat, &lon);

    RevealObjectAt(lat, lon, 14.0 * ratio, false);
    return false;
}

void legend_pi::SetCursorLatLon(double lat, double lon)
{
    m_cursor_lat = lat;
    m_cursor_lon = lon;
    m_cursor_valid = true;
}

// Fills the submenu "Notitie" just before the chart's menu opens: what
// fits the spot (a note's sign under the mouse) and the state (a note
// being edited).
void legend_pi::PrepareContextMenu(int canvasIndex)
{
    (void)canvasIndex;
    if (!m_context_submenu)
        return;

    while (m_context_submenu->GetMenuItemCount() > 0)
        m_context_submenu->Destroy(m_context_submenu->FindItemByPosition(0));

    // The note whose sign is under the mouse, if any.
    m_menu_note_path.Clear();
    wxString noteTitle;
    if (m_cursor_valid && m_viewport_valid) {
        PlugIn_ViewPort vp = m_viewport;
        wxPoint at;
        GetCanvasPixLL(&vp, &at, m_cursor_lat, m_cursor_lon);
        const int note = NotePlaceAt(at, 14.0 * ViewportPixelsPerWindowPixel());
        if (note >= 0) {
            m_menu_note_path = m_note_places[note].path;
            noteTitle = m_note_places[note].title;
        }
    }

    // In the submenu "Notitie" the word need not be repeated.
    // With a recent GPS fix also the boat's position, wherever the click was.
    const bool boat = BoatPositionKnown();
    if (m_editing) {
        m_context_submenu->Append(m_menu_link_place, "Link naar deze plek in de tekst");
        m_context_submenu->Append(m_menu_pin_note, "Hier vastpinnen op de kaart");
        if (boat) {
            m_context_submenu->AppendSeparator();
            m_context_submenu->Append(m_menu_link_boat, "Positie van de boot in de tekst");
            m_context_submenu->Append(m_menu_pin_boat, "Vastpinnen op de positie van de boot");
        }
    }
    else {
        m_context_submenu->Append(m_menu_new_note, "Nieuwe notitie hier");
        if (boat)
            m_context_submenu->Append(m_menu_new_note_boat, "Nieuwe notitie bij de boot");
    }
    if (!m_menu_note_path.IsEmpty()) {
        m_context_submenu->AppendSeparator();
        const wxString quoted =
            wxString::FromUTF8("\u201C") + noteTitle + wxString::FromUTF8("\u201D");
        m_context_submenu->Append(m_menu_open_note, quoted + " openen");
        m_context_submenu->Append(m_menu_delete_note,
                                  quoted + wxString::FromUTF8(" verwijderen\u2026"));
    }
    if (m_undo_move_valid) {
        m_context_submenu->AppendSeparator();
        m_context_submenu->Append(m_menu_undo_move,
                                  wxString::FromUTF8("\u201C") + m_undo_move_title +
                                      wxString::FromUTF8("\u201D terugzetten op de oude plek"));
    }
}

void legend_pi::OnContextMenuItemCallback(int id)
{
    // The submenu itself also reports a click: the hidden item does the work.
    if (id == m_menu_parent)
        return;
    if (id == m_menu_new_note_boat) {
        NewNoteAtBoat();
        return;
    }
    if (id == m_menu_link_boat) {
        InsertBoatPosition();
        return;
    }
    if (id == m_menu_pin_boat) {
        PinNoteAtBoat();
        return;
    }
    if (id == m_menu_undo_move) {
        UndoNoteMove();
        return;
    }
    if (!m_cursor_valid)
        return;

    if (id == m_menu_reveal)
        RevealObjectAt(m_cursor_lat, m_cursor_lon, 20.0 * ViewportPixelsPerWindowPixel(), true);
    else if (id == m_menu_vhf)
        ShowVhfHere(m_cursor_lat, m_cursor_lon);
    else if (id == m_menu_new_note)
        NewNoteAt(m_cursor_lat, m_cursor_lon);
    else if (id == m_menu_link_place)
        InsertPlace(m_cursor_lat, m_cursor_lon);
    else if (id == m_menu_pin_note)
        SetNotePlace(m_cursor_lat, m_cursor_lon);
    else if (id == m_menu_open_note && !m_menu_note_path.IsEmpty())
        OpenNote(m_menu_note_path);
    else if (id == m_menu_delete_note && !m_menu_note_path.IsEmpty())
        DeleteNoteFile(m_menu_note_path);
    else if (id == m_menu_new_note_boat)
        NewNoteAtBoat();
    else if (id == m_menu_link_boat)
        InsertBoatPosition();
    else if (id == m_menu_pin_boat)
        PinNoteAtBoat();
}

bool legend_pi::RevealObjectAt(
    double lat,
    double lon,
    double tolerancePx,
    bool openWindow)
{
    if (openWindow) {
        CreateLegendWindow();
        if (!m_legend_window->IsShown()) {
            RefreshLegendFiles();
            m_legend_window->Show();
            m_legend_window->Raise();
        }
        // The dynamic legend is always the first item.
        if (m_current_legend_path != kDynamicGpxId && !m_legend_files.IsEmpty())
            ShowLegend(0);
    }

    if (!IsDynamicLegendVisible() || !m_viewport_valid || m_reorder_mode)
        return false;

    // The list is still being filled: reveal when that is done.
    if (m_loading || m_showing_loading_page) {
        m_pending_reveal = true;
        m_pending_reveal_lat = lat;
        m_pending_reveal_lon = lon;
        m_pending_reveal_tolerance = tolerancePx;
        return false;
    }

    PlugIn_ViewPort vp = m_viewport;
    wxPoint target;
    GetCanvasPixLL(&vp, &target, lat, lon);

    double best = tolerancePx * tolerancePx;
    size_t bestGroup = 0;
    size_t bestPosition = 0;
    bool found = false;

    // A lit buoy has a row for its symbol, in a chart also one for the
    // light's flare, and a light row: the symbol comes first (its light is
    // shown along with it), the flare next, a light row only where there
    // is nothing else.
    auto pass = [](const DynamicGroup &group) {
        return group.lightCharacter ? 2 : group.lightSymbol ? 1 : 0;
    };
    // The same buoy can be listed twice (a Nautin waypoint and a chart
    // symbol); of rows about equally near, the one that carries a light
    // there wins, so the light is shown along with it.
    auto carriesLight = [this](size_t index, const std::pair<double, double> &at) {
        const DynamicGroup &group = m_dynamic_groups[index];
        for (size_t k = index + 1;
             k < m_dynamic_groups.size() && m_dynamic_groups[k].parentKey == group.key;
             ++k) {
            for (const auto &p : m_dynamic_groups[k].positions) {
                if (std::fabs(p.first - at.first) < 0.0003 &&
                    std::fabs(p.second - at.second) < 0.0005)
                    return true;
            }
        }
        return false;
    };
    const double lightBonus = 12.0 * 12.0;

    for (int round = 0; round < 3 && !found; ++round) {
        for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
            const DynamicGroup &group = m_dynamic_groups[i];
            if (pass(group) != round)
                continue;
            for (size_t j = 0; j < group.positions.size(); ++j) {
                wxPoint pixel;
                GetCanvasPixLL(&vp, &pixel, group.positions[j].first,
                               group.positions[j].second);
                const double dx = pixel.x - target.x;
                const double dy = pixel.y - target.y;
                const double raw = dx * dx + dy * dy;
                if (raw > tolerancePx * tolerancePx)
                    continue;
                double distance = raw;
                if (round == 0 && carriesLight(i, group.positions[j]))
                    distance = std::max(0.0, raw - lightBonus);
                if (!found || distance <= best) {
                    best = distance;
                    bestGroup = i;
                    bestPosition = j;
                    found = true;
                }
            }
        }
    }

    if (!found)
        return false;

    RevealGroupPosition(bestGroup, bestPosition);
    return true;
}

// Shows one object in the list: its group open, its row highlighted and
// scrolled into view, the counter on this object, and a marker on the chart.
void legend_pi::RevealGroupPosition(size_t groupIndex, size_t positionIndex)
{
    if (groupIndex >= m_dynamic_groups.size())
        return;

    DynamicGroup &group = m_dynamic_groups[groupIndex];
    if (positionIndex >= group.positions.size())
        return;

    group.current = static_cast<int>(positionIndex);
    m_active_group_key = group.key;
    m_active_light_key.Clear();
    m_flash_light_key.Clear();

    if (m_collapsed_sections.erase(SectionName(group.section)) > 0)
        SaveConfig();

    const std::pair<double, double> position = group.positions[positionIndex];

    // The light this object carries: the nearest position in the light
    // rows under it, within the distance at which lights count as one.
    int lightRow = -1;
    size_t lightPosition = 0;
    if (!group.lightCharacter) {
        double best = 0.0;
        for (size_t k = groupIndex + 1;
             k < m_dynamic_groups.size() && m_dynamic_groups[k].parentKey == group.key;
             ++k) {
            const DynamicGroup &row = m_dynamic_groups[k];
            for (size_t p = 0; p < row.positions.size(); ++p) {
                const double dlat = std::fabs(row.positions[p].first - position.first);
                const double dlon = std::fabs(row.positions[p].second - position.second);
                if (dlat >= 0.0003 || dlon >= 0.0005)
                    continue;
                const double distance = dlat * dlat + dlon * dlon;
                if (lightRow < 0 || distance < best) {
                    lightRow = static_cast<int>(k);
                    lightPosition = p;
                    best = distance;
                }
            }
        }
    }

    // No light under this row: the nearest light row anywhere in the list,
    // for a light that ended up under another row of the same buoy.
    if (lightRow < 0 && !group.lightCharacter) {
        double best = 0.0;
        for (size_t k = 0; k < m_dynamic_groups.size(); ++k) {
            const DynamicGroup &row = m_dynamic_groups[k];
            if (!row.lightCharacter)
                continue;
            for (size_t p = 0; p < row.positions.size(); ++p) {
                const double dlat = std::fabs(row.positions[p].first - position.first);
                const double dlon = std::fabs(row.positions[p].second - position.second);
                if (dlat >= 0.0003 || dlon >= 0.0005)
                    continue;
                const double distance = dlat * dlat + dlon * dlon;
                if (lightRow < 0 || distance < best) {
                    lightRow = static_cast<int>(k);
                    lightPosition = p;
                    best = distance;
                }
            }
        }
    }

    wxLogMessage("Legend: reveal %s (%zu objects), light: %s",
                 group.key, group.positions.size(),
                 lightRow >= 0 ? m_dynamic_groups[lightRow].key : wxString("none"));

    if (lightRow >= 0) {
        m_dynamic_groups[lightRow].current = static_cast<int>(lightPosition);
        m_active_light_key = m_dynamic_groups[lightRow].key;
        m_flash_light_key = m_active_light_key;
    }

    // A search that leaves this row out is given up, or the row could not
    // be shown. A light row is shown with its row, and the other way round.
    bool matches = GroupMatchesFilter(group);
    if (!matches && !group.parentKey.IsEmpty()) {
        for (const DynamicGroup &other : m_dynamic_groups) {
            if (other.key == group.parentKey) {
                matches = GroupMatchesFilter(other);
                break;
            }
        }
    }
    if (!matches && lightRow >= 0)
        matches = GroupMatchesFilter(m_dynamic_groups[lightRow]);
    if (!matches) {
        m_filter_timer.Stop();
        m_filter.Clear();
        m_filter_words.clear();
        if (m_search)
            m_search->ChangeValue(wxEmptyString);
    }

    m_flash_key = group.key;
    m_flash_phase = 0;
    m_flash_timer.Stop();
    m_flash_timer.Start(150);

    m_scroll_to_row = static_cast<int>(groupIndex);
    RenderDynamicLegend(false);

    // On the chart the light shines, when there is one; else the marker.
    const DynamicGroup &lightGroup =
        lightRow >= 0 ? m_dynamic_groups[lightRow] : group;
    LightCharacter light;
    if (lightGroup.lightCharacter && light.Parse(lightGroup.lightText))
        ShowLightMarkers(light, {position});
    else
        ShowMarker(position.first, position.second);
}

// One step of the fade of the found row.
void legend_pi::OnFlashTimer()
{
    ++m_flash_phase;
    if (m_flash_phase >= kFadeSteps || m_flash_key.IsEmpty() ||
        m_current_legend_path != kDynamicGpxId) {
        m_flash_timer.Stop();
        m_flash_key.Clear();
        m_flash_light_key.Clear();
    }
    RenderDynamicLegend(true);
}

void legend_pi::GoToDynamicGroup(size_t index)
{
    if (index >= m_dynamic_groups.size())
        return;

    DynamicGroup &group = m_dynamic_groups[index];

    if (group.positions.empty())
        return;

    // Distance from the boat shown: a first click on this row, or one
    // after a click on another row, starts at the object nearest to the
    // boat; further clicks go on from there.
    if (DistanceFromBoatShown() && (group.current < 0 || m_active_group_key != group.key)) {
        SortFromBoat(group);
        group.current = -1;
    }

    group.current =
        (group.current + 1) % static_cast<int>(group.positions.size());
    m_active_group_key = group.key;
    m_active_light_key.Clear();

    const std::pair<double, double> &position =
        group.positions[static_cast<size_t>(group.current)];

    // Keep the current zoom level, unless the object is hidden at this
    // scale: then zoom in to just past the scale at which it appears.
    double scale = m_viewport_valid ? m_viewport.view_scale_ppm : 0.0;
    if (scale <= 0.0)
        scale = 0.1;

    const double zoomScale =
        group.hiddenAtScale ? ViewScaleForLimit(group.visibleFrom) : 0.0;
    const bool zoomIn = zoomScale > 0.0;
    if (zoomIn)
        scale = zoomScale;

    // Only move the chart when the object is not comfortably in view.
    // Otherwise the chart stays where it is and the marker shows the spot.
    bool comfortablyVisible = false;

    if (m_viewport_valid && !zoomIn) {
        wxPoint pixel;
        GetCanvasPixLL(&m_viewport, &pixel, position.first, position.second);

        const int marginX = m_viewport.pix_width / 8;
        const int marginY = m_viewport.pix_height / 8;

        comfortablyVisible =
            pixel.x >= marginX &&
            pixel.x <= m_viewport.pix_width - marginX &&
            pixel.y >= marginY &&
            pixel.y <= m_viewport.pix_height - marginY;
    }

    if (!comfortablyVisible)
        JumpTo(position.first, position.second, scale);

    // A light character shines on the chart in its rhythm; other symbols
    // get the marker.
    LightCharacter light;
    if (group.lightCharacter && light.Parse(group.lightText))
        ShowLightMarkers(light, {position});
    else
        ShowMarker(position.first, position.second);

    // A VHF post: its whole area lights up as well.
    if (!group.chartSymbol)
        HighlightVhfAreas(group.iconName.IsEmpty() ? group.key : group.iconName, {position});

    RenderDynamicLegend(true);
}


// Marks every object of a symbol in view at once. The chart does not move:
// they are all in view already. The counter of the row stays as it is.
// The view scale at which objects with this limit are just drawn: the
// limit is compared with the scale of the status bar, and the view scale
// is inversely proportional to that.
double legend_pi::ViewScaleForLimit(int limit)
{
    if (limit <= 0 || !m_viewport_valid)
        return 0.0;

    const double displayed = DisplayedScale(m_viewport);
    if (displayed <= 0.0)
        return 0.0;

    const double target = limit * 0.8;
    const double scale = m_viewport.view_scale_ppm * displayed / target;

    wxLogMessage(
        "Legend: zoom: displayed scale 1:%.0f (viewport chart scale 1:%.0f, "
        "ppm %.6f) -> target 1:%.0f for limit 1:%d, requested ppm %.6f",
        displayed, static_cast<double>(m_viewport.chart_scale),
        m_viewport.view_scale_ppm, target, limit, scale);
    m_log_next_viewport = true;
    m_logged_chart_scale = m_viewport.chart_scale;
    return scale;
}

void legend_pi::JumpTo(double lat, double lon, double viewScale)
{
    wxWindow *canvas = PluginGetFocusCanvas();
    if (!canvas)
        canvas = GetCanvasByIndex(0);

    if (canvas)
        CanvasJumpToPosition(canvas, lat, lon, viewScale);
    else
        JumpToPosition(lat, lon, viewScale);
}

void legend_pi::MarkAllOfDynamicGroup(size_t index)
{
    if (index >= m_dynamic_groups.size())
        return;

    const DynamicGroup &group = m_dynamic_groups[index];
    if (group.positions.empty())
        return;

    // A row hidden at this scale: zoom in to its limit around the middle
    // of its objects first, so the marked objects can be seen.
    if (group.hiddenAtScale) {
        const double zoomScale = ViewScaleForLimit(group.visibleFrom);
        if (zoomScale > 0.0) {
            double lat = 0.0;
            double lon = 0.0;
            for (const auto &position : group.positions) {
                lat += position.first;
                lon += position.second;
            }
            lat /= group.positions.size();
            lon /= group.positions.size();
            JumpTo(lat, lon, zoomScale);
        }
    }

    m_active_group_key = group.key;
    m_active_light_key.Clear();

    LightCharacter light;
    if (group.lightCharacter && light.Parse(group.lightText))
        ShowLightMarkers(light, group.positions);
    else
        ShowMarkers(group.positions);

    if (!group.chartSymbol)
        HighlightVhfAreas(group.iconName.IsEmpty() ? group.key : group.iconName,
                          group.positions);

    RenderDynamicLegend(true);
}

void legend_pi::RefreshAllCanvases()
{
    const int count = GetCanvasCount();

    for (int i = 0; i < count; ++i) {
        wxWindow *canvas = GetCanvasByIndex(i);

        if (canvas)
            RequestRefresh(canvas);
    }
}

void legend_pi::ShowMarker(double lat, double lon)
{
    ShowMarkers({{lat, lon}});
}

// Marks one or more positions for about six seconds.
void legend_pi::ShowMarkers(
    const std::vector<std::pair<double, double>> &positions)
{
    if (positions.empty())
        return;

    // One marker at a time: a shining light gives way.
    m_chosen_light.Stop();

    m_marker_positions = positions;
    m_marker_phase = 0;
    m_marker_active = true;

    if (m_marker_timer.IsRunning())
        m_marker_timer.Stop();

    m_marker_timer.Start(120);
    RefreshAllCanvases();
}

void legend_pi::StopMarker()
{
    if (m_marker_timer.IsRunning())
        m_marker_timer.Stop();

    if (m_marker_active) {
        m_marker_active = false;
        RefreshAllCanvases();
    }
}

void legend_pi::OnMarkerTimer()
{
    ++m_marker_phase;

    // About six seconds in total.
    if (m_marker_phase > 50) {
        StopMarker();
        return;
    }

    RefreshAllCanvases();
}

// A light with a rhythm shines; one without is marked as any object.
void legend_pi::ShowLightMarkers(
    const LightCharacter &light,
    const std::vector<std::pair<double, double>> &positions)
{
    if (positions.empty())
        return;

    if (!m_chosen_light.Show(light, positions)) {
        ShowMarkers(positions);
        return;
    }
    StopMarker();
}

LightStyle legend_pi::CurrentLightStyle() const
{
    LightStyle style;
    MarkerColours(style.outline, style.highlight);
    style.dim = std::max(0.6, SchemeDim());
    style.pixelsPerPoint = ViewportPixelsPerWindowPixel();
    return style;
}

bool legend_pi::RenderOverlayMultiCanvas(
    wxDC &dc,
    PlugIn_ViewPort *vp,
    int canvasIndex,
    int priority)
{
    (void)canvasIndex;

    if ((!m_marker_active && !m_chosen_light.IsActive() && !m_all_lights.IsOn() &&
         !m_night_preview && m_vhf_highlight.IsEmpty() && m_note_places.empty() &&
         !AheadFanVisible()) ||
        !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    if (m_night_preview)
        DrawNightShade(dc, vp);
    const bool drewFan = DrawAheadFan(dc, vp);
    const bool drewVhf = !m_vhf_highlight.IsEmpty() &&
                         m_vhf_highlight.Draw(dc, vp, ViewportPixelsPerWindowPixel(), IsDimScheme());
    const bool drewNotes = DrawNoteSigns(dc, vp);
    const LightStyle style = CurrentLightStyle();
    const bool drewAll = (m_all_lights.IsOn() && m_all_lights.Draw(dc, vp, style, m_light_sectors)) ||
                         m_night_preview || drewVhf || drewNotes || drewFan;
    const bool drewLight =
        (m_chosen_light.IsActive() && m_chosen_light.Draw(dc, vp, style, m_light_sectors)) ||
        drewAll;
    if (!m_marker_active)
        return drewLight;

    const std::vector<wxPoint> centres = MarkerCentres(vp, m_marker_positions);
    if (centres.empty())
        return drewLight;

    const int radius = 16;
    const int pulse = m_marker_phase % 10;

    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    wxColour outline;
    wxColour highlight;
    MarkerColours(outline, highlight);

    for (const wxPoint &centre : centres) {
        // Outline first, so the marker stands out on any chart colour.
        dc.SetPen(wxPen(outline, 7));
        dc.DrawCircle(centre, radius);
        dc.DrawLine(centre.x - radius - 12, centre.y,
                    centre.x - radius + 2, centre.y);
        dc.DrawLine(centre.x + radius - 2, centre.y,
                    centre.x + radius + 12, centre.y);
        dc.DrawLine(centre.x, centre.y - radius - 12,
                    centre.x, centre.y - radius + 2);
        dc.DrawLine(centre.x, centre.y + radius - 2,
                    centre.x, centre.y + radius + 12);

        dc.SetPen(wxPen(highlight, 3));
        dc.DrawCircle(centre, radius);
        dc.DrawLine(centre.x - radius - 10, centre.y,
                    centre.x - radius + 2, centre.y);
        dc.DrawLine(centre.x + radius - 2, centre.y,
                    centre.x + radius + 10, centre.y);
        dc.DrawLine(centre.x, centre.y - radius - 10,
                    centre.x, centre.y - radius + 2);
        dc.DrawLine(centre.x, centre.y + radius - 2,
                    centre.x, centre.y + radius + 10);

        // A pulsing ring that draws the eye.
        dc.SetPen(wxPen(highlight, 2));
        dc.DrawCircle(centre, radius + 8 + pulse * 3);
    }

    return true;
}

bool legend_pi::RenderGLOverlayMultiCanvas(
    wxGLContext *pcontext,
    PlugIn_ViewPort *vp,
    int canvasIndex,
    int priority)
{
    (void)pcontext;
    (void)canvasIndex;

    if ((!m_marker_active && !m_chosen_light.IsActive() && !m_all_lights.IsOn() &&
         !m_night_preview && m_vhf_highlight.IsEmpty() && m_note_places.empty() &&
         !AheadFanVisible()) ||
        !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    if (m_night_preview)
        DrawNightShadeGL(vp);
    const bool drewFan = DrawAheadFanGL(vp);
    const bool drewVhf = !m_vhf_highlight.IsEmpty() &&
                         m_vhf_highlight.DrawGL(vp, ViewportPixelsPerWindowPixel(), IsDimScheme());
    const bool drewNotes = DrawNoteSignsGL(vp);
    const LightStyle style = CurrentLightStyle();
    const bool drewAll = (m_all_lights.IsOn() && m_all_lights.DrawGL(vp, style, m_light_sectors)) ||
                         m_night_preview || drewVhf || drewNotes || drewFan;
    const bool drewLight =
        (m_chosen_light.IsActive() && m_chosen_light.DrawGL(vp, style, m_light_sectors)) ||
        drewAll;
    if (!m_marker_active)
        return drewLight;

    const std::vector<wxPoint> centres = MarkerCentres(vp, m_marker_positions);
    if (centres.empty())
        return drewLight;

    const double radius = 16.0;
    const int pulse = m_marker_phase % 10;

    // GL_CURRENT_BIT included: the drawing colour set here would otherwise
    // stay in effect for whatever OpenCPN draws next, and chart symbols
    // are modulated by it (black symbols in the night scheme).
    glPushAttrib(
        GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
        GL_HINT_BIT);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    auto circle = [](double x, double y, double r) {
        const int segments = 48;
        const double kPi = 3.14159265358979323846;

        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    auto ticks = [](double x, double y, double r, double length) {
        glBegin(GL_LINES);
        glVertex2d(x - r - length, y);
        glVertex2d(x - r + 2, y);
        glVertex2d(x + r - 2, y);
        glVertex2d(x + r + length, y);
        glVertex2d(x, y - r - length);
        glVertex2d(x, y - r + 2);
        glVertex2d(x, y + r - 2);
        glVertex2d(x, y + r + length);
        glEnd();
    };

    wxColour outline;
    wxColour highlight;
    MarkerColours(outline, highlight);

    for (const wxPoint &centre : centres) {
        const double cx = centre.x;
        const double cy = centre.y;

        // Outline first, so the marker stands out on any chart colour.
        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(7.0f);
        circle(cx, cy, radius);
        ticks(cx, cy, radius, 12.0);

        glColor4ub(highlight.Red(), highlight.Green(), highlight.Blue(), 255);
        glLineWidth(3.0f);
        circle(cx, cy, radius);
        ticks(cx, cy, radius, 10.0);

        // A pulsing ring that draws the eye.
        glLineWidth(2.0f);
        circle(cx, cy, radius + 8.0 + pulse * 3.0);
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();

    return true;
}

// Nachtvoorbeeld: the chart as it looks from the water at night. A dark
// shade over the chart, through which the coast still shows faintly, with
// all lights in view shining on it in their rhythm.
void legend_pi::SetNightPreview(bool on)
{
    if (on == m_night_preview)
        return;
    m_night_preview = on;
    g_night_preview = on;
    ApplyViewColours();
    if (on) {
        m_night_had_all_lights = m_all_lights.IsOn();
        if (!m_all_lights.IsOn())
            SetAllLightsShining(true);
    }
    else if (!m_night_had_all_lights) {
        SetAllLightsShining(false);
    }
    RefreshAllCanvases();
}

void legend_pi::SetAllLightsShining(bool on)
{
    if (on)
        UpdateAllLights();
    m_all_lights.SetOn(on);
}

// The lights of the list, so the shining lights follow the chart.
void legend_pi::UpdateAllLights()
{
    m_all_lights.ClearLights();
    for (const DynamicGroup &group : m_dynamic_groups) {
        // Only what the chart draws at this scale.
        if (group.lightCharacter && !group.hiddenAtScale)
            m_all_lights.AddLight(group.lightText, group.positions);
    }
}

// The fan is drawn while it means something: the setting is on, the
// window is open and the boat sails (a recent fix and some speed, as
// ComputeAhead() asks). The window may show a note meanwhile: a glance at
// it does not take the fan away.
bool legend_pi::AheadFanVisible() const
{
    return m_ahead_fan_shown && m_legend_window && m_legend_window->IsShown() &&
           m_ship_time > 0 &&
           (wxGetUTCTimeMillis() - m_ship_time) < 60000 && !std::isnan(m_ship_cog) &&
           !std::isnan(m_ship_sog) && m_ship_sog >= 0.5;
}

std::vector<wxPoint> legend_pi::AheadFanPixels(PlugIn_ViewPort *vp) const
{
    std::vector<wxPoint> points;
    if (!AheadFanVisible())
        return points;
    for (const auto &corner : FanOutline(m_ship_lat, m_ship_lon, m_ship_cog, AheadRange(),
                                         m_ahead_angle, kAheadMinHalfWidth)) {
        wxPoint at;
        GetCanvasPixLL(vp, &at, corner.first, corner.second);
        points.push_back(at);
    }
    return points;
}

void legend_pi::AheadFanMarks(PlugIn_ViewPort *vp, std::vector<std::vector<wxPoint>> *lines,
                              AheadFanLabel *label) const
{
    label->text.Clear();
    if (!AheadFanVisible())
        return;
    auto pixel = [vp](double lat, double lon) {
        wxPoint at;
        GetCanvasPixLL(vp, &at, lat, lon);
        return at;
    };

    // Only while the fan is large enough on the screen to carry them.
    const double scale = std::max(1.0, ViewportPixelsPerWindowPixel());
    const double range = AheadRange();
    double lat = 0.0;
    double lon = 0.0;
    PositionAt(m_ship_lat, m_ship_lon, m_ship_cog, range, 0.0, &lat, &lon);
    const wxPoint boat = pixel(m_ship_lat, m_ship_lon);
    const wxPoint end = pixel(lat, lon);
    const double length = std::hypot(end.x - boat.x, end.y - boat.y);
    if (length < 80.0 * scale)
        return;

    label->text = FormatMiles(range);
    const wxString time = AheadTimeText(range);
    if (!time.IsEmpty())
        label->text << ", " << time;
    // At the starboard corner of the far end, beyond it as seen from the
    // boat: the middle of the far end, straight ahead, stays clear.
    const auto farEnd = FanArc(m_ship_lat, m_ship_lon, m_ship_cog, range, m_ahead_angle,
                               kAheadMinHalfWidth);
    const wxPoint corner = pixel(farEnd.back().first, farEnd.back().second);
    const double reach = std::hypot(corner.x - boat.x, corner.y - boat.y);
    label->anchor = corner;
    label->dx = reach > 0.0 ? (corner.x - boat.x) / reach : 0.0;
    label->dy = reach > 0.0 ? (corner.y - boat.y) / reach : -1.0;

    // Lines too close together on the screen say nothing: none then.
    const std::vector<double> miles = FanMileLines(range);
    const double step = miles.empty() ? range : miles.front();
    if (step / range * length < 25.0 * scale)
        return;
    for (double distance : miles) {
        std::vector<wxPoint> line;
        for (const auto &point : FanArc(m_ship_lat, m_ship_lon, m_ship_cog, distance,
                                        m_ahead_angle, kAheadMinHalfWidth))
            line.push_back(pixel(point.first, point.second));
        lines->push_back(line);
    }
}

wxFont legend_pi::AheadFanFont() const
{
    const int height =
        static_cast<int>(std::lround(12.0 * std::max(1.0, ViewportPixelsPerWindowPixel())));
    return wxFont(wxFontInfo(wxSize(0, height)).Family(wxFONTFAMILY_SWISS));
}

bool legend_pi::DrawAheadFan(wxDC &dc, PlugIn_ViewPort *vp) const
{
    const std::vector<wxPoint> points = AheadFanPixels(vp);
    if (points.empty())
        return false;
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return false;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    wxColour fill;
    wxColour edge;
    AheadFanColours(m_ahead_colour, &fill, &edge);
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(points[0].x, points[0].y);
    for (size_t i = 1; i < points.size(); ++i)
        path.AddLineToPoint(points[i].x, points[i].y);
    path.CloseSubpath();
    gc->SetBrush(wxBrush(fill));
    gc->FillPath(path);
    const double scale = std::max(1.0, ViewportPixelsPerWindowPixel());
    gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(edge).Width(1.5 * scale)));
    gc->StrokePath(path);

    std::vector<std::vector<wxPoint>> lines;
    AheadFanLabel label;
    AheadFanMarks(vp, &lines, &label);
    gc->SetPen(gc->CreatePen(
        wxGraphicsPenInfo(wxColour(edge.Red(), edge.Green(), edge.Blue(), edge.Alpha() * 2 / 3))
            .Width(scale)
            .Style(wxPENSTYLE_SHORT_DASH)));
    for (const auto &line : lines) {
        wxGraphicsPath across = gc->CreatePath();
        across.MoveToPoint(line[0].x, line[0].y);
        for (size_t i = 1; i < line.size(); ++i)
            across.AddLineToPoint(line[i].x, line[i].y);
        gc->StrokePath(across);
    }
    if (!label.text.IsEmpty()) {
        gc->SetFont(AheadFanFont(), wxColour(edge.Red(), edge.Green(), edge.Blue(), 220));
        double width = 0.0;
        double height = 0.0;
        gc->GetTextExtent(label.text, &width, &height);
        const double margin = 3.0 * scale;
        const wxPoint at = AheadLabelCorner(
            label.anchor, label.dx, label.dy,
            wxSize(static_cast<int>(width + 2 * margin), static_cast<int>(height + 2 * margin)),
            4.0 * scale);
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(AheadLabelGround()));
        gc->DrawRoundedRectangle(at.x, at.y, width + 2 * margin, height + 2 * margin, margin);
        gc->DrawText(label.text, at.x + margin, at.y + margin);
    }
    return true;
}

bool legend_pi::DrawAheadFanGL(PlugIn_ViewPort *vp) const
{
    const std::vector<wxPoint> points = AheadFanPixels(vp);
    if (points.empty())
        return false;

    wxColour fill;
    wxColour edge;
    AheadFanColours(m_ahead_colour, &fill, &edge);

    // GL_CURRENT_BIT included, see the marker in RenderGLOverlayMultiCanvas.
    glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_HINT_BIT | GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    // The outline bends outward where the strip near the boat starts to
    // widen, so triangles from one of its corners would overlap there (a
    // darker edge). From the boat, the middle of the circle, every point
    // is in sight: triangles from there cover the fan once.
    wxPoint boat;
    GetCanvasPixLL(vp, &boat, m_ship_lat, m_ship_lon);
    glColor4ub(fill.Red(), fill.Green(), fill.Blue(), fill.Alpha());
    glBegin(GL_TRIANGLE_FAN);
    glVertex2d(boat.x, boat.y);
    for (const wxPoint &point : points)
        glVertex2d(point.x, point.y);
    glVertex2d(points.front().x, points.front().y);
    glEnd();

    glColor4ub(edge.Red(), edge.Green(), edge.Blue(), edge.Alpha());
    const double scale = std::max(1.0, ViewportPixelsPerWindowPixel());
    glLineWidth(static_cast<float>(1.5 * scale));
    glBegin(GL_LINE_LOOP);
    for (const wxPoint &point : points)
        glVertex2d(point.x, point.y);
    glEnd();

    std::vector<std::vector<wxPoint>> lines;
    AheadFanLabel label;
    AheadFanMarks(vp, &lines, &label);
    if (!lines.empty()) {
        glColor4ub(edge.Red(), edge.Green(), edge.Blue(), edge.Alpha() * 2 / 3);
        glLineWidth(static_cast<float>(scale));
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(static_cast<GLint>(std::lround(scale)), 0x0F0F);
        for (const auto &line : lines) {
            glBegin(GL_LINE_STRIP);
            for (const wxPoint &point : line)
                glVertex2d(point.x, point.y);
            glEnd();
        }
        glDisable(GL_LINE_STIPPLE);
    }
    if (!label.text.IsEmpty())
        DrawTextGL(label.text, AheadFanFont(),
                   wxColour(edge.Red(), edge.Green(), edge.Blue(), 220), AheadLabelGround(),
                   label.anchor, label.dx, label.dy, 4.0 * scale,
                   static_cast<int>(std::lround(3.0 * scale)));

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopClientAttrib();
    glPopAttrib();
    return true;
}
