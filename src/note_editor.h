// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _NOTE_EDITOR_H_
#define _NOTE_EDITOR_H_

#include <functional>

#include <wx/bmpbndl.h>
#include <wx/richtext/richtextctrl.h>

// A note edited as it looks: headings, bold, lists and links shown as they
// are, without Markdown marks. The note is read from Markdown and written
// back as Markdown, so the file stays plain text.
//
// Each paragraph remembers what it is through its paragraph style name
// ("Kop 2", "Lijst", "Taak", ...); a new paragraph made with Enter copies
// it. What the editor cannot show as it looks, such as a table, stays as
// Markdown text in a fixed-width font and is written back unchanged.
class NoteEditor : public wxRichTextCtrl
{
public:
    NoteEditor(wxWindow *parent, wxWindowID id);

    // baseDirectory: where relative image paths of the note start.
    void LoadMarkdown(const wxString &markdown, const wxString &baseDirectory);
    wxString ToMarkdown() const;

    // Called after every change made by the user, not after loading.
    void SetChangedHandler(std::function<void()> handler) { m_changed = handler; }

    // Called with the address of a clicked "geo:" link: a place on the chart.
    void SetPlaceHandler(std::function<void(const wxString &)> handler) { m_place = handler; }

    // Colours of the text, links and background (dusk and night).
    void SetColours(const wxColour &background, const wxColour &text, const wxColour &link);

    // The buttons above the editor.
    int HeadingLevel() const;      // of the paragraph with the cursor, 0 = text
    void SetHeading(int level);    // 0 = normal text
    void ToggleBold();
    void ToggleItalic();
    void ToggleList();
    void ToggleNumbered();
    void ToggleTasks();
    void AddLink(const wxString &address);
    // A link to an address; without a selection `text` is put in as its text.
    void AddLinkWithText(const wxString &address, const wxString &text);
    bool InsertNoteImage(const wxString &source, const wxString &alt);

private:
    struct Paragraph;

    void ApplyKind(long from, long to, const wxString &kind, int level = 1, bool checked = false,
                   int number = 1);
    wxRichTextAttr KindAttr(const wxString &kind, int level, bool checked, int number) const;
    std::vector<wxRichTextParagraph *> SelectedParagraphs() const;
    wxImage LoadImageFor(const wxString &source) const;
    void InsertImageObject(const wxImage &image, const wxString &source, const wxString &alt);
    void Changed();
    void OnReturn(wxRichTextEvent &event);
    void OnLeftDown(wxMouseEvent &event);

    wxString m_base_directory;
    wxString m_mono_face;
    int m_base_size = 13;
    wxColour m_link_colour = wxColour(10, 99, 201);
    wxColour m_muted_colour = wxColour(120, 120, 126);
    bool m_loading = false;
    std::function<void()> m_changed;
    std::function<void(const wxString &)> m_place;
};

// Icons for the buttons above the editor, drawn in the given colour on a
// transparent background, sharp at twice the size: "heading", "bold",
// "italic", "bullets", "numbers", "tasks", "link", "image", and for the
// notes themselves "edit", "new", "rename", "trash" and "place" (a map pin).
wxBitmapBundle NoteToolIcon(const wxString &name, const wxColour &colour);

#endif
