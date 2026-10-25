// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, notes: writing and managing notes, and notes tied to a place
// on the chart (signs, radius, notices, moving them).

#ifdef __WXOSX__
#define GL_SILENCE_DEPRECATION
#endif

#include "legend_pi.h"

#include "legend_shared.h"

#include <wx/glcanvas.h>
#include <wx/dc.h>
#include <wx/graphics.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

bool legend_pi::IsMarkdownItem(const wxString &path) const
{
    if (path.IsEmpty() || path == kDynamicGpxId)
        return false;
    const wxString ext = wxFileName(path).GetExt().Lower();
    return ext == "md" || ext == "markdown";
}

// Outside editing: the pencil for a note and the plus. While editing: the
// buttons that format the text, and Opslaan.
void legend_pi::UpdateNoteButtons()
{
    if (!m_edit_button || !m_new_button || !m_format_bar || !m_nav_panel || !m_edit_footer)
        return;

    m_edit_button->Show(!m_editing && IsMarkdownItem(m_current_legend_path));
    const bool note = !m_editing && IsMarkdownItem(m_current_legend_path);
    if (m_rename_button)
        m_rename_button->Show(note);
    if (m_delete_button)
        m_delete_button->Show(note);
    m_new_button->Show(!m_editing);
    if (m_save_button)
        m_save_button->Show(m_editing);
    m_format_bar->Show(m_editing);
    m_nav_panel->Show(!m_editing);
    m_edit_footer->Show(m_editing);
    if (m_place_bar)
        m_place_bar->Show(m_editing && m_note_has_place);
    if (m_legend_choice)
        m_legend_choice->Enable(!m_editing);
    m_edit_button->GetParent()->Layout();
}

void legend_pi::StartEditing()
{
    if (m_editing || !m_editor || !m_content_book || !IsMarkdownItem(m_current_legend_path))
        return;

    wxString text;
    wxFile file(m_current_legend_path);
    if (!file.IsOpened() || !file.ReadAll(&text, wxConvUTF8)) {
        wxMessageBox("Deze notitie kon niet worden geopend.", "Notitie bewerken",
                     wxOK | wxICON_WARNING, m_legend_window);
        return;
    }

    SaveCurrentMarkdownScroll();
    m_editing = true;
    m_editing_path = m_current_legend_path;
    m_note_dirty = false;

    // The block with the note's place is kept apart and shown as a bar.
    const NoteFrontMatter front = SplitFrontMatter(text);
    m_note_block = front.block;
    m_note_has_place = front.hasPlace;
    m_note_lat = front.lat;
    m_note_lon = front.lon;
    m_note_radius = front.radius;
    UpdatePlaceBar();
    m_editor->LoadMarkdown(front.body, wxFileName(m_editing_path).GetPath());
    if (m_save_status)
        m_save_status->SetLabel("Wijzigingen worden vanzelf bewaard");
    m_content_book->SetSelection(2);
    UpdateNoteButtons();
    m_editor->SetFocus();
}

void legend_pi::StopEditing()
{
    if (!m_editing)
        return;

    SaveNote();
    m_editing = false;
    m_editing_path.Clear();

    // The title may have changed: build the choice again and show the note
    // as it is now.
    RefreshLegendFiles();
    UpdateNoteButtons();
}

// Writes the note as Markdown, through a temporary file so a failure never
// leaves half a note behind.
void legend_pi::SaveNote()
{
    m_save_timer.Stop();
    if (!m_editor || m_editing_path.IsEmpty() || !m_note_dirty)
        return;

    const wxString text =
        BlockWithPlace(m_note_block, m_note_has_place, m_note_lat, m_note_lon, m_note_radius) +
        m_editor->ToMarkdown();
    wxTempFile file(m_editing_path);
    const bool saved = file.IsOpened() && file.Write(text, wxConvUTF8) && file.Commit();
    if (saved) {
        m_note_dirty = false;
        m_note_block = SplitFrontMatter(text).block;
        RefreshNotePlaces();
        if (m_save_status)
            m_save_status->SetLabel("Bewaard om " + wxDateTime::Now().Format("%H:%M"));
    }
    else {
        wxLogMessage("Legend: could not save %s", m_editing_path);
        if (m_save_status)
            m_save_status->SetLabel("Kon niet bewaren");
    }
}

