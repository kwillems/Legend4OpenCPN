// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "legend_html_window.h"

#include "legend_shared.h"

#include <wx/dc.h>
#include <wx/filename.h>
#include <wx/filesys.h>
#include <wx/image.h>
#include <wx/log.h>

wxString g_tap_clock_text;

namespace {

class TapClockCell : public wxHtmlCell
{
public:
    explicit TapClockCell(int height) { m_Height = height; }

    // As wide as the line it is on, so the text can be centred.
    void Layout(int w) override
    {
        m_Width = w;
        wxHtmlCell::Layout(w);
    }

    // It asks for no width of its own: a table sizing its columns would
    // otherwise take the width of an earlier layout as what this cell
    // needs, and grow wider than the window.
    int GetMaxTotalWidth() const override { return 0; }

    void Draw(wxDC &dc, int x, int y, int, int, wxHtmlRenderingInfo &) override
    {
        wxCoord width = 0;
        wxCoord height = 0;
        dc.GetTextExtent(g_tap_clock_text, &width, &height);
        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.DrawText(g_tap_clock_text, x + m_PosX + (m_Width - width) / 2,
                    y + m_PosY + (m_Height - height) / 2);
    }
};

class TapClockTagHandler : public wxHtmlWinTagHandler
{
public:
    wxString GetSupportedTags() override { return "LEGENDCLOCK"; }

    bool HandleTag(const wxHtmlTag &tag) override
    {
        auto *cell = new TapClockCell(m_WParser->GetCharHeight() + 2);
        cell->SetId(tag.GetParam("id"));
        m_WParser->GetContainer()->InsertCell(cell);
        return false;
    }
};

} // namespace

void TapClockModule::FillHandlersTable(wxHtmlWinParser *parser)
{
    parser->AddTagHandler(new TapClockTagHandler);
}

LegendHtmlWindow::LegendHtmlWindow(
    wxWindow *parent,
    wxWindowID id,
    const wxPoint &pos,
    const wxSize &size,
    long style)
    : wxHtmlWindow(parent, id, pos, size, style)
{
    // A note's pictures that fit no longer, or fit again, after the
    // window changed size: the page is built again.
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        event.Skip();
        if (m_markdownHtml.empty() || FitDecisions() == m_fitted)
            return;
        CallAfter([this]() {
            if (m_markdownHtml.empty())
                return;
            int x = 0;
            int y = 0;
            GetViewStart(&x, &y);
            SetPage(FittedMarkdown());
            Scroll(x, y);
        });
    });
}

void LegendHtmlWindow::SetMarkdownPage(
    const wxString &html,
    const wxString &markdownPath)
{
    wxFileName markdownFile(markdownPath);
    m_baseDirectory = markdownFile.GetPath();
    m_markdownHtml = html;
    SetPage(FittedMarkdown());
}

void LegendHtmlWindow::SetDynamicPage(const wxString &html)
{
    // The dynamic page only uses memory: images, no relative paths.
    m_baseDirectory.Clear();
    m_markdownHtml.Clear();
    m_fitted.clear();
    SetPage(html);
}

void LegendHtmlWindow::ScrollToRow(int row)
{
    ScrollToAnchor(wxString::Format("legend-row-%d", row));
}

int LegendHtmlWindow::RowAt(const wxPoint &windowPos) const
{
    return HitAt(windowPos, kLegendRowIdPrefix);
}

int LegendHtmlWindow::SectionAt(const wxPoint &windowPos) const
{
    return HitAt(windowPos, kLegendSectionIdPrefix);
}

int LegendHtmlWindow::IdNumberAt(const wxPoint &windowPos, const wxString &prefix) const
{
    return HitAt(windowPos, prefix);
}

void LegendHtmlWindow::SetCellBackground(const wxString &id, const wxColour &colour)
{
    wxHtmlContainerCell *root = GetInternalRepresentation();
    if (!root)
        return;

    auto *cell = dynamic_cast<wxHtmlContainerCell *>(
        const_cast<wxHtmlCell *>(FindCellWithId(root, id)));
    if (!cell)
        return;

    cell->SetBackgroundColour(colour);
    RefreshCellNow(cell);
}

void LegendHtmlWindow::RefreshCell(const wxString &id)
{
    if (const wxHtmlContainerCell *root = GetInternalRepresentation())
        if (const wxHtmlCell *cell = FindCellWithId(root, id))
            RefreshCellNow(cell);
}

