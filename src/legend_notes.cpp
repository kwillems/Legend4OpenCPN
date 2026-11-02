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

#include "geo.h"

#include "legend_shared.h"

#include <wx/glcanvas.h>
#include <wx/dc.h>
#include <wx/graphics.h>
#include "wx_compat.h"
#include "pidc.h"
#include "pidc_shapes.h"
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/button.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>
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

// The buttons for notes in the header row: edit, rename, delete (into the
// folder for deleted notes) and a new one; while editing, Opslaan.
void legend_pi::AddNoteButtons(wxPanel *panel, wxSizer *headerRow)
{
    auto noteButton = [&](const wxString &icon) {
        auto *button = new wxBitmapButton(panel, wxID_ANY,
                                          NoteToolIcon(icon, GetLegendPalette().text),
                                          wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_format_icons.push_back(std::make_pair(button, icon));
#ifdef __ANDROID__
        // Big enough for a finger.
        const int side = 2 * button->GetCharHeight() + 8;
        button->SetMinSize(wxSize(side, side));
#endif
        headerRow->Add(button, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
        return button;
    };
    headerRow->AddSpacer(4);
#ifdef __ANDROID__
    // Dragging a sign on the chart does not work on Android: this button,
    // then a tap on the chart, moves a pinned note.
    m_move_button = noteButton("place");
    m_move_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { StartNoteMove(); });
#endif
    m_edit_button = noteButton("edit");
    m_rename_button = noteButton("rename");
    m_delete_button = noteButton("trash");
    m_new_button = noteButton("new");
    m_save_button = new wxButton(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxBU_EXACTFIT);
    m_save_button->Hide();
    headerRow->Add(m_save_button, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);

    m_edit_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { StartEditing(); });
    m_rename_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RenameNote(); });
    m_delete_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DeleteNote(); });
    m_save_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { StopEditing(); });
    // With a GPS fix the plus offers a note pinned where the boat is.
    m_new_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!BoatPositionKnown()) {
            NewNote();
            return;
        }
        enum { kPlain = 1, kBoat };
        wxMenu menu;
        menu.Append(kPlain, _("New note"));
        menu.Append(kBoat, _("New note at the boat"));
        const int choice = m_new_button->GetPopupMenuSelectionFromUser(menu);
        if (choice == kPlain)
            NewNote();
        else if (choice == kBoat)
            NewNoteAtBoat();
    });
}