// A new note in the Legend folder, named after its title, opened for
// writing.
bool legend_pi::NewNote()
{
    if (m_editing)
        StopEditing();

    wxTextEntryDialog dialog(m_legend_window, "Titel van de nieuwe notitie:", "Nieuwe notitie");
    if (dialog.ShowModal() != wxID_OK)
        return false;

    wxString title = dialog.GetValue();
    title.Trim(true).Trim(false);
    if (title.IsEmpty())
        return false;

    const wxString name = NoteFileName(title);

    EnsureDirectories();
    wxFileName file(GetLegendDirectory(), name, "md");
    for (int n = 2; file.FileExists(); ++n)
        file.SetName(wxString::Format("%s %d", name, n));

    wxFile out;
    if (!out.Create(file.GetFullPath()) || !out.Write("# " + title + "\n\n", wxConvUTF8)) {
        wxMessageBox("De notitie kon niet worden aangemaakt in\n" + GetLegendDirectory(),
                     "Nieuwe notitie", wxOK | wxICON_WARNING, m_legend_window);
        return false;
    }
    out.Close();

    ShowNoteNamed(file.GetFullName());
    StartEditing();
    if (m_editor) {
        // A line to write on under the title: text, not another heading.
        m_editor->SetInsertionPointEnd();
        m_editor->Newline();
        m_editor->AfterNewParagraph();
    }
    return m_editing;
}

// "Nieuwe notitie hier" from the chart menu: a new note, pinned at once to
// the clicked spot, in the legend window, also when that was closed.
void legend_pi::NewNoteAt(double lat, double lon)
{
    CreateLegendWindow();
    if (!m_legend_window->IsShown()) {
        RefreshLegendFiles();
        m_legend_window->Show();
    }
    m_legend_window->Raise();
    if (NewNote())
        SetNotePlace(lat, lon);
}

// The selected text becomes a link; without a selection the address itself
// is put in.
void legend_pi::InsertLink()
{
    if (!m_editor)
        return;

    wxTextEntryDialog dialog(m_legend_window, "Adres van de link:", "Link", "https://");
    if (dialog.ShowModal() != wxID_OK)
        return;
    wxString address = dialog.GetValue();
    address.Trim(true).Trim(false);
    if (address.IsEmpty() || address == "https://")
        return;
    m_editor->AddLink(address);
}