void LegendHtmlWindow::SetRowCursor(bool overRow)
{
    if (overRow == m_overRow)
        return;

    m_overRow = overRow;
    SetCursor(overRow ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
}

wxCursor LegendHtmlWindow::GetHTMLCursor(HTMLCursor type) const
{
    if (m_overRow)
        return wxCursor(wxCURSOR_HAND);

    return wxHtmlWindow::GetHTMLCursor(type);
}

wxHtmlOpeningStatus LegendHtmlWindow::OnOpeningURL(
    wxHtmlURLType type,
    const wxString &url,
    wxString *redirect) const
{
    if (type != wxHTML_URL_IMAGE)
        return wxHTML_OPEN;

    const wxString imagePath = LocalImagePath(url);
    if (imagePath.IsEmpty())
        return wxHTML_OPEN;

    *redirect = wxFileSystem::FileNameToURL(wxFileName(imagePath));
    return wxHTML_REDIRECT;
}

wxString LegendHtmlWindow::LocalImagePath(const wxString &url) const
{
    if (m_baseDirectory.IsEmpty())
        return wxEmptyString;

    // Leave remote and already-qualified resources alone.
    if (url.StartsWith("http://") ||
        url.StartsWith("https://") ||
        url.StartsWith("file:") ||
        url.StartsWith("memory:") ||
        url.StartsWith("data:")) {
        return wxEmptyString;
    }

    wxString imagePath = url;

    // A query or fragment is meaningful in a URL but not in a local
    // filename used by wxFileSystem.
    int queryPos = imagePath.Find('?');
    if (queryPos != wxNOT_FOUND)
        imagePath = imagePath.Left(queryPos);

    int fragmentPos = imagePath.Find('#');
    if (fragmentPos != wxNOT_FOUND)
        imagePath = imagePath.Left(fragmentPos);

    // Markdown writes a space in a path as %20.
    imagePath.Replace("%20", " ");

    wxFileName imageFile(imagePath);

    if (!imageFile.IsAbsolute())
        imageFile.MakeAbsolute(m_baseDirectory);

    imageFile.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return imageFile.GetFullPath();
}

std::vector<LegendHtmlWindow::ImageTag> LegendHtmlWindow::ImageTags() const
{
    std::vector<ImageTag> tags;
    size_t at = 0;
    while ((at = m_markdownHtml.find("<img ", at)) != wxString::npos) {
        const size_t close = m_markdownHtml.find('>', at);
        if (close == wxString::npos)
            break;
        const wxString tag = m_markdownHtml.Mid(at, close - at);
        at += 4;
        const int src = tag.Find("src=\"");
        if (src == wxNOT_FOUND || tag.Contains(" width="))
            continue;
        wxString url = tag.Mid(src + 5).BeforeFirst('"');
        url.Replace("&amp;", "&");
        tags.push_back({at, LocalImagePath(url)});
    }
    return tags;
}

int LegendHtmlWindow::ImageWidth(const wxString &path) const
{
    auto known = m_imageWidths.find(path);
    if (known != m_imageWidths.end())
        return known->second;
    wxImage image;
    int width = 0;
    {
        wxLogNull quiet;
        if (!path.IsEmpty() && image.LoadFile(path))
            width = image.GetWidth();
    }
    m_imageWidths[path] = width;
    return width;
}

std::vector<bool> LegendHtmlWindow::FitDecisions() const
{
    const int room = GetClientSize().GetWidth() - 2 * 12 - 16;
    std::vector<bool> fit;
    for (const ImageTag &tag : ImageTags())
        fit.push_back(room > 0 && ImageWidth(tag.path) > room);
    return fit;
}

wxString LegendHtmlWindow::FittedMarkdown()
{
    m_fitted = FitDecisions();
    const std::vector<ImageTag> tags = ImageTags();
    wxString html = m_markdownHtml;
    for (size_t i = tags.size(); i-- > 0;) {
        if (m_fitted[i])
            html.insert(tags[i].end, " width=\"100%\"");
    }
    return html;
}

int LegendHtmlWindow::HitAt(const wxPoint &windowPos, const wxString &prefix) const
{
    const wxHtmlCell *root = GetInternalRepresentation();
    if (!root)
        return -1;

    const wxPoint pos = CalcUnscrolledPosition(windowPos);
    return FindId(
        root, pos.x - root->GetPosX(), pos.y - root->GetPosY(), prefix);
}

int LegendHtmlWindow::FindId(
    const wxHtmlCell *container,
    int x,
    int y,
    const wxString &prefix)
{
    for (const wxHtmlCell *cell = container->GetFirstChild();
         cell;
         cell = cell->GetNext()) {
        const int cx = x - cell->GetPosX();
        const int cy = y - cell->GetPosY();

        if (cx < 0 || cy < 0 ||
            cx >= cell->GetWidth() || cy >= cell->GetHeight())
            continue;

        wxString rest;
        long number = -1;
        if (cell->GetId().StartsWith(prefix, &rest) &&
            rest.ToLong(&number))
            return static_cast<int>(number);

        const int found = FindId(cell, cx, cy, prefix);
        if (found >= 0)
            return found;
    }

    return -1;
}

void LegendHtmlWindow::RefreshCellNow(const wxHtmlCell *cell)
{
    const wxPoint at = CalcScrolledPosition(cell->GetAbsPos());
    RefreshRect(wxRect(at, wxSize(cell->GetWidth(), cell->GetHeight())));
    Update();
}

const wxHtmlCell *LegendHtmlWindow::FindCellWithId(const wxHtmlCell *container,
                                                    const wxString &id)
{
    for (const wxHtmlCell *cell = container->GetFirstChild(); cell; cell = cell->GetNext()) {
        if (cell->GetId() == id)
            return cell;
        if (const wxHtmlCell *found = FindCellWithId(cell, id))
            return found;
    }
    return nullptr;
}