// Buttons that put Markdown into the note, for those who do not know it by
// heart and on a tablet, where # and * are hard to reach. They wrap to a
// second line in a narrow window.
wxWindow *legend_pi::CreateFormatBar(wxPanel *panel)
{
    m_format_bar = new wxPanel(panel);
    auto *formatSizer = new wxWrapSizer(wxHORIZONTAL);
    auto formatButton = [&](const wxString &icon, std::function<void()> action) {
        auto *button = new wxBitmapButton(m_format_bar, wxID_ANY,
                                          NoteToolIcon(icon, GetLegendPalette().text),
                                          wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        if (action)
            button->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
        formatSizer->Add(button, 0, wxRIGHT | wxBOTTOM, 2);
        m_format_icons.push_back(std::make_pair(button, icon));
        return button;
    };
    auto groupGap = [&]() { formatSizer->AddSpacer(8); };

    // H opens a menu with the heading levels; the current one is ticked.
    wxBitmapButton *headingButton = formatButton("heading", std::function<void()>());
    headingButton->Bind(wxEVT_BUTTON, [this, headingButton](wxCommandEvent &) {
        if (!m_editor)
            return;
        const int current = m_editor->HeadingLevel();
        wxMenu menu;
        menu.AppendRadioItem(100, _("Plain text"))->Check(current == 0);
        for (int level = 1; level <= 6; ++level)
            menu.AppendRadioItem(100 + level, wxString::Format(_("Heading %d"), level))
                ->Check(current == level);
        const int choice = headingButton->GetPopupMenuSelectionFromUser(menu);
        if (choice >= 100 && choice <= 106)
            m_editor->SetHeading(choice - 100);
    });
    formatButton("bold", [this]() { m_editor->ToggleBold(); });
    formatButton("italic", [this]() { m_editor->ToggleItalic(); });
    groupGap();
    formatButton("bullets", [this]() { m_editor->ToggleList(); });
    formatButton("numbers", [this]() { m_editor->ToggleNumbered(); });
    formatButton("tasks", [this]() { m_editor->ToggleTasks(); });
    groupGap();
    formatButton("link", [this]() { InsertLink(); });
    formatButton("image", [this]() { InsertImage(); });
    wxBitmapButton *placeButton = formatButton("place", std::function<void()>());
    placeButton->Bind(wxEVT_BUTTON, [this, placeButton](wxCommandEvent &) {
        if (!m_viewport_valid)
            return;
        enum { kLink = 1, kBelongs, kBoatLink, kBoatPin };
        wxMenu menu;
        menu.Append(kLink, _("Link to the centre of the chart in the text"));
        menu.Append(kBelongs, _("Pin note to the centre of the chart"));
        menu.AppendSeparator();
        menu.Append(kBoatLink, _("Boat's position in the text"));
        menu.Append(kBoatPin, _("Pin note to the boat's position"));
        const bool boat = BoatPositionKnown();
        menu.Enable(kBoatLink, boat);
        menu.Enable(kBoatPin, boat);
        const int choice = placeButton->GetPopupMenuSelectionFromUser(menu);
        if (choice == kLink)
            InsertPlace(m_viewport.clat, m_viewport.clon);
        else if (choice == kBelongs)
            SetNotePlace(m_viewport.clat, m_viewport.clon);
        else if (choice == kBoatLink)
            InsertBoatPosition();
        else if (choice == kBoatPin)
            PinNoteAtBoat();
    });
    m_format_bar->SetSizer(formatSizer);
    m_format_bar->Hide();
    return m_format_bar;
}

// "Vastgepind op ...": where the note being edited is pinned, with its
// radius.
wxWindow *legend_pi::CreatePlaceBar(wxPanel *panel)
{
    m_place_bar = new wxPanel(panel);
    auto *placeSizer = new wxBoxSizer(wxHORIZONTAL);
    m_place_text = new wxStaticText(m_place_bar, wxID_ANY, wxEmptyString);
    m_radius_button = new wxButton(m_place_bar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize, wxBU_EXACTFIT);
    m_loose_button = new wxButton(m_place_bar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxBU_EXACTFIT);
    placeSizer->Add(m_place_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    placeSizer->Add(m_radius_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    placeSizer->Add(m_loose_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_place_bar->SetSizer(placeSizer);
    m_place_bar->Hide();
    m_place_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        if (m_note_has_place)
            GoToPlace(m_note_lat, m_note_lon);
    });
    m_radius_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { AskNoteRadius(); });
    m_loose_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_note_has_place = false;
        NotePlaceChanged();
    });
    return m_place_bar;
}

// A note while it is edited, as it looks; saved as Markdown a moment after
// typing stops.
void legend_pi::CreateNoteEditor()
{
    m_editor = new NoteEditor(m_content_book, wxID_ANY);
    m_editor->SetPlaceHandler([this](const wxString &uri) {
        double lat = 0.0;
        double lon = 0.0;
        if (ParseGeoUri(uri, &lat, &lon))
            GoToPlace(lat, lon);
    });
    m_editor->SetChangedHandler([this]() {
        if (!m_editing)
            return;
        m_note_dirty = true;
        if (m_save_status)
            m_save_status->SetLabel(_("Not saved yet\u2026"));
        m_save_timer.StartOnce(1500);
    });
}

// While a note is edited: whether it has been saved.
wxWindow *legend_pi::CreateEditFooter(wxPanel *panel)
{
    m_edit_footer = new wxPanel(panel);
    auto *footerSizer = new wxBoxSizer(wxHORIZONTAL);
    m_save_status = new wxStaticText(m_edit_footer, wxID_ANY, wxEmptyString);
    footerSizer->Add(m_save_status, 1, wxALIGN_CENTER_VERTICAL);
    m_edit_footer->SetSizer(footerSizer);
    m_edit_footer->Hide();
    return m_edit_footer;
}