// A picture from outside the Legend folder is copied into its images folder
// first, so the note keeps working when the original moves.
void legend_pi::InsertImage()
{
    if (!m_editor)
        return;

    const wxString legendDir = GetLegendDirectory();
    wxFileDialog dialog(m_legend_window, "Afbeelding kiezen", legendDir, wxEmptyString,
                        "Afbeeldingen (*.png;*.jpg;*.jpeg;*.gif)|*.png;*.jpg;*.jpeg;*.gif",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    wxFileName image(dialog.GetPath());
    wxFileName relative(image);
    if (!relative.MakeRelativeTo(legendDir) || relative.GetFullPath().StartsWith("..")) {
        wxFileName copy(legendDir + wxFileName::GetPathSeparator() + "images",
                        image.GetFullName());
        wxFileName::Mkdir(copy.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        for (int n = 2; copy.FileExists(); ++n)
            copy.SetName(wxString::Format("%s %d", image.GetName(), n));
        if (!wxCopyFile(image.GetFullPath(), copy.GetFullPath())) {
            wxMessageBox("De afbeelding kon niet naar de Legend-map worden gekopieerd.",
                         "Afbeelding", wxOK | wxICON_WARNING, m_legend_window);
            return;
        }
        relative = copy;
        relative.MakeRelativeTo(legendDir);
    }

    wxString path = relative.GetFullPath(wxPATH_UNIX);
    if (path.Contains(" "))
        path = "<" + path + ">";
    if (!m_editor->InsertNoteImage(path, image.GetName()))
        wxMessageBox("Deze afbeelding kon niet worden geopend.", "Afbeelding",
                     wxOK | wxICON_WARNING, m_legend_window);
}

// The file name of a note: its title without the characters file systems
// refuse.
wxString legend_pi::NoteFileName(const wxString &title) const
{
    wxString name = title;
    for (const char *bad = "/\\:*?\"<>|"; *bad; ++bad)
        name.Replace(wxString(*bad), "-");
    name.Trim(true).Trim(false);
    while (name.StartsWith("."))
        name.Remove(0, 1);
    return name.IsEmpty() ? wxString("Notitie") : name;
}

// Shows the note with this file name, after the folder changed.
void legend_pi::ShowNoteNamed(const wxString &fullName)
{
    ScanLegendFiles();
    RebuildLegendChoice();
    size_t index = 0;
    for (size_t i = 0; i < m_legend_files.GetCount(); ++i) {
        if (wxFileName(m_legend_files[i]).GetFullName() == fullName)
            index = i;
    }
    ShowLegend(index);
}

// A new title: the file gets the matching name, and a "# Title" on the
// first line changes along.
void legend_pi::RenameNote()
{
    if (m_editing || !IsMarkdownItem(m_current_legend_path))
        return;

    const wxString path = m_current_legend_path;
    const wxString oldTitle = MakeDisplayTitle(path);
    wxTextEntryDialog dialog(m_legend_window, "Nieuwe titel van de notitie:", "Notitie hernoemen",
                             oldTitle);
    if (dialog.ShowModal() != wxID_OK)
        return;
    wxString title = dialog.GetValue();
    title.Trim(true).Trim(false);
    if (title.IsEmpty() || title == oldTitle)
        return;

    wxFileName target(path);
    target.SetName(NoteFileName(title));
    if (target.GetFullPath() != path && target.FileExists()) {
        wxMessageBox("Er is al een notitie met de naam \"" + target.GetName() + "\".",
                     "Notitie hernoemen", wxOK | wxICON_INFORMATION, m_legend_window);
        return;
    }

    // The heading on the first line.
    wxString text;
    {
        wxFile file(path);
        if (file.IsOpened())
            file.ReadAll(&text, wxConvUTF8);
    }
    // The title is in the body, after the block with the note's place.
    const NoteFrontMatter front = SplitFrontMatter(text);
    wxString body = front.body;
    size_t start = 0;
    while (start < body.length() && (body[start] == '\n' || body[start] == '\r' ||
                                     body[start] == ' ' || body[start] == '\t'))
        ++start;
    if (body.Mid(start, 2) == "# ") {
        size_t end = body.find('\n', start);
        if (end == wxString::npos)
            end = body.length();
        text = front.block + body.Left(start) + "# " + title + body.Mid(end);
        wxTempFile out(path);
        if (!out.IsOpened() || !out.Write(text, wxConvUTF8) || !out.Commit()) {
            wxMessageBox("De notitie kon niet worden aangepast.", "Notitie hernoemen",
                         wxOK | wxICON_WARNING, m_legend_window);
            return;
        }
    }

    if (target.GetFullPath() != path && !wxRenameFile(path, target.GetFullPath(), false)) {
        wxMessageBox("Het bestand kon niet worden hernoemd.", "Notitie hernoemen",
                     wxOK | wxICON_WARNING, m_legend_window);
        return;
    }

    // A note the boat is near stays near under its new name.
    if (m_notes_near.erase(path) > 0)
        m_notes_near.insert(target.GetFullPath());

    auto scroll = m_markdown_scroll_positions.find(path);
    if (scroll != m_markdown_scroll_positions.end()) {
        m_markdown_scroll_positions[target.GetFullPath()] = scroll->second;
        m_markdown_scroll_positions.erase(path);
    }
    m_current_legend_path = target.GetFullPath();
    ShowNoteNamed(target.GetFullName());
}

// Moves the note into the folder "Verwijderd" in the Legend folder, where
// it can be fetched back; images it uses stay where they are.
void legend_pi::DeleteNote()
{
    if (m_editing || !IsMarkdownItem(m_current_legend_path))
        return;
    DeleteNoteFile(m_current_legend_path);
}

// Moves a note into the folder "Verwijderd" in the Legend folder, after
// asking; from the legend window or from the chart's menu.
void legend_pi::DeleteNoteFile(const wxString &path)
{
    const wxString title = MakeDisplayTitle(path);
    wxWindow *parent = m_legend_window && m_legend_window->IsShown()
                           ? static_cast<wxWindow *>(m_legend_window)
                           : GetOCPNCanvasWindow();
    wxMessageDialog confirm(parent,
                            "Notitie \"" + title + "\" verwijderen?\n\nHet bestand gaat naar de map "
                            "\"Verwijderd\" in de Legend-map; daar kun je het terughalen.",
                            "Notitie verwijderen", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    confirm.SetYesNoLabels("Verwijderen", "Annuleren");
    if (confirm.ShowModal() != wxID_YES)
        return;

    wxFileName target(GetLegendDirectory() + wxFileName::GetPathSeparator() + "Verwijderd",
                      wxFileName(path).GetFullName());
    wxFileName::Mkdir(target.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    for (int n = 2; target.FileExists(); ++n)
        target.SetName(wxString::Format("%s %d", wxFileName(path).GetName(), n));
    // A note being edited is saved and closed first.
    if (m_editing && m_editing_path == path)
        StopEditing();

    if (!wxRenameFile(path, target.GetFullPath(), false)) {
        wxMessageBox("De notitie kon niet worden verplaatst.", "Notitie verwijderen",
                     wxOK | wxICON_WARNING, parent);
        return;
    }

    m_markdown_scroll_positions.erase(path);
    if (m_current_legend_path == path) {
        m_current_legend_path.Clear();
        const size_t index = m_legend_index > 0 ? m_legend_index - 1 : 0;
        ScanLegendFiles();
        RebuildLegendChoice();
        ShowLegend(std::min(index, m_legend_files.IsEmpty() ? 0 : m_legend_files.GetCount() - 1));
    }
    else {
        // Another item is shown: only the list of items changes.
        const wxString shown = m_current_legend_path;
        ScanLegendFiles();
        RebuildLegendChoice();
        size_t index = 0;
        for (size_t i = 0; i < m_legend_files.GetCount(); ++i)
            if (m_legend_files[i] == shown)
                index = i;
        if (m_legend_window)
            ShowLegend(index);
    }
}

// "52°34,818′N 5°45,072′E": degrees and minutes, as on a chart.
wxString legend_pi::FormatPosition(double lat, double lon) const
{
    auto part = [this](double value, char positive, char negative) {
        const char hemisphere = value >= 0 ? positive : negative;
        value = std::fabs(value);
        int degrees = static_cast<int>(value);
        double minutes = (value - degrees) * 60.0;
        if (minutes >= 59.9995) {
            minutes = 0.0;
            ++degrees;
        }
        wxString text = wxString::Format("%d", degrees) + wxString::FromUTF8("\u00b0") +
                        wxString::Format("%06.3f", minutes) + wxString::FromUTF8("\u2032") +
                        wxString(hemisphere);
        if (m_dutch_names)
            text.Replace(".", ",");
        return text;
    };
    return part(lat, 'N', 'S') + " " + part(lon, 'E', 'W');
}

// "geo:52.58030,5.75120" (RFC 5870), with optional altitude and
// parameters, which are left alone.
bool legend_pi::ParseGeoUri(const wxString &uri, double *lat, double *lon) const
{
    const wxString body = uri.Mid(4).BeforeFirst(';');
    const wxArrayString parts = wxSplit(body, ',', '\0');
    if (parts.GetCount() < 2)
        return false;
    wxString a = parts[0];
    wxString b = parts[1];
    a.Trim(true).Trim(false);
    b.Trim(true).Trim(false);
    return a.ToCDouble(lat) && b.ToCDouble(lon) && std::fabs(*lat) <= 90.0 &&
           std::fabs(*lon) <= 180.0;
}

// The chart goes to a place of a note, unless it is well in view already,
// and marks it.
void legend_pi::GoToPlace(double lat, double lon)
{
    bool inView = false;
    if (m_viewport_valid) {
        wxPoint pixel;
        GetCanvasPixLL(&m_viewport, &pixel, lat, lon);
        const int marginX = m_viewport.pix_width / 8;
        const int marginY = m_viewport.pix_height / 8;
        inView = pixel.x >= marginX && pixel.x <= m_viewport.pix_width - marginX &&
                 pixel.y >= marginY && pixel.y <= m_viewport.pix_height - marginY;
    }
    if (!inView) {
        const double scale = m_viewport_valid && m_viewport.view_scale_ppm > 0.0
                                 ? m_viewport.view_scale_ppm
                                 : 0.1;
        JumpTo(lat, lon, scale);
    }
    ShowMarker(lat, lon);
}

// A link to a place in the note being edited: the selected text becomes the
// link, or the position itself is put in. The place is marked on the chart.
void legend_pi::InsertPlace(double lat, double lon, const wxString &text)
{
    if (!m_editing || !m_editor)
        return;
    const wxString uri = wxString::Format("geo:%s,%s", wxString::FromCDouble(lat, 5),
                                          wxString::FromCDouble(lon, 5));
    m_editor->AddLinkWithText(uri, text.IsEmpty() ? FormatPosition(lat, lon) : text);
    ShowMarker(lat, lon);
    if (m_legend_window)
        m_legend_window->Raise();
    m_editor->SetFocus();
}

// A position fix of the last minute: where the boat is now.
bool legend_pi::BoatPositionKnown() const
{
    return m_ship_time > 0 && (wxGetUTCTimeMillis() - m_ship_time) < 60000 &&
           !std::isnan(m_ship_lat) && !std::isnan(m_ship_lon);
}

// The boat's position in the note being edited, with the time: "52°34,818′N
// 5°45,072′E (14:32)", as a link to the place, for a log for instance.
void legend_pi::InsertBoatPosition()
{
    if (!BoatPositionKnown()) {
        ShowNoGpsNotice();
        return;
    }
    InsertPlace(m_ship_lat, m_ship_lon,
                FormatPosition(m_ship_lat, m_ship_lon) + " (" +
                    wxDateTime::Now().Format("%H:%M") + ")");
}

void legend_pi::PinNoteAtBoat()
{
    if (!BoatPositionKnown()) {
        ShowNoGpsNotice();
        return;
    }
    SetNotePlace(m_ship_lat, m_ship_lon);
}

void legend_pi::NewNoteAtBoat()
{
    if (!BoatPositionKnown()) {
        ShowNoGpsNotice();
        return;
    }
    NewNoteAt(m_ship_lat, m_ship_lon);
}

void legend_pi::ShowNoGpsNotice()
{
    ShowNotice("Geen positie van de boot",
               "Er is de laatste minuut geen GPS-positie binnengekomen.", false,
               wxColour(186, 117, 23));
}

// The notes of the Legend folder that belong to a place: read again when
// the folder changes and after a note is saved.
void legend_pi::RefreshNotePlaces()
{
    std::vector<wxString> paths;
    wxDir dir(GetLegendDirectory());
    if (dir.IsOpened()) {
        wxString name;
        bool found = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES);
        while (found) {
            const wxString ext = wxFileName(name).GetExt().Lower();
            if (ext == "md" || ext == "markdown")
                paths.push_back(GetLegendDirectory() + wxFileName::GetPathSeparator() + name);
            found = dir.GetNext(&name);
        }
    }
    // The clicked sign keeps its radius: opening its note reads the folder
    // again right after the click.
    wxString selected;
    if (m_note_selected >= 0 && static_cast<size_t>(m_note_selected) < m_note_places.size())
        selected = m_note_places[m_note_selected].path;
    m_note_places = ReadNotePlaces(paths);
    m_note_selected = -1;
    for (size_t i = 0; i < m_note_places.size(); ++i) {
        if (!selected.IsEmpty() && m_note_places[i].path == selected)
            m_note_selected = static_cast<int>(i);
    }
    m_note_hover = -1;
    RefreshAllCanvases();
}

// Sea miles between two positions, flat: the distances here are short.
double legend_pi::SeaMiles(double lat1, double lon1, double lat2, double lon2) const
{
    const double kPi = 3.14159265358979323846;
    const double dy = (lat2 - lat1) * 60.0;
    double dlon = lon2 - lon1;
    while (dlon > 180.0)
        dlon -= 360.0;
    while (dlon < -180.0)
        dlon += 360.0;
    const double dx = dlon * 60.0 * std::cos((lat1 + lat2) / 2.0 * kPi / 180.0);
    return std::sqrt(dx * dx + dy * dy);
}

// The scale of the status bar, as the chart uses it for scale limits; worked
// out again only when the zoom changes.
double legend_pi::CurrentDisplayedScale()
{
    if (!m_viewport_valid)
        return 0.0;
    if (m_viewport.view_scale_ppm != m_scale_cache_ppm ||
        m_viewport.clat != m_scale_cache_lat) {
        m_scale_cache_ppm = m_viewport.view_scale_ppm;
        m_scale_cache_lat = m_viewport.clat;
        m_scale_cache = DisplayedScale(m_viewport);
        if (m_scale_cache <= 0.0)
            m_scale_cache = m_viewport.chart_scale;
    }
    return m_scale_cache;
}

// Beyond its scale limit a note's sign is not drawn, like the buoys of a
// layer.
bool legend_pi::NoteHiddenAtScale(const NotePlace &place)
{
    const double scale = CurrentDisplayedScale();
    return scale > 0.0 && scale > place.scamin;
}

// Opens a note in the legend window, from the chart or a notice.
void legend_pi::OpenNote(const wxString &path)
{
    CreateLegendWindow();
    if (m_editing)
        StopEditing();
    if (!m_legend_window->IsShown()) {
        RefreshLegendFiles();
        m_legend_window->Show();
    }
    ShowNoteNamed(wxFileName(path).GetFullName());
    m_legend_window->Raise();
}

// The sign of a note on the chart under a click, or -1.
int legend_pi::NotePlaceAt(const wxPoint &pixel, double tolerancePx)
{
    if (!m_viewport_valid)
        return -1;
    PlugIn_ViewPort vp = m_viewport;
    int best = -1;
    double bestDistance = tolerancePx * tolerancePx;
    for (size_t i = 0; i < m_note_places.size(); ++i) {
        if (NoteHiddenAtScale(m_note_places[i]))
            continue;
        wxPoint at;
        GetCanvasPixLL(&vp, &at, m_note_places[i].lat, m_note_places[i].lon);
        const double dx = at.x - pixel.x;
        const double dy = at.y - pixel.y;
        if (dx * dx + dy * dy <= bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = static_cast<int>(i);
        }
    }
    return best;
}

namespace {

// Paper yellow with a brown edge, like the notes of the sketch; softer in
// dusk and night.
void NoteSignColours(wxColour *fill, wxColour *edge)
{
    // As bright as the icons: 55 % at dusk, 30 % at night.
    const double dim = SchemeDim();
    *fill = wxColour(static_cast<unsigned char>(250 * dim), static_cast<unsigned char>(199 * dim),
                     static_cast<unsigned char>(117 * dim));
    *edge = wxColour(static_cast<unsigned char>(133 * dim), static_cast<unsigned char>(79 * dim),
                     static_cast<unsigned char>(11 * dim));
}

} // namespace

// A small sheet of paper with a folded corner at the place of each note in
// view, and the radius of a clicked one as a dashed circle.
bool legend_pi::DrawNoteSigns(wxDC &dc, PlugIn_ViewPort *vp)
{
    if (m_note_places.empty())
        return false;
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return false;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
    wxColour fill;
    wxColour edge;
    NoteSignColours(&fill, &edge);
    const double s = std::max(1.0, ViewportPixelsPerWindowPixel());
    bool drew = false;

    for (size_t i = 0; i < m_note_places.size(); ++i) {
        const NotePlace &place = m_note_places[i];
        if (NoteHiddenAtScale(place))
            continue;
        wxPoint at;
        GetCanvasPixLL(vp, &at, place.lat, place.lon);
        if (at.x < -20 || at.y < -20 || at.x > vp->pix_width + 20 || at.y > vp->pix_height + 20)
            continue;
        const double x = at.x;
        const double y = at.y;

        if (static_cast<int>(i) == m_note_selected) {
            wxPoint north;
            GetCanvasPixLL(vp, &north, place.lat + place.radius / 60.0, place.lon);
            const double r = std::fabs(north.y - at.y);
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->SetPen(gc->CreatePen(
                wxGraphicsPenInfo(edge).Width(1.5 * s).Style(wxPENSTYLE_SHORT_DASH)));
            gc->DrawEllipse(x - r, y - r, 2 * r, 2 * r);
        }

        const double w = 9.0 * s;
        const double h = 11.0 * s;
        const double fold = 4.0 * s;
        wxGraphicsPath sheet = gc->CreatePath();
        sheet.MoveToPoint(x - w, y - h);
        sheet.AddLineToPoint(x + w - fold, y - h);
        sheet.AddLineToPoint(x + w, y - h + fold);
        sheet.AddLineToPoint(x + w, y + h);
        sheet.AddLineToPoint(x - w, y + h);
        sheet.CloseSubpath();
        gc->SetBrush(wxBrush(fill));
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(edge).Width(1.3 * s).Join(wxJOIN_ROUND)));
        gc->DrawPath(sheet);
        wxGraphicsPath marks = gc->CreatePath();
        marks.MoveToPoint(x + w - fold, y - h);
        marks.AddLineToPoint(x + w - fold, y - h + fold);
        marks.AddLineToPoint(x + w, y - h + fold);
        for (int line = 0; line < 3; ++line) {
            const double ly = y - h * 0.25 + line * h * 0.35;
            marks.MoveToPoint(x - w * 0.6, ly);
            marks.AddLineToPoint(x + w * 0.6, ly);
        }
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(edge).Width(1.0 * s)));
        gc->StrokePath(marks);
        drew = true;
    }
    return drew;
}

