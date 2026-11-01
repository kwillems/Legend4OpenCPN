// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// The note editor on Android: the Markdown text in a plain text field (see
// note_editor.h). The buttons work on whole lines or on the selection.
//
// Positions in the text field are taken as indices in GetValue(). That
// holds for everything but characters outside the Basic Multilingual Plane
// (most emoji), which Qt counts twice.

#include "note_editor.h"

#include <algorithm>
#include <vector>

#include <wx/tokenzr.h>

namespace {

const wxString kListMark = "- ";
const wxString kOpenTask = "- [ ] ";

// The Markdown mark at the start of a line that makes it a list item or a
// task ("- ", "* ", "1. ", "- [ ] ", "- [x] "), or "" for none.
wxString ListPrefix(const wxString &line)
{
    if (line.StartsWith("- [ ] ") || line.StartsWith("- [x] ") || line.StartsWith("- [X] "))
        return line.Left(6);
    if (line.StartsWith("- ") || line.StartsWith("* ") || line.StartsWith("+ "))
        return line.Left(2);
    size_t digits = 0;
    while (digits < line.length() && digits < 9 && wxIsdigit(line[digits]))
        ++digits;
    if (digits > 0 && line.Mid(digits, 2) == ". ")
        return line.Left(digits + 2);
    return wxEmptyString;
}

// "list", "numbered", "task" or "" for the prefix above.
wxString PrefixKind(const wxString &prefix)
{
    if (prefix.IsEmpty())
        return wxEmptyString;
    if (prefix.length() == 6 && prefix.StartsWith("- ["))
        return "task";
    if (wxIsdigit(prefix[0]))
        return "numbered";
    return "list";
}

int Heading(const wxString &line)
{
    size_t level = 0;
    while (level < line.length() && line[level] == '#')
        ++level;
    if (level == 0 || level > 6 || line.Mid(level, 1) != " ")
        return 0;
    return static_cast<int>(level);
}

wxString WithoutHeading(const wxString &line)
{
    const int level = Heading(line);
    return level > 0 ? line.Mid(level + 1) : line;
}

std::vector<wxString> SplitLines(const wxString &text)
{
    std::vector<wxString> lines;
    wxStringTokenizer tokens(text, "\n", wxTOKEN_RET_EMPTY_ALL);
    while (tokens.HasMoreTokens())
        lines.push_back(tokens.GetNextToken());
    if (lines.empty())
        lines.push_back(wxEmptyString);
    return lines;
}

wxString JoinLines(const std::vector<wxString> &lines)
{
    wxString text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0)
            text += "\n";
        text += lines[i];
    }
    return text;
}

} // namespace

NoteEditor::NoteEditor(wxWindow *parent, wxWindowID id)
    : wxTextCtrl(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                 wxTE_MULTILINE | wxBORDER_NONE)
{
    Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
        event.Skip();
        if (m_changed)
            m_changed();
    });
    // After Enter a list goes on, as in the editor on other systems.
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
        event.Skip();
        if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER)
            CallAfter([this]() { AfterNewParagraph(); });
    });
}

void NoteEditor::LoadMarkdown(const wxString &markdown, const wxString &)
{
    // ChangeValue sends no wxEVT_TEXT, so loading is not a change.
    ChangeValue(markdown);
    SetInsertionPoint(0);
}

void NoteEditor::SetColours(const wxColour &background, const wxColour &text, const wxColour &)
{
    SetBackgroundColour(background);
    SetForegroundColour(text);
    const wxTextAttr attr(text, background);
    SetDefaultStyle(attr);
    SetStyle(0, GetLastPosition(), attr);
    Refresh();
}

// From the start of the first to the end of the last line with selected
// text (or with the cursor).
void NoteEditor::LinesOfSelection(long *from, long *to) const
{
    long start = 0, end = 0;
    GetSelection(&start, &end);
    const wxString text = GetValue();
    const long length = static_cast<long>(text.length());
    start = std::min(start, length);
    end = std::min(end, length);
    // A selection up to the start of a line does not take in that line.
    if (end > start && text[end - 1] == '\n')
        --end;

    long first = start;
    while (first > 0 && text[first - 1] != '\n')
        --first;
    long last = end;
    while (last < length && text[last] != '\n')
        ++last;
    *from = first;
    *to = last;
}

void NoteEditor::ReplaceLines(long from, long to, const wxString &text)
{
    Replace(from, to, text);
    SetSelection(from, from + static_cast<long>(text.length()));
    SetFocus();
}

int NoteEditor::HeadingLevel() const
{
    long from = 0, to = 0;
    LinesOfSelection(&from, &to);
    return Heading(SplitLines(GetRange(from, to)).front());
}

void NoteEditor::SetHeading(int level)
{
    long from = 0, to = 0;
    LinesOfSelection(&from, &to);
    std::vector<wxString> lines = SplitLines(GetRange(from, to));
    level = std::max(0, std::min(6, level));
    for (wxString &line : lines) {
        line = WithoutHeading(line);
        if (level > 0)
            line = wxString('#', level) + " " + line;
    }
    ReplaceLines(from, to, JoinLines(lines));
}