// Outside editing: the pencil for a note and the plus. While editing: the
// buttons that format the text, and Opslaan.
void legend_pi::UpdateNoteButtons()
{
    if (!m_edit_button || !m_new_button || !m_format_bar || !m_nav_panel || !m_edit_footer)
        return;

    // With notes off a Markdown item is a legend to read, nothing more.
    const bool note = m_notes_on && !m_editing && IsMarkdownItem(m_current_legend_path);
    m_edit_button->Show(note);
    if (m_rename_button)
        m_rename_button->Show(note);
    if (m_delete_button)
        m_delete_button->Show(note);
#ifdef __ANDROID__
    bool pinned = false;
    for (const NotePlace &place : m_note_places)
        pinned = pinned || place.path == m_current_legend_path;
    m_move_button->Show(note && pinned);
#endif
    m_new_button->Show(m_notes_on && !m_editing);
    if (m_save_button)
        m_save_button->Show(m_editing);
    m_format_bar->Show(m_editing);
    // Vorige / Volgende only when there is more than the chart legend.
    m_nav_panel->Show(!m_editing && m_legend_files.GetCount() > 1);
    m_edit_footer->Show(m_editing);
    if (m_place_bar)
        m_place_bar->Show(m_editing && m_note_has_place);
    if (m_legend_choice)
        m_legend_choice->Enable(!m_editing);
    m_edit_button->GetParent()->Layout();
}