bool legend_pi::DrawNoteSignsGL(PlugIn_ViewPort *vp)
{
    if (m_note_places.empty())
        return false;
    wxColour fill;
    wxColour edge;
    NoteSignColours(&fill, &edge);
    const double s = std::max(1.0, ViewportPixelsPerWindowPixel());
    const double kPi = 3.14159265358979323846;
    bool drew = false;

    glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_HINT_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    for (size_t i = 0; i < m_note_places.size(); ++i) {
        const NotePlace &place = m_note_places[i];
        if (NoteHiddenAtScale(place))
            continue;
        wxPoint at;
        GetCanvasPixLL(vp, &at, place.lat, place.lon);
        if (at.x < -20 || at.y < -20 || at.x > vp->pix_width + 20 || at.y > vp->pix_height + 20)
            continue;
        const double x = at.x;
        const double y = at.y;

        if (static_cast<int>(i) == m_note_selected) {
            wxPoint north;
            GetCanvasPixLL(vp, &north, place.lat + place.radius / 60.0, place.lon);
            const double r = std::fabs(north.y - at.y);
            glColor4ub(edge.Red(), edge.Green(), edge.Blue(), 230);
            glLineWidth(static_cast<float>(1.5 * s));
            glBegin(GL_LINES);
            const int segments = 72;
            for (int k = 0; k < segments; k += 2) {
                const double a1 = 2.0 * kPi * k / segments;
                const double a2 = 2.0 * kPi * (k + 1) / segments;
                glVertex2d(x + r * std::cos(a1), y + r * std::sin(a1));
                glVertex2d(x + r * std::cos(a2), y + r * std::sin(a2));
            }
            glEnd();
        }

        const double w = 9.0 * s;
        const double h = 11.0 * s;
        const double fold = 4.0 * s;
        const double sheet[5][2] = {{x - w, y - h}, {x + w - fold, y - h}, {x + w, y - h + fold},
                                    {x + w, y + h}, {x - w, y + h}};
        glColor4ub(fill.Red(), fill.Green(), fill.Blue(), 245);
        glBegin(GL_POLYGON);
        for (const auto &p : sheet)
            glVertex2d(p[0], p[1]);
        glEnd();
        glColor4ub(edge.Red(), edge.Green(), edge.Blue(), 255);
        glLineWidth(static_cast<float>(1.3 * s));
        glBegin(GL_LINE_LOOP);
        for (const auto &p : sheet)
            glVertex2d(p[0], p[1]);
        glEnd();
        glLineWidth(static_cast<float>(1.0 * s));
        glBegin(GL_LINES);
        glVertex2d(x + w - fold, y - h);
        glVertex2d(x + w - fold, y - h + fold);
        glVertex2d(x + w - fold, y - h + fold);
        glVertex2d(x + w, y - h + fold);
        for (int line = 0; line < 3; ++line) {
            const double ly = y - h * 0.25 + line * h * 0.35;
            glVertex2d(x - w * 0.6, ly);
            glVertex2d(x + w * 0.6, ly);
        }
        glEnd();
        drew = true;
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();
    return drew;
}

// With every fix: a notice when the boat comes within the radius of a
// note it was not near before. A notice about a VHF area goes first; a
// note waits until that one is gone.
void legend_pi::CheckNotePlaces()
{
    std::set<wxString> near;
    std::vector<const NotePlace *> entered;
    for (const NotePlace &place : m_note_places) {
        const double distance = SeaMiles(m_ship_lat, m_ship_lon, place.lat, place.lon);
        if (distance > place.radius)
            continue;
        near.insert(place.path);
        // The note being written is no news.
        if (m_editing && place.path == m_editing_path)
            continue;
        if (m_note_places_known && m_notes_near.count(place.path) == 0)
            entered.push_back(&place);
    }
    m_notes_near = near;
    m_note_places_known = true;

    if (entered.empty() || !m_note_notice_on)
        return;

    const NotePlace &place = *entered.front();
    wxString distance = wxString::Format(
        "%.1f", SeaMiles(m_ship_lat, m_ship_lon, place.lat, place.lon));
    distance.Replace(".", ",");
    m_pending_note_title = "Notitie in de buurt";
    m_pending_note_text = place.title + wxString::FromUTF8(" \u00b7 ") + distance +
                          " NM\nKlik om de notitie te openen";
    m_pending_note_path = place.path;
    if (m_notice.IsShown())
        m_note_notice_timer.StartOnce(12500);
    else
        ShowPendingNoteNotice();
}

void legend_pi::ShowPendingNoteNotice()
{
    if (m_pending_note_path.IsEmpty())
        return;
    const wxString path = m_pending_note_path;
    m_pending_note_path.Clear();
    ShowNotice(m_pending_note_title, m_pending_note_text, true, wxColour(186, 117, 23),
               [this, path]() { OpenNote(path); });
}

// The note being edited belongs to this place; the radius stays, or is
// half a sea mile for a note that had no place yet.
void legend_pi::SetNotePlace(double lat, double lon)
{
    if (!m_editing)
        return;
    if (!m_note_has_place)
        m_note_radius = 0.5;
    m_note_has_place = true;
    m_note_lat = lat;
    m_note_lon = lon;
    NotePlaceChanged();
    ShowMarker(lat, lon);
    if (m_legend_window)
        m_legend_window->Raise();
}

// A sign dragged to another place: the note is pinned there, its radius
// and the rest of its settings stay.
void legend_pi::MoveNotePlace(const wxString &path, double lat, double lon)
{
    if (m_editing && m_editing_path == path) {
        m_note_lat = lat;
        m_note_lon = lon;
        NotePlaceChanged();
        return;
    }

    wxString text;
    {
        wxFile file(path);
        if (!file.IsOpened() || !file.ReadAll(&text, wxConvUTF8)) {
            RefreshNotePlaces();
            return;
        }
    }
    const NoteFrontMatter front = SplitFrontMatter(text);
    const wxString moved =
        BlockWithPlace(front.block, true, lat, lon, front.radius) + front.body;
    wxTempFile out(path);
    if (!out.IsOpened() || !out.Write(moved, wxConvUTF8) || !out.Commit())
        wxMessageBox("De nieuwe plek kon niet in de notitie worden bewaard.", "Notitie verplaatsen",
                     wxOK | wxICON_WARNING, GetOCPNCanvasWindow());
    RefreshNotePlaces();

    // The note in the window shows its place on top.
    if (m_legend_window && m_legend_window->IsShown() && m_current_legend_path == path)
        ShowLegend(m_legend_index);
}

// The last dragged sign back where it was.
void legend_pi::UndoNoteMove()
{
    if (!m_undo_move_valid)
        return;
    m_undo_move_valid = false;
    m_notice.Hide();
    MoveNotePlace(m_undo_move_path, m_undo_move_lat, m_undo_move_lon);
}

void legend_pi::AskNoteRadius()
{
    wxString value = wxString::Format("%.2f", m_note_radius);
    while (value.EndsWith("0"))
        value.RemoveLast();
    if (value.EndsWith("."))
        value.RemoveLast();
    value.Replace(".", ",");
    wxTextEntryDialog dialog(m_legend_window,
                             "Straal in zeemijl: binnen deze afstand krijg je onderweg een "
                             "melding van deze notitie.",
                             "Straal", value);
    if (dialog.ShowModal() != wxID_OK)
        return;
    wxString text = dialog.GetValue();
    text.Trim(true).Trim(false);
    text.Replace(",", ".");
    text = text.BeforeFirst(' ');
    double radius = 0.0;
    if (!text.ToCDouble(&radius) || radius <= 0.0 || radius >= 100.0) {
        wxMessageBox("Vul een straal in zeemijl in, bijvoorbeeld 0,5.", "Straal",
                     wxOK | wxICON_INFORMATION, m_legend_window);
        return;
    }
    m_note_radius = radius;
    NotePlaceChanged();
}

// After a change of the place of the note being edited: the bar, saving,
// and the sign on the chart.
void legend_pi::NotePlaceChanged()
{
    // Pinned where the boat already is: it counts as near, so no notice
    // follows now, only after sailing away and coming back.
    if (m_note_has_place && BoatPositionKnown() &&
        SeaMiles(m_ship_lat, m_ship_lon, m_note_lat, m_note_lon) <= m_note_radius)
        m_notes_near.insert(m_editing_path);
    UpdatePlaceBar();
    m_note_dirty = true;
    if (m_save_status)
        m_save_status->SetLabel(wxString::FromUTF8("Nog niet bewaard\u2026"));
    m_save_timer.StartOnce(500);
    UpdateNoteButtons();
}

void legend_pi::UpdatePlaceBar()
{
    if (!m_place_bar || !m_place_text)
        return;
    if (m_note_has_place) {
        wxString radius = wxString::Format("%.2f", m_note_radius);
        while (radius.EndsWith("0"))
            radius.RemoveLast();
        if (radius.EndsWith("."))
            radius.RemoveLast();
        radius.Replace(".", ",");
        m_place_text->SetLabel(wxString::FromUTF8("Vastgepind op ") +
                               FormatPosition(m_note_lat, m_note_lon) +
                               wxString::FromUTF8(" \u00b7 straal ") + radius + " NM");
        m_place_text->SetToolTip("Klik om de plek op de kaart te zien");
    }
    const LegendPalette palette = GetLegendPalette();
    const bool dim = IsDimScheme();
    m_place_bar->SetBackgroundColour(dim ? palette.stripe : wxColour(250, 238, 218));
    m_place_text->SetForegroundColour(dim ? palette.text : wxColour(99, 56, 6));
    m_place_bar->Show(m_editing && m_note_has_place);
    m_place_bar->GetParent()->Layout();
}