// Puts the mark ("**" or "*") around the selection, or takes it away when
// it is already there. Without a selection the cursor goes between two
// marks.
void NoteEditor::ToggleMarks(const wxString &mark)
{
    long start = 0, end = 0;
    GetSelection(&start, &end);
    const long size = static_cast<long>(mark.length());
    if (start == end) {
        WriteText(mark + mark);
        SetInsertionPoint(start + size);
        SetFocus();
        return;
    }

    const wxString text = GetValue();
    const wxString selected = GetRange(start, end);
    if (static_cast<long>(selected.length()) >= 2 * size && selected.StartsWith(mark) &&
        selected.EndsWith(mark)) {
        const wxString inner = selected.Mid(size, selected.length() - 2 * size);
        Replace(start, end, inner);
        SetSelection(start, start + static_cast<long>(inner.length()));
    }
    else if (start >= size && text.Mid(start - size, size) == mark &&
             text.Mid(end, size) == mark) {
        Replace(end, end + size, wxEmptyString);
        Replace(start - size, start, wxEmptyString);
        SetSelection(start - size, end - size);
    }
    else {
        Replace(start, end, mark + selected + mark);
        SetSelection(start + size, end + size);
    }
    SetFocus();
}

void NoteEditor::ToggleBold()
{
    ToggleMarks("**");
}

void NoteEditor::ToggleItalic()
{
    ToggleMarks("*");
}

// Makes the lines with the selection list items of the kind ("list",
// "numbered" or "task"), or plain text again when they all are already.
void NoteEditor::ToggleLinePrefix(const wxString &kind)
{
    long from = 0, to = 0;
    LinesOfSelection(&from, &to);
    std::vector<wxString> lines = SplitLines(GetRange(from, to));
    const bool single = lines.size() == 1;
    const bool all = std::all_of(lines.begin(), lines.end(), [&](const wxString &line) {
        return (!single && line.IsEmpty()) || PrefixKind(ListPrefix(line)) == kind;
    });

    int number = 1;
    for (wxString &line : lines) {
        if (!single && line.IsEmpty())
            continue;
        line = WithoutHeading(line);
        line = line.Mid(ListPrefix(line).length());
        if (all)
            continue;
        if (kind == "numbered")
            line = wxString::Format("%d. ", number++) + line;
        else if (kind == "task")
            line = kOpenTask + line;
        else
            line = kListMark + line;
    }
    ReplaceLines(from, to, JoinLines(lines));
}

void NoteEditor::ToggleList()
{
    ToggleLinePrefix("list");
}

void NoteEditor::ToggleNumbered()
{
    ToggleLinePrefix("numbered");
}

void NoteEditor::ToggleTasks()
{
    ToggleLinePrefix("task");
}

void NoteEditor::AddLink(const wxString &address)
{
    if (address.IsEmpty())
        return;
    long start = 0, end = 0;
    GetSelection(&start, &end);
    const wxString text = start == end ? address : GetRange(start, end);
    AddLinkWithText(address, text);
}

void NoteEditor::AddLinkWithText(const wxString &address, const wxString &text)
{
    long start = 0, end = 0;
    GetSelection(&start, &end);
    const wxString label = start == end ? text : GetRange(start, end);
    const wxString link = "[" + label + "](" + address + ")";
    Replace(start, end, link);
    SetInsertionPoint(start + static_cast<long>(link.length()));
    SetFocus();
}

bool NoteEditor::InsertNoteImage(const wxString &source, const wxString &alt)
{
    long start = 0, end = 0;
    GetSelection(&start, &end);
    const wxString image = "![" + alt + "](" + source + ")";
    Replace(start, end, image);
    SetInsertionPoint(start + static_cast<long>(image.length()));
    SetFocus();
    return true;
}

// The cursor is at the start of a line just made with Enter. After a list
// item the new line gets the same mark (the next number, an open task);
// after an empty list item the list ends and that mark goes away.
void NoteEditor::AfterNewParagraph()
{
    const long caret = GetInsertionPoint();
    const wxString text = GetValue();
    if (caret <= 0 || caret > static_cast<long>(text.length()) || text[caret - 1] != '\n')
        return;

    long before = caret - 1;
    while (before > 0 && text[before - 1] != '\n')
        --before;
    const wxString previous = text.Mid(before, caret - 1 - before);
    const wxString prefix = ListPrefix(previous);
    if (prefix.IsEmpty())
        return;

    wxString rest = previous.Mid(prefix.length());
    rest.Trim(true).Trim(false);
    if (rest.IsEmpty()) {
        // Takes away the empty item and the line under it.
        Replace(before, caret, wxEmptyString);
        SetInsertionPoint(before);
        return;
    }

    wxString next = prefix;
    const wxString kind = PrefixKind(prefix);
    if (kind == "task") {
        next = kOpenTask;
    }
    else if (kind == "numbered") {
        long number = 0;
        prefix.BeforeFirst('.').ToLong(&number);
        next = wxString::Format("%ld. ", number + 1);
    }
    WriteText(next);
}
