// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _LEGEND_HTML_WINDOW_H_
#define _LEGEND_HTML_WINDOW_H_

#include <map>
#include <vector>

#include <wx/bitmap.h>
#include <wx/html/htmlwin.h>
#include <wx/html/winpars.h>

// The line in the pad of "Licht herkennen" that counts the seconds of light
// and dark. It is a cell of its own, drawn in the font and colour around
// it, so it can change several times a second without building the page
// again. The tag <legendclock id="..."> places it.
extern wxString g_tap_clock_text;

// The Ververs icon in the title of the dynamic legend, a cell of its own
// for the same reason: it spins while the legend is read again. The tag
// <legendrefresh id="..." size="..."> places it; size in pixels. An empty
// <legendspacer height="..."> keeps the page as tall as it was meanwhile.
extern wxBitmap g_refresh_icon;

// Only the legend view gets the tag: the module is registered while that
// view is made and removed again right after, so no other HTML window of
// OpenCPN holds a handler of this plugin.
class TapClockModule : public wxHtmlTagsModule
{
public:
    void FillHandlersTable(wxHtmlWinParser *parser) override;
};

// The view of the legend window: a note (Markdown as HTML) or the page of
// the dynamic legend, whose rows and buttons are found by the ids of their
// table cells.
class LegendHtmlWindow : public wxHtmlWindow
{
public:
    LegendHtmlWindow(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint &pos,
        const wxSize &size,
        long style);

    void SetMarkdownPage(
        const wxString &html,
        const wxString &markdownPath);

    void SetDynamicPage(const wxString &html);

    // Scrolls a row of the dynamic legend into view (anchor "legend-row-N").
    void ScrollToRow(int row);

    // Row of the dynamic legend at a window position, or -1.
    int RowAt(const wxPoint &windowPos) const;

    // Heading of the dynamic legend at a window position, or -1.
    int SectionAt(const wxPoint &windowPos) const;

    // Number after an id prefix of the cell at a window position, or -1.
    int IdNumberAt(const wxPoint &windowPos, const wxString &prefix) const;

    // Gives the table cell with this id another background and paints it
    // at once, without building the page again: feedback under a press.
    void SetCellBackground(const wxString &id, const wxColour &colour);

    // Paints the cell with this id again, such as the clock of the pad.
    void RefreshCell(const wxString &id);

    void SetRowCursor(bool overRow);

    // wxHtmlWindow sets its own cursor (text or arrow) when the mouse moves
    // to another cell; keep the hand over a row.
    wxCursor GetHTMLCursor(HTMLCursor type) const override;

protected:
    wxHtmlOpeningStatus OnOpeningURL(
        wxHtmlURLType type,
        const wxString &url,
        wxString *redirect) const override;

private:
    // The file of a picture in a note, or empty for remote and
    // already-qualified ones.
    wxString LocalImagePath(const wxString &url) const;

    // The <img> tags of the note's page, each with the file it shows.
    struct ImageTag {
        size_t end;  // just past "<img"
        wxString path;
    };

    std::vector<ImageTag> ImageTags() const;

    // Width of a picture in pixels, read once.
    int ImageWidth(const wxString &path) const;

    // For each picture of the note: wider than the window, so shrunk to fit.
    std::vector<bool> FitDecisions() const;

    // The note's page with its wide pictures as wide as the window; smaller
    // ones keep their size.
    wxString FittedMarkdown();

    int HitAt(const wxPoint &windowPos, const wxString &prefix) const;

    // x and y are relative to container. Table cells fill the full row
    // height, so a hit on any cell with a row id identifies the row.
    static int FindId(
        const wxHtmlCell *container,
        int x,
        int y,
        const wxString &prefix);

    void RefreshCellNow(const wxHtmlCell *cell);

    static const wxHtmlCell *FindCellWithId(const wxHtmlCell *container, const wxString &id);

    wxString m_baseDirectory;
    wxString m_markdownHtml;           // the note's page before fitting
    std::vector<bool> m_fitted;        // its pictures shrunk to fit
    mutable std::map<wxString, int> m_imageWidths;
    bool m_overRow = false;
};

#endif