void legend_pi::StartEditing()
{
    if (!m_notes_on || m_editing || !m_editor || !m_content_book || !IsMarkdownItem(m_current_legend_path))
        return;

    wxString text;
    wxFile file(m_current_legend_path);
    if (!file.IsOpened() || !file.ReadAll(&text, wxConvUTF8)) {
        wxMessageBox(_("Could not open this note."),
                     _("Edit note"),
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
        m_save_status->SetLabel(_("Changes are saved automatically"));
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
            m_save_status->SetLabel(_("Saved at ") + wxDateTime::Now().Format("%H:%M"));
    }
    else {
        wxLogMessage("Legend: could not save %s", m_editing_path);
        if (m_save_status)
            m_save_status->SetLabel(_("Could not save"));
    }
}

// A new note in the Legend folder, named after its title, opened for
// writing.
bool legend_pi::NewNote()
{
    if (m_editing)
        StopEditing();

    wxTextEntryDialog dialog(m_legend_window, _("Title of the new note:"),
                             _("New note"));
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
        wxMessageBox(_("Could not create the note in\n") +
                         GetLegendDirectory(),
                     _("New note"), wxOK | wxICON_WARNING, m_legend_window);
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

    wxTextEntryDialog dialog(m_legend_window, _("Address of the link:"), "Link",
                             "https://");
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
    wxFileDialog dialog(m_legend_window, _("Choose an image"), legendDir,
                        wxEmptyString,
                        _("Images") + " (*.png;*.jpg;*.jpeg;*.gif)|*.png;*.jpg;*.jpeg;*.gif",
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
            wxMessageBox(_("Could not copy the image to the Legend folder."),
                         _("Image"), wxOK | wxICON_WARNING, m_legend_window);
            return;
        }
        relative = copy;
        relative.MakeRelativeTo(legendDir);
    }

    wxString path = relative.GetFullPath(wxPATH_UNIX);
    if (path.Contains(" "))
        path = "<" + path + ">";
    if (!m_editor->InsertNoteImage(path, image.GetName()))
        wxMessageBox(_("Could not open this image."),
                     _("Image"),
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
    return name.IsEmpty() ? _("Note") : name;
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
    wxTextEntryDialog dialog(m_legend_window, _("New title of the note:"),
                             _("Rename note"),
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
        wxMessageBox(_("There is already a note named \"") +
                         target.GetName() + "\".",
                     _("Rename note"), wxOK | wxICON_INFORMATION, m_legend_window);
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
            wxMessageBox(_("Could not change the note."),
                         _("Rename note"),
                         wxOK | wxICON_WARNING, m_legend_window);
            return;
        }
    }

    if (target.GetFullPath() != path && !wxRenameFile(path, target.GetFullPath(), false)) {
        wxMessageBox(_("Could not rename the file."),
                     _("Rename note"),
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

// Moves the note into the folder for deleted notes in the Legend folder,
// where it can be fetched back; images it uses stay where they are.
void legend_pi::DeleteNote()
{
    if (m_editing || !IsMarkdownItem(m_current_legend_path))
        return;
    DeleteNoteFile(m_current_legend_path);
}

// The folder for deleted notes in the Legend folder, named in OpenCPN's
// language: "Deleted", "Verwijderd". After a change of language the folder
// of the remembered name is renamed first, so that there is one.
wxString legend_pi::DeletedFolder()
{
    const wxString name = _("Deleted");
    const wxString legendDir = GetLegendDirectory() + wxFileName::GetPathSeparator();
    if (name != m_deleted_folder) {
        if (wxDirExists(legendDir + m_deleted_folder) && !wxDirExists(legendDir + name))
            wxRenameFile(legendDir + m_deleted_folder, legendDir + name, false);
        m_deleted_folder = name;
        SaveConfig();
    }
    return legendDir + name;
}

// Moves a note into the folder for deleted notes, after asking; from the
// legend window or from the chart's menu.
void legend_pi::DeleteNoteFile(const wxString &path)
{
    const wxString title = MakeDisplayTitle(path);
    wxWindow *parent = m_legend_window && m_legend_window->IsShown()
                           ? static_cast<wxWindow *>(m_legend_window)
                           : GetOCPNCanvasWindow();
    const wxString question =
        wxString::Format(_("Delete note \"%s\"?\n\nThe file goes to the folder "
                           "\"%s\" in the Legend folder; you can get it back from there."),
                         title, _("Deleted"));
    wxMessageDialog confirm(parent, question, _("Delete note"),
                            wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    confirm.SetYesNoLabels(_("Delete"), _("Cancel"));
    if (confirm.ShowModal() != wxID_YES)
        return;

    wxFileName target(DeletedFolder(), wxFileName(path).GetFullName());
    wxFileName::Mkdir(target.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    for (int n = 2; target.FileExists(); ++n)
        target.SetName(wxString::Format("%s %d", wxFileName(path).GetName(), n));
    // A note being edited is saved and closed first.
    if (m_editing && m_editing_path == path)
        StopEditing();

    if (!wxRenameFile(path, target.GetFullPath(), false)) {
        wxMessageBox(_("Could not move the note."),
                     _("Delete note"),
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
    ShowNotice(_("No position of the boat"),
               _("No GPS position has come in during the last minute."),
               false,
               wxColour(186, 117, 23));
}

// The notes of the Legend folder that belong to a place: read again when
// the folder changes and after a note is saved.
void legend_pi::RefreshNotePlaces()
{
    // With notes off there are none on the chart.
    std::vector<wxString> paths;
    wxDir dir(GetLegendDirectory());
    if (m_notes_on && dir.IsOpened()) {
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

// "Notities" in the settings: off, the plugin is a legend only; no signs,
// no rows, no notices, no buttons or chart menu for notes. The Markdown
// files stay in the list, to read.
void legend_pi::SetNotesOn(bool on)
{
    if (!on && m_editing)
        StopEditing();
    // Turned off: once, a notice that the notes are hidden, not gone.
    const size_t onChart = m_note_places.size();
    size_t notes = 0;
    if (!on) {
        wxDir dir(GetLegendDirectory());
        wxString name;
        bool found = dir.IsOpened() && dir.GetFirst(&name, wxEmptyString, wxDIR_FILES);
        while (found) {
            const wxString ext = wxFileName(name).GetExt().Lower();
            if (ext == "md" || ext == "markdown")
                ++notes;
            found = dir.GetNext(&name);
        }
    }
    m_notes_on = on;
    SaveConfig();
    SetCanvasContextMenuItemViz(m_menu_parent, on);
    if (!on) {
        m_ahead_notes.clear();
        m_note_notice_timer.Stop();
        m_pending_note_path.Clear();
#ifdef __ANDROID__
        if (!m_note_move_path.IsEmpty())
            SetNoteMovePath(wxEmptyString);
#endif
    }
    // Back on, the notes the boat is near already are no news.
    m_note_places_known = false;
    m_notes_near.clear();
    RefreshNotePlaces();
    if (m_legend_window)
        RefreshLegendFiles();
    UpdateNoteButtons();

    if (on) {
        if (m_notice.IsShown())
            m_notice.Hide();
        return;
    }
    if (notes == 0)
        return;
    wxString text = onChart == 1 ? _("1 note on the chart is hidden, not deleted.")
                    : onChart > 1
                        ? wxString::Format(_("%zu notes on the chart are hidden, not deleted."),
                                           onChart)
                        : wxString(_("The notes are hidden, not deleted."));
    text << "\n" << _("Back on through Settings \u25b8 Notes.");
    ShowNotice(_("Notes are off"), text, false, wxColour(186, 117, 23),
               [this]() { SetNotesOn(true); }, _("Back on"));
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

// A note row in the legend or the lookout clicked: the note opens, or,
// when its sign is hidden at this scale, the chart zooms in to just past
// its limit and shows the sign, as for a hidden buoy.
void legend_pi::OpenNoteOrShowSign(const wxString &path)
{
    for (size_t i = 0; i < m_note_places.size(); ++i) {
        const NotePlace &place = m_note_places[i];
        if (place.path != path || !NoteHiddenAtScale(place))
            continue;
        const double zoom = ViewScaleForLimit(place.scamin);
        if (zoom > 0.0)
            JumpTo(place.lat, place.lon, zoom);
        m_note_selected = static_cast<int>(i);
        ShowMarker(place.lat, place.lon);
        RefreshAllCanvases();
        return;
    }
    OpenNote(path);
}

// The chart's mouse for the signs of notes: hovering shows a title, a
// click opens the note, a drag moves it; on Android a tap after "move"
// puts the note there. Returns whether the event was the notes'; consume
// then says whether OpenCPN should leave it alone.
bool legend_pi::NoteMouseEvent(wxMouseEvent &event, bool &consume)
{
    // A new press ends a drag whose release never came.
    if (event.LeftDown())
        m_note_drag = -1;

#ifdef __ANDROID__
    // Dragging a sign does not reach the plugin on Android: OpenCPN pans
    // the chart instead. After "move" in the note, a tap on the chart puts
    // the note there; panning meanwhile is no tap.
    if (!m_note_move_path.IsEmpty() && m_viewport_valid) {
        if (event.LeftDown()) {
            m_note_move_down = event.GetPosition();
            consume = true;
            return true;
        }
        if (event.LeftUp()) {
            const wxPoint up = event.GetPosition();
            if (std::abs(up.x - m_note_move_down.x) > 8 || std::abs(up.y - m_note_move_down.y) > 8) {
                consume = false;
                return true;
            }
            PlaceMovingNote(up);
            consume = true;
            return true;
        }
        consume = false;
        return true;
    }
#endif

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
                    canvas->SetToolTip(_("Note: ") + m_note_places[hover].title);
                else
                    canvas->UnsetToolTip();
            }
        }
        consume = false;
        return true;
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
            consume = true;
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
            if (m_note_drag < 0) {
                consume = false;
                return true;
            }
        }

        if (event.Dragging() && event.LeftIsDown()) {
#ifdef __ANDROID__
            // OpenCPN pans the chart, and the release does not come.
            m_note_drag = -1;
            consume = false;
            return true;
#endif
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
            consume = true;
            return true;
        }

        if (event.LeftUp()) {
            const NotePlace place = m_note_places[m_note_drag];
            m_note_drag = -1;
            if (m_note_drag_moved) {
                FinishNoteMove(place, m_note_drag_from_lat, m_note_drag_from_lon);
                consume = true;
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
            consume = true;
            return true;
        }
        consume = true;
        return true;
    }
    return false;
}

// A click on the chart beside the signs: no radius shows any more.
void legend_pi::ClearNoteSelection()
{
    if (m_note_selected < 0)
        return;
    m_note_selected = -1;
    RefreshAllCanvases();
}

// "Notitie" in the chart's right-click menu, with a submenu that
// PrepareContextMenu() fills each time for the spot and the state.
void legend_pi::AddNoteMenuItems()
{
    // OpenCPN tells a plugin of this API version which submenu was used,
    // not which item in it. So every note action is also an item of its
    // own, hidden, whose id the submenu item carries: OpenCPN calls back
    // with that id too (it looks through all items, hidden ones as well).
    auto action = [this](const char *label) {
        const int id = AddCanvasContextMenuItem(
            new wxMenuItem(MenuItemOwner(), wxID_ANY, wxString::FromUTF8(label)), this);
        SetCanvasContextMenuItemViz(id, false);
        m_menu_actions.push_back(id);
        return id;
    };
    m_menu_new_note = action("Nieuwe notitie hier");
    m_menu_open_note = action("Notitie openen");
    m_menu_delete_note = action("Notitie verwijderen");
    m_menu_undo_move = action("Notitie terugzetten op de oude plek");
    m_menu_link_place = action("Link naar deze plek in de tekst");
    m_menu_pin_note = action("Notitie hier vastpinnen op de kaart");
    m_menu_new_note_boat = action("Nieuwe notitie bij de boot");
    m_menu_link_boat = action("Positie van de boot in de tekst");
    m_menu_pin_boat = action("Vastpinnen op de positie van de boot");
    m_context_submenu = new wxMenu();
    m_menu_parent = AddCanvasContextMenuItem(
        new wxMenuItem(MenuItemOwner(), wxID_ANY, _("Note"), wxEmptyString, wxITEM_NORMAL,
                       m_context_submenu),
        this);
    PrepareContextMenu(0);
    SetCanvasContextMenuItemViz(m_menu_parent, m_notes_on);
}

// The note actions of the chart's menu; false for an item of another kind.
bool legend_pi::OnNoteMenuItem(int id)
{
    // Wherever the click was.
    if (id == m_menu_new_note_boat)
        NewNoteAtBoat();
    else if (id == m_menu_link_boat)
        InsertBoatPosition();
    else if (id == m_menu_pin_boat)
        PinNoteAtBoat();
    else if (id == m_menu_undo_move)
        UndoNoteMove();
    else if (id == m_menu_open_note) {
        if (!m_menu_note_path.IsEmpty())
            OpenNote(m_menu_note_path);
    }
    else if (id == m_menu_delete_note) {
        if (!m_menu_note_path.IsEmpty())
            DeleteNoteFile(m_menu_note_path);
    }
    // At the clicked spot.
    else if (id == m_menu_new_note) {
        if (m_cursor_valid)
            NewNoteAt(m_cursor_lat, m_cursor_lon);
    }
    else if (id == m_menu_link_place) {
        if (m_cursor_valid)
            InsertPlace(m_cursor_lat, m_cursor_lon);
    }
    else if (id == m_menu_pin_note) {
        if (m_cursor_valid)
            SetNotePlace(m_cursor_lat, m_cursor_lon);
    }
    else
        return false;
    return true;
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
        m_context_submenu->Append(m_menu_link_place, _("Link to this place in the text"));
        m_context_submenu->Append(m_menu_pin_note, _("Pin here on the chart"));
        if (boat) {
            m_context_submenu->AppendSeparator();
            m_context_submenu->Append(m_menu_link_boat, _("Boat's position in the text"));
            m_context_submenu->Append(m_menu_pin_boat, _("Pin to the boat's position"));
        }
    }
    else {
        m_context_submenu->Append(m_menu_new_note, _("New note here"));
        if (boat)
            m_context_submenu->Append(m_menu_new_note_boat, _("New note at the boat"));
    }
    if (!m_menu_note_path.IsEmpty()) {
        m_context_submenu->AppendSeparator();
        const wxString quoted =
            wxString::FromUTF8("\u201C") + noteTitle + wxString::FromUTF8("\u201D");
        m_context_submenu->Append(m_menu_open_note, wxString::Format(_("Open %s"), quoted));
        m_context_submenu->Append(m_menu_delete_note,
                                  wxString::Format(_("Delete %s\u2026"), quoted));
    }
    if (m_undo_move_valid) {
        m_context_submenu->AppendSeparator();
        const wxString quoted =
            wxString::FromUTF8("\u201C") + m_undo_move_title + wxString::FromUTF8("\u201D");
        m_context_submenu->Append(m_menu_undo_move,
                                  wxString::Format(_("Put %s back in its old place"), quoted));
    }
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

// The same with piDC, for OpenGL. piDC draws in whole pixels and its
// circles have no dashes on Android, so the dashed radius is drawn as
// short lines.
bool legend_pi::DrawNoteSigns(piDC &dc, PlugIn_ViewPort *vp)
{
    if (m_note_places.empty())
        return false;
    wxColour fill;
    wxColour edge;
    NoteSignColours(&fill, &edge);
    const double s = std::max(1.0, ViewportPixelsPerWindowPixel());
    const double kPi = 3.14159265358979323846;
    auto width = [s](double points) {
        return std::max(1, static_cast<int>(std::lround(points * s)));
    };
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
            dc.SetPen(wxPen(wxColour(edge.Red(), edge.Green(), edge.Blue(), 230), width(1.5)));
            const int segments = 72;
            for (int k = 0; k < segments; k += 2) {
                const double a1 = 2.0 * kPi * k / segments;
                const double a2 = 2.0 * kPi * (k + 1) / segments;
                dc.DrawLine(std::lround(x + r * std::cos(a1)), std::lround(y + r * std::sin(a1)),
                            std::lround(x + r * std::cos(a2)), std::lround(y + r * std::sin(a2)));
            }
        }

        const double w = 9.0 * s;
        const double h = 11.0 * s;
        const double fold = 4.0 * s;
        auto point = [](double px, double py) {
            return wxPoint(static_cast<int>(std::lround(px)), static_cast<int>(std::lround(py)));
        };
        wxPoint sheet[5] = {point(x - w, y - h), point(x + w - fold, y - h),
                            point(x + w, y - h + fold), point(x + w, y + h),
                            point(x - w, y + h)};
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(fill.Red(), fill.Green(), fill.Blue(), 245)));
        FillPolygon(dc, std::vector<wxPoint>(sheet, sheet + 5));
        wxPoint edgeLine[6] = {sheet[0], sheet[1], sheet[2], sheet[3], sheet[4], sheet[0]};
        dc.SetPen(wxPen(edge, width(1.3)));
        dc.DrawLines(6, edgeLine);

        dc.SetPen(wxPen(edge, width(1.0)));
        const wxPoint corner = point(x + w - fold, y - h + fold);
        dc.DrawLine(sheet[1].x, sheet[1].y, corner.x, corner.y);
        dc.DrawLine(corner.x, corner.y, sheet[2].x, sheet[2].y);
        for (int line = 0; line < 3; ++line) {
            const wxPoint from = point(x - w * 0.6, y - h * 0.25 + line * h * 0.35);
            const wxPoint to = point(x + w * 0.6, y - h * 0.25 + line * h * 0.35);
            dc.DrawLine(from.x, from.y, to.x, to.y);
        }
        drew = true;
    }
    return drew;
}


// The lookout: notes whose place lies in the fan, in order of arrival; each
// adds to the key that tells whether the list changed.
void legend_pi::CollectAheadNotes(double range, wxString &key)
{
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
}

// With every fix: a notice when the boat comes within the radius of a
// note it was not near before. A notice about a VHF area goes first; a
// note waits until that one is gone.
void legend_pi::CheckNotePlaces()
{
    std::set<wxString> nearby;
    std::vector<const NotePlace *> entered;
    for (const NotePlace &place : m_note_places) {
        const double distance = SeaMiles(m_ship_lat, m_ship_lon, place.lat, place.lon);
        if (distance > place.radius)
            continue;
        nearby.insert(place.path);
        // The note being written is no news.
        if (m_editing && place.path == m_editing_path)
            continue;
        if (m_note_places_known && m_notes_near.count(place.path) == 0)
            entered.push_back(&place);
    }
    m_notes_near = nearby;
    m_note_places_known = true;

    if (entered.empty() || !m_note_notice_on)
        return;

    const NotePlace &place = *entered.front();
    wxString distance = wxString::Format(
        "%.1f", SeaMiles(m_ship_lat, m_ship_lon, place.lat, place.lon));
    if (m_dutch_names)
        distance.Replace(".", ",");
    m_pending_note_title = _("Note nearby");
    m_pending_note_text = place.title + wxString::FromUTF8(" \u00b7 ") + distance + " NM\n" +
                          _("Click to open the note");
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
        wxMessageBox(_("Could not save the new place in the note."),
                     _("Move note"),
                     wxOK | wxICON_WARNING, GetOCPNCanvasWindow());
    RefreshNotePlaces();

    // The note in the window shows its place on top.
    if (m_legend_window && m_legend_window->IsShown() && m_current_legend_path == path)
        ShowLegend(m_legend_index);
}

// The last dragged sign back where it was.
// A sign put in a new place: saved, with a notice that puts it back, for
// a move made by mistake while panning the chart. The place is a copy:
// saving reads the places again.
void legend_pi::FinishNoteMove(NotePlace place, double fromLat, double fromLon)
{
    m_undo_move_valid = true;
    m_undo_move_path = place.path;
    m_undo_move_title = place.title;
    m_undo_move_lat = fromLat;
    m_undo_move_lon = fromLon;
    MoveNotePlace(place.path, place.lat, place.lon);
    ShowNotice(_("Note moved"),
               wxString::FromUTF8("\u201C") + place.title + _("\u201D is in a new place."),
               false, wxColour(186, 117, 23), [this]() { UndoNoteMove(); }, _("Put back"));
}

#ifdef __ANDROID__
// The move button of a pinned note: the next tap on the chart puts the
// note there. A second press on the button gives up.
void legend_pi::StartNoteMove()
{
    if (!m_note_move_path.IsEmpty()) {
        SetNoteMovePath(wxEmptyString);
        m_notice.Hide();
        return;
    }
    for (const NotePlace &place : m_note_places) {
        if (place.path != m_current_legend_path)
            continue;
        // The notice stays while the tap is awaited.
        ShowNotice(_("Move note"),
                   wxString::Format(_("Tap the chart where \u201C%s\u201D should go."),
                                    place.title),
                   false, wxColour(186, 117, 23));
        m_notice.KeepShown();
        SetNoteMovePath(place.path);
        return;
    }
}

// The note waiting for a tap on the chart, or none; meanwhile the move
// button is orange.
void legend_pi::SetNoteMovePath(const wxString &path)
{
    m_note_move_path = path;
    m_move_button->SetBitmap(NoteToolIcon(
        "place", path.IsEmpty() ? GetLegendPalette().text : wxColour(186, 117, 23)));
}

// The tap after the move button: the note goes to that point of the chart.
void legend_pi::PlaceMovingNote(const wxPoint &at)
{
    const wxString path = m_note_move_path;
    SetNoteMovePath(wxEmptyString);
    m_notice.Hide();
    for (size_t i = 0; i < m_note_places.size(); ++i) {
        NotePlace &place = m_note_places[i];
        if (place.path != path)
            continue;
        const double ratio = ViewportPixelsPerWindowPixel();
        PlugIn_ViewPort vp = m_viewport;
        double lat = 0.0;
        double lon = 0.0;
        GetCanvasLLPix(&vp,
                       wxPoint(static_cast<int>(std::lround(at.x * ratio)),
                               static_cast<int>(std::lround(at.y * ratio))),
                       &lat, &lon);
        const double fromLat = place.lat;
        const double fromLon = place.lon;
        place.lat = lat;
        place.lon = lon;
        m_note_selected = static_cast<int>(i);
        RefreshAllCanvases();
        FinishNoteMove(place, fromLat, fromLon);  // a copy, see there
        return;
    }
}
#endif

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
    if (m_dutch_names)
        value.Replace(".", ",");
    wxTextEntryDialog dialog(m_legend_window,
                             _("Radius in nautical miles: within this distance you get a "
                                "notice of this note under way."),
                             _("Radius"), value);
    if (dialog.ShowModal() != wxID_OK)
        return;
    wxString text = dialog.GetValue();
    text.Trim(true).Trim(false);
    text.Replace(",", ".");
    text = text.BeforeFirst(' ');
    double radius = 0.0;
    if (!text.ToCDouble(&radius) || radius <= 0.0 || radius >= 100.0) {
        wxMessageBox(_("Enter a radius in nautical miles, for example 0.5."),
                     _("Radius"),
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
        m_save_status->SetLabel(_("Not saved yet\u2026"));
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
        if (m_dutch_names)
            radius.Replace(".", ",");
        m_place_text->SetLabel(_("Pinned at ") +
                               FormatPosition(m_note_lat, m_note_lon) +
                               _(" \u00b7 radius ") + radius + " NM");
        m_place_text->SetToolTip(_("Click to see the place on the chart"));
    }
    const LegendPalette palette = GetLegendPalette();
    const bool dim = IsDimScheme();
    m_place_bar->SetBackgroundColour(dim ? palette.stripe : wxColour(250, 238, 218));
    m_place_text->SetForegroundColour(dim ? palette.text : wxColour(99, 56, 6));
    m_place_bar->Show(m_editing && m_note_has_place);
    m_place_bar->GetParent()->Layout();
}
