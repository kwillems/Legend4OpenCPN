// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifdef __WXOSX__
#define GL_SILENCE_DEPRECATION
#endif

#include "legend_pi.h"

#include "config.h"

#include <wx/glcanvas.h>

#include <wx/button.h>
#include <wx/dc.h>
#include <wx/bmpbndl.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/html/winpars.h>
#include <wx/dir.h>
#include <wx/display.h>
#include <wx/gdicmn.h>
#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filesys.h>
#include <wx/fs_mem.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/statline.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/textfile.h>
#include <wx/wrapsizer.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <memory>
#include <set>
#include <string>

extern "C" {
#include "md4c-html.h"
#include "note_places.h"
}

#ifdef __WXOSX__
#include <objc/message.h>
#include <objc/runtime.h>

// AppKit constant (macOS 10.14 and later); weakly linked so the plugin
// still loads where it is missing.
extern "C" void *const NSAppearanceNameDarkAqua __attribute__((weak_import));
#endif

namespace {

// Pseudo-path of the dynamic legend entry in the list of legend items.
const wxString kDynamicGpxId = "@gpx-legend";

// Steps of the fade of a row found from the chart (150 ms each).
const int kFadeSteps = 8;

// Added to the colour index of a light that shines dimmed: the steady
// part of a fixed and flashing light.
const int kDimLight = 1000;

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

// Grid of the sectors of sector lights, about two metres.
const double kSectorCell = 2e-5;

std::pair<long, long> SectorCell(double lat, double lon)
{
    return std::make_pair(static_cast<long>(std::floor(lat / kSectorCell)),
                          static_cast<long>(std::floor(lon / kSectorCell)));
}

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

// Id of the table cells of one row in the dynamic legend, followed by the
// row number.
const wxString kLegendRowIdPrefix = "legend-row-";

// Id of a heading in the dynamic legend, followed by its number on the page.
const wxString kLegendSectionIdPrefix = "legend-section-";

// Id of the counter of a row, which marks all objects of that symbol.
const wxString kLegendAllIdPrefix = "legend-all-";

// Ids of the move buttons in the order edit mode, followed by the heading
// number in symbolen.txt.
const wxString kLegendMoveUpIdPrefix = "legend-move-up-";
const wxString kLegendMoveDownIdPrefix = "legend-move-down-";

// Id of the cells of the header icons, for their tooltips: 0 settings,
// 1 Ververs.
const wxString kLegendTipIdPrefix = "legend-tip-";

// Id of the pad of "Licht herkennen", and of its lines with matching
// lights, followed by their number.
const wxString kLegendTapIdPrefix = "legend-tap-";
const wxString kTapClockId = "legend-tapclock";
const wxString kLegendMatchIdPrefix = "legend-match-";
// Buttons of "Licht herkennen", whole cells to click without a link line:
// 0 clears the taps, 1 closes, 10 to 14 choose a colour.
const wxString kLegendButtonIdPrefix = "legend-button-";

// Id of a line of the "Vooruit" block, followed by its number.
const wxString kLegendAheadIdPrefix = "legend-ahead-";
const wxString kLegendAheadVhfIdPrefix = "legend-aheadvhf-";
const wxString kLegendAheadNoteIdPrefix = "legend-aheadnote-";
const wxString kLegendNoteIdPrefix = "legend-note-";
const wxString kLegendVhfAreaIdPrefix = "legend-vhfarea-";

// The "memory:" file system handler for the icons of the dynamic legend.
// Its code is compiled into this plugin, so it must be removed again in
// DeInit: wxWidgets deletes registered handlers only when OpenCPN exits,
// long after the plugin library has been unloaded.
wxMemoryFSHandler *g_memoryHandler = nullptr;

// The line in the pad of "Licht herkennen" that counts the seconds of light
// and dark. It is a cell of its own, drawn in the font and colour around
// it, so it can change several times a second without building the page
// again. The tag <legendclock id="..."> places it.
wxString g_tap_clock_text;

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

// Only the legend view gets the tag: the module is registered while that
// view is made and removed again right after, so no other HTML window of
// OpenCPN holds a handler of this plugin.
class TapClockModule : public wxHtmlTagsModule
{
public:
    void FillHandlersTable(wxHtmlWinParser *parser) override
    {
        parser->AddTagHandler(new TapClockTagHandler);
    }
};

class LegendHtmlWindow : public wxHtmlWindow
{
public:
    LegendHtmlWindow(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint &pos,
        const wxSize &size,
        long style)
        : wxHtmlWindow(parent, id, pos, size, style)
    {
    }

    void SetMarkdownPage(
        const wxString &html,
        const wxString &markdownPath)
    {
        wxFileName markdownFile(markdownPath);
        m_baseDirectory = markdownFile.GetPath();
        SetPage(html);
    }

    void SetDynamicPage(const wxString &html)
    {
        // The dynamic page only uses memory: images, no relative paths.
        m_baseDirectory.Clear();
        SetPage(html);
    }

    // Scrolls a row of the dynamic legend into view (anchor "legend-row-N").
    void ScrollToRow(int row)
    {
        ScrollToAnchor(wxString::Format("legend-row-%d", row));
    }

    // Row of the dynamic legend at a window position, or -1.
    int RowAt(const wxPoint &windowPos) const
    {
        return HitAt(windowPos, kLegendRowIdPrefix);
    }

    // Heading of the dynamic legend at a window position, or -1.
    int SectionAt(const wxPoint &windowPos) const
    {
        return HitAt(windowPos, kLegendSectionIdPrefix);
    }

    // Number after an id prefix of the cell at a window position, or -1.
    int IdNumberAt(const wxPoint &windowPos, const wxString &prefix) const
    {
        return HitAt(windowPos, prefix);
    }

    // Gives the table cell with this id another background and paints it
    // at once, without building the page again: feedback under a press.
    void SetCellBackground(const wxString &id, const wxColour &colour)
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

    // Paints the cell with this id again, such as the clock of the pad.
    void RefreshCell(const wxString &id)
    {
        if (const wxHtmlContainerCell *root = GetInternalRepresentation())
            if (const wxHtmlCell *cell = FindCellWithId(root, id))
                RefreshCellNow(cell);
    }

    void SetRowCursor(bool overRow)
    {
        if (overRow == m_overRow)
            return;

        m_overRow = overRow;
        SetCursor(overRow ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
    }

    // wxHtmlWindow sets its own cursor (text or arrow) when the mouse moves
    // to another cell; keep the hand over a row.
    wxCursor GetHTMLCursor(HTMLCursor type) const override
    {
        if (m_overRow)
            return wxCursor(wxCURSOR_HAND);

        return wxHtmlWindow::GetHTMLCursor(type);
    }

protected:
    wxHtmlOpeningStatus OnOpeningURL(
        wxHtmlURLType type,
        const wxString &url,
        wxString *redirect) const override
    {
        if (type != wxHTML_URL_IMAGE || m_baseDirectory.IsEmpty())
            return wxHTML_OPEN;

        // Leave remote and already-qualified resources alone.
        if (url.StartsWith("http://") ||
            url.StartsWith("https://") ||
            url.StartsWith("file:") ||
            url.StartsWith("memory:") ||
            url.StartsWith("data:")) {
            return wxHTML_OPEN;
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

        *redirect = wxFileSystem::FileNameToURL(imageFile);
        return wxHTML_REDIRECT;
    }

private:
    int HitAt(const wxPoint &windowPos, const wxString &prefix) const
    {
        const wxHtmlCell *root = GetInternalRepresentation();
        if (!root)
            return -1;

        const wxPoint pos = CalcUnscrolledPosition(windowPos);
        return FindId(
            root, pos.x - root->GetPosX(), pos.y - root->GetPosY(), prefix);
    }

    // x and y are relative to container. Table cells fill the full row
    // height, so a hit on any cell with a row id identifies the row.
    static int FindId(
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

    void RefreshCellNow(const wxHtmlCell *cell)
    {
        const wxPoint at = CalcScrolledPosition(cell->GetAbsPos());
        RefreshRect(wxRect(at, wxSize(cell->GetWidth(), cell->GetHeight())));
        Update();
    }

    static const wxHtmlCell *FindCellWithId(const wxHtmlCell *container, const wxString &id)
    {
        for (const wxHtmlCell *cell = container->GetFirstChild(); cell; cell = cell->GetNext()) {
            if (cell->GetId() == id)
                return cell;
            if (const wxHtmlCell *found = FindCellWithId(cell, id))
                return found;
        }
        return nullptr;
    }

    wxString m_baseDirectory;
    bool m_overRow = false;
};

void Md4cOutputCallback(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    auto *out = static_cast<std::string *>(userdata);
    out->append(text, size);
}

// wxHTML knows no check boxes. A list of tasks becomes an indented list
// without bullets (<dl>), each item starting with an empty or a ticked box.
std::string ShowTaskLists(const std::string &html)
{
    static const std::string kTaskItem = "<li class=\"task-list-item\">";
    static const std::string kBox =
        "<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled";
    if (html.find(kTaskItem) == std::string::npos)
        return html;

    std::string out;
    out.reserve(html.size() + 64);
    std::vector<bool> lists; // per open <ul>: shown as <dl>
    std::vector<bool> items; // per open <li>: shown as <dd>
    bool dropParagraphEnd = false;
    size_t pos = 0;
    while (pos < html.size()) {
        if (html.compare(pos, 4, "<ul>") == 0) {
            const size_t next = html.find('<', pos + 4);
            const bool tasks =
                next != std::string::npos && html.compare(next, kTaskItem.size(), kTaskItem) == 0;
            lists.push_back(tasks);
            out += tasks ? "<dl>" : "<ul>";
            pos += 4;
        }
        else if (html.compare(pos, 5, "</ul>") == 0) {
            const bool tasks = !lists.empty() && lists.back();
            if (!lists.empty())
                lists.pop_back();
            out += tasks ? "</dl>" : "</ul>";
            pos += 5;
        }
        else if (html.compare(pos, kTaskItem.size(), kTaskItem) == 0) {
            pos += kTaskItem.size();
            bool checked = false;
            if (html.compare(pos, kBox.size(), kBox) == 0) {
                const size_t end = html.find('>', pos);
                checked = html.substr(pos, end - pos).find("checked") != std::string::npos;
                pos = end == std::string::npos ? html.size() : end + 1;
            }
            const bool asTerm = !lists.empty() && lists.back();
            items.push_back(asTerm);
            out += asTerm ? "<dd>" : "<li>";
            out += checked ? "<font size=\"+1\">\xE2\x98\x91</font>&nbsp;"
                           : "<font size=\"+1\">\xE2\x98\x90</font>&nbsp;";
            // In a list with empty lines between the items the text is a
            // paragraph; it stays on the line of its box.
            if (html.compare(pos, 3, "<p>") == 0) {
                pos += 3;
                dropParagraphEnd = true;
            }
        }
        else if (dropParagraphEnd && html.compare(pos, 4, "</p>") == 0) {
            dropParagraphEnd = false;
            pos += 4;
        }
        else if (html.compare(pos, 4, "<li>") == 0 || html.compare(pos, 4, "<li ") == 0) {
            items.push_back(false);
            const size_t end = html.find('>', pos);
            out.append(html, pos, end - pos + 1);
            pos = end + 1;
        }
        else if (html.compare(pos, 5, "</li>") == 0) {
            const bool asTerm = !items.empty() && items.back();
            if (!items.empty())
                items.pop_back();
            out += asTerm ? "</dd>" : "</li>";
            pos += 5;
        }
        else {
            out += html[pos++];
        }
    }
    return out;
}

// Draw an image (with or without alpha channel or mask) on a background
// colour and return the opaque result.
wxImage FlattenImage(const wxImage &image, const wxColour &background)
{
    if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0)
        return wxImage();

    const int width = image.GetWidth();
    const int height = image.GetHeight();
    const size_t pixels = static_cast<size_t>(width) * height;

    const unsigned char *src = image.GetData();
    const unsigned char *alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;

    const bool hasMask = !alpha && image.HasMask();
    const unsigned char maskR = hasMask ? image.GetMaskRed() : 0;
    const unsigned char maskG = hasMask ? image.GetMaskGreen() : 0;
    const unsigned char maskB = hasMask ? image.GetMaskBlue() : 0;

    wxImage flat(width, height, false);
    unsigned char *dst = flat.GetData();

    for (size_t i = 0; i < pixels; ++i) {
        const unsigned char r = src[i * 3];
        const unsigned char g = src[i * 3 + 1];
        const unsigned char b = src[i * 3 + 2];

        int a = alpha ? alpha[i] : 255;

        if (hasMask && r == maskR && g == maskG && b == maskB)
            a = 0;

        dst[i * 3] = static_cast<unsigned char>(
            (r * a + background.Red() * (255 - a)) / 255);
        dst[i * 3 + 1] = static_cast<unsigned char>(
            (g * a + background.Green() * (255 - a)) / 255);
        dst[i * 3 + 2] = static_cast<unsigned char>(
            (b * a + background.Blue() * (255 - a)) / 255);
    }

    return flat;
}

bool IsSafeIconKey(const wxString &key)
{
    return !key.IsEmpty() && key.Find('/') == wxNOT_FOUND &&
           key.Find('\\') == wxNOT_FOUND && key.Find("..") == wxNOT_FOUND;
}

// A user icon can also be an SVG file (UserIcons/<key>.svg); OpenCPN then
// uses it instead of a PNG of the same name.
wxString SvgIconPath(const wxString &key)
{
    if (!IsSafeIconKey(key))
        return wxString();

    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (!privateDir || privateDir->IsEmpty())
        return wxString();

    const wxString path = *privateDir + wxFileName::GetPathSeparator() +
                          "UserIcons" + wxFileName::GetPathSeparator() + key +
                          ".svg";
    return wxFileName::FileExists(path) ? path : wxString();
}

// The size OpenCPN shows an SVG icon at: its width and height attributes in
// pixels, as OpenCPN reads them.
wxSize SvgIconSize(const wxString &path)
{
    wxSize size(64, 64);
    wxFile file(path);
    wxString text;
    if (!file.IsOpened() || !file.ReadAll(&text))
        return size;

    const int svg = text.Find("<svg");
    if (svg == wxNOT_FOUND)
        return size;
    const wxString tag = text.Mid(svg).BeforeFirst('>');

    auto attribute = [&tag](const wxString &name, int fallback) {
        const int at = tag.Find(" " + name + "=\"");
        long value = 0;
        if (at == wxNOT_FOUND ||
            !tag.Mid(at + name.length() + 3).BeforeFirst('"').ToLong(&value) ||
            value <= 0 || value > 512)
            return fallback;
        return static_cast<int>(value);
    };
    size.SetWidth(attribute("width", size.GetWidth()));
    size.SetHeight(attribute("height", size.GetHeight()));
    return size;
}

// OpenCPN keeps waypoint icons as PNG or SVG files: user icons in the
// private data directory (UserIcons) and the built-in ones in the shared
// data directory. Reading the file ourselves is more reliable than asking
// OpenCPN for a bitmap, which can come back empty on macOS. An SVG icon is
// drawn at `scale` times its size, so a Retina screen gets it sharp.
wxImage LoadIconFromFile(const wxString &key, const wxColour &background,
                         int scale = 1)
{
    if (!IsSafeIconKey(key))
        return wxImage();

    const wxString svg = SvgIconPath(key);
    if (!svg.IsEmpty()) {
        wxLogNull quiet;
        const wxSize size = SvgIconSize(svg);
        const wxBitmap bitmap = wxBitmapBundle::FromSVGFile(svg, size)
                                    .GetBitmap(size * std::max(1, scale));
        if (bitmap.IsOk()) {
            const wxImage image = bitmap.ConvertToImage();
            if (image.IsOk())
                return FlattenImage(image, background);
        }
    }

    wxArrayString candidates;

    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (privateDir && !privateDir->IsEmpty()) {
        candidates.Add(
            *privateDir + wxFileName::GetPathSeparator() +
            "UserIcons" + wxFileName::GetPathSeparator() + key + ".png");
    }

    wxString *sharedDir = GetpSharedDataLocation();
    if (sharedDir && !sharedDir->IsEmpty()) {
        candidates.Add(
            *sharedDir + wxFileName::GetPathSeparator() +
            "uidata" + wxFileName::GetPathSeparator() +
            "markicons" + wxFileName::GetPathSeparator() + key + ".png");
        candidates.Add(
            *sharedDir + wxFileName::GetPathSeparator() +
            "uidata" + wxFileName::GetPathSeparator() + key + ".png");
    }

    for (size_t i = 0; i < candidates.GetCount(); ++i) {
        if (!wxFileName::FileExists(candidates[i]))
            continue;

        wxImage image;
        wxLogNull quiet;

        if (image.LoadFile(candidates[i], wxBITMAP_TYPE_PNG) && image.IsOk())
            return FlattenImage(image, background);
    }

    return wxImage();
}

// Convert an OpenCPN icon bitmap to an opaque image by drawing it on the
// background colour. Transparency (alpha channel or mask) is resolved here,
// because it is lost or turns black when the image is shown by wxHtmlWindow.
wxImage FlattenIcon(
    const wxBitmap &bitmap,
    const wxColour &background,
    const wxString &key)
{
    // First attempt: let the platform draw the bitmap on a canvas with the
    // background colour. On some platforms reading the pixels of a bitmap
    // directly (ConvertToImage) gives an empty result while drawing works.
    {
        const int bw = bitmap.GetWidth();
        const int bh = bitmap.GetHeight();

        if (bw > 0 && bh > 0) {
            wxBitmap canvas(bw, bh, 24);

            {
                wxMemoryDC dc;
                dc.SelectObject(canvas);
                dc.SetBackground(wxBrush(background));
                dc.Clear();
                dc.DrawBitmap(bitmap, 0, 0, true);
                dc.SelectObject(wxNullBitmap);
            }

            wxImage drawn = canvas.ConvertToImage();

            if (drawn.IsOk()) {
                const unsigned char *d = drawn.GetData();
                const size_t n =
                    static_cast<size_t>(drawn.GetWidth()) * drawn.GetHeight();

                std::set<unsigned int> seen;
                for (size_t i = 0; i < n; ++i) {
                    seen.insert(
                        (static_cast<unsigned int>(d[i * 3]) << 16) |
                        (static_cast<unsigned int>(d[i * 3 + 1]) << 8) |
                        static_cast<unsigned int>(d[i * 3 + 2]));
                }

                // More than the plain background colour: the drawing worked.
                if (seen.size() > 1)
                    return drawn;
            }
        }
    }

    (void)key;

    return FlattenImage(bitmap.ConvertToImage(), background);
}

// Every icon gets a square of the same size, so all rows in the dynamic
// legend are equally high. Icons keep their own size and are centred; only
// icons larger than the square are scaled down. With a tile colour the icon
// sits on a rounded tile of that colour, padding pixels wider than the box.
wxImage NormalizeIcon(
    const wxImage &image,
    const wxColour &background,
    int box,
    const wxColour *tile = nullptr,
    int padding = 0)
{
    wxImage icon = image;
    int width = icon.GetWidth();
    int height = icon.GetHeight();

    if (width > box || height > box) {
        const double factor =
            std::min(static_cast<double>(box) / width,
                     static_cast<double>(box) / height);

        width = std::max(1, static_cast<int>(std::lround(width * factor)));
        height = std::max(1, static_cast<int>(std::lround(height * factor)));
        icon = icon.Scale(width, height, wxIMAGE_QUALITY_HIGH);
    }

    if (!tile)
        padding = 0;

    const int size = box + 2 * padding;

    wxImage square(size, size, false);
    square.SetRGB(
        wxRect(0, 0, size, size),
        background.Red(),
        background.Green(),
        background.Blue());

    if (tile) {
        // Rounded corners, blended at the edge for a smooth outline.
        const double radius = std::min(6.0, size / 4.0);
        unsigned char *data = square.GetData();

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const double cx =
                    std::max({radius - (x + 0.5), (x + 0.5) - (size - radius),
                              0.0});
                const double cy =
                    std::max({radius - (y + 0.5), (y + 0.5) - (size - radius),
                              0.0});
                const double outside = std::sqrt(cx * cx + cy * cy) - radius;
                const double cover = std::min(1.0, std::max(0.0, 0.5 - outside));

                unsigned char *px = data + (y * size + x) * 3;
                px[0] = static_cast<unsigned char>(std::lround(
                    px[0] + (tile->Red() - px[0]) * cover));
                px[1] = static_cast<unsigned char>(std::lround(
                    px[1] + (tile->Green() - px[1]) * cover));
                px[2] = static_cast<unsigned char>(std::lround(
                    px[2] + (tile->Blue() - px[2]) * cover));
            }
        }
    }

    square.Paste(icon, (size - width) / 2, (size - height) / 2);

    return square;
}

// Text prepared for searching: lower case and without accents, so that
// "waterskien" finds "Waterskiën" and capitals do not matter.
wxString FoldForSearch(const wxString &text)
{
    const wxString lower = text.Lower();
    wxString folded;
    folded.reserve(lower.length());

    for (const wxUniChar ch : lower) {
        switch (ch.GetValue()) {
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
            folded << 'a';
            break;
        case 0xE6:
            folded << "ae";
            break;
        case 0xE7:
            folded << 'c';
            break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            folded << 'e';
            break;
        case 0xEC: case 0xED: case 0xEE: case 0xEF:
            folded << 'i';
            break;
        case 0xF1:
            folded << 'n';
            break;
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8:
            folded << 'o';
            break;
        case 0x153:
            folded << "oe";
            break;
        case 0xF9: case 0xFA: case 0xFB: case 0xFC:
            folded << 'u';
            break;
        case 0xFD: case 0xFF:
            folded << 'y';
            break;
        case 0xDF:
            folded << "ss";
            break;
        default:
            folded << ch;
            break;
        }
    }
    return folded;
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

// OpenCPN's colour scheme (day, dusk, night), set through SetColorScheme().
PI_ColorScheme g_color_scheme = PI_GLOBAL_COLOR_SCHEME_DAY;

// Nachtvoorbeeld is on: the legend takes the night colours, whatever the
// scheme of OpenCPN is.
bool g_night_preview = false;

// The scheme the legend window is drawn in.
PI_ColorScheme LegendScheme()
{
    return g_night_preview ? PI_GLOBAL_COLOR_SCHEME_NIGHT : g_color_scheme;
}

bool IsDimScheme()
{
    return LegendScheme() == PI_GLOBAL_COLOR_SCHEME_DUSK ||
           LegendScheme() == PI_GLOBAL_COLOR_SCHEME_NIGHT;
}

// Brightness of icons and images in the current scheme. OpenCPN has darker
// symbol sets for dusk and night; user icons only exist in one version, so
// they are dimmed instead.
double SchemeDim()
{
    switch (LegendScheme()) {
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
        return 0.55;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
        return 0.3;
    default:
        return 1.0;
    }
}

wxString SchemeName(PI_ColorScheme scheme)
{
    switch (scheme) {
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
        return "dusk";
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
        return "night";
    case PI_GLOBAL_COLOR_SCHEME_DAY:
        return "day";
    default:
        return "rgb";
    }
}

wxColour BlendColour(const wxColour &a, const wxColour &b, double amount)
{
    auto mix = [amount](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(std::lround(x + (y - x) * amount));
    };
    return wxColour(
        mix(a.Red(), b.Red()),
        mix(a.Green(), b.Green()),
        mix(a.Blue(), b.Blue()));
}

// Darkens the colours of an image, keeping its transparency.
wxImage DimImage(const wxImage &image, double dim)
{
    if (!image.IsOk() || dim >= 1.0)
        return image;

    wxImage dimmed = image.Copy();
    unsigned char *data = dimmed.GetData();
    const size_t count =
        static_cast<size_t>(dimmed.GetWidth()) * dimmed.GetHeight() * 3;

    for (size_t i = 0; i < count; ++i)
        data[i] = static_cast<unsigned char>(std::lround(data[i] * dim));

    // A mask colour must follow, or the transparent area changes.
    if (dimmed.HasMask()) {
        dimmed.SetMaskColour(
            static_cast<unsigned char>(std::lround(image.GetMaskRed() * dim)),
            static_cast<unsigned char>(std::lround(image.GetMaskGreen() * dim)),
            static_cast<unsigned char>(std::lround(image.GetMaskBlue() * dim)));
    }

    return dimmed;
}

// Flattens an icon on a background with only the icon itself dimmed.
// flatten() draws the icon on a given colour. Drawing it on black and on
// white gives its colour and its coverage, from which the dimmed icon on
// the real background follows.
wxImage FlattenDimmed(
    const std::function<wxImage(const wxColour &)> &flatten,
    const wxColour &background,
    double dim)
{
    if (dim >= 1.0)
        return flatten(background);

    const wxImage onBlack = flatten(*wxBLACK);
    const wxImage onWhite = flatten(*wxWHITE);

    if (!onBlack.IsOk() || !onWhite.IsOk() ||
        onBlack.GetSize() != onWhite.GetSize())
        return DimImage(flatten(background), dim);

    wxImage result(onBlack.GetWidth(), onBlack.GetHeight(), false);
    const unsigned char *k = onBlack.GetData();
    const unsigned char *w = onWhite.GetData();
    unsigned char *out = result.GetData();
    const unsigned char bg[3] = {
        background.Red(), background.Green(), background.Blue()};
    const size_t pixels =
        static_cast<size_t>(result.GetWidth()) * result.GetHeight();

    for (size_t i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            const size_t at = i * 3 + c;
            const int uncovered = std::max(0, w[at] - k[at]); // 255 * (1 - a)
            const double value = k[at] * dim + bg[c] * uncovered / 255.0;
            out[at] = static_cast<unsigned char>(
                std::min(255L, std::max(0L, std::lround(value))));
        }
    }

    return result;
}

// Native controls on macOS (buttons, choice) ignore colours set by the
// program; they follow the appearance of their window. In dusk and night
// the legend window gets the dark appearance; by day it follows the system
// again (nil appearance).
void SetWindowDarkAppearance(wxTopLevelWindow *window, bool dark)
{
#ifdef __WXOSX__
    if (!window)
        return;

    void *nsWindow = window->GetWXWindow();
    if (!nsWindow)
        return;

    id appearance = nil;
    if (dark && &NSAppearanceNameDarkAqua != nullptr) {
        appearance = reinterpret_cast<id (*)(id, SEL, id)>(objc_msgSend)(
            reinterpret_cast<id>(objc_getClass("NSAppearance")),
            sel_registerName("appearanceNamed:"),
            reinterpret_cast<id>(NSAppearanceNameDarkAqua));
    }

    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
        reinterpret_cast<id>(nsWindow),
        sel_registerName("setAppearance:"),
        appearance);
#else
    (void)window;
    (void)dark;
#endif
}

// Marker on the chart: bright by day, subdued in dusk and night so it does
// not spoil night vision, but still distinct from the chart.
void MarkerColours(wxColour &outline, wxColour &highlight)
{
    switch (g_color_scheme) {
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
        outline = wxColour(150, 150, 150);
        highlight = wxColour(190, 0, 85);
        break;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
        outline = wxColour(45, 45, 45);
        highlight = wxColour(150, 35, 35);
        break;
    default:
        outline = wxColour(255, 255, 255);
        highlight = wxColour(255, 0, 110);
        break;
    }
}

// A chart scale in a few characters: "1:22k", "1:1,5M".
wxString FormatScaleShort(int scale)
{
    if (scale >= 1000000) {
        const double millions = scale / 1000000.0;
        wxString text = millions == std::floor(millions)
                            ? wxString::Format("%.0f", millions)
                            : wxString::Format("%.1f", millions);
        text.Replace(".", ",");
        return "1:" + text + "M";
    }
    if (scale >= 1000)
        return wxString::Format("1:%dk", (scale + 500) / 1000);
    return wxString::Format("1:%d", scale);
}

// Blends an image towards a colour: faded icons for rows the chart does
// not draw at this scale.
wxImage FadeImage(const wxImage &image, const wxColour &towards, double amount)
{
    if (!image.IsOk())
        return image;

    wxImage faded = image.Copy();
    unsigned char *data = faded.GetData();
    const size_t pixels =
        static_cast<size_t>(faded.GetWidth()) * faded.GetHeight();
    const unsigned char target[3] = {
        towards.Red(), towards.Green(), towards.Blue()};

    for (size_t i = 0; i < pixels; ++i) {
        for (int c = 0; c < 3; ++c) {
            unsigned char &v = data[i * 3 + c];
            v = static_cast<unsigned char>(
                std::lround(v + (target[c] - v) * amount));
        }
    }
    return faded;
}

// A small eye, closed (a stroke through it) for rows hidden at this
// scale, drawn on the row colour.
wxImage EyeIcon(
    bool open, const wxColour &colour, const wxColour &background, int scale = 1)
{
    const int width = 20 * scale;
    const int height = 13 * scale;

    wxBitmap bitmap(width, height, 24);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(background));
        dc.Clear();

        wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(1.4 * scale)));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);

            // The lid: two curves from corner to corner.
            const double y = height / 2.0;
            const double edge = 1.5 * scale;
            wxGraphicsPath lens = gc->CreatePath();
            lens.MoveToPoint(edge, y);
            lens.AddQuadCurveToPoint(width / 2.0, -height * 0.45, width - edge, y);
            lens.AddQuadCurveToPoint(width / 2.0, height * 1.45, edge, y);
            gc->StrokePath(lens);

            // The pupil.
            gc->SetBrush(wxBrush(colour));
            const double radius = height / 4.5;
            gc->DrawEllipse(width / 2.0 - radius, y - radius, 2 * radius, 2 * radius);

            if (!open) {
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(1.6 * scale)));
                gc->StrokeLine(3.0 * scale, height - edge, width - 3.0 * scale, edge);
            }
            delete gc;
        }
        dc.SelectObject(wxNullBitmap);
    }

    return bitmap.ConvertToImage();
}

// The icons of "Instellingen" (a gear) and "Ververs" (a circling arrow),
// drawn rather than taken from a font, so their size is certain.
// A colour darkened for dusk and night (dim 1 = as it is).
wxColour Dimmed(const wxColour &colour, double dim)
{
    return wxColour(static_cast<unsigned char>(colour.Red() * dim),
                    static_cast<unsigned char>(colour.Green() * dim),
                    static_cast<unsigned char>(colour.Blue() * dim), colour.Alpha());
}

// The sign of a note for a row of the legend: a sheet with a folded corner,
// darkened in dusk and night like the other icons (the row stays as it is).
wxImage NoteSignImage(const wxColour &background, int size)
{
    const double dim = SchemeDim();
    wxBitmap bitmap(size, size, 24);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(background));
        dc.Clear();
        wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            const double s = size / 24.0;
            const double x = size / 2.0;
            const double y = size / 2.0;
            const double w = 8.0 * s;
            const double h = 10.0 * s;
            const double fold = 4.0 * s;
            wxGraphicsPath sheet = gc->CreatePath();
            sheet.MoveToPoint(x - w, y - h);
            sheet.AddLineToPoint(x + w - fold, y - h);
            sheet.AddLineToPoint(x + w, y - h + fold);
            sheet.AddLineToPoint(x + w, y + h);
            sheet.AddLineToPoint(x - w, y + h);
            sheet.CloseSubpath();
            gc->SetBrush(wxBrush(Dimmed(wxColour(250, 199, 117), dim)));
            gc->SetPen(gc->CreatePen(
                wxGraphicsPenInfo(Dimmed(wxColour(133, 79, 11), dim)).Width(1.2 * s)));
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
            gc->SetPen(gc->CreatePen(
                wxGraphicsPenInfo(Dimmed(wxColour(133, 79, 11), dim)).Width(1.0 * s)));
            gc->StrokePath(marks);
            delete gc;
        }
    }
    return bitmap.ConvertToImage();
}

// A small VHF sign for a row: the channel in a red-edged rounded square,
// like the icons of the VHFinfo layer.
wxImage VhfBadgeImage(const wxString &channel, const wxColour &background, int size)
{
    const double dim = SchemeDim();
    wxBitmap bitmap(size, size, 24);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(background));
        dc.Clear();
        wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            const double s = size;
            gc->SetPen(gc->CreatePen(
                wxGraphicsPenInfo(Dimmed(wxColour(220, 40, 40), dim)).Width(s * 0.08)));
            gc->SetBrush(wxBrush(Dimmed(wxColour(255, 235, 235), dim)));
            gc->DrawRoundedRectangle(s * 0.08, s * 0.14, s * 0.84, s * 0.72, s * 0.14);
            wxFont font(wxFontInfo(s * (channel.length() > 2 ? 0.3 : 0.4)).Bold());
            gc->SetFont(font, Dimmed(wxColour(160, 20, 20), dim));
            double w = 0, h = 0, descent = 0, leading = 0;
            gc->GetTextExtent(channel, &w, &h, &descent, &leading);
            gc->DrawText(channel, (s - w) / 2, (s - h) / 2);
            delete gc;
        }
    }
    return bitmap.ConvertToImage();
}

wxImage HeaderIcon(bool gear, const wxColour &colour, const wxColour &background, int size)
{
    wxBitmap bitmap(size, size, 24);
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(background));
        dc.Clear();
        wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
        if (gc) {
            gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
            const double kPi = 3.14159265358979323846;
            const double c = size / 2.0;
            if (gear) {
                // Eight teeth around a ring with a hole.
                const double outer = size * 0.46;
                const double inner = size * 0.33;
                wxGraphicsPath path = gc->CreatePath();
                const int teeth = 8;
                for (int i = 0; i < teeth * 4; ++i) {
                    const double angle = 2.0 * kPi * (i - 0.5) / (teeth * 4);
                    const double r = (i % 4 == 1 || i % 4 == 2) ? outer : inner;
                    const double x = c + r * std::cos(angle);
                    const double y = c + r * std::sin(angle);
                    if (i == 0)
                        path.MoveToPoint(x, y);
                    else
                        path.AddLineToPoint(x, y);
                }
                path.CloseSubpath();
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(colour));
                gc->FillPath(path);

                // The hole in the middle.
                const double hole = size * 0.14;
                gc->SetBrush(wxBrush(background));
                gc->DrawEllipse(c - hole, c - hole, 2 * hole, 2 * hole);
            }
            else {
                // Most of a circle with an arrowhead at its end.
                const double r = size * 0.33;
                const double width = std::max(1.5, size * 0.11);
                const double start = -kPi * 0.35;
                const double end = start + kPi * 1.6;
                gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(width)));
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                wxGraphicsPath arc = gc->CreatePath();
                arc.AddArc(c, c, r, start, end, true);
                gc->StrokePath(arc);

                const double tipX = c + r * std::cos(end);
                const double tipY = c + r * std::sin(end);
                const double along = end + kPi / 2.0; // direction of travel
                const double head = size * 0.2;
                wxGraphicsPath arrow = gc->CreatePath();
                arrow.MoveToPoint(tipX + head * std::cos(along), tipY + head * std::sin(along));
                arrow.AddLineToPoint(tipX + head * 0.8 * std::cos(along + kPi * 0.6),
                                     tipY + head * 0.8 * std::sin(along + kPi * 0.6));
                arrow.AddLineToPoint(tipX + head * 0.8 * std::cos(along - kPi * 0.6),
                                     tipY + head * 0.8 * std::sin(along - kPi * 0.6));
                arrow.CloseSubpath();
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(colour));
                gc->FillPath(arrow);
            }
            delete gc;
        }
        dc.SelectObject(wxNullBitmap);
    }
    return bitmap.ConvertToImage();
}

// Colours of the legend view. Fixed values instead of the system colours:
// on macOS the dynamic system colours turn into white when converted to
// RGB in dark mode, which left white text on a white page.
struct LegendPalette
{
    wxColour background;
    wxColour text;
    wxColour muted;
    wxColour stripe;
    wxColour active;
    wxColour rule;
    wxColour link;
    bool iconTiles; // icons on a light tile, for contrast on a dark page
    wxColour tile;
    wxColour header;     // tint behind the block above the list
    wxColour headerRule; // line between that block and the list
};

// Dusk and night: the dialog colours of OpenCPN itself, so the legend dims
// along with the rest of the screen. Fixed dark colours when OpenCPN does
// not provide usable ones.
LegendPalette GetDimPalette()
{
    const bool night = LegendScheme() == PI_GLOBAL_COLOR_SCHEME_NIGHT;

    // In Nachtvoorbeeld by day, OpenCPN's dialog colours are those of the
    // day; the fixed night colours are used then.
    wxColour background;
    wxColour text;
    const bool fromOpenCPN =
        LegendScheme() == g_color_scheme &&
        GetGlobalColor("DILG1", &background) && GetGlobalColor("UITX1", &text);

    auto luminance = [](const wxColour &c) {
        return 0.299 * c.Red() + 0.587 * c.Green() + 0.114 * c.Blue();
    };

    if (!fromOpenCPN || !background.IsOk() || !text.IsOk() ||
        std::fabs(luminance(background) - luminance(text)) < 30.0) {
        background = night ? wxColour(18, 18, 18) : wxColour(52, 52, 54);
        text = night ? wxColour(120, 96, 96) : wxColour(185, 185, 185);
    }

    LegendPalette palette;
    palette.background = background;
    palette.text = text;
    palette.muted = BlendColour(text, background, 0.4);
    palette.stripe = BlendColour(background, text, 0.07);
    palette.active = BlendColour(background, text, 0.22);
    palette.rule = BlendColour(background, text, 0.25);
    palette.link = text; // no bright blue in the dark
    palette.iconTiles = false; // a light tile would glare
    palette.tile = background;
    palette.header = BlendColour(background, text, 0.1);
    palette.headerRule = BlendColour(background, text, 0.35);
    return palette;
}

LegendPalette GetLegendPalette()
{
    if (IsDimScheme())
        return GetDimPalette();

    if (wxSystemSettings::GetAppearance().IsDark()) {
        return {
            wxColour(30, 30, 30),    // background
            wxColour(230, 230, 230), // text
            wxColour(150, 150, 156), // muted
            wxColour(42, 42, 44),    // stripe
            wxColour(38, 68, 110),   // active
            wxColour(64, 64, 68),    // rule
            wxColour(108, 180, 255), // link
            true,
            wxColour(232, 232, 230), // tile: light, like a paper chart
            wxColour(38, 44, 52),    // header
            wxColour(80, 98, 122)    // header rule
        };
    }

    return {
        wxColour(255, 255, 255),
        wxColour(29, 29, 31),
        wxColour(110, 110, 115),
        wxColour(244, 245, 247),
        wxColour(214, 228, 250),
        wxColour(217, 217, 222),
        wxColour(10, 99, 201),
        false,
        wxColour(255, 255, 255),
        wxColour(234, 240, 247),
        wxColour(157, 180, 211)
    };
}

wxString HtmlColour(const wxColour &colour)
{
    return colour.GetAsString(wxC2S_HTML_SYNTAX);
}

// Opening body tag with the palette, so wxHtmlWindow does not fall back to
// its own default colours.
wxString LegendBodyTag(bool plainLinks = false)
{
    const LegendPalette palette = GetLegendPalette();

    return wxString::Format(
        "<body bgcolor=\"%s\" text=\"%s\" link=\"%s\">",
        HtmlColour(palette.background),
        HtmlColour(palette.text),
        HtmlColour(plainLinks ? palette.text : palette.link));
}

}

extern "C" DECL_EXP opencpn_plugin *create_pi(void *ppimgr)
{
    return new legend_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin *p)
{
    delete p;
}

legend_pi::legend_pi(void *ppimgr)
    : opencpn_plugin_118(ppimgr),
      m_toolbar_item_id(-1),
      m_legend_window(nullptr),
      m_header_book(nullptr),
      m_single_title(nullptr),
      m_legend_position(nullptr),
      m_legend_choice(nullptr),
      m_previous_button(nullptr),
      m_next_button(nullptr),
      m_content_book(nullptr),
      m_image_page(nullptr),
      m_legend_bitmap(nullptr),
      m_markdown_view(nullptr),
      m_legend_index(0),
      m_viewport_valid(false),
      m_dynamic_generation(0),
      m_dynamic_total(0),
      m_loading(false),
      m_loading_reload(false),
      m_loading_next(0),
      m_loading_fetched(0),
      m_prefetch_scheduled(false),
      m_showing_loading_page(false),
      m_collected_viewport_valid(false),
      m_marker_active(false),
      m_marker_phase(0),
      m_window_x(-1),
      m_window_y(-1),
      m_window_width(560),
      m_window_height(460)
{
    m_viewport = PlugIn_ViewPort();
    m_collected_viewport = PlugIn_ViewPort();
    m_pending_viewport = PlugIn_ViewPort();

    m_marker_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnMarkerTimer(); });

    m_auto_refresh_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnAutoRefreshTimer(); });

    m_loading_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnLoadingTimer(); });

    m_visibility_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { CheckVisibilityChanges(); });

    m_flash_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnFlashTimer(); });

    m_filter_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) {
            if (m_search)
                SetFilter(m_search->GetValue());
        });

    m_light_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnLightTimer(); });

    m_all_lights_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnAllLightsTimer(); });

    m_vhf_highlight_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnVhfHighlightTimer(); });

    // Tries again until OpenCPN takes the next beep.
    m_vhf_sound_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        if (m_vhf_beeps_left <= 0) {
            m_vhf_sound_timer.Stop();
            return;
        }
        if (PlayVhfSound())
            --m_vhf_beeps_left;
    });

    m_tap_clock_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { UpdateTapClock(); });

    // Fill the cache once OpenCPN has settled after starting, so the first
    // look at the dynamic legend does not have to wait for it.
    m_prefetch_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) {
            if (!m_loading && m_waypoint_cache.empty())
                StartWaypointLoading(false);
        });
}

legend_pi::~legend_pi()
{
}

int legend_pi::Init()
{
    wxString data_dir = GetPluginDataDir("legend_pi");

    wxString icon_path =
        data_dir +
        wxFileName::GetPathSeparator() +
        "data" +
        wxFileName::GetPathSeparator() +
        "legend_toolbar.png";

    m_toolbar_bitmap = wxBitmap(icon_path, wxBITMAP_TYPE_PNG);

    m_toolbar_item_id = InsertPlugInTool(
        "",
        &m_toolbar_bitmap,
        &m_toolbar_bitmap,
        wxITEM_NORMAL,
        "Legenda",
        "Toon of verberg de legenda",
        nullptr,
        -1,
        0,
        this);

    EnsureDirectories();
    LoadConfig();

    RefreshNotePlaces();
    m_note_notice_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { ShowPendingNoteNotice(); });

    // In the chart's right-click menu: "Toon in legenda" and "Marifoon
    // hier", and "Notitie" with a submenu that PrepareContextMenu() fills
    // each time for the spot and the state.
    m_menu_reveal = AddCanvasContextMenuItem(
        new wxMenuItem(nullptr, wxID_ANY, wxString::FromUTF8("Toon in legenda")), this);
    m_menu_actions.push_back(m_menu_reveal);
    m_menu_vhf = AddCanvasContextMenuItem(
        new wxMenuItem(nullptr, wxID_ANY, wxString::FromUTF8("Marifoon hier")), this);
    m_menu_actions.push_back(m_menu_vhf);

    // OpenCPN tells a plugin of this API version which submenu was used,
    // not which item in it. So every note action is also an item of its
    // own, hidden, whose id the submenu item carries: OpenCPN calls back
    // with that id too (it looks through all items, hidden ones as well).
    auto action = [this](const char *label) {
        const int id = AddCanvasContextMenuItem(
            new wxMenuItem(nullptr, wxID_ANY, wxString::FromUTF8(label)), this);
        SetCanvasContextMenuItemViz(id, false);
        m_menu_actions.push_back(id);
        return id;
    };
    m_menu_new_note = action("Nieuwe notitie hier");
    m_menu_open_note = action("Notitie openen");
    m_menu_delete_note = action("Notitie verwijderen");
    m_menu_link_place = action("Link naar deze plek in de tekst");
    m_menu_pin_note = action("Notitie hier vastpinnen op de kaart");
    m_menu_new_note_boat = action("Nieuwe notitie bij de boot");
    m_menu_link_boat = action("Positie van de boot in de tekst");
    m_menu_pin_boat = action("Vastpinnen op de positie van de boot");
    m_context_submenu = new wxMenu();
    m_menu_parent = AddCanvasContextMenuItem(
        new wxMenuItem(nullptr, wxID_ANY, "Notitie", wxEmptyString, wxITEM_NORMAL,
                       m_context_submenu),
        this);
    PrepareContextMenu(0);

    m_ocharts.SetCacheFile(
        GetConfigDirectory() + wxFileName::GetPathSeparator() +
        "ocharts-cells.txt");
    m_s57.SetCacheFile(
        GetConfigDirectory() + wxFileName::GetPathSeparator() +
        "s57-cells.txt");
    m_s57.SetCatalogue(&m_chart_symbols);

    // OpenCPN's own configuration file, read for its vector display settings.
    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (privateDir && !privateDir->IsEmpty()) {
        m_display_settings.SetFile(
            *privateDir + wxFileName::GetPathSeparator() + "opencpn.ini");
    }

    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
           WANTS_ONPAINT_VIEWPORT | WANTS_OVERLAY_CALLBACK |
           WANTS_OPENGL_OVERLAY_CALLBACK |
           WANTS_DYNAMIC_OPENGL_OVERLAY_CALLBACK | WANTS_MOUSE_EVENTS |
           WANTS_CURSOR_LATLON | INSTALLS_CONTEXTMENU_ITEMS | WANTS_NMEA_EVENTS;
}

bool legend_pi::DeInit()
{
    m_save_timer.Stop();
    if (m_editing)
        SaveNote();
    SaveCurrentMarkdownScroll();
    SaveWindowState();
    SaveConfig();
    StopMarker();
    m_auto_refresh_timer.Stop();
    m_loading_timer.Stop();
    m_prefetch_timer.Stop();
    m_visibility_timer.Stop();
    m_flash_timer.Stop();
    m_filter_timer.Stop();
    m_light_timer.Stop();
    m_light_active = false;
    m_all_lights_timer.Stop();
    m_all_lights_on = false;
    m_vhf_highlight_timer.Stop();
    m_vhf_highlight.clear();
    m_vhf_notice_timer.Stop();
    m_note_notice_timer.Stop();
    m_vhf_sound_timer.Stop();
    m_night_preview = false;
    g_night_preview = false;
    m_tap_clock_timer.Stop();
    m_loading = false;

    // Charts of the o-charts plugin must go before this plugin is unloaded.
    m_ocharts.Close();
    ClearDynamicImages();

    if (m_legend_window) {
        // Delete right away instead of Destroy(): Destroy() postpones the
        // deletion to a later idle moment, when this plugin's code may
        // already be unloaded. Deleting also drops pending CallAfter calls.
        delete m_legend_window;
        m_legend_window = nullptr;
        m_header_book = nullptr;
        m_single_title = nullptr;
        m_legend_position = nullptr;
        m_legend_choice = nullptr;
        m_previous_button = nullptr;
        m_next_button = nullptr;
        m_content_book = nullptr;
        m_image_page = nullptr;
        m_legend_bitmap = nullptr;
        m_markdown_view = nullptr;
        m_search = nullptr;
        m_edit_button = nullptr;
        m_new_button = nullptr;
        m_rename_button = nullptr;
        m_delete_button = nullptr;
        m_format_bar = nullptr;
        m_format_icons.clear();
        m_editor = nullptr;
        m_nav_panel = nullptr;
        m_edit_footer = nullptr;
        m_save_status = nullptr;
        m_place_bar = nullptr;
        m_place_text = nullptr;
    }

    if (g_memoryHandler) {
        wxFileSystem::RemoveHandler(g_memoryHandler);
        delete g_memoryHandler;
        g_memoryHandler = nullptr;
        m_pin_images.clear();
    }

    if (m_toolbar_item_id >= 0) {
        RemovePlugInTool(m_toolbar_item_id);
        m_toolbar_item_id = -1;
    }

    if (m_vhf_notice) {
        delete m_vhf_notice;
        m_vhf_notice = nullptr;
    }
    // OpenCPN owns the menu items it was given, and the submenu with its
    // item; only their places in the menu are given up.
    if (m_menu_parent >= 0) {
        RemoveCanvasContextMenuItem(m_menu_parent);
        m_menu_parent = -1;
    }
    for (int id : m_menu_actions)
        RemoveCanvasContextMenuItem(id);
    m_menu_actions.clear();
    m_context_submenu = nullptr;

    return true;
}

int legend_pi::GetAPIVersionMajor()
{
    return 1;
}

int legend_pi::GetAPIVersionMinor()
{
    return 18;
}

int legend_pi::GetPlugInVersionMajor()
{
    return PLUGIN_VERSION_MAJOR;
}

int legend_pi::GetPlugInVersionMinor()
{
    return PLUGIN_VERSION_MINOR;
}

wxString legend_pi::GetCommonName()
{
    return "Legend";
}

wxString legend_pi::GetShortDescription()
{
    return "Configurable legend for OpenCPN";
}

wxString legend_pi::GetLongDescription()
{
    return "Displays PNG legends and Markdown notes in OpenCPN, plus a dynamic legend of the waypoint symbols visible on screen.";
}

wxString legend_pi::GetLegendDirectory() const
{
    return
        wxStandardPaths::Get().GetDocumentsDir() +
        wxFileName::GetPathSeparator() +
        "OpenCPN" +
        wxFileName::GetPathSeparator() +
        "Legend";
}

wxString legend_pi::GetConfigDirectory() const
{
    // OpenCPN's own private data directory is platform specific and known
    // to OpenCPN itself; use it instead of a hardcoded macOS path.
    wxString base;
    wxString *privateDir = GetpPrivateApplicationDataLocation();

    if (privateDir && !privateDir->IsEmpty())
        base = *privateDir;
    else
        base = wxStandardPaths::Get().GetUserDataDir();

    return
        base +
        wxFileName::GetPathSeparator() +
        "Legend";
}

void legend_pi::EnsureDirectories() const
{
    wxFileName::Mkdir(
        GetLegendDirectory(),
        wxS_DIR_DEFAULT,
        wxPATH_MKDIR_FULL);

    wxFileName::Mkdir(
        GetConfigDirectory(),
        wxS_DIR_DEFAULT,
        wxPATH_MKDIR_FULL);
}

wxString legend_pi::MakeDisplayTitle(const wxString &path) const
{
    if (path == kDynamicGpxId)
        return wxString::FromUTF8("Kaartlegenda (in beeld)");

    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    // For Markdown, use the first non-empty H1 as the visible title.
    // If the document does not start with an H1, fall back to the filename.
    if (ext == "md" || ext == "markdown") {
        wxString text;
        wxFile markdownFile(path);
        if (markdownFile.IsOpened() && markdownFile.ReadAll(&text, wxConvUTF8)) {
            // The block of settings at the top (a note's place) is no title.
            const wxArrayString lines = wxSplit(SplitFrontMatter(text).body, '\n', '\0');
            for (wxString line : lines) {
                line.Trim(true);
                line.Trim(false);

                if (line.IsEmpty())
                    continue;

                if (line.StartsWith("# ") && !line.StartsWith("##")) {
                    wxString title = line.Mid(2);
                    title.Trim(true);
                    title.Trim(false);

                    if (!title.IsEmpty())
                        return title;
                }

                // Only the first non-empty line can act as the document title.
                break;
            }
        }
    }

    wxString title = filename.GetName();
    title.Replace("_", " ");
    title.Replace("-", " ");

    return title;
}

void legend_pi::LoadConfig()
{
    wxString config_path =
        GetConfigDirectory() +
        wxFileName::GetPathSeparator() +
        "legend.ini";

    wxFileConfig config(
        "Legend",
        wxEmptyString,
        config_path,
        wxEmptyString,
        wxCONFIG_USE_LOCAL_FILE);

    config.Read("LastLegend", &m_last_legend_name, "");

    config.Read("WindowX", &m_window_x, -1);
    config.Read("WindowY", &m_window_y, -1);
    config.Read("WindowWidth", &m_window_width, 560);
    config.Read("WindowHeight", &m_window_height, 460);

    if (m_window_width < 380)
        m_window_width = 380;

    if (m_window_height < 300)
        m_window_height = 300;

    config.Read("ShowHiddenAtScale", &m_show_hidden_at_scale, false);
    config.Read("ShowLights", &m_show_lights, true);
    config.Read("VhfKeepLit", &m_vhf_keep_lit, false);
    config.Read("VhfNotice", &m_vhf_notice_on, true);
    config.Read("NoteNotice", &m_note_notice_on, true);
    wxString language;
    config.Read("Language", &language, "nl");
    m_dutch_names = language != "en";

    wxString collapsed;
    config.Read("CollapsedSections", &collapsed, "");
    m_collapsed_sections.clear();
    for (const wxString &name : wxSplit(collapsed, '\t', '\0')) {
        if (!name.IsEmpty())
            m_collapsed_sections.insert(name);
    }
}

void legend_pi::SaveConfig() const
{
    wxString config_path =
        GetConfigDirectory() +
        wxFileName::GetPathSeparator() +
        "legend.ini";

    wxFileConfig config(
        "Legend",
        wxEmptyString,
        config_path,
        wxEmptyString,
        wxCONFIG_USE_LOCAL_FILE);

    config.Write("LastLegend", m_last_legend_name);

    config.Write("WindowX", m_window_x);
    config.Write("WindowY", m_window_y);
    config.Write("WindowWidth", m_window_width);
    config.Write("WindowHeight", m_window_height);

    wxArrayString collapsed;
    for (const wxString &name : m_collapsed_sections)
        collapsed.Add(name);
    config.Write("CollapsedSections", wxJoin(collapsed, '\t', '\0'));
    config.Write("ShowHiddenAtScale", m_show_hidden_at_scale);
    config.Write("ShowLights", m_show_lights);
    config.Write("VhfKeepLit", m_vhf_keep_lit);
    config.Write("VhfNotice", m_vhf_notice_on);
    config.Write("NoteNotice", m_note_notice_on);
    config.Write("Language", m_dutch_names ? "nl" : "en");

    config.Flush();
}

void legend_pi::RestoreWindowState()
{
    if (!m_legend_window)
        return;

    m_legend_window->SetMinSize(wxSize(380, 300));

    wxSize size(m_window_width, m_window_height);

    if (m_window_x >= 0 && m_window_y >= 0) {
        wxPoint pos(m_window_x, m_window_y);

        if (wxDisplay::GetFromPoint(pos) != wxNOT_FOUND) {
            m_legend_window->SetSize(
                m_window_x,
                m_window_y,
                size.GetWidth(),
                size.GetHeight());
            return;
        }
    }

    m_legend_window->SetSize(size);
    m_legend_window->CentreOnParent();
}

void legend_pi::SaveWindowState()
{
    if (!m_legend_window)
        return;

    wxPoint pos = m_legend_window->GetPosition();
    wxSize size = m_legend_window->GetSize();

    m_window_x = pos.x;
    m_window_y = pos.y;
    m_window_width = size.GetWidth();
    m_window_height = size.GetHeight();
}

void legend_pi::ScanLegendFiles()
{
    m_legend_files.Clear();

    wxString legend_dir = GetLegendDirectory();
    wxDir dir(legend_dir);

    if (dir.IsOpened()) {
        wxString filename;
        bool found = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);

        while (found) {
            wxFileName file(filename);
            wxString ext = file.GetExt().Lower();

            if (ext == "png" || ext == "md" || ext == "markdown") {
                m_legend_files.Add(
                    legend_dir +
                    wxFileName::GetPathSeparator() +
                    filename);
            }

            found = dir.GetNext(&filename);
        }

        m_legend_files.Sort();
    }

    // The dynamic legend is always the first item.
    m_legend_files.Insert(kDynamicGpxId, 0);

    // Notes may have been added, renamed or removed: their places too.
    RefreshNotePlaces();
}

void legend_pi::RebuildLegendChoice()
{
    if (!m_header_book || !m_legend_choice ||
        !m_legend_position || !m_single_title)
        return;

    m_legend_choice->Clear();

    const size_t count = m_legend_files.GetCount();
    const bool canNavigate = count > 1;

    if (m_previous_button)
        m_previous_button->Enable(canNavigate);

    if (m_next_button)
        m_next_button->Enable(canNavigate);

    if (count == 0) {
        // No separate header is useful when the folder is empty.
        m_header_book->Hide();

        if (m_header_book->GetParent())
            m_header_book->GetParent()->Layout();

        return;
    }

    m_header_book->Show();

    if (count == 1) {
        // One item: show only its title. No selector and no "1 van 1".
        m_single_title->SetLabel(MakeDisplayTitle(m_legend_files[0]));
        m_header_book->SetSelection(1);

        if (m_header_book->GetParent())
            m_header_book->GetParent()->Layout();

        return;
    }

    // Multiple items: show the selector and position counter.
    for (size_t i = 0; i < count; ++i)
        m_legend_choice->Append(MakeDisplayTitle(m_legend_files[i]));

    m_legend_choice->Enable();
    m_header_book->SetSelection(2);

    if (m_header_book->GetParent())
        m_header_book->GetParent()->Layout();
}

void legend_pi::RefreshLegendFiles()
{
    SaveCurrentMarkdownScroll();

    wxString preferred = m_last_legend_name;

    if (!m_legend_files.IsEmpty() &&
        m_legend_index < m_legend_files.GetCount()) {
        wxFileName current(m_legend_files[m_legend_index]);
        preferred = current.GetFullName();
    }

    ScanLegendFiles();
    RebuildLegendChoice();

    if (m_legend_window)
        m_legend_window->Layout();

    if (m_legend_files.IsEmpty()) {
        ShowEmptyState();
        return;
    }

    size_t index = 0;

    if (!preferred.IsEmpty()) {
        for (size_t i = 0; i < m_legend_files.GetCount(); ++i) {
            wxFileName file(m_legend_files[i]);

            if (file.GetFullName() == preferred) {
                index = i;
                break;
            }
        }
    }

    ShowLegend(index);
}

wxString legend_pi::RenderMarkdownFile(const wxString &path) const
{
    wxFile file(path);

    if (!file.IsOpened())
        return "<html>" + LegendBodyTag() + "<p>Markdown-bestand kon niet worden geopend.</p></body></html>";

    wxFileOffset len = file.Length();

    if (len <= 0)
        return "<html>" + LegendBodyTag() + "<p>Leeg Markdown-bestand.</p></body></html>";

    std::string markdown;
    markdown.resize(static_cast<size_t>(len));

    if (file.Read(&markdown[0], static_cast<size_t>(len)) != len)
        return "<html>" + LegendBodyTag() + "<p>Markdown-bestand kon niet volledig worden gelezen.</p></body></html>";

    // The block with the note's place is not shown as text; a line on top
    // says where the note belongs, as a link that takes the chart there.
    const NoteFrontMatter front = SplitFrontMatter(wxString::FromUTF8(markdown.c_str()));
    if (front.block.IsEmpty())
        return RenderMarkdownText(markdown);

    const wxScopedCharBuffer body = front.body.utf8_str();
    wxString html = RenderMarkdownText(std::string(body.data(), body.length()));
    if (front.hasPlace) {
        const LegendPalette palette = GetLegendPalette();
        wxString radius = wxString::Format("%.2f", front.radius);
        while (radius.EndsWith("0"))
            radius.RemoveLast();
        if (radius.EndsWith("."))
            radius.RemoveLast();
        radius.Replace(".", ",");
        // A tinted bar with a pin, the colours of the bar in the editor: it
        // is about the note, not part of its text.
        const bool dim = IsDimScheme();
        const wxColour fill = dim ? palette.stripe : wxColour(250, 238, 218);
        const wxColour ink = dim ? palette.text : wxColour(99, 56, 6);
        if (!g_memoryHandler) {
            g_memoryHandler = new wxMemoryFSHandler;
            wxFileSystem::AddHandler(g_memoryHandler);
        }
        const wxString pin = "legend_pin_" + HtmlColour(ink).Mid(1) + "_" +
                             HtmlColour(fill).Mid(1) + ".png";
        if (m_pin_images.insert(pin).second) {
            const wxBitmapBundle bundle = NoteToolIcon("place", ink);
            const wxImage small = FlattenImage(bundle.GetBitmap(wxSize(16, 16)).ConvertToImage(), fill);
            const wxImage large = FlattenImage(bundle.GetBitmap(wxSize(32, 32)).ConvertToImage(), fill);
            wxMemoryFSHandler::AddFile(pin, small, wxBITMAP_TYPE_PNG);
            wxMemoryFSHandler::AddFile(pin.Left(pin.length() - 4) + "@2x.png", large,
                                       wxBITMAP_TYPE_PNG);
        }
        wxString line;
        line << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" bgcolor=\""
             << HtmlColour(fill) << "\"><tr><td width=\"20\" valign=\"middle\"><img src=\"memory:"
             << pin << "\"></td><td valign=\"middle\"><font size=\"-1\" color=\"" << HtmlColour(ink)
             << "\">Deze notitie is vastgepind op de kaart: <a href=\"geo:"
             << wxString::FromCDouble(front.lat, 5) << "," << wxString::FromCDouble(front.lon, 5)
             << "\">" << FormatPosition(front.lat, front.lon) << "</a> &middot; straal " << radius
             << " NM</font></td></tr></table><br>";
        const int bodyTag = html.Find("<body");
        const int bodyEnd = bodyTag == wxNOT_FOUND ? wxNOT_FOUND : html.find('>', bodyTag);
        if (bodyEnd != wxNOT_FOUND)
            html = html.Left(bodyEnd + 1) + line + html.Mid(bodyEnd + 1);
    }
    return html;
}

// Markdown text (UTF-8) as the page of the viewer.
wxString legend_pi::RenderMarkdownText(std::string markdown, bool dropTitle) const
{
    // If the first non-empty Markdown line is an H1, it is already used
    // as the document title in the selector/header. Remove that one line
    // from the rendered body to avoid showing the title twice.
    if (dropTitle) {
        size_t pos = 0;

        // Skip UTF-8 BOM if present.
        if (markdown.size() >= 3 &&
            static_cast<unsigned char>(markdown[0]) == 0xEF &&
            static_cast<unsigned char>(markdown[1]) == 0xBB &&
            static_cast<unsigned char>(markdown[2]) == 0xBF) {
            pos = 3;
        }

        while (pos < markdown.size()) {
            size_t lineEnd = markdown.find('\n', pos);
            size_t contentEnd =
                (lineEnd == std::string::npos) ? markdown.size() : lineEnd;

            size_t trimmedStart = pos;
            while (trimmedStart < contentEnd &&
                   (markdown[trimmedStart] == ' ' ||
                    markdown[trimmedStart] == '\t' ||
                    markdown[trimmedStart] == '\r')) {
                ++trimmedStart;
            }

            size_t trimmedEnd = contentEnd;
            while (trimmedEnd > trimmedStart &&
                   (markdown[trimmedEnd - 1] == ' ' ||
                    markdown[trimmedEnd - 1] == '\t' ||
                    markdown[trimmedEnd - 1] == '\r')) {
                --trimmedEnd;
            }

            if (trimmedStart == trimmedEnd) {
                if (lineEnd == std::string::npos)
                    break;

                pos = lineEnd + 1;
                continue;
            }

            const bool isH1 =
                (trimmedEnd - trimmedStart >= 3) &&
                markdown[trimmedStart] == '#' &&
                markdown[trimmedStart + 1] == ' ';

            if (isH1) {
                size_t eraseEnd =
                    (lineEnd == std::string::npos) ? contentEnd : lineEnd + 1;

                markdown.erase(pos, eraseEnd - pos);
            }

            break;
        }
    }

    std::string html;

    unsigned parser_flags =
        MD_DIALECT_GITHUB |
        MD_FLAG_NOHTML;

    int result = md_html(
        markdown.data(),
        static_cast<MD_SIZE>(markdown.size()),
        Md4cOutputCallback,
        &html,
        parser_flags,
        0);

    if (result != 0)
        return "<html>" + LegendBodyTag() + "<p>Markdown kon niet worden verwerkt.</p></body></html>";

    html = ShowTaskLists(html);

    wxString wrapped;
    wrapped << "<html>" << LegendBodyTag()
            << wxString::FromUTF8(html.c_str())
            << "</body></html>";

    return wrapped;
}

void legend_pi::SaveCurrentMarkdownScroll()
{
    if (!m_markdown_view || m_current_legend_path.IsEmpty())
        return;

    wxFileName currentFile(m_current_legend_path);
    wxString ext = currentFile.GetExt().Lower();

    if (ext != "md" && ext != "markdown")
        return;

    int x = 0;
    int y = 0;
    m_markdown_view->GetViewStart(&x, &y);

    m_markdown_scroll_positions[m_current_legend_path] =
        wxPoint(x, y);
}

void legend_pi::RestoreMarkdownScroll(const wxString &path)
{
    if (!m_markdown_view)
        return;

    wxPoint position(0, 0);

    auto it = m_markdown_scroll_positions.find(path);
    if (it != m_markdown_scroll_positions.end())
        position = it->second;

    // SetPage() rebuilds the HTML contents. Restore the scroll position
    // after wxWidgets has had a chance to lay out the new page.
    m_markdown_view->CallAfter(
        [this, path, position]() {
            if (m_markdown_view &&
                m_current_legend_path == path) {
                m_markdown_view->Scroll(
                    position.x,
                    position.y);
            }
        });
}

void legend_pi::ShowEmptyState()
{
    SaveCurrentMarkdownScroll();

    m_legend_index = 0;
    m_last_legend_name.Clear();
    m_current_legend_path.Clear();

    if (m_markdown_view) {
        wxString legend_dir = GetLegendDirectory();
        legend_dir.Replace("&", "&amp;");
        legend_dir.Replace("<", "&lt;");
        legend_dir.Replace(">", "&gt;");

        wxString html;
        html << "<html>" << LegendBodyTag()
             << "<font size=\"+1\">"
             << "<p><b>Geen legenda's of notities gevonden.</b></p>"
             << "<p>Plaats een PNG- of Markdown-bestand in:</p>"
             << "<p><code>" << legend_dir << "</code></p>"
             << "</font>"
             << "</body></html>";

        m_markdown_view->SetPage(html);
    }

    if (m_content_book && m_markdown_view)
        m_content_book->SetSelection(1);

    UpdateSearchVisibility();
}

void legend_pi::UpdateImageScale()
{
    if (!m_image_page || !m_legend_bitmap || !m_current_image.IsOk())
        return;

    wxSize client = m_image_page->GetClientSize();

    // Leave some breathing room around the image.
    const int availableWidth = client.GetWidth() - 20;
    const int availableHeight = client.GetHeight() - 20;

    if (availableWidth <= 0 || availableHeight <= 0)
        return;

    const int imageWidth = m_current_image.GetWidth();
    const int imageHeight = m_current_image.GetHeight();

    if (imageWidth <= 0 || imageHeight <= 0)
        return;

    double scale = 1.0;

    if (imageWidth > availableWidth || imageHeight > availableHeight) {
        const double scaleX =
            static_cast<double>(availableWidth) / imageWidth;
        const double scaleY =
            static_cast<double>(availableHeight) / imageHeight;

        scale = wxMin(scaleX, scaleY);
    }

    int displayWidth =
        static_cast<int>(imageWidth * scale);
    int displayHeight =
        static_cast<int>(imageHeight * scale);

    displayWidth = wxMax(1, displayWidth);
    displayHeight = wxMax(1, displayHeight);

    wxImage displayImage = m_current_image;

    if (displayWidth != imageWidth || displayHeight != imageHeight) {
        displayImage = m_current_image.Scale(
            displayWidth,
            displayHeight,
            wxIMAGE_QUALITY_HIGH);
    }

    displayImage = DimImage(displayImage, SchemeDim());

    m_legend_bitmap->SetBitmap(wxBitmap(displayImage));

    // The image page is a normal panel, not a scrolled window. The bitmap
    // is therefore always constrained to the available view.
    m_image_page->Layout();
}

void legend_pi::ShowLegend(size_t index)
{
    // While a note is edited the window stays on it; a refresh from
    // elsewhere (colours, the folder) only redraws the preview.
    if (m_editing) {
        UpdateNoteButtons();
        return;
    }

    SaveCurrentMarkdownScroll();

    if (m_legend_files.IsEmpty()) {
        ShowEmptyState();
        return;
    }

    if (index >= m_legend_files.GetCount())
        index = 0;

    m_legend_index = index;

    wxString path = m_legend_files[m_legend_index];
    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    m_last_legend_name = filename.GetFullName();

    if (m_legend_files.GetCount() > 1) {
        if (m_legend_position) {
            m_legend_position->SetLabel(
                wxString::Format(
                    "%zu van %zu",
                    m_legend_index + 1,
                    m_legend_files.GetCount()));
        }

        if (m_legend_choice)
            m_legend_choice->SetSelection(static_cast<int>(m_legend_index));
    }

    if (path == kDynamicGpxId) {
        m_current_image = wxImage();
        m_current_legend_path = path;
        UpdateSearchVisibility();
        ShowDynamicLegend();
    }
    else if (ext == "png") {
        wxImage image;

        if (!image.LoadFile(path, wxBITMAP_TYPE_PNG))
            return;

        m_current_legend_path = path;
        m_current_image = image.Copy();

        if (m_content_book)
            m_content_book->SetSelection(0);

        UpdateImageScale();
    }
    else if (ext == "md" || ext == "markdown") {
        m_current_image = wxImage();
        m_current_legend_path = path;

        if (m_markdown_view) {
            auto *markdownWindow =
                static_cast<LegendHtmlWindow *>(m_markdown_view);

            markdownWindow->SetMarkdownPage(
                RenderMarkdownFile(path),
                path);
        }

        if (m_content_book)
            m_content_book->SetSelection(1);

        RestoreMarkdownScroll(path);
    }

    UpdateSearchVisibility();
    UpdateNoteButtons();

    if (m_legend_window)
        m_legend_window->Layout();

    SaveConfig();
}

void legend_pi::OnPreviousLegend(wxCommandEvent &event)
{
    if (m_legend_files.IsEmpty())
        return;

    size_t index =
        (m_legend_index == 0)
            ? m_legend_files.GetCount() - 1
            : m_legend_index - 1;

    ShowLegend(index);
}

void legend_pi::OnNextLegend(wxCommandEvent &event)
{
    if (m_legend_files.IsEmpty())
        return;

    size_t index = m_legend_index + 1;

    if (index >= m_legend_files.GetCount())
        index = 0;

    ShowLegend(index);
}

void legend_pi::OnLegendSelected(wxCommandEvent &event)
{
    if (!m_legend_choice)
        return;

    int selection = m_legend_choice->GetSelection();

    if (selection == wxNOT_FOUND)
        return;

    ShowLegend(static_cast<size_t>(selection));

    // Keep keyboard focus on the selector after a choice has been made.
    // This allows Up/Down to continue moving through the list immediately.
    m_legend_choice->SetFocus();
}

bool legend_pi::IsMarkdownItem(const wxString &path) const
{
    if (path.IsEmpty() || path == kDynamicGpxId)
        return false;
    const wxString ext = wxFileName(path).GetExt().Lower();
    return ext == "md" || ext == "markdown";
}

// Outside editing: the pencil for a note and the plus. While editing: the
// buttons that format the text, and Klaar.
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

void legend_pi::CreateLegendWindow()
{
    if (m_legend_window)
        return;

    m_legend_window = new wxFrame(
        GetOCPNCanvasWindow(),
        wxID_ANY,
        "Legenda",
        wxDefaultPosition,
        wxSize(560, 460),
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);

    auto *panel = new wxPanel(m_legend_window);
    auto *mainSizer = new wxBoxSizer(wxVERTICAL);

    m_header_book = new wxSimplebook(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    // Page 0: empty state. The whole book is hidden when there are no items.
    auto *emptyHeaderPage = new wxPanel(m_header_book);

    // Page 1: exactly one item -> title only.
    auto *singleHeaderPage = new wxPanel(m_header_book);
    auto *singleHeaderSizer = new wxBoxSizer(wxHORIZONTAL);

    m_single_title = new wxStaticText(
        singleHeaderPage,
        wxID_ANY,
        "");

    wxFont singleTitleFont = m_single_title->GetFont();
    singleTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_single_title->SetFont(singleTitleFont);

    singleHeaderSizer->Add(
        m_single_title,
        1,
        wxALIGN_CENTER_VERTICAL);

    singleHeaderPage->SetSizer(singleHeaderSizer);

    // Page 2: multiple items -> selector plus "x van y".
    auto *multiHeaderPage = new wxPanel(m_header_book);
    auto *multiHeaderSizer = new wxBoxSizer(wxHORIZONTAL);

    m_legend_choice = new wxChoice(
        multiHeaderPage,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_legend_choice->SetMinSize(wxSize(160, -1));

    wxFont choiceFont = m_legend_choice->GetFont();
    choiceFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_legend_choice->SetFont(choiceFont);

    m_legend_position = new wxStaticText(
        multiHeaderPage,
        wxID_ANY,
        "");

    // Keep the position counter visible on the right, even in a narrow window.
    m_legend_position->SetMinSize(wxSize(52, -1));

    multiHeaderSizer->Add(
        m_legend_choice,
        1,
        wxRIGHT | wxEXPAND | wxALIGN_CENTER_VERTICAL,
        10);

    multiHeaderSizer->Add(
        m_legend_position,
        0,
        wxALIGN_CENTER_VERTICAL);

    multiHeaderPage->SetSizer(multiHeaderSizer);

    m_header_book->AddPage(emptyHeaderPage, "Leeg");
    m_header_book->AddPage(singleHeaderPage, "Een");
    m_header_book->AddPage(multiHeaderPage, "Meerdere");
    m_header_book->SetSelection(0);
    m_header_book->Hide();

    // The header row: the choice of item, with buttons for notes: edit,
    // rename, delete (into the folder "Verwijderd") and a new one.
    auto *headerRow = new wxBoxSizer(wxHORIZONTAL);
    headerRow->Add(m_header_book, 1, wxALIGN_CENTER_VERTICAL);

    auto noteButton = [&](const wxString &icon, const wxString &tip) {
        auto *button = new wxBitmapButton(panel, wxID_ANY,
                                          NoteToolIcon(icon, GetLegendPalette().text),
                                          wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        button->SetToolTip(tip);
        m_format_icons.push_back(std::make_pair(button, icon));
        headerRow->Add(button, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
        return button;
    };
    headerRow->AddSpacer(4);
    m_edit_button = noteButton("edit", "Deze notitie bewerken");
    m_rename_button = noteButton("rename", "Deze notitie een andere naam geven");
    m_delete_button = noteButton("trash", "Deze notitie verwijderen (naar de map Verwijderd)");
    m_new_button = noteButton("new", "Nieuwe notitie");

    mainSizer->Add(
        headerRow,
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    m_edit_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { StartEditing(); });
    m_rename_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RenameNote(); });
    m_delete_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DeleteNote(); });
    // With a GPS fix the plus offers a note pinned where the boat is.
    m_new_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!BoatPositionKnown()) {
            NewNote();
            return;
        }
        enum { kPlain = 1, kBoat };
        wxMenu menu;
        menu.Append(kPlain, "Nieuwe notitie");
        menu.Append(kBoat, "Nieuwe notitie bij de boot");
        const int choice = m_new_button->GetPopupMenuSelectionFromUser(menu);
        if (choice == kPlain)
            NewNote();
        else if (choice == kBoat)
            NewNoteAtBoat();
    });

    // Buttons that put Markdown into the note, for those who do not know
    // it by heart and on a tablet, where # and * are hard to reach. They
    // wrap to a second line in a narrow window.
    m_format_bar = new wxPanel(panel);
    auto *formatSizer = new wxWrapSizer(wxHORIZONTAL);
    auto formatButton = [&](const wxString &icon, const wxString &tip,
                            std::function<void()> action) {
        auto *button = new wxBitmapButton(m_format_bar, wxID_ANY,
                                          NoteToolIcon(icon, GetLegendPalette().text),
                                          wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        button->SetToolTip(tip);
        if (action)
            button->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
        formatSizer->Add(button, 0, wxRIGHT | wxBOTTOM, 2);
        m_format_icons.push_back(std::make_pair(button, icon));
        return button;
    };
    auto groupGap = [&]() { formatSizer->AddSpacer(8); };

    // H opens a menu with the heading levels; the current one is ticked.
    wxBitmapButton *headingButton = formatButton(
        "heading", "Kopniveau: kop 1 tot en met 6, of gewone tekst", std::function<void()>());
    headingButton->Bind(wxEVT_BUTTON, [this, headingButton](wxCommandEvent &) {
        if (!m_editor)
            return;
        const int current = m_editor->HeadingLevel();
        wxMenu menu;
        menu.AppendRadioItem(100, "Gewone tekst")->Check(current == 0);
        for (int level = 1; level <= 6; ++level)
            menu.AppendRadioItem(100 + level, wxString::Format("Kop %d", level))
                ->Check(current == level);
        const int choice = headingButton->GetPopupMenuSelectionFromUser(menu);
        if (choice >= 100 && choice <= 106)
            m_editor->SetHeading(choice - 100);
    });
    formatButton("bold", "Vet (Cmd+B)", [this]() { m_editor->ToggleBold(); });
    formatButton("italic", "Cursief (Cmd+I)", [this]() { m_editor->ToggleItalic(); });
    groupGap();
    formatButton("bullets", "Opsomming met bolletjes aan of uit",
                 [this]() { m_editor->ToggleList(); });
    formatButton("numbers", "Genummerde lijst aan of uit",
                 [this]() { m_editor->ToggleNumbered(); });
    formatButton("tasks", "Afvinklijst aan of uit; klik op een hokje om af te vinken",
                 [this]() { m_editor->ToggleTasks(); });
    groupGap();
    formatButton("link", "Van de geselecteerde tekst een link maken", [this]() { InsertLink(); });
    formatButton("image", "Een afbeelding uit de Legend-map of daarbuiten",
                 [this]() { InsertImage(); });
    wxBitmapButton *placeButton = formatButton(
        "place",
        "Een plek op de kaart: als link in de tekst, of de hele notitie vastpinnen "
        "op de kaart. Voor een andere plek dan het midden: klik met rechts op de "
        "kaart",
        std::function<void()>());
    placeButton->Bind(wxEVT_BUTTON, [this, placeButton](wxCommandEvent &) {
        if (!m_viewport_valid)
            return;
        enum { kLink = 1, kBelongs, kBoatLink, kBoatPin };
        wxMenu menu;
        menu.Append(kLink, "Link naar het midden van de kaart in de tekst");
        menu.Append(kBelongs, "Notitie vastpinnen op het midden van de kaart");
        menu.AppendSeparator();
        menu.Append(kBoatLink, "Positie van de boot in de tekst");
        menu.Append(kBoatPin, "Notitie vastpinnen op de positie van de boot");
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

    mainSizer->Add(
        m_format_bar,
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    // "Vastgepind op ...": where the note being edited is pinned, with its
    // radius.
    m_place_bar = new wxPanel(panel);
    auto *placeSizer = new wxBoxSizer(wxHORIZONTAL);
    m_place_text = new wxStaticText(m_place_bar, wxID_ANY, wxEmptyString);
    auto *radiusButton = new wxButton(m_place_bar, wxID_ANY, "Straal", wxDefaultPosition,
                                      wxDefaultSize, wxBU_EXACTFIT);
    radiusButton->SetToolTip("Binnen welke afstand je onderweg een melding krijgt");
    auto *looseButton = new wxButton(m_place_bar, wxID_ANY, "Losmaken", wxDefaultPosition,
                                     wxDefaultSize, wxBU_EXACTFIT);
    looseButton->SetToolTip("De notitie niet meer op de kaart vastpinnen");
    placeSizer->Add(m_place_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    placeSizer->Add(radiusButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    placeSizer->Add(looseButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_place_bar->SetSizer(placeSizer);
    m_place_bar->Hide();
    m_place_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        if (m_note_has_place)
            GoToPlace(m_note_lat, m_note_lon);
    });
    radiusButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { AskNoteRadius(); });
    looseButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_note_has_place = false;
        NotePlaceChanged();
    });

    mainSizer->Add(
        m_place_bar,
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    m_legend_choice->Bind(
        wxEVT_CHOICE,
        &legend_pi::OnLegendSelected,
        this);

    // Search field of the dynamic legend; hidden for other legend items.
    // The list follows the text after a short pause in typing, Enter
    // applies it at once.
    m_search = new wxSearchCtrl(
        panel,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_PROCESS_ENTER);
    m_search->ShowCancelButton(true);
    m_search->SetDescriptiveText("Zoek in de legenda");
    m_search->SetToolTip(
        "Toont alleen de symbolen waarvan de naam, het kopje of de icoonnaam "
        "alle getypte woorden bevat (Cmd+F)");
    m_search->Hide();

    mainSizer->Add(
        m_search,
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    m_search->Bind(
        wxEVT_TEXT,
        [this](wxCommandEvent &event) {
            event.Skip();
            m_filter_timer.StartOnce(250);
        });

    m_search->Bind(
        wxEVT_TEXT_ENTER,
        [this](wxCommandEvent &) { SetFilter(m_search->GetValue()); });

    m_search->Bind(
        wxEVT_SEARCHCTRL_SEARCH_BTN,
        [this](wxCommandEvent &) { SetFilter(m_search->GetValue()); });

    m_search->Bind(
        wxEVT_SEARCHCTRL_CANCEL_BTN,
        [this](wxCommandEvent &) { SetFilter(wxEmptyString); });

    m_content_book = new wxSimplebook(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_image_page = new wxPanel(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_image_page->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent &event) {
            event.Skip();
            UpdateImageScale();
        });

    auto *imageSizer = new wxBoxSizer(wxVERTICAL);

    m_legend_bitmap = new wxStaticBitmap(
        m_image_page,
        wxID_ANY,
        wxBitmap(1, 1));

    imageSizer->Add(
        m_legend_bitmap,
        0,
        wxALL | wxALIGN_CENTER,
        10);

    m_image_page->SetSizer(imageSizer);

    TapClockModule clockModule;
    wxHtmlWinParser::AddModule(&clockModule);
    m_markdown_view = new LegendHtmlWindow(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxHW_SCROLLBAR_AUTO);
    wxHtmlWinParser::RemoveModule(&clockModule);

    // The rhythms of lights are images; in a narrow window they are drawn
    // narrower, so a row never needs more width than the window has.
    m_markdown_view->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent &event) {
            event.Skip();
            if (IsDynamicLegendVisible() && RhythmWidth() != m_rhythm_width)
                m_markdown_view->CallAfter([this]() { RenderDynamicLegend(true); });
        });

    // Give Markdown some breathing room without using extra layout rows.
    m_markdown_view->SetBorders(12);

    // Use the surrounding OpenCPN UI font as the basis for Markdown.
    // The seven values correspond to HTML font sizes 1 through 7.
    const int baseFontSize =
        wxMax(11, panel->GetFont().GetPointSize());

    int htmlFontSizes[7] = {
        wxMax(9, baseFontSize - 2),
        wxMax(10, baseFontSize - 1),
        baseFontSize,
        baseFontSize + 2,
        baseFontSize + 4,
        baseFontSize + 7,
        baseFontSize + 10
    };

    wxString normalFace = panel->GetFont().GetFaceName();

    wxFont fixedFont(
        baseFontSize,
        wxFONTFAMILY_TELETYPE,
        wxFONTSTYLE_NORMAL,
        wxFONTWEIGHT_NORMAL);

    m_markdown_view->SetFonts(
        normalFace,
        fixedFont.GetFaceName(),
        htmlFontSizes);

    ApplyViewColours();

    // Open normal external Markdown links in the system browser.
    m_markdown_view->Bind(
        wxEVT_HTML_LINK_CLICKED,
        [this](wxHtmlLinkEvent &event) {
            wxString href = event.GetLinkInfo().GetHref();

            if (href.StartsWith("legend-reorder-done:")) {
                m_markdown_view->CallAfter([this]() { SetReorderMode(false); });
                return;
            }

            if (href.StartsWith("legend-identify-")) {
                const wxString what = href.Mid(16).BeforeFirst(':');
                m_markdown_view->CallAfter([this, what]() { RunIdentifyCommand(what); });
                return;
            }

            if (href.StartsWith("legend-settings:")) {
                m_markdown_view->CallAfter([this]() { ShowSettingsMenu(); });
                return;
            }

            // The settings, also reachable as links: "legend-<command>:".
            for (const char *command : {"reorder", "collapse-all", "expand-all", "language",
                                        "lights", "all-lights", "hidden"}) {
                if (href == wxString("legend-") + command + ":") {
                    const wxString name(command);
                    m_markdown_view->CallAfter([this, name]() { RunLegendCommand(name); });
                    return;
                }
            }

            if (href.StartsWith("legend-refresh:")) {
                m_markdown_view->CallAfter([this]() {
                    m_ocharts.Rescan();
                    m_s57.Rescan();
                    m_vhf_loaded = false;
                    LoadChartNames();
                    ShowDynamicLegend(true);
                });
                return;
            }

            // A place in a note: the chart goes there and marks it.
            if (href.StartsWith("geo:")) {
                double lat = 0.0;
                double lon = 0.0;
                if (ParseGeoUri(href, &lat, &lon))
                    m_markdown_view->CallAfter([this, lat, lon]() { GoToPlace(lat, lon); });
                return;
            }

            if (href.StartsWith("http://") ||
                href.StartsWith("https://") ||
                href.StartsWith("mailto:") ||
                href.StartsWith("tel:")) {
                wxLaunchDefaultBrowser(href);
                return;
            }

            event.Skip();
        });

    // Whole rows of the dynamic legend are clickable, not only the text.
    // A press and release on the same row counts as a click; dragging to
    // select text does not.
    // Headings work the same way and fold their group in or out; in the
    // order edit mode the arrow buttons move a heading.
    enum class HitKind { None, Row, All, Section, MoveUp, MoveDown, Ahead, Tip, Tap, Match, Button, AheadVhf,
                         VhfArea, AheadNote, Note };
    struct Hit
    {
        HitKind kind = HitKind::None;
        int index = -1;
        bool operator==(const Hit &other) const
        {
            return kind == other.kind && index == other.index;
        }
    };

    auto *legendView = static_cast<LegendHtmlWindow *>(m_markdown_view);

    auto hitAt = [this, legendView](const wxPoint &pos) {
        Hit hit;
        if (m_current_legend_path != kDynamicGpxId)
            return hit;

        const std::pair<HitKind, int> candidates[] = {
            {HitKind::MoveUp, legendView->IdNumberAt(pos, kLegendMoveUpIdPrefix)},
            {HitKind::MoveDown,
             legendView->IdNumberAt(pos, kLegendMoveDownIdPrefix)},
            {HitKind::Tap, legendView->IdNumberAt(pos, kLegendTapIdPrefix)},
            {HitKind::Match, legendView->IdNumberAt(pos, kLegendMatchIdPrefix)},
            {HitKind::Button, legendView->IdNumberAt(pos, kLegendButtonIdPrefix)},
            {HitKind::Tip, legendView->IdNumberAt(pos, kLegendTipIdPrefix)},
            {HitKind::AheadVhf, legendView->IdNumberAt(pos, kLegendAheadVhfIdPrefix)},
            {HitKind::AheadNote, legendView->IdNumberAt(pos, kLegendAheadNoteIdPrefix)},
            {HitKind::Note, legendView->IdNumberAt(pos, kLegendNoteIdPrefix)},
            {HitKind::VhfArea, legendView->IdNumberAt(pos, kLegendVhfAreaIdPrefix)},
            {HitKind::Ahead, legendView->IdNumberAt(pos, kLegendAheadIdPrefix)},
            {HitKind::All, legendView->IdNumberAt(pos, kLegendAllIdPrefix)},
            {HitKind::Row, legendView->RowAt(pos)},
            {HitKind::Section, legendView->SectionAt(pos)}};

        for (const auto &candidate : candidates) {
            if (candidate.second >= 0) {
                hit.kind = candidate.first;
                hit.index = candidate.second;
                break;
            }
        }
        return hit;
    };

    auto pressed = std::make_shared<Hit>();

    m_markdown_view->Bind(
        wxEVT_LEFT_DOWN,
        [this, hitAt, pressed](wxMouseEvent &event) {
            *pressed = hitAt(event.GetPosition());
            // The pad of "Licht herkennen": the light shines while held.
            if (pressed->kind == HitKind::Tap) {
                TapDown();
                *pressed = Hit();
                return;
            }
            event.Skip();
        });

    m_markdown_view->Bind(
        wxEVT_LEFT_UP,
        [this, hitAt, pressed](wxMouseEvent &event) {
            event.Skip();

            if (m_tap_down) {
                TapUp();
                return;
            }

            const Hit hit = *pressed;
            *pressed = Hit();

            if (hit.kind == HitKind::None || !(hitAt(event.GetPosition()) == hit))
                return;

            m_markdown_view->CallAfter([this, hit]() {
                switch (hit.kind) {
                case HitKind::Row:
                    GoToDynamicGroup(static_cast<size_t>(hit.index));
                    break;
                case HitKind::All:
                    MarkAllOfDynamicGroup(static_cast<size_t>(hit.index));
                    break;
                case HitKind::Section:
                    ToggleSection(static_cast<size_t>(hit.index));
                    break;
                case HitKind::MoveUp:
                    MoveSection(hit.index, -1);
                    break;
                case HitKind::MoveDown:
                    MoveSection(hit.index, +1);
                    break;
                case HitKind::Ahead:
                    ShowAheadItem(static_cast<size_t>(hit.index));
                    break;
                case HitKind::AheadNote:
                    if (static_cast<size_t>(hit.index) < m_ahead_notes.size())
                        OpenNote(m_ahead_notes[hit.index].path);
                    break;
                case HitKind::Note:
                    if (static_cast<size_t>(hit.index) < m_note_rows.size() &&
                        m_note_rows[hit.index] < m_note_places.size()) {
                        const size_t index = m_note_rows[hit.index];
                        const NotePlace place = m_note_places[index];
                        // Hidden at this scale: zoom in to just past its limit
                        // and show its sign, as for a hidden buoy.
                        if (NoteHiddenAtScale(place)) {
                            const double zoom = ViewScaleForLimit(place.scamin);
                            if (zoom > 0.0)
                                JumpTo(place.lat, place.lon, zoom);
                            m_note_selected = static_cast<int>(index);
                            ShowMarker(place.lat, place.lon);
                            RefreshAllCanvases();
                        }
                        else {
                            OpenNote(place.path);
                        }
                    }
                    break;
                case HitKind::AheadVhf:
                    if (static_cast<size_t>(hit.index) < m_ahead_vhf.size()) {
                        const AheadVhf &next = m_ahead_vhf[hit.index];
                        ShowVhfHighlight(VhfAreasWithParts(next.lat, next.lon));
                    }
                    break;
                case HitKind::VhfArea:
                    if (static_cast<size_t>(hit.index) < m_vhf_rows.size()) {
                        std::vector<VhfArea> parts;
                        for (const VhfArea &part : m_vhf_areas.All())
                            if (part.description == m_vhf_rows[hit.index].description)
                                parts.push_back(part);
                        ShowVhfHighlight(parts);
                    }
                    break;
                case HitKind::Match:
                    if (static_cast<size_t>(hit.index) < m_identify_matches.size())
                        GoToDynamicGroup(m_identify_matches[hit.index]);
                    break;
                case HitKind::Button: {
                    static const char *const colours[] = {"any", "W", "R", "G", "Y"};
                    if (hit.index == 0)
                        RunIdentifyCommand("reset");
                    else if (hit.index == 1)
                        RunIdentifyCommand("stop");
                    else if (hit.index >= 10 && hit.index < 15)
                        RunIdentifyCommand(wxString("colour-") + colours[hit.index - 10]);
                    break;
                }
                case HitKind::Tap:
                case HitKind::Tip: // the link inside does the work
                case HitKind::None:
                    break;
                }
            });
        });

    // Tooltips explain what a click does; they change only when the mouse
    // moves to another kind of target, so they do not flicker.
    auto shownTip = std::make_shared<wxString>();

    m_markdown_view->Bind(
        wxEVT_MOTION,
        [this, hitAt, legendView, shownTip](wxMouseEvent &event) {
            const Hit hit = hitAt(event.GetPosition());
            legendView->SetRowCursor(hit.kind != HitKind::None);

            wxString tip;
            switch (hit.kind) {
            case HitKind::All:
                tip = "Alle exemplaren in beeld tegelijk aanwijzen";
                if (hit.index >= 0 &&
                    static_cast<size_t>(hit.index) < m_dynamic_groups.size() &&
                    m_dynamic_groups[hit.index].lightCharacter)
                    tip = "Alle lichten met dit karakter in beeld laten branden, "
                          "in hun ritme";
                break;
            case HitKind::Row:
                tip = "Naar het dichtstbijzijnde exemplaar; nog een klik gaat "
                      "naar het volgende";
                // The symbol name is what symbolen.txt needs for a chart
                // symbol; the list itself shows only the description.
                if (hit.index >= 0 &&
                    static_cast<size_t>(hit.index) < m_dynamic_groups.size()) {
                    const DynamicGroup &group = m_dynamic_groups[hit.index];
                    if (group.hiddenAtScale) {
                        tip = "Op deze schaal niet getekend";
                        if (group.visibleFrom > 0)
                            tip << ", zichtbaar vanaf " << FormatScaleShort(group.visibleFrom);
                        tip << "; een klik zoomt in tot het dichtstbijzijnde "
                               "exemplaar zichtbaar is";
                    }
                    if (group.chartSymbol)
                        tip = "Kaartsymbool " + group.symbol + ". " + tip;
                    if (group.lightCharacter)
                        tip = "Het dichtstbijzijnde licht met dit karakter gaat op "
                              "de kaart branden in zijn ritme; nog een klik gaat "
                              "naar het volgende. Periode en groepen komen uit de "
                              "kaart, de duur van een schittering of verduistering "
                              "is een gangbare waarde";
                }
                break;
            case HitKind::Section:
                tip = hit.index >= 0 &&
                              static_cast<size_t>(hit.index) <
                                  m_rendered_headings.size() &&
                              m_collapsed_sections.count(
                                  m_rendered_headings[hit.index]) > 0
                          ? "Groep uitklappen"
                          : "Groep inklappen";
                break;
            case HitKind::MoveUp:
                tip = "Kopje omhoog";
                break;
            case HitKind::MoveDown:
                tip = "Kopje omlaag";
                break;
            case HitKind::Ahead:
                tip = "Op de kaart aanwijzen";
                break;
            case HitKind::AheadVhf:
            case HitKind::VhfArea:
                tip = "Het marifoongebied op de kaart laten oplichten";
                break;
            case HitKind::AheadNote:
            case HitKind::Note:
                tip = "De notitie openen";
                break;
            case HitKind::Tap:
                tip = "Ingedrukt houden zolang het licht brandt (of de spatiebalk)";
                break;
            case HitKind::Match:
                tip = "Dit licht op de kaart laten branden";
                break;
            case HitKind::Button:
                tip = hit.index == 0   ? "Het getikte ritme weggooien en opnieuw beginnen"
                      : hit.index == 1 ? "Licht herkennen sluiten; het menu opent het weer"
                                       : "Alleen lichten van deze kleur vergelijken";
                break;
            case HitKind::Tip:
                tip = hit.index == 0 ? "Instellingen: wat de legenda toont"
                                     : "Ververs: kaarten en lagen opnieuw inlezen";
                break;
            case HitKind::None:
                break;
            }

            if (tip != *shownTip) {
                *shownTip = tip;
                if (tip.IsEmpty())
                    m_markdown_view->UnsetToolTip();
                else
                    m_markdown_view->SetToolTip(tip);
            }

            event.Skip();
        });

    m_markdown_view->Bind(
        wxEVT_LEAVE_WINDOW,
        [this, legendView, shownTip](wxMouseEvent &event) {
            // A press on the pad that ends outside the window ends there.
            if (m_tap_down)
                TapUp();
            legendView->SetRowCursor(false);
            if (!shownTip->IsEmpty()) {
                shownTip->Clear();
                m_markdown_view->UnsetToolTip();
            }
            event.Skip();
        });

    // A note while it is edited, as it looks; saved as Markdown a moment
    // after typing stops.
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
            m_save_status->SetLabel(wxString::FromUTF8("Nog niet bewaard\u2026"));
        m_save_timer.StartOnce(1500);
    });

    m_content_book->AddPage(m_image_page, "Afbeelding");
    m_content_book->AddPage(m_markdown_view, "Markdown");
    m_content_book->AddPage(m_editor, "Notitie");

    mainSizer->Add(
        m_content_book,
        1,
        wxEXPAND | wxALL,
        8);

    auto *separator = new wxStaticLine(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxLI_HORIZONTAL);

    mainSizer->Add(
        separator,
        0,
        wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    auto *buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    m_nav_panel = new wxPanel(panel);

    m_previous_button =
        new wxButton(m_nav_panel, wxID_ANY, wxString::FromUTF8("‹  Vorige"));

    m_next_button =
        new wxButton(m_nav_panel, wxID_ANY, wxString::FromUTF8("Volgende  ›"));

    m_previous_button->SetMinSize(wxSize(110, 36));
    m_next_button->SetMinSize(wxSize(110, 36));

    m_previous_button->SetToolTip("Toon de vorige legenda of notitie");
    m_next_button->SetToolTip("Toon de volgende legenda of notitie");

    buttonSizer->Add(m_previous_button, 0, wxRIGHT, 8);
    buttonSizer->Add(m_next_button, 0, wxLEFT, 8);
    m_nav_panel->SetSizer(buttonSizer);

    mainSizer->Add(
        m_nav_panel,
        0,
        wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    // While a note is edited: whether it has been saved, and Klaar.
    m_edit_footer = new wxPanel(panel);
    auto *footerSizer = new wxBoxSizer(wxHORIZONTAL);
    m_save_status = new wxStaticText(m_edit_footer, wxID_ANY, "Bewaard");
    auto *doneButton = new wxButton(m_edit_footer, wxID_ANY, "Klaar");
    doneButton->SetMinSize(wxSize(110, 36));
    doneButton->SetToolTip("Klaar met bewerken (Esc)");
    footerSizer->Add(m_save_status, 1, wxALIGN_CENTER_VERTICAL);
    footerSizer->Add(doneButton, 0, wxALIGN_CENTER_VERTICAL);
    m_edit_footer->SetSizer(footerSizer);
    m_edit_footer->Hide();
    doneButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { StopEditing(); });

    mainSizer->Add(
        m_edit_footer,
        0,
        wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    m_save_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { SaveNote(); });

    m_previous_button->Bind(
        wxEVT_BUTTON,
        &legend_pi::OnPreviousLegend,
        this);

    m_next_button->Bind(
        wxEVT_BUTTON,
        &legend_pi::OnNextLegend,
        this);

    // Until the first scan completes, there is nothing to navigate.
    m_previous_button->Disable();
    m_next_button->Disable();

    panel->SetSizer(mainSizer);

    RestoreWindowState();

    m_legend_window->Bind(
        wxEVT_CLOSE_WINDOW,
        [this](wxCloseEvent &event) {
            if (m_editing)
                StopEditing();
            SaveCurrentMarkdownScroll();
            SaveWindowState();
            SaveConfig();
            m_legend_window->Hide();
            event.Veto();
        });

    // Switching macOS between light and dark: repaint with the new palette.
    m_legend_window->Bind(
        wxEVT_SYS_COLOUR_CHANGED,
        [this](wxSysColourChangedEvent &event) {
            event.Skip();
            m_legend_window->CallAfter([this]() {
                ApplyViewColours();

                if (m_legend_window && m_legend_window->IsShown() &&
                    !m_legend_files.IsEmpty())
                    ShowLegend(m_legend_index);
            });
        });

    // Letting go of the space bar ends a tap; key-up events go to the window
    // with the focus, so every window of the legend listens.
    auto spaceUp = [this](wxKeyEvent &event) {
        if (m_tap_down && event.GetKeyCode() == WXK_SPACE) {
            TapUp();
            return;
        }
        event.Skip();
    };
    m_markdown_view->Bind(wxEVT_KEY_UP, spaceUp);
    m_legend_choice->Bind(wxEVT_KEY_UP, spaceUp);
    m_legend_window->Bind(wxEVT_KEY_UP, spaceUp);

    m_legend_window->Bind(
        wxEVT_CHAR_HOOK,
        [this](wxKeyEvent &event) {
            const bool plainKey =
                !event.ControlDown() &&
                !event.AltDown() &&
                !event.MetaDown();

            // While a note is edited the keys are for the text: Escape and
            // Cmd+B are the only ones the legend takes.
            if (m_editing) {
                if (event.GetKeyCode() == WXK_ESCAPE && plainKey) {
                    StopEditing();
                    return;
                }
                if (event.GetModifiers() == wxMOD_CMD && m_editor) {
                    if (event.GetKeyCode() == 'B') {
                        m_editor->ToggleBold();
                        return;
                    }
                    if (event.GetKeyCode() == 'I') {
                        m_editor->ToggleItalic();
                        return;
                    }
                }
                event.Skip();
                return;
            }

            // The space bar is the pad of "Licht herkennen", outside the
            // search field.
            if (m_identify_on && event.GetKeyCode() == WXK_SPACE && plainKey &&
                !(m_search && wxWindow::FindFocus() == m_search)) {
                if (!m_tap_down)
                    TapDown();
                return;
            }

            // Cmd+F (Ctrl+F elsewhere) goes to the search field.
            if (event.GetKeyCode() == 'F' && event.GetModifiers() == wxMOD_CMD &&
                m_search && m_search->IsShown()) {
                m_search->SetFocus();
                m_search->SelectAll();
                return;
            }

            // In the search field the arrows move the cursor, and Escape
            // first empties the field before it closes the window.
            wxWindow *focus = wxWindow::FindFocus();
            const bool inSearch =
                m_search && focus &&
                (focus == m_search || focus->GetParent() == m_search);

            if (inSearch && plainKey &&
                (event.GetKeyCode() == WXK_LEFT || event.GetKeyCode() == WXK_RIGHT)) {
                event.Skip();
                return;
            }

            if (inSearch && plainKey && event.GetKeyCode() == WXK_ESCAPE &&
                !m_search->GetValue().IsEmpty()) {
                SetFilter(wxEmptyString);
                return;
            }

            if (plainKey &&
                event.GetKeyCode() == WXK_ESCAPE) {
                SaveCurrentMarkdownScroll();
                SaveWindowState();
                SaveConfig();
                m_legend_window->Hide();
                return;
            }

            if (plainKey &&
                m_legend_files.GetCount() > 1) {

                if (event.GetKeyCode() == WXK_LEFT) {
                    wxCommandEvent commandEvent;
                    OnPreviousLegend(commandEvent);
                    return;
                }

                if (event.GetKeyCode() == WXK_RIGHT) {
                    wxCommandEvent commandEvent;
                    OnNextLegend(commandEvent);
                    return;
                }
            }

            event.Skip();
        });
}

void legend_pi::ApplyViewColours()
{
    if (!m_markdown_view)
        return;

    const LegendPalette palette = GetLegendPalette();
    const bool dim = IsDimScheme();

    // The window around the list: panels and labels take the colours of
    // the list in dusk and night and get their normal colours back by day.
    // Buttons and the choice follow the window appearance.
    if (m_legend_window) {
        SetWindowDarkAppearance(m_legend_window, dim);

        std::function<void(wxWindow *)> paint = [&](wxWindow *window) {
            if (window == m_markdown_view || window == m_image_page)
                return;

            const bool plain = window == m_legend_window ||
                               wxDynamicCast(window, wxPanel) ||
                               wxDynamicCast(window, wxSimplebook) ||
                               wxDynamicCast(window, wxStaticText);
            if (plain) {
                window->SetBackgroundColour(
                    dim ? palette.background : wxNullColour);
                window->SetForegroundColour(dim ? palette.text : wxNullColour);
            }

            for (wxWindow *child : window->GetChildren())
                paint(child);
        };

        paint(m_legend_window);
        m_legend_window->Refresh();
    }
    m_markdown_view->SetBackgroundColour(palette.background);
    m_markdown_view->SetForegroundColour(palette.text);
    m_markdown_view->Refresh();

    for (const auto &icon : m_format_icons)
        icon.first->SetBitmap(NoteToolIcon(icon.second, palette.text));

    if (m_editor) {
        m_editor->SetColours(palette.background, palette.text,
                             palette.link == palette.text ? palette.muted : palette.link);
    }

    // The page of PNG legends darkens along in dusk and night; by day it
    // keeps the normal window colour.
    if (m_image_page) {
        m_image_page->SetBackgroundColour(
            IsDimScheme() ? palette.background : wxNullColour);
        m_image_page->Refresh();
    }
}

void legend_pi::ToggleLegendWindow()
{
    CreateLegendWindow();

    if (m_legend_window->IsShown()) {
        if (m_editing)
            StopEditing();
        SaveCurrentMarkdownScroll();
        SaveWindowState();
        SaveConfig();
        m_legend_window->Hide();
    } else {
        RefreshLegendFiles();
        m_legend_window->Show();
        m_legend_window->Raise();
    }
}

void legend_pi::SetColorScheme(PI_ColorScheme cs)
{
    if (cs == g_color_scheme)
        return;

    g_color_scheme = cs;
    m_ocharts.SetColorScheme(cs);

    const LegendPalette palette = GetLegendPalette();
    wxLogMessage(
        "Legend: colour scheme %s, background %s, text %s",
        SchemeName(cs),
        HtmlColour(palette.background),
        HtmlColour(palette.text));

    ApplyViewColours();

    if (m_legend_window && m_legend_window->IsShown() &&
        !m_legend_files.IsEmpty())
        ShowLegend(m_legend_index);

    if (m_marker_active)
        RefreshAllCanvases();
}

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

    // The sign of a note: the note opens and its radius shows.
    const int note = NotePlaceAt(pixel, 12.0 * ratio);
    if (note >= 0) {
        m_note_selected = note;
        RefreshAllCanvases();
        const wxString path = m_note_places[note].path;
        if (wxWindow *canvas = GetOCPNCanvasWindow())
            canvas->CallAfter([this, path]() { OpenNote(path); });
        return false;
    }
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

void legend_pi::OnToolbarToolCallback(int id)
{
    if (id == m_toolbar_item_id)
        ToggleLegendWindow();
}


void legend_pi::SetCurrentViewPort(PlugIn_ViewPort &vp)
{
    m_viewport = vp;
    m_viewport_valid = (vp.pix_width > 0 && vp.pix_height > 0);

    if (m_log_next_viewport && m_viewport_valid &&
        vp.chart_scale != m_logged_chart_scale) {
        m_log_next_viewport = false;
        wxLogMessage(
            "Legend: viewport after zoom: displayed scale 1:%.0f, chart scale "
            "1:%.0f, ppm %.6f, %dx%d px",
            DisplayedScale(vp), static_cast<double>(vp.chart_scale),
            vp.view_scale_ppm, vp.pix_width, vp.pix_height);
    }

    if (m_viewport_valid && !m_prefetch_scheduled) {
        m_prefetch_scheduled = true;
        m_prefetch_timer.StartOnce(5000);
    }

    if (!m_viewport_valid || !IsDynamicLegendVisible())
        return;

    // OpenCPN calls this on every repaint, also while the marker animates.
    // Only a real change of position, zoom, rotation or window size counts.
    const bool changed =
        !m_collected_viewport_valid || ViewportMoved(vp, m_collected_viewport);

    // Wait until the chart has stopped moving before rebuilding the list.
    // The wait restarts only while the chart keeps moving: repaints of a
    // still chart (the marker animation) must not keep postponing it.
    if (changed) {
        if (!m_auto_refresh_timer.IsRunning() ||
            ViewportMoved(vp, m_pending_viewport)) {
            m_pending_viewport = vp;
            m_auto_refresh_timer.StartOnce(400);
        }
        return;
    }

    // The chart object scale factor was changed in the options: redraw the
    // list so its icons follow.
    const double scale = GetOCPNChartScaleFactor_Plugin();
    if (scale > 0.0 &&
        std::fabs(std::min(4.0, std::max(0.5, static_cast<double>(scale))) -
                  m_rendered_icon_scale) > 1e-3 &&
        !m_showing_loading_page)
        RenderDynamicLegend(true);

    // The vector display settings of OpenCPN changed (category, object
    // classes, symbol style): the chart symbols in the list follow.
    const int plibHash = PI_GetPLIBStateHash();
    if (plibHash != m_plib_state_hash) {
        const bool first = m_plib_state_hash == 0;
        m_plib_state_hash = plibHash;
        if (!first && !m_showing_loading_page) {
            m_display_settings.Refresh(true);
            RefreshDynamicLegendKeepingState();
            return;
        }
    }

    // A repaint without movement may come from hiding or showing a layer.
    // Not restarted on every repaint, so the marker animation cannot keep
    // postponing the check.
    if (!m_visibility_timer.IsRunning())
        m_visibility_timer.StartOnce(700);
}

void legend_pi::CheckVisibilityChanges()
{
    // OpenCPN wrote new display settings to its configuration file.
    if (IsDynamicLegendVisible() && !m_loading && !m_showing_loading_page &&
        m_display_settings.Refresh()) {
        RefreshDynamicLegendKeepingState();
        return;
    }

    if (!IsDynamicLegendVisible() || m_loading || !m_viewport_valid ||
        m_waypoint_cache.empty() ||
        m_waypoint_cache.size() != m_waypoint_guids.GetCount())
        return;

    const PlugIn_ViewPort &vp = m_viewport;

    auto lonInView = [&vp](double lon) {
        for (double offset : {0.0, 360.0, -360.0}) {
            const double l = lon + offset;
            if (l >= vp.lon_min && l <= vp.lon_max)
                return true;
        }
        return false;
    };

    // Waypoints of one layer are next to each other in OpenCPN's list.
    // Probe the first waypoint in view of every unbroken run, visible or
    // not: hiding or showing a layer changes all of its waypoints at once.
    std::vector<size_t> probes;
    size_t previous = static_cast<size_t>(-1);

    for (size_t i = 0; i < m_waypoint_cache.size(); ++i) {
        const CachedWaypoint &wp = m_waypoint_cache[i];

        if (wp.lat < vp.lat_min || wp.lat > vp.lat_max || !lonInView(wp.lon))
            continue;

        if (previous == static_cast<size_t>(-1) || i != previous + 1)
            probes.push_back(i);

        previous = i;
    }

    // Every probe asks OpenCPN for one waypoint; keep the total small.
    const size_t kMaxProbes = 200;
    if (probes.size() > kMaxProbes) {
        std::vector<size_t> spread;
        for (size_t k = 0; k < kMaxProbes; ++k)
            spread.push_back(probes[k * probes.size() / kMaxProbes]);
        probes.swap(spread);
    }

    for (size_t index : probes) {
        std::unique_ptr<PlugIn_Waypoint_Ex> wp =
            GetWaypointEx_Plugin(m_waypoint_guids[index]);

        const bool visible = wp && wp->IsVisible;

        if (visible != m_waypoint_cache[index].visible) {
            wxLogMessage(
                "Legend: waypoint visibility changed, reloading waypoints");

            // The list stays in place while loading; its end refreshes it.
            StartWaypointLoading(true);
            return;
        }
    }
}

bool legend_pi::ViewportMoved(const PlugIn_ViewPort &a, const PlugIn_ViewPort &b)
{
    return std::fabs(a.clat - b.clat) > 1e-7 ||
           std::fabs(a.clon - b.clon) > 1e-7 ||
           std::fabs(a.view_scale_ppm - b.view_scale_ppm) >
               1e-6 * b.view_scale_ppm ||
           std::fabs(a.rotation - b.rotation) > 1e-4 ||
           a.pix_width != b.pix_width || a.pix_height != b.pix_height;
}

bool legend_pi::IsDynamicLegendVisible() const
{
    return m_legend_window &&
           m_legend_window->IsShown() &&
           m_current_legend_path == kDynamicGpxId;
}

void legend_pi::OnAutoRefreshTimer()
{
    if (!IsDynamicLegendVisible())
        return;

    // When new waypoints still have to be fetched, the end of the loading
    // refreshes the list.
    if (!StartWaypointLoading(false))
        return;

    RefreshDynamicLegendKeepingState();
}

void legend_pi::RefreshDynamicLegendKeepingState()
{
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

    size_t missing = 0;
    for (size_t i = 0; i < guids.GetCount(); ++i) {
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

    // Work for a short moment per step, then give OpenCPN room to paint
    // and handle input.
    wxStopWatch step;

    while (m_loading_next < m_loading_guids.GetCount() &&
           step.Time() < 40) {
        const wxString &guid = m_loading_guids[m_loading_next++];

        auto found = m_loading_known.find(guid);
        if (found != m_loading_known.end()) {
            m_loading_cache.push_back(m_waypoint_cache[found->second]);
            continue;
        }

        CachedWaypoint entry;
        std::unique_ptr<PlugIn_Waypoint_Ex> wp = GetWaypointEx_Plugin(guid);
        ++m_loading_fetched;

        if (wp) {
            entry.lat = wp->m_lat;
            entry.lon = wp->m_lon;
            entry.icon = wp->IconName;
            entry.description = wp->IconDescription;
            entry.firstLine = FirstDescriptionLine(wp->m_MarkDescription);
            entry.visible = wp->IsVisible;
            entry.useScamin = wp->b_useScamin;
            entry.scamin = wp->scamin;

            LightCharacter light;
            if (light.FindInWaypoint(wp->m_MarkName, wp->m_MarkDescription))
                entry.light = light.Abbreviation();
        }

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
    if (!m_markdown_view)
        return;

    const LegendPalette palette = GetLegendPalette();

    wxString progress;
    if (m_loading && !m_loading_guids.IsEmpty()) {
        progress = wxString::Format(
            "%zu van %zu",
            m_loading_next,
            m_loading_guids.GetCount());
    }

    wxString html;
    html << "<html>" << LegendBodyTag(true)
         << "<p><font size=\"+1\"><b>Kaartlegenda</b></font></p>"
         << "<p>Waypoints worden geladen&hellip;</p>";

    if (!progress.IsEmpty()) {
        html << "<p><font color=\"" << HtmlColour(palette.muted) << "\">"
             << progress << "</font></p>";
    }

    html << "</body></html>";

    static_cast<LegendHtmlWindow *>(m_markdown_view)->SetDynamicPage(html);
    m_showing_loading_page = true;
}

wxString legend_pi::PickLanguage(const wxString &text) const
{
    const int bar = text.Find(" | ");
    if (bar == wxNOT_FOUND)
        return text;

    wxString dutch = text.Left(bar);
    wxString english = text.Mid(bar + 3);
    dutch.Trim(true).Trim(false);
    english.Trim(true).Trim(false);

    if (m_dutch_names)
        return dutch.IsEmpty() ? english : dutch;
    return english.IsEmpty() ? dutch : english;
}

wxString legend_pi::SectionTitle(int section) const
{
    if (section >= 0 && section < static_cast<int>(m_meaning_sections.size()))
        return PickLanguage(m_meaning_sections[static_cast<size_t>(section)]);
    if (section == -2)
        return m_dutch_names ? "Kaartsymbolen" : "Chart symbols";
    if (section == -3)
        return m_dutch_names ? "Lichtkarakters" : "Light characters";
    return m_dutch_names ? "Overig" : "Other";
}

wxString legend_pi::SectionName(int section) const
{
    if (section >= 0 && section < static_cast<int>(m_meaning_sections.size()))
        return m_meaning_sections[static_cast<size_t>(section)];

    // Chart symbols not mentioned in symbolen.txt have a heading of their
    // own, before the waypoints without heading.
    if (section == -2)
        return "Kaartsymbolen";

    // Light characters of waypoints and chart lights in view.
    if (section == -3)
        return "Lichtkarakters";

    return "Overig";
}

void legend_pi::ToggleSection(size_t renderedIndex)
{
    if (renderedIndex >= m_rendered_headings.size())
        return;

    const wxString &name = m_rendered_headings[renderedIndex];

    if (m_collapsed_sections.erase(name) == 0)
        m_collapsed_sections.insert(name);

    SaveConfig();
    RenderDynamicLegend(true);
}

void legend_pi::SetAllSectionsCollapsed(bool collapsed)
{
    m_collapsed_sections.clear();

    if (collapsed) {
        for (const wxString &name : m_meaning_sections)
            m_collapsed_sections.insert(name);
        m_collapsed_sections.insert(SectionName(-2));
        m_collapsed_sections.insert(SectionName(-3));
        m_collapsed_sections.insert(SectionName(-1));
        m_collapsed_sections.insert("@marifoon");
        m_collapsed_sections.insert("@vooruit");
        m_collapsed_sections.insert("@notities");
        m_collapsed_sections.insert("@herkennen");
    }

    SaveConfig();
    RenderDynamicLegend(true);
}

void legend_pi::RunLegendCommand(const wxString &command)
{
    if (command == "reorder") {
        SetReorderMode(true);
    }
    else if (command == "collapse-all" || command == "expand-all") {
        SetAllSectionsCollapsed(command == "collapse-all");
    }
    else if (command == "language") {
        m_dutch_names = !m_dutch_names;
        SaveConfig();
        // Labels are made when the list is collected.
        CollectDynamicLegend();
        RenderDynamicLegend(true);
    }
    else if (command == "lights") {
        m_show_lights = !m_show_lights;
        if (!m_show_lights) {
            SetNightPreview(false);
            SetAllLightsShining(false);
        }
        SaveConfig();
        RefreshDynamicLegendKeepingState();
        RenderDynamicLegend(true);
    }
    else if (command == "all-lights") {
        if (m_night_preview)
            SetNightPreview(false);
        SetAllLightsShining(!m_all_lights_on);
        RenderDynamicLegend(true);
    }
    else if (command == "night") {
        SetNightPreview(!m_night_preview);
        RenderDynamicLegend(true);
    }
    else if (command == "hidden") {
        m_show_hidden_at_scale = !m_show_hidden_at_scale;
        SaveConfig();
        RefreshDynamicLegendKeepingState();
        RenderDynamicLegend(true);
    }
}

// The menu behind "Instellingen": what is shown, with a tick when it is on,
// and the actions on the list.
void legend_pi::ShowSettingsMenu()
{
    if (!m_markdown_view)
        return;

    enum
    {
        kLights = 1000, kHidden, kEnglish, kAllLights, kNight, kIdentify, kVhfKeep, kVhfNotice,
        kNoteNotice,
        kFold, kReorder
    };

    wxMenu menu;
    menu.AppendCheckItem(kLights, "Lichten")->Check(m_show_lights);
    menu.AppendCheckItem(kHidden, "Ook op deze schaal verborgen tonen")
        ->Check(m_show_hidden_at_scale);
    if (m_chart_sources_available || m_menu_has_lights)
        menu.AppendCheckItem(kEnglish, "Engelse namen")->Check(!m_dutch_names);

    menu.AppendSeparator();
    if (m_show_lights && (m_menu_has_lights || m_all_lights_on))
        menu.AppendCheckItem(kAllLights, "Alle lichten laten branden")->Check(m_all_lights_on);
    if (m_show_lights && (m_menu_has_lights || m_night_preview))
        menu.AppendCheckItem(kNight, "Nachtvoorbeeld")->Check(m_night_preview);
    if (m_show_lights && (m_menu_has_lights || m_identify_on))
        menu.AppendCheckItem(kIdentify, "Licht herkennen")->Check(m_identify_on);
    EnsureVhfAreas();
    if (!m_vhf_areas.IsEmpty()) {
        menu.AppendSeparator();
        menu.AppendCheckItem(kVhfKeep, "Marifoongebied blijft oplichten")->Check(m_vhf_keep_lit);
        menu.AppendCheckItem(kVhfNotice, "Melding bij nieuw marifoongebied")
            ->Check(m_vhf_notice_on);
    }
    if (!m_note_places.empty())
        menu.AppendCheckItem(kNoteNotice, "Melding bij notitie in de buurt")
            ->Check(m_note_notice_on);
    if (m_menu_has_headings) {
        menu.Append(kFold, m_menu_any_open ? "Alles inklappen" : "Alles uitklappen");
        menu.Append(kReorder, wxString::FromUTF8("Volgorde wijzigen\u2026"));
    }

    switch (m_markdown_view->GetPopupMenuSelectionFromUser(menu)) {
    case kLights:
        RunLegendCommand("lights");
        break;
    case kHidden:
        RunLegendCommand("hidden");
        break;
    case kEnglish:
        RunLegendCommand("language");
        break;
    case kAllLights:
        RunLegendCommand("all-lights");
        break;
    case kNight:
        RunLegendCommand("night");
        break;
    case kIdentify:
        SetIdentify(!m_identify_on);
        break;
    case kVhfKeep:
        m_vhf_keep_lit = !m_vhf_keep_lit;
        SaveConfig();
        // A lit area now stays, or starts to fade.
        if (!m_vhf_highlight.empty()) {
            m_vhf_highlight_started = wxGetUTCTimeMillis();
            if (m_vhf_keep_lit)
                m_vhf_highlight_timer.Stop();
            else
                m_vhf_highlight_timer.Start(50);
        }
        break;
    case kVhfNotice:
        m_vhf_notice_on = !m_vhf_notice_on;
        SaveConfig();
        break;
    case kNoteNotice:
        m_note_notice_on = !m_note_notice_on;
        SaveConfig();
        break;
    case kFold:
        RunLegendCommand(m_menu_any_open ? "collapse-all" : "expand-all");
        break;
    case kReorder:
        RunLegendCommand("reorder");
        break;
    default:
        break;
    }
}

int legend_pi::BuiltInSection(int builtIn) const
{
    const wxString name = builtIn == -2 ? "kaartsymbolen" : "lichtkarakters";
    for (size_t i = 0; i < m_meaning_sections.size(); ++i) {
        wxString first = m_meaning_sections[i].BeforeFirst('|');
        first.Trim(true).Trim(false);
        if (first.Lower() == name)
            return static_cast<int>(i);
    }
    return builtIn;
}

void legend_pi::AddBuiltInHeadings()
{
    wxString missing;
    if (m_chart_sources_available && BuiltInSection(-2) == -2)
        missing << "\n[Kaartsymbolen | Chart symbols]\n";
    if (m_show_lights && BuiltInSection(-3) == -3)
        missing << "\n[Lichtkarakters | Light characters]\n";
    if (missing.IsEmpty())
        return;

    wxFile file;
    if (!file.Open(GetMeaningsFile(), wxFile::write_append))
        return;
    file.Write(missing, wxConvUTF8);
    file.Close();

    // Read again even when the file time did not change within a second.
    m_meanings_modified = wxDateTime();
    LoadMeanings();
    CollectDynamicLegend();
}

void legend_pi::SetReorderMode(bool on)
{
    if (on)
        AddBuiltInHeadings();
    m_reorder_mode = on;
    UpdateSearchVisibility();
    RenderDynamicLegend(!on);
}

void legend_pi::UpdateSearchVisibility()
{
    if (!m_search)
        return;

    // Only the dynamic legend can be searched; the order edit mode shows
    // every heading and has no use for it.
    const bool show = m_current_legend_path == kDynamicGpxId && !m_reorder_mode;
    if (m_search->IsShown() == show)
        return;

    m_search->Show(show);
    m_search->GetParent()->Layout();
}

void legend_pi::SetFilter(const wxString &text)
{
    m_filter_timer.Stop();

    if (m_search && m_search->GetValue() != text)
        m_search->ChangeValue(text);

    wxString trimmed = text;
    trimmed.Trim(true).Trim(false);

    std::vector<wxString> words;
    wxStringTokenizer tokens(FoldForSearch(trimmed), " \t", wxTOKEN_STRTOK);
    while (tokens.HasMoreTokens())
        words.push_back(tokens.GetNextToken());

    if (words == m_filter_words)
        return;

    m_filter = trimmed;
    m_filter_words = words;

    // A new search starts at the top of the list. While the list is still
    // being filled it is drawn with the filter once loading has finished.
    if (IsDynamicLegendVisible() && !m_loading && !m_showing_loading_page)
        RenderDynamicLegend(false);
}

bool legend_pi::GroupMatchesFilter(const DynamicGroup &group) const
{
    if (m_filter_words.empty())
        return true;

    wxString text = group.label;
    text << ' '
         << (group.chartSymbol      ? group.symbol
             : group.lightCharacter ? group.lightText
                                    : group.iconName)
         << ' ' << SectionTitle(group.section);
    const wxString folded = FoldForSearch(text);

    for (const wxString &word : m_filter_words) {
        if (folded.Find(word) == wxNOT_FOUND)
            return false;
    }
    return true;
}

// Moves the block of a heading in symbolen.txt: the [Heading] line and
// everything up to the next heading, comments included. Text before the
// first heading stays in place.
void legend_pi::MoveSection(int section, int delta)
{
    const int target = section + delta;
    const int count = static_cast<int>(m_meaning_sections.size());

    if (section < 0 || section >= count || target < 0 || target >= count)
        return;

    const wxString path = GetMeaningsFile();

    wxTextFile file;
    if (!file.Open(path, wxConvUTF8))
        return;

    auto isHeading = [](wxString line) {
        line.Trim(true).Trim(false);
        return line.StartsWith("[") && line.EndsWith("]");
    };

    std::vector<wxString> preamble;
    std::vector<std::vector<wxString>> blocks;

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        const wxString &line = file[i];

        if (isHeading(line))
            blocks.emplace_back();

        if (blocks.empty())
            preamble.push_back(line);
        else
            blocks.back().push_back(line);
    }

    // The blocks must match the headings that were read; otherwise the
    // file changed in the meantime and nothing is moved.
    if (static_cast<int>(blocks.size()) != count) {
        m_meanings_modified = wxDateTime();
        RefreshDynamicLegendKeepingState();
        RenderDynamicLegend(true);
        return;
    }

    std::swap(blocks[static_cast<size_t>(section)],
              blocks[static_cast<size_t>(target)]);

    // One empty line between blocks, whatever order they end up in.
    auto trimEnd = [](std::vector<wxString> &lines) {
        while (!lines.empty() && wxString(lines.back()).Trim().IsEmpty())
            lines.pop_back();
    };

    trimEnd(preamble);

    file.Clear();
    for (const wxString &line : preamble)
        file.AddLine(line);

    for (auto &block : blocks) {
        trimEnd(block);
        if (file.GetLineCount() > 0)
            file.AddLine(wxEmptyString);
        for (const wxString &line : block)
            file.AddLine(line);
    }

    file.Write(wxTextFileType_None, wxConvUTF8);
    file.Close();

    // Read again even if the modification time did not visibly change.
    m_meanings_modified = wxDateTime();
    RefreshDynamicLegendKeepingState();
    RenderDynamicLegend(true);
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
    m_light_sectors.clear();
    {
        std::vector<PendingLight> joined;
        std::map<wxString, size_t> sectorLights;
        for (const PendingLight &pending : lights) {
            if (!pending.sector) {
                joined.push_back(pending);
                continue;
            }
            const std::pair<long, long> cell = SectorCell(pending.lat, pending.lon);
            m_light_sectors[cell].push_back(
                std::make_pair(pending.light.RhythmAbbreviation(), pending.part));

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

std::vector<LightSector> legend_pi::SectorsAt(
    double lat, double lon, const wxString &rhythm) const
{
    std::vector<LightSector> sectors;
    if (rhythm.IsEmpty())
        return sectors;

    const std::pair<long, long> cell = SectorCell(lat, lon);
    for (long dLat = -1; dLat <= 1; ++dLat) {
        for (long dLon = -1; dLon <= 1; ++dLon) {
            auto found = m_light_sectors.find(
                std::make_pair(cell.first + dLat, cell.second + dLon));
            if (found == m_light_sectors.end())
                continue;
            for (const auto &entry : found->second) {
                if (entry.first == rhythm)
                    sectors.push_back(entry.second);
            }
        }
    }
    return sectors;
}

namespace {

// The abbreviation of a light character without its colours.
wxString RhythmOf(const wxString &abbreviation)
{
    LightCharacter light;
    return light.Parse(abbreviation) ? light.RhythmAbbreviation() : wxString();
}

} // namespace

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

    m_light_sectors.clear();
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
        if (m_all_lights_on)
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

    if (m_all_lights_on)
        UpdateAllLights();
}

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

    ClearDynamicImages();
    ++m_dynamic_generation;

    auto escape = [](wxString text) {
        text.Replace("&", "&amp;");
        text.Replace("<", "&lt;");
        text.Replace(">", "&gt;");
        return text;
    };

    const LegendPalette palette = GetLegendPalette();
    const wxColour &background = palette.background;
    const wxColour &foreground = palette.text;
    const wxColour &stripe = palette.stripe;
    const wxColour &active = palette.active;
    const wxColour &muted = palette.muted;
    const wxColour &rule = palette.rule;

    auto hex = [](const wxColour &colour) { return HtmlColour(colour); };

    // An image of the list under "name.png", and when given its version at
    // twice the size under "name@2x.png", which wxHTML on macOS takes on a
    // Retina screen, so it stays sharp there.
    auto addImage = [this](const wxString &name, const wxImage &image,
                           const wxImage &doubled) {
        wxMemoryFSHandler::AddFile(name, image, wxBITMAP_TYPE_PNG);
        m_dynamic_image_names.push_back(name);
        if (doubled.IsOk() && name.EndsWith(".png")) {
            const wxString hiDpi = name.Left(name.length() - 4) + "@2x.png";
            wxMemoryFSHandler::AddFile(hiDpi, doubled, wxBITMAP_TYPE_PNG);
            m_dynamic_image_names.push_back(hiDpi);
        }
        return name;
    };

    // "×12", or "3/12" once a click went to the third one.
    auto countTextOf = [](const DynamicGroup &group) {
        const size_t count = group.positions.size();
        if (group.current >= 0 && count > 1)
            return wxString::Format("%d/%zu", group.current + 1, count);
        return wxString::Format("&times;%zu", count);
    };

    // A light character as on the chart, in words, with its rhythm below.
    auto lightLabel = [&](const DynamicGroup &group, size_t index,
                          const wxColour &rowColour) {
        wxString label =
            "<b>" + escape(group.lightText) + "</b>&nbsp; " + escape(group.label);

        // A sector light: its sectors, of the light the counter is on (the
        // nearest one until a click went to another).
        if (!group.positions.empty()) {
            const size_t at =
                group.current >= 0 ? static_cast<size_t>(group.current) : 0;
            const std::vector<LightSector> sectors =
                SectorsAt(group.positions[at].first, group.positions[at].second,
                          RhythmOf(group.lightText));
            if (!sectors.empty())
                label << "<br>" << escape(SectorWords(sectors, m_dutch_names));
        }
        LightCharacter light;
        if (light.Parse(group.lightText)) {
            const int width = m_rhythm_width;
            const wxImage rhythm =
                LightRhythmImage(light, width, 14, palette.muted, rowColour, SchemeDim());
            if (rhythm.IsOk()) {
                const wxString name = addImage(
                    wxString::Format("legend_dyn_%d_rhythm_%zu.png", m_dynamic_generation, index),
                    rhythm,
                    LightRhythmImage(light, 2 * width, 28, palette.muted, rowColour, SchemeDim(), 2.0));
                label << "<br><img src=\"memory:" << name << "\">";
            }
        }
        return label;
    };

    // Load all icons first: the largest one sets the square every icon
    // gets, so all rows are equally high without shrinking any icon.
    // Very large icons are limited to kMaxIconBox.
    const int kMaxIconBox = 64;
    const int tilePadding = palette.iconTiles ? 3 : 0;

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

    std::vector<wxImage> icons(m_dynamic_groups.size());
    std::vector<wxImage> icons2(m_dynamic_groups.size()); // twice the size
    std::vector<wxColour> rowColours(m_dynamic_groups.size());
    int iconBox = 16;

    // A search leaves out the rows that do not match; their headings go
    // too when nothing under them matches. Matching rows are shown even
    // under a collapsed heading.
    const bool filtering = !m_filter_words.empty() && !m_reorder_mode;
    const size_t groupCount = m_dynamic_groups.size();

    // The row a light sub-row belongs to: the row it follows.
    std::vector<int> parentOf(groupCount, -1);
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
    std::vector<bool> shown(groupCount, true);
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

    size_t mainRows = 0;
    size_t matches = 0;
    size_t lightRows = 0;
    bool lightsShown = false;
    for (size_t i = 0; i < groupCount; ++i) {
        if (parentOf[i] < 0) {
            ++mainRows;
            if (shown[i])
                ++matches;
        }
        if (m_dynamic_groups[i].lightCharacter) {
            ++lightRows;
            if (shown[i])
                lightsShown = true;
        }
    }

    // Headings only when symbolen.txt defines them; a collapsed heading
    // hides its rows.
    const bool showHeadings = !m_meaning_sections.empty();
    std::vector<bool> collapsed(m_dynamic_groups.size(), false);
    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        // In the order edit mode only the headings are shown.
        collapsed[i] =
            showHeadings && !filtering &&
            (m_reorder_mode ||
             m_collapsed_sections.count(
                 SectionName(m_dynamic_groups[i].section)) > 0);
    }

    // Stripes start again under every heading and skip rows left out; the
    // lights under a row share its stripe.
    std::vector<size_t> stripeIndex(groupCount);
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

    auto isActiveRow = [this](const DynamicGroup &group) {
        return (!m_active_group_key.IsEmpty() && group.key == m_active_group_key) ||
               (!m_active_light_key.IsEmpty() && group.key == m_active_light_key);
    };

    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        if (!shown[i])
            continue;

        const DynamicGroup &group = m_dynamic_groups[i];

        const bool isActive = isActiveRow(group);

        rowColours[i] =
            isActive ? active
                     : (stripeIndex[i] % 2 == 1 ? stripe : background);

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
            rowColours[i] = BlendColour(active, lit, remaining * remaining);
        }

        // Transparency is resolved against what ends up behind the icon:
        // the tile in dark mode, otherwise the row itself. Icons of a
        // collapsed group appear small next to the heading instead.
        if (collapsed[i])
            rowColours[i] = background;

        // The lamp of a light under a row sits on the row itself, no tile.
        const wxColour iconBackground =
            (palette.iconTiles && parentOf[i] < 0) ? palette.tile : rowColours[i];

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
            icons2[i] = doubled.IsOk() ? (std::fabs(iconScale - 1.0) > 1e-3
                                              ? scaled(doubled, iconScale)
                                              : doubled)
                                       : scaled(image, 2.0 * iconScale);
        }
        if (image.IsOk() && std::fabs(iconScale - 1.0) > 1e-3)
            image = scaled(image, iconScale);

        if (image.IsOk() && !collapsed[i] && shown[i]) {
            iconBox = std::max(
                iconBox,
                std::min(kMaxIconBox,
                         std::max(image.GetWidth(), image.GetHeight())));
        }

        icons[i] = image;
    }

    const int iconCell = iconBox + 2 * tilePadding;

    wxString html;
    html << "<html>" << LegendBodyTag(true);

    // The title, with the icons of the settings menu and Ververs on the
    // right (their words in tooltips); one line high. The counts are at
    // the foot of the list. A line under the title only to say the chart
    // position is unknown, or how many rows match a search.
    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" "
         << "bgcolor=\"" << hex(palette.header) << "\"><tr><td valign=\"middle\">"
         << "<font size=\"+1\"><b>Kaartlegenda</b></font>";

    if (!m_viewport_valid) {
        html << "<br><font size=\"-1\" color=\"" << hex(muted) << "\">"
             << "De kaartpositie is nog niet bekend. Beweeg de kaart "
                "een klein stukje en klik daarna op Ververs.</font>";
    }
    else if (filtering) {
        html << "<br><font size=\"-1\" color=\"" << hex(muted) << "\">"
             << static_cast<int>(matches) << " van de " << static_cast<int>(mainRows)
             << " symbolen in beeld passen bij &lsquo;" << escape(m_filter)
             << "&rsquo;</font>";
    }

    html << "</td><td align=\"right\" valign=\"middle\" nowrap>";
    if (showHeadings && m_reorder_mode) {
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
        if (m_viewport_valid) {
            html << "<td id=\"" << kLegendTipIdPrefix << "0\"><a href=\"legend-settings:\">"
                 << icon(true) << "</a></td><td width=\"10\"></td>";
        }
        html << "<td id=\"" << kLegendTipIdPrefix << "1\"><a href=\"legend-refresh:\">"
             << icon(false) << "</a></td></tr></table>";
    }
    html << "</td></tr></table>";

    // What the settings menu offers depends on the list.
    m_menu_has_headings = showHeadings && !m_dynamic_groups.empty() && !filtering;
    m_menu_any_open = false;
    for (const DynamicGroup &group : m_dynamic_groups) {
        if (m_collapsed_sections.count(SectionName(group.section)) == 0) {
            m_menu_any_open = true;
            break;
        }
    }
    m_menu_has_lights = lightRows > 0;

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
         << "<tr><td bgcolor=\"" << hex(palette.headerRule)
         << "\" height=\"2\"></td></tr>"
         << "<tr><td height=\"6\"></td></tr>"
         << "</table>";

    // Marifoon and Vooruit: headings at the top of the list that fold like
    // the others; folded, the heading still tells the channel or the next
    // object. Not during a search or the order edit mode.
    m_rendered_headings.clear();

    auto extraHeading = [&](const wxString &key, const wxString &title,
                            const wxString &summary) {
        const bool folded = m_collapsed_sections.count(key) > 0;
        const wxString sectionId = wxString::Format(
            "%s%zu", kLegendSectionIdPrefix, m_rendered_headings.size());
        m_rendered_headings.push_back(key);
        html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
             << "<tr><td height=\"6\"></td></tr>"
             << "<tr><td id=\"" << sectionId << "\" valign=\"middle\">"
             << "<font size=\"+3\">" << (folded ? "&#9654;" : "&#9660;")
             << "</font>&nbsp;<b>" << title << "</b>";
        if (!summary.IsEmpty())
            html << " <font color=\"" << hex(muted) << "\">&middot; " << summary << "</font>";
        html << "</td></tr>"
             << "<tr><td height=\"4\"></td></tr>"
             << "<tr><td bgcolor=\"" << hex(rule) << "\" height=\"1\"></td></tr></table>";
        return !folded;
    };

    if (m_viewport_valid && !m_reorder_mode && !filtering) {
        const bool nl = m_dutch_names;

        bool fromShip = false;
        bool layerShown = false;
        const std::vector<const VhfArea *> areas = VhfAreasHere(&fromShip, &layerShown);
        if (layerShown) {
            wxString summary;
            if (areas.empty()) {
                summary = nl ? "geen VHFinfo-gebied hier" : "no VHFinfo area here";
            }
            else {
                const VhfArea *first = areas.front();
                const wxString who = first->callName.IsEmpty() ? first->name : first->callName;
                summary << (nl ? "kanaal " : "channel ") << escape(first->channel);
                if (!who.IsEmpty())
                    summary << ", " << escape(who);
                if (areas.size() > 1)
                    summary << wxString::Format(" (+%zu)", areas.size() - 1);
            }
            if (!fromShip)
                summary << (nl ? " &middot; midden van de kaart, geen GPS"
                               : " &middot; middle of the chart, no GPS");

            if (extraHeading("@marifoon", nl ? "Marifoon" : "VHF", summary) &&
                !areas.empty()) {
                html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";
                m_vhf_rows.clear();
                for (size_t k = 0; k < areas.size(); ++k) {
                    const VhfArea *area = areas[k];
                    m_vhf_rows.push_back(*area);
                    const wxString who = area->callName.IsEmpty() ? area->name : area->callName;
                    html << "<tr bgcolor=\"" << hex(k % 2 == 1 ? stripe : background)
                         << "\"><td id=\"" << kLegendVhfAreaIdPrefix << k << "\"><b>"
                         << (nl ? "kanaal " : "channel ")
                         << escape(area->channel) << "</b>";
                    if (!who.IsEmpty())
                        html << " &middot; " << escape(who);
                    if (!area->type.IsEmpty())
                        html << " &middot; " << escape(VhfTypeWord(area->type, nl));
                    if (!area->usage.IsEmpty())
                        html << " &middot; " << escape(VhfUsageWord(area->usage, nl));
                    if (!area->phone.IsEmpty()) {
                        html << " &middot; <a href=\"tel:" << escape(area->phone) << "\">"
                             << escape(area->phone) << "</a>";
                    }
                    html << "</td></tr>";
                }
                html << "</table>";
            }
        }

        // Vooruit: while sailing, what the boat meets in the coming half
        // hour, in order of arrival.
        ComputeAhead();
        m_ahead_drawn_key = m_ahead_key;
        m_ahead_rendered = wxGetUTCTimeMillis();

        if (m_ahead_sailing) {
            auto when = [nl](const AheadItem &item) {
                if (item.minutes < 1.0)
                    return wxString(nl ? "nu" : "now");
                return wxString::Format(nl ? "over %d min" : "in %d min",
                                        static_cast<int>(std::lround(item.minutes)));
            };
            auto side = [nl](const AheadItem &item) {
                if (std::fabs(item.cross) < 0.02)
                    return wxString(nl ? "recht vooruit" : "dead ahead");
                if (item.cross > 0.0)
                    return wxString(nl ? "stuurboord" : "starboard");
                return wxString(nl ? "bakboord" : "port");
            };
            auto name = [&](const AheadItem &item) {
                const DynamicGroup &group = m_dynamic_groups[item.group];
                return escape(group.lightCharacter ? group.lightText : group.label);
            };

            auto whenMinutes = [nl](double minutes) {
                if (minutes < 1.0)
                    return wxString(nl ? "nu" : "now");
                return wxString::Format(nl ? "over %d min" : "in %d min",
                                        static_cast<int>(std::lround(minutes)));
            };
            auto vhfText = [&](const AheadVhf &next) {
                const VhfArea &area = next.area;
                wxString text = (nl ? "kanaal " : "channel ") + escape(area.channel);
                const wxString who = area.callName.IsEmpty() ? area.name : area.callName;
                if (!who.IsEmpty())
                    text << " &middot; " << escape(who);
                if (!area.usage.IsEmpty())
                    text << " &middot; " << escape(VhfUsageWord(area.usage, nl));
                return text;
            };

            wxString summary;
            const double inf = 1e9;
            const double firstObject = m_ahead.empty() ? inf : m_ahead.front().minutes;
            const double firstVhf = m_ahead_vhf.empty() ? inf : m_ahead_vhf.front().minutes;
            const double firstNote = m_ahead_notes.empty() ? inf : m_ahead_notes.front().minutes;
            if (firstNote < inf && firstNote <= firstVhf && firstNote < firstObject)
                summary = whenMinutes(firstNote) + ": " + (nl ? "notitie " : "note ") +
                          escape(m_ahead_notes.front().title);
            else if (firstVhf < inf && firstVhf < firstObject)
                summary = whenMinutes(m_ahead_vhf.front().minutes) + ": " +
                          vhfText(m_ahead_vhf.front());
            else if (m_ahead.empty())
                summary = nl ? "niets in de komende 30 minuten" : "nothing in the next 30 minutes";
            else
                summary = when(m_ahead.front()) + ", " + side(m_ahead.front()) + ": " +
                          name(m_ahead.front());

            if (extraHeading("@vooruit", nl ? "Vooruit" : "Ahead", summary)) {
                html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";
                size_t row = 0;
                size_t nextVhf = 0;
                // A VHF area ahead, in its place in time.
                auto vhfRow = [&](size_t v) {
                    const AheadVhf &next = m_ahead_vhf[v];
                    const wxString id = wxString::Format("%s%zu", kLegendAheadVhfIdPrefix, v);
                    const wxColour &rowColour = row++ % 2 == 1 ? stripe : background;
                    const wxString image = addImage(
                        wxString::Format("legend_dyn_%d_aheadvhf_%zu.png", m_dynamic_generation, v),
                        VhfBadgeImage(next.area.channel, rowColour, 24),
                        VhfBadgeImage(next.area.channel, rowColour, 48));
                    html << "<tr bgcolor=\"" << hex(rowColour) << "\"><td id=\"" << id
                         << "\" width=\"34\" align=\"center\" valign=\"middle\"><img src=\"memory:"
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
                    const wxString image = addImage(
                        wxString::Format("legend_dyn_%d_aheadnote_%zu.png", m_dynamic_generation, n),
                        NoteSignImage(rowColour, 24), NoteSignImage(rowColour, 48));
                    wxString where = std::fabs(note.cross) < 0.02
                                         ? wxString(nl ? "recht vooruit" : "dead ahead")
                                         : note.cross > 0.0 ? wxString(nl ? "stuurboord" : "starboard")
                                                            : wxString(nl ? "bakboord" : "port");
                    html << "<tr bgcolor=\"" << hex(rowColour) << "\"><td id=\"" << id
                         << "\" width=\"34\" align=\"center\" valign=\"middle\"><img src=\"memory:"
                         << image << "\"></td><td id=\"" << id << "\" valign=\"middle\"><b>"
                         << whenMinutes(note.minutes) << "</b> &middot; " << where << " &middot; "
                         << (nl ? "notitie " : "note ") << escape(note.title) << "</td></tr>";
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

                    html << "<tr bgcolor=\"" << hex(rowColour) << "\"><td id=\"" << id
                         << "\" width=\"34\" align=\"center\" valign=\"middle\">";
                    if (icons[item.group].IsOk()) {
                        const wxString image = addImage(
                            wxString::Format("legend_dyn_%d_ahead_%zu.png",
                                             m_dynamic_generation, k),
                            NormalizeIcon(icons[item.group], rowColour, 24, nullptr, 0),
                            NormalizeIcon(icons2[item.group], rowColour, 48, nullptr, 0));
                        html << "<img src=\"memory:" << image << "\">";
                    }
                    html << "</td><td id=\"" << id << "\" valign=\"middle\">"
                         << "<b>" << when(item) << "</b> &middot; " << side(item)
                         << " &middot; " << name(item);
                    if (!item.light.IsEmpty() && !group.lightCharacter)
                        html << " &middot; <b>" << escape(item.light) << "</b>";
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
        }

        // Notities: notes whose place is in view, nearest first.
        m_note_rows.clear();
        {
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

            if (!inView.empty()) {
                const NotePlace &nearest = m_note_places[inView.front().second];
                wxString summary;
                if (shownCount > 0) {
                    summary = escape(nearest.title) + ", " + miles(inView.front().first);
                    if (shownCount > 1)
                        summary << wxString::Format(" (+%zu)", shownCount - 1);
                }
                else {
                    summary = wxString::Format(nl ? "%zu op deze schaal verborgen"
                                                  : "%zu hidden at this scale",
                                               inView.size());
                }
                if (extraHeading("@notities", nl ? "Notities" : "Notes", summary)) {
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
                        const wxString image = addImage(
                            wxString::Format("legend_dyn_%d_note_%zu.png", m_dynamic_generation, k),
                            sign, sign2);
                        wxString title = escape(place.title);
                        if (hidden) {
                            // Not drawn at this scale: grey, a closed eye and the
                            // scale at which it appears; a click zooms in.
                            const wxString eye = addImage(
                                wxString::Format("legend_dyn_%d_noteeye_%zu.png",
                                                 m_dynamic_generation, k),
                                EyeIcon(false, muted, rowColour), EyeIcon(false, muted, rowColour, 2));
                            title = "<font color=\"" + hex(muted) + "\">" + title +
                                    "</font>&nbsp; <img src=\"memory:" + eye + "\"> <font size=\"-1\" color=\"" +
                                    hex(muted) + "\">" + FormatScaleShort(place.scamin) + "</font>";
                        }
                        html << "<tr bgcolor=\"" << hex(rowColour) << "\"><td id=\"" << id
                             << "\" width=\"34\" align=\"center\" valign=\"top\"><img src=\"memory:"
                             << image << "\"></td><td id=\"" << id << "\">" << title
                             << "<br><font size=\"-1\" color=\"" << hex(muted) << "\">"
                             << miles(distance);
                        if (!place.snippet.IsEmpty()) {
                            wxString snippet = place.snippet;
                            if (snippet.length() > 70)
                                snippet = snippet.Left(68) + wxString::FromUTF8("\u2026");
                            html << " &middot; " << escape(snippet);
                        }
                        html << "</font></td></tr>";
                    }
                    html << "</table>";
                }
            }
        }

        // Licht herkennen: tap along with a light, see which lights fit.
        m_identify_matches.clear();
        if (m_identify_on) {
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
                          escape(m_dynamic_groups[scored.front().second].lightText);
            else
                summary = escape(pattern.Words(nl));

            if (extraHeading("@herkennen", nl ? "Licht herkennen" : "Identify a light",
                             summary)) {
                // A frame sets the block apart from the list below it: a
                // coloured border around a striped panel.
                html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"2\" bgcolor=\""
                     << hex(palette.headerRule) << "\"><tr><td>"
                     << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\" bgcolor=\""
                     << hex(stripe) << "\">"
                     << "<tr><td colspan=\"2\"><font size=\"-1\" color=\"" << hex(muted) << "\">"
                     << (nl ? "Houd het vlak of de spatiebalk ingedrukt zolang het licht "
                              "brandt en laat los als het donker is, twee of drie perioden lang."
                            : "Hold the pad or the space bar while the light shines and let go "
                              "while it is dark, for two or three periods.")
                     << "</font></td></tr>"
                     << "<tr><td colspan=\"2\" id=\"" << kLegendTapIdPrefix << "0\" bgcolor=\""
                     << hex(palette.active) << "\" align=\"center\" valign=\"middle\" height=\"64\">"
                     << "<font size=\"+1\"><b>"
                     << (nl ? "Indrukken = licht aan" : "Press = light on")
                     << "</b></font><br><font size=\"-1\" color=\"" << hex(muted) << "\">"
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
                    const wxString lamp = addImage(
                        wxString::Format("legend_dyn_%d_idcolour_%d.png", m_dynamic_generation, c),
                        LightLampImage(colours, muted, tile, SchemeDim(), 18),
                        LightLampImage(colours, muted, tile, SchemeDim(), 36));
                    const wxString label = nl ? namesNl[c] : namesEn[c];
                    html << "<td id=\"" << kLegendButtonIdPrefix << 10 + c << "\" align=\"center\" bgcolor=\""
                         << hex(tile) << "\"><img src=\"memory:" << lamp << "\"><br><font size=\"-1\">"
                         << (chosen ? "<b>" + label + "</b>" : label) << "</font></td>";
                }
                html << "</tr></table></td></tr>";

                if (!m_taps.empty()) {
                    const wxString name = addImage(
                        wxString::Format("legend_dyn_%d_taps.png", m_dynamic_generation),
                        TapRhythmImage(m_taps, m_rhythm_width + 20, 16, muted, palette.text, stripe),
                        TapRhythmImage(m_taps, 2 * (m_rhythm_width + 20), 32, muted, palette.text,
                                       stripe, 2.0));
                    html << "<tr><td colspan=\"2\"><img src=\"memory:" << name << "\">";
                    if (pattern.valid)
                        html << "<br><font size=\"-1\">" << escape(pattern.Words(nl)) << "</font>";
                    html << "</td></tr>";
                }

                if (scored.empty()) {
                    html << "<tr><td colspan=\"2\"><font color=\"" << hex(muted) << "\">"
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
                        html << "<tr bgcolor=\"" << hex(k % 2 == 0 ? background : stripe) << "\">"
                             << "<td colspan=\"2\" id=\"" << id << "\"><b>" << escape(group.lightText)
                             << "</b>&nbsp; " << escape(group.label) << "<br><font size=\"-1\" color=\""
                             << hex(muted) << "\">" << fit << " (" << percent << "%) &middot; &times;"
                             << group.positions.size() << "</font></td></tr>";
                    }
                    if (m_identify_matches.empty()) {
                        html << "<tr><td colspan=\"2\"><font color=\"" << hex(muted) << "\">"
                             << (nl ? "Geen licht in beeld past bij dit ritme."
                                    : "No light in view fits this rhythm.")
                             << "</font></td></tr>";
                    }
                }

                // Two buttons at the foot: erase the taps and start again,
                // or close the block (the settings menu opens it again).
                auto button = [&](int number, const wxString &label) {
                    html << "<td><table cellspacing=\"0\" cellpadding=\"1\" bgcolor=\""
                         << hex(palette.headerRule) << "\"><tr><td>"
                         << "<table cellspacing=\"0\" cellpadding=\"5\" bgcolor=\"" << hex(background)
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
                addImage(space, spaceImage, wxImage());
                html << "<img src=\"memory:" << space << "\"><br>";
            }
        }
    }

    if (showHeadings && m_reorder_mode) {
        // Every heading of symbolen.txt, also those with nothing in view,
        // with large buttons that also suit a finger on a tablet.
        const int count = static_cast<int>(m_meaning_sections.size());
        const size_t kStripIcons = 6;

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

                if (symbols > kStripIcons || !icons[j].IsOk())
                    continue;

                const wxImage small = NormalizeIcon(
                    icons[j],
                    rowColour,
                    20,
                    palette.iconTiles ? &palette.tile : nullptr,
                    palette.iconTiles ? 2 : 0);

                const wxString name = addImage(
                    wxString::Format("legend_dyn_%d_order_%zu.png", m_dynamic_generation, j),
                    small,
                    NormalizeIcon(icons2[j], rowColour, 40,
                                  palette.iconTiles ? &palette.tile : nullptr,
                                  palette.iconTiles ? 4 : 0));
                strip << "<img src=\"memory:" << name << "\"> ";
            }

            if (symbols > kStripIcons) {
                strip << "<font color=\"" << hex(muted) << "\">+"
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
                    cell << " color=\"" << hex(rule) << "\"";
                cell << ">" << arrow << "</font></td>";
                return cell;
            };

            html << "<tr bgcolor=\"" << hex(rowColour) << "\">"
                 << "<td valign=\"middle\"><b>"
                 << escape(SectionTitle(section))
                 << "</b><br><font size=\"-1\" color=\"" << hex(muted)
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
             << "<p><font size=\"-1\" color=\"" << hex(muted) << "\">"
             << "Tik of klik op &#9650; of &#9660; om een kopje te verplaatsen. "
                "De volgorde wordt in symbolen.txt bewaard. 'Overig' staat "
                "altijd onderaan.</font></p>";
    }
    else if (m_viewport_valid && m_dynamic_groups.empty()) {
        html << "<p><font color=\"" << hex(muted) << "\">"
             << "Geen zichtbare waypoints in dit kaartbeeld.</font></p>";
    }
    else if (m_viewport_valid && matches == 0) {
        html << "<p>Geen symbolen in beeld die passen bij &lsquo;"
             << escape(m_filter) << "&rsquo;.</p>"
             << "<p><font size=\"-1\" color=\"" << hex(muted) << "\">"
             << "Er wordt gezocht in de namen in de lijst, de kopjes en de "
                "icoonnamen, alleen van wat nu in beeld is. Verschuif de "
                "kaart, zoom uit, of maak het zoekveld leeg.";
        if (!m_show_hidden_at_scale)
            html << " Symbolen die op deze schaal verborgen zijn, tellen pas "
                    "mee met &lsquo;Ook verborgen tonen&rsquo;.";
        html << "</font></p>";
    }
    else if (m_viewport_valid) {
        html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";

        // Section of the last row drawn; a row of another section starts a
        // new heading. Rows left out by a search do not count.
        bool firstRow = true;
        int previousSection = 0;

        for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
            DynamicGroup &group = m_dynamic_groups[i];

            if (!shown[i]) {
                group.imageName.Clear();
                continue;
            }

            const bool newHeading =
                firstRow || group.section != previousSection;
            const bool atTop = firstRow;
            firstRow = false;
            previousSection = group.section;

            if (showHeadings && newHeading) {
                const wxString heading = SectionName(group.section);
                const wxString headingTitle = SectionTitle(group.section);
                const bool folded = collapsed[i];

                // Symbols and waypoints of this group, and the icons shown
                // next to a collapsed heading.
                const size_t kStripIcons = 6;
                size_t symbols = 0;
                size_t waypoints = 0;
                wxString strip;

                for (size_t j = i; j < m_dynamic_groups.size() &&
                                   m_dynamic_groups[j].section == group.section;
                     ++j) {
                    // Lights under a row are part of that row's symbol.
                    if (!shown[j] || parentOf[j] >= 0)
                        continue;

                    ++symbols;
                    waypoints += m_dynamic_groups[j].positions.size();

                    if (!folded || symbols > kStripIcons || !icons[j].IsOk())
                        continue;

                    const wxImage small = NormalizeIcon(
                        icons[j],
                        background,
                        20,
                        palette.iconTiles ? &palette.tile : nullptr,
                        palette.iconTiles ? 2 : 0);

                    const wxString name = addImage(
                        wxString::Format("legend_dyn_%d_strip_%zu.png", m_dynamic_generation, j),
                        small,
                        NormalizeIcon(icons2[j], background, 40,
                                      palette.iconTiles ? &palette.tile : nullptr,
                                      palette.iconTiles ? 4 : 0));
                    strip << "<img src=\"memory:" << name << "\"> ";
                }

                if (folded && symbols > kStripIcons) {
                    strip << "<font color=\"" << hex(muted) << "\">+"
                          << symbols - kStripIcons << "</font>";
                }

                // Every cell of the heading carries its id, so a click
                // anywhere on it folds the group in or out. While searching
                // nothing folds, so the heading is not a button then.
                const wxString sectionId = wxString::Format(
                    "%s%zu", kLegendSectionIdPrefix, m_rendered_headings.size());
                m_rendered_headings.push_back(heading);
                const wxString idAttribute =
                    filtering ? wxString() : " id=\"" + sectionId + "\"";

                // Every group gets its own table; between them the heading
                // with a thin line under it. Inside the row table the
                // padding would turn the line into a thick bar.
                html << "</table>"
                     << "<table width=\"100%\" cellspacing=\"0\" "
                        "cellpadding=\"0\">"
                     << "<tr><td colspan=\"2\" height=\"" << (atTop ? 6 : 14)
                     << "\"></td></tr>"
                     << "<tr><td" << idAttribute << " valign=\"middle\">"
                     << "<font size=\"+3\">"
                     << (folded ? "&#9654;" : "&#9660;") << "</font>&nbsp;<b>"
                     << escape(headingTitle) << "</b> <font color=\"" << hex(muted)
                     << "\">&middot; " << symbols
                     << (symbols == 1 ? " symbool, " : " symbolen, ")
                     << waypoints << " in beeld</font></td>"
                     << "<td" << idAttribute
                     << " align=\"right\" valign=\"middle\">"
                     << strip << "</td></tr>"
                     << "<tr><td colspan=\"2\" height=\"4\"></td></tr>"
                     << "<tr><td colspan=\"2\" bgcolor=\"" << hex(rule)
                     << "\" height=\"1\"></td></tr></table>"
                     << "<table width=\"100%\" cellspacing=\"0\" "
                        "cellpadding=\"5\">";
            }

            if (collapsed[i]) {
                group.imageName.Clear();
                continue;
            }

            // Lights under a row are drawn with that row, below.
            if (parentOf[i] >= 0)
                continue;

            group.imageName.Clear();

            const bool isActive = isActiveRow(group);

            const wxColour &rowColour = rowColours[i];
            wxImage image = icons[i];
            wxImage doubled = icons2[i];

            if (image.IsOk()) {
                image = NormalizeIcon(
                    image,
                    rowColour,
                    iconBox,
                    palette.iconTiles ? &palette.tile : nullptr,
                    tilePadding);
                if (doubled.IsOk()) {
                    doubled = NormalizeIcon(
                        doubled,
                        rowColour,
                        2 * iconBox,
                        palette.iconTiles ? &palette.tile : nullptr,
                        2 * tilePadding);
                }

                // The chart does not draw these at this scale: faded.
                if (group.hiddenAtScale) {
                    image = FadeImage(image, rowColour, 0.55);
                    if (doubled.IsOk())
                        doubled = FadeImage(doubled, rowColour, 0.55);
                }
            }

            if (image.IsOk()) {
                group.imageName = addImage(
                    wxString::Format("legend_dyn_%d_%zu.png", m_dynamic_generation, i),
                    image,
                    doubled);
            }

            wxString label = group.lightCharacter
                                 ? lightLabel(group, i, rowColour)
                                 : escape(group.label);
            if (group.labelIsName) {
                // No meaning known yet: the icon name, in grey, as it must
                // be written in symbolen.txt.
                label = "<font color=\"" + hex(muted) + "\">" + label +
                        "</font>";
            }
            if (group.hiddenAtScale) {
                // Not drawn on the chart at this scale: a closed eye and the
                // scale at which it appears; a click zooms in.
                label = "<font color=\"" + hex(muted) + "\">" + label +
                        "</font>";

                const wxString eyeName = addImage(
                    wxString::Format("legend_dyn_%d_eye_%zu.png", m_dynamic_generation, i),
                    EyeIcon(false, muted, rowColour),
                    EyeIcon(false, muted, rowColour, 2));

                label << "&nbsp; <img src=\"memory:" << eyeName << "\">";
                if (group.visibleFrom > 0) {
                    label << " <font size=\"-1\" color=\"" << hex(muted) << "\">"
                          << FormatScaleShort(group.visibleFrom) << "</font>";
                }
            }
            if (isActive)
                label = "<b>" + label + "</b>";

            // Every cell of the row carries the row id, so a click anywhere
            // in the row can be traced back to its symbol. The counter is a
            // button of its own: it marks all objects of the symbol at once.
            const wxString rowId =
                wxString::Format("%s%zu", kLegendRowIdPrefix, i);
            const wxString allId =
                wxString::Format("%s%zu", kLegendAllIdPrefix, i);

            html << "<tr bgcolor=\"" << hex(rowColour) << "\">"
                 << "<td id=\"" << rowId << "\" width=\"" << iconCell + 10
                 << "\" height=\"" << iconCell + 10
                 << "\" align=\"center\" valign=\"middle\">"
                 << "<a name=\"legend-row-" << i << "\"></a>";
            if (!group.imageName.IsEmpty())
                html << "<img src=\"memory:" << group.imageName << "\">";
            html << "</td><td id=\"" << rowId << "\" valign=\"middle\">"
                 << label << "</td>"
                 << "<td id=\"" << allId
                 << "\" align=\"right\" valign=\"middle\" nowrap>"
                 << "<font color=\"" << hex(isActive ? foreground : muted)
                 << "\">" << countTextOf(group) << "&nbsp;<font size=\"+1\">&#9678;"
                 << "</font></font></td></tr>";

            // The lights this row carries: a block under its text, smaller,
            // joined to the row by a line on the left like a branch.
            std::vector<size_t> children;
            for (size_t j = i + 1; j < m_dynamic_groups.size() &&
                                   parentOf[j] == static_cast<int>(i);
                 ++j) {
                if (shown[j])
                    children.push_back(j);
            }
            if (children.empty())
                continue;

            const wxColour blockColour =
                stripeIndex[i] % 2 == 1 ? stripe : background;
            const wxString branch = hex(palette.headerRule);

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

            html << "<tr bgcolor=\"" << hex(blockColour) << "\">"
                 << "<td width=\"" << iconCell + 10 << "\"></td>"
                 << "<td colspan=\"2\">"
                 << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">";

            for (const size_t j : children) {
                DynamicGroup &light = m_dynamic_groups[j];
                light.imageName.Clear();

                const wxColour &lightColour = rowColours[j];
                const bool lightActive = isActiveRow(light);

                if (icons[j].IsOk()) {
                    const wxString name = wxString::Format(
                        "legend_dyn_%d_%zu.png", m_dynamic_generation, j);
                    // Not drawn at this scale: faded, like its row.
                    const wxImage lamp = light.hiddenAtScale
                                             ? FadeImage(icons[j], lightColour, 0.55)
                                             : icons[j];
                    wxImage lamp2 = icons2[j];
                    if (lamp2.IsOk() && light.hiddenAtScale)
                        lamp2 = FadeImage(lamp2, lightColour, 0.55);
                    light.imageName = addImage(name, lamp, lamp2);
                }

                wxString lightText = lightLabel(light, j, lightColour);
                if (light.hiddenAtScale)
                    lightText = "<font color=\"" + hex(muted) + "\">" + lightText + "</font>";
                if (lightActive)
                    lightText = "<b>" + lightText + "</b>";

                const wxString lightRowId =
                    wxString::Format("%s%zu", kLegendRowIdPrefix, j);
                const wxString lightAllId =
                    wxString::Format("%s%zu", kLegendAllIdPrefix, j);

                html << "<tr bgcolor=\"" << hex(lightColour) << "\">"
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
                     << hex(lightActive ? foreground : muted) << "\">"
                     << countTextOf(light) << "&nbsp;&#9678;</font></td></tr>"
                     // Room between the lights; the line runs on.
                     << "<tr bgcolor=\"" << hex(blockColour) << "\">"
                     << "<td bgcolor=\"" << branch << "\"><img src=\"memory:" << gapName
                     << "\"></td><td colspan=\"4\"></td></tr>";
            }

            html << "</table></td></tr>";
        }

        html << "</table>";

        // The counts, at the foot of the list.
        html << "<p><font size=\"-1\" color=\"" << hex(muted) << "\">In beeld: "
             << static_cast<int>(m_dynamic_groups.size() - lightRows) << " symbolen"
             << " &middot; " << m_dynamic_total << " waypoints";
        if (m_chart_object_total > 0)
            html << " &middot; " << m_chart_object_total << " kaartobjecten";
        if (lightRows > 0)
            html << " &middot; " << static_cast<int>(lightRows) << " lichtkarakters";
        if (m_show_hidden_at_scale && m_dynamic_hidden + m_chart_objects_scale_hidden > 0) {
            html << " &middot; " << m_dynamic_hidden + m_chart_objects_scale_hidden
                 << " op deze schaal verborgen";
        }
        html << ".</font></p>";

        html << "<p><font size=\"-1\" color=\"" << hex(muted) << "\">"
                "Klik op een rij om naar het dichtstbijzijnde symbool te gaan; "
                "nog een klik gaat naar het volgende. Klik op &#9678; om alle "
                "exemplaren in beeld tegelijk aan te wijzen. De lijst past zich "
                "vanzelf aan als je de kaart verschuift of zoomt.";
        if (showHeadings)
            html << " Klik op een kopje om de groep in of uit te klappen.";
        if (m_chart_object_total > 0)
            html << " Kaartsymbolen komen uit de kaarten in beeld.";
        if (lightsShown)
            html << " Lichtkarakters staan voluit in woorden; het ritme eronder "
                    "is schematisch getekend. Een klik laat het licht op de kaart "
                    "branden in dat ritme.";
        if (m_show_hidden_at_scale)
            html << " Grijze rijen zijn op deze schaal verborgen; een klik "
                    "zoomt in tot ze zichtbaar zijn.";
        html << "</font></p>";
    }

    html << "</body></html>";

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

void legend_pi::ShowDynamicLegend(bool reloadWaypoints)
{
    if (m_content_book)
        m_content_book->SetSelection(1);

    if (StartWaypointLoading(reloadWaypoints)) {
        CollectDynamicLegend();
        RenderDynamicLegend(false);

        // A layer may have been hidden or shown while the legend was closed.
        m_visibility_timer.StartOnce(300);
        return;
    }

    // Loading runs in small steps; the window shows the progress and fills
    // itself at the end.
    RenderLoadingPage();
}

void legend_pi::GoToDynamicGroup(size_t index)
{
    if (index >= m_dynamic_groups.size())
        return;

    DynamicGroup &group = m_dynamic_groups[index];

    if (group.positions.empty())
        return;

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
    StopLightMarker();

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

// The light shines for a few periods, at least a quarter of a minute and
// at most a minute. Only a change between light and dark repaints the
// chart.
void legend_pi::ShowLightMarkers(
    const LightCharacter &light,
    const std::vector<std::pair<double, double>> &positions)
{
    if (positions.empty())
        return;

    std::vector<LightPhase> phases = light.Phases();
    double cycle = 0.0;
    for (const LightPhase &phase : phases)
        cycle += phase.seconds;
    if (cycle <= 0.0) {
        ShowMarkers(positions);
        return;
    }

    StopMarker();

    m_light_positions = positions;
    m_light_rhythm = light.RhythmAbbreviation();
    m_light_phases = phases;
    m_light_colours = light.Colours();
    m_light_cycle = cycle;
    m_light_duration = std::min(60.0, std::max(15.0, 3.0 * cycle));
    m_light_started = wxGetUTCTimeMillis();
    m_light_state = -2; // nothing drawn yet
    m_light_active = true;

    m_light_timer.Stop();
    m_light_timer.Start(40);
    OnLightTimer();
}

void legend_pi::StopLightMarker()
{
    m_light_timer.Stop();

    if (m_light_active) {
        m_light_active = false;
        RefreshAllCanvases();
    }
}

namespace {

// The colour index of a light at a moment in its period, -1 while dark,
// plus kDimLight for the steady part of a fixed and flashing light.
int PhaseStateAt(const std::vector<LightPhase> &phases, double seconds)
{
    double end = 0.0;
    for (const LightPhase &phase : phases) {
        end += phase.seconds;
        if (seconds < end) {
            if (phase.colour < 0)
                return -1;
            return phase.colour + (phase.dim ? kDimLight : 0);
        }
    }
    return -1;
}

} // namespace

int legend_pi::LightStateAt(double seconds) const
{
    return PhaseStateAt(m_light_phases, seconds);
}

void legend_pi::OnLightTimer()
{
    if (!m_light_active) {
        m_light_timer.Stop();
        return;
    }

    const double elapsed =
        (wxGetUTCTimeMillis() - m_light_started).ToDouble() / 1000.0;
    if (elapsed > m_light_duration) {
        StopLightMarker();
        return;
    }

    const int state = LightStateAt(std::fmod(elapsed, m_light_cycle));
    if (state != m_light_state) {
        m_light_state = state;
        RefreshAllCanvases();
    }
}

// Screen positions of the marked objects on a canvas, leaving out those
// far outside it.
std::vector<wxPoint> legend_pi::MarkerCentres(
    PlugIn_ViewPort *vp,
    const std::vector<std::pair<double, double>> &positions) const
{
    std::vector<wxPoint> centres;

    for (const auto &position : positions) {
        wxPoint centre;
        GetCanvasPixLL(vp, &centre, position.first, position.second);

        if (centre.x < -60 || centre.y < -60 ||
            centre.x > vp->pix_width + 60 || centre.y > vp->pix_height + 60)
            continue;

        centres.push_back(centre);
    }

    return centres;
}

bool legend_pi::RenderOverlayMultiCanvas(
    wxDC &dc,
    PlugIn_ViewPort *vp,
    int canvasIndex,
    int priority)
{
    (void)canvasIndex;

    if ((!m_marker_active && !m_light_active && !m_all_lights_on && !m_night_preview &&
         m_vhf_highlight.empty() && m_note_places.empty()) ||
        !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    if (m_night_preview)
        DrawNightShade(dc, vp);
    const bool drewVhf = !m_vhf_highlight.empty() && DrawVhfHighlight(dc, vp);
    const bool drewNotes = DrawNoteSigns(dc, vp);
    const bool drewAll = (m_all_lights_on && DrawAllLights(dc, vp)) || m_night_preview || drewVhf ||
                         drewNotes;
    const bool drewLight = (m_light_active && DrawLightMarkers(dc, vp)) || drewAll;
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

    if ((!m_marker_active && !m_light_active && !m_all_lights_on && !m_night_preview &&
         m_vhf_highlight.empty() && m_note_places.empty()) ||
        !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    if (m_night_preview)
        DrawNightShadeGL(vp);
    const bool drewVhf = !m_vhf_highlight.empty() && DrawVhfHighlightGL(vp);
    const bool drewNotes = DrawNoteSignsGL(vp);
    const bool drewAll = (m_all_lights_on && DrawAllLightsGL(vp)) || m_night_preview || drewVhf ||
                         drewNotes;
    const bool drewLight = (m_light_active && DrawLightMarkersGL(vp)) || drewAll;
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

// The chosen light on the chart. A ring with ticks, like the marker, stays
// where the light is, also while it is dark. While it shines, a large glow
// with rays in the colour of the light fills the ring; the steady part of a
// fixed and flashing light is a smaller, paler glow without rays. Sizes
// follow the pixel density of the screen.
namespace {

struct LightMarkerLook
{
    double scale = 1.0;
    double ring = 0.0;     // radius of the ring
    double glow = 0.0;     // radius of the glow
    double core = 0.0;     // radius of the bright centre
    double rayFrom = 0.0;  // rays run from here...
    double rayTo = 0.0;    // ...to here
    int rays = 8;
};

// Screen angle (radians, y down) of a bearing at a position: the chart can
// be turned, so north is found by projecting a point a little to the north.
double ScreenAngle(PlugIn_ViewPort *vp, double lat, double lon, double bearing)
{
    wxPoint centre;
    wxPoint north;
    GetCanvasPixLL(vp, &centre, lat, lon);
    GetCanvasPixLL(vp, &north, lat + 0.01, lon);
    const double kPi = 3.14159265358979323846;
    const double up = std::atan2(static_cast<double>(north.y - centre.y),
                                 static_cast<double>(north.x - centre.x));
    return up + bearing * kPi / 180.0;
}

// The sectors of a sector light as coloured wedges around it. Sector limits
// are bearings from seaward towards the light; the wedge lies on the other
// side, where the ship sees that colour. Bright while the light shines,
// faint while it is dark.
void DrawSectorsGC(
    wxGraphicsContext *gc,
    PlugIn_ViewPort *vp,
    double lat,
    double lon,
    double cx,
    double cy,
    const std::vector<LightSector> &sectors,
    double radius,
    bool lit,
    const wxColour &outline)
{
    const double kPi = 3.14159265358979323846;
    for (const LightSector &sector : sectors) {
        double sweep = std::fmod(sector.to - sector.from + 360.0, 360.0);
        if (sweep <= 0.0)
            sweep = 360.0;
        const double start = ScreenAngle(vp, lat, lon, sector.from + 180.0);
        const wxColour colour =
            LightDisplayColour(sector.colour, std::max(0.6, SchemeDim()));

        wxGraphicsPath wedge = gc->CreatePath();
        wedge.MoveToPoint(cx, cy);
        wedge.AddArc(cx, cy, radius, start, start + sweep * kPi / 180.0, true);
        wedge.CloseSubpath();
        gc->SetBrush(wxBrush(
            wxColour(colour.Red(), colour.Green(), colour.Blue(), lit ? 120 : 45)));
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(
            wxColour(outline.Red(), outline.Green(), outline.Blue(), lit ? 200 : 90))
                                     .Width(1.2)));
        gc->DrawPath(wedge);
    }
}

void DrawSectorsGL(
    PlugIn_ViewPort *vp,
    double lat,
    double lon,
    double cx,
    double cy,
    const std::vector<LightSector> &sectors,
    double radius,
    bool lit,
    const wxColour &outline)
{
    const double kPi = 3.14159265358979323846;
    for (const LightSector &sector : sectors) {
        double sweep = std::fmod(sector.to - sector.from + 360.0, 360.0);
        if (sweep <= 0.0)
            sweep = 360.0;
        const double start = ScreenAngle(vp, lat, lon, sector.from + 180.0);
        const double end = start + sweep * kPi / 180.0;
        const int steps = std::max(4, static_cast<int>(sweep / 4.0));
        const wxColour colour =
            LightDisplayColour(sector.colour, std::max(0.6, SchemeDim()));

        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), lit ? 120 : 45);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2d(cx, cy);
        for (int i = 0; i <= steps; ++i) {
            const double angle = start + (end - start) * i / steps;
            glVertex2d(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
        }
        glEnd();

        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), lit ? 200 : 90);
        glLineWidth(1.2f);
        glBegin(GL_LINE_LOOP);
        glVertex2d(cx, cy);
        for (int i = 0; i <= steps; ++i) {
            const double angle = start + (end - start) * i / steps;
            glVertex2d(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
        }
        glEnd();
    }
}

LightMarkerLook MakeLightMarkerLook(double pixelsPerPoint, bool dimPart)
{
    LightMarkerLook look;
    look.scale = std::max(1.0, pixelsPerPoint);
    look.ring = 26.0 * look.scale;
    look.glow = (dimPart ? 16.0 : 34.0) * look.scale;
    look.core = (dimPart ? 7.0 : 11.0) * look.scale;
    look.rayFrom = 15.0 * look.scale;
    look.rayTo = 31.0 * look.scale;
    return look;
}

} // namespace

bool legend_pi::DrawLightMarkers(wxDC &dc, PlugIn_ViewPort *vp)
{
    const std::vector<wxPoint> centres = MarkerCentres(vp, m_light_positions);
    if (centres.empty() || m_light_colours.empty())
        return false;

    wxColour outline;
    wxColour highlight;
    MarkerColours(outline, highlight);

    const bool lit = m_light_state >= 0;
    const bool dimPart = m_light_state >= kDimLight;
    const wxColour colour =
        lit ? LightDisplayColour(
                  m_light_colours[static_cast<size_t>(m_light_state % kDimLight) %
                                  m_light_colours.size()],
                  std::max(0.6, SchemeDim()))
            : wxColour();
    const LightMarkerLook look =
        MakeLightMarkerLook(ViewportPixelsPerWindowPixel(), dimPart);
    const double s = look.scale;
    const double kPi = 3.14159265358979323846;

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));

    // Sectors first, under the light.
    if (gc) {
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        for (const auto &position : m_light_positions) {
            const std::vector<LightSector> sectors =
                SectorsAt(position.first, position.second, m_light_rhythm);
            if (sectors.empty())
                continue;
            wxPoint centre;
            GetCanvasPixLL(vp, &centre, position.first, position.second);
            DrawSectorsGC(gc.get(), vp, position.first, position.second, centre.x,
                          centre.y, sectors, 90.0 * look.scale, lit, outline);
        }
    }

    for (const wxPoint &centre : centres) {
        const double cx = centre.x;
        const double cy = centre.y;

        if (!gc) {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(outline, static_cast<int>(6 * s)));
            dc.DrawCircle(centre, static_cast<int>(look.ring));
            dc.SetPen(wxPen(highlight, static_cast<int>(3 * s)));
            dc.DrawCircle(centre, static_cast<int>(look.ring));
            if (lit) {
                dc.SetPen(wxPen(outline, static_cast<int>(2 * s)));
                dc.SetBrush(wxBrush(colour));
                dc.DrawCircle(centre, static_cast<int>(look.core * 1.6));
            }
            continue;
        }

        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        // The glow under everything else.
        if (lit) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(gc->CreateRadialGradientBrush(
                cx, cy, cx, cy, look.glow,
                wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 120 : 210),
                wxColour(colour.Red(), colour.Green(), colour.Blue(), 0)));
            gc->DrawEllipse(cx - look.glow, cy - look.glow, 2 * look.glow, 2 * look.glow);
        }

        // The ring with ticks, as the marker.
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        const double tick = 12.0 * s;
        for (int pass = 0; pass < 2; ++pass) {
            gc->SetPen(gc->CreatePen(
                wxGraphicsPenInfo(pass == 0 ? outline : highlight).Width((pass == 0 ? 6.0 : 3.0) * s)));
            gc->DrawEllipse(cx - look.ring, cy - look.ring, 2 * look.ring, 2 * look.ring);
            gc->StrokeLine(cx - look.ring - tick, cy, cx - look.ring + 2 * s, cy);
            gc->StrokeLine(cx + look.ring - 2 * s, cy, cx + look.ring + tick, cy);
            gc->StrokeLine(cx, cy - look.ring - tick, cx, cy - look.ring + 2 * s);
            gc->StrokeLine(cx, cy + look.ring - 2 * s, cx, cy + look.ring + tick);
        }

        if (!lit)
            continue;

        // Rays, outlined so that white and yellow show on a light chart.
        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                gc->SetPen(gc->CreatePen(
                    wxGraphicsPenInfo(pass == 0 ? outline : colour)
                        .Width((pass == 0 ? 5.0 : 3.0) * s)
                        .Cap(wxCAP_ROUND)));
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    gc->StrokeLine(cx + look.rayFrom * std::cos(angle),
                                   cy + look.rayFrom * std::sin(angle),
                                   cx + look.rayTo * std::cos(angle),
                                   cy + look.rayTo * std::sin(angle));
                }
            }
        }

        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(outline).Width(2.0 * s)));
        gc->SetBrush(wxBrush(
            wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255)));
        gc->DrawEllipse(cx - look.core, cy - look.core, 2 * look.core, 2 * look.core);
    }

    return true;
}

bool legend_pi::DrawLightMarkersGL(PlugIn_ViewPort *vp)
{
    const std::vector<wxPoint> centres = MarkerCentres(vp, m_light_positions);
    if (centres.empty() || m_light_colours.empty())
        return false;

    wxColour outline;
    wxColour highlight;
    MarkerColours(outline, highlight);

    const bool lit = m_light_state >= 0;
    const bool dimPart = m_light_state >= kDimLight;
    const wxColour colour =
        lit ? LightDisplayColour(
                  m_light_colours[static_cast<size_t>(m_light_state % kDimLight) %
                                  m_light_colours.size()],
                  std::max(0.6, SchemeDim()))
            : wxColour();
    const LightMarkerLook look =
        MakeLightMarkerLook(ViewportPixelsPerWindowPixel(), dimPart);
    const double s = look.scale;

    // GL_CURRENT_BIT included: see RenderGLOverlayMultiCanvas.
    // GL_LIGHTING_BIT keeps the shade model, set to smooth for the glow.
    glPushAttrib(
        GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
        GL_HINT_BIT | GL_LIGHTING_BIT);

    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    const int segments = 64;
    const double kPi = 3.14159265358979323846;

    auto circle = [&](double x, double y, double r) {
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    // A disc that fades from the centre colour to transparent at its edge.
    auto glowDisc = [&](double x, double y, double r, unsigned char alpha) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), alpha);
        glVertex2d(x, y);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 0);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    auto disc = [&](double x, double y, double r) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2d(x, y);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    auto ticks = [&](double x, double y, double r, double length) {
        glBegin(GL_LINES);
        glVertex2d(x - r - length, y);
        glVertex2d(x - r + 2 * s, y);
        glVertex2d(x + r - 2 * s, y);
        glVertex2d(x + r + length, y);
        glVertex2d(x, y - r - length);
        glVertex2d(x, y - r + 2 * s);
        glVertex2d(x, y + r - 2 * s);
        glVertex2d(x, y + r + length);
        glEnd();
    };

    // Sectors first, under the light.
    for (const auto &position : m_light_positions) {
        const std::vector<LightSector> sectors =
            SectorsAt(position.first, position.second, m_light_rhythm);
        if (sectors.empty())
            continue;
        wxPoint centre;
        GetCanvasPixLL(vp, &centre, position.first, position.second);
        DrawSectorsGL(vp, position.first, position.second, centre.x, centre.y,
                      sectors, 90.0 * s, lit, outline);
    }

    for (const wxPoint &centre : centres) {
        const double cx = centre.x;
        const double cy = centre.y;

        if (lit)
            glowDisc(cx, cy, look.glow, dimPart ? 120 : 210);

        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(static_cast<float>(6.0 * s));
        circle(cx, cy, look.ring);
        ticks(cx, cy, look.ring, 12.0 * s);
        glColor4ub(highlight.Red(), highlight.Green(), highlight.Blue(), 255);
        glLineWidth(static_cast<float>(3.0 * s));
        circle(cx, cy, look.ring);
        ticks(cx, cy, look.ring, 10.0 * s);

        if (!lit)
            continue;

        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0)
                    glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
                else
                    glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 255);
                glLineWidth(static_cast<float>((pass == 0 ? 5.0 : 3.0) * s));
                glBegin(GL_LINES);
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    glVertex2d(cx + look.rayFrom * std::cos(angle),
                               cy + look.rayFrom * std::sin(angle));
                    glVertex2d(cx + look.rayTo * std::cos(angle),
                               cy + look.rayTo * std::sin(angle));
                }
                glEnd();
            }
        }

        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255);
        disc(cx, cy, look.core);
        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(static_cast<float>(2.0 * s));
        circle(cx, cy, look.core);
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();

    return true;
}

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

    if (!IsDynamicLegendVisible() || m_loading || m_showing_loading_page ||
        m_reorder_mode)
        return;

    // VhfAreasHere() keeps the key of what it found.
    const wxString previous = m_vhf_shown_key;
    bool fromShip = false;
    bool layerShown = false;
    VhfAreasHere(&fromShip, &layerShown);

    // What lies ahead changes with every fix while sailing; the list is
    // redrawn at most every five seconds for it.
    ComputeAhead();
    const bool aheadChanged =
        m_ahead_key != m_ahead_drawn_key &&
        (wxGetUTCTimeMillis() - m_ahead_rendered) >= 5000;

    if (m_vhf_shown_key != previous || aheadChanged)
        RenderDynamicLegend(true);
}

// The objects of the list in a fan ahead of the boat: a strip that widens
// with the distance (at least 0.12 sea mile on either side, widening at
// 15 degrees), as far as the boat goes in half an hour.
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

    const double kPi = 3.14159265358979323846;
    const double course = m_ship_cog * kPi / 180.0;
    const double range = std::min(8.0, std::max(0.3, m_ship_sog * 0.5));
    const double cosLat = std::cos(m_ship_lat * kPi / 180.0);
    const double widen = std::tan(15.0 * kPi / 180.0);

    struct Found
    {
        AheadItem item;
        double along;
        double lat;
        double lon;
    };
    std::vector<Found> found;

    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        const DynamicGroup &group = m_dynamic_groups[i];
        // Lights under a row come with their row; flares double a buoy.
        if (!group.parentKey.IsEmpty() || group.lightSymbol)
            continue;

        for (size_t j = 0; j < group.positions.size(); ++j) {
            const double lat = group.positions[j].first;
            const double lon = group.positions[j].second;
            const double dy = (lat - m_ship_lat) * 60.0;
            double dlon = lon - m_ship_lon;
            while (dlon > 180.0)
                dlon -= 360.0;
            while (dlon < -180.0)
                dlon += 360.0;
            const double dx = dlon * 60.0 * cosLat;

            const double along = dx * std::sin(course) + dy * std::cos(course);
            const double cross = dx * std::cos(course) - dy * std::sin(course);
            if (along < 0.01 || along > range ||
                std::fabs(cross) > std::max(0.12, along * widen))
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
                for (size_t k = i + 1; k < m_dynamic_groups.size() &&
                                       m_dynamic_groups[k].parentKey == group.key;
                     ++k) {
                    for (const auto &p : m_dynamic_groups[k].positions) {
                        if (std::fabs(p.first - lat) < 0.0003 &&
                            std::fabs(p.second - lon) < 0.0005) {
                            f.item.light = m_dynamic_groups[k].lightText;
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
    // the first, or the one that knows its light.
    const size_t kMaxItems = 8;
    wxString key;
    for (const Found &f : found) {
        bool duplicate = false;
        for (AheadItem &kept : m_ahead) {
            const auto &p = m_dynamic_groups[kept.group].positions[kept.position];
            if (std::fabs(p.first - f.lat) < 0.0002 && std::fabs(p.second - f.lon) < 0.0003) {
                if (kept.light.IsEmpty() && !f.item.light.IsEmpty())
                    kept = f.item;
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (m_ahead.size() >= kMaxItems)
            break;
        m_ahead.push_back(f.item);
    }

    for (const AheadItem &item : m_ahead) {
        key << m_dynamic_groups[item.group].key << '@' << item.position << '/'
            << static_cast<int>(item.minutes) << (item.cross >= 0.0 ? 's' : 'p') << ';';
    }

    // Notes whose place lies in the same fan, up to three.
    m_ahead_notes.clear();
    for (const NotePlace &place : m_note_places) {
        const double dy = (place.lat - m_ship_lat) * 60.0;
        double dlon = place.lon - m_ship_lon;
        while (dlon > 180.0)
            dlon -= 360.0;
        while (dlon < -180.0)
            dlon += 360.0;
        const double dx = dlon * 60.0 * cosLat;
        const double along = dx * std::sin(course) + dy * std::cos(course);
        const double cross = dx * std::cos(course) - dy * std::sin(course);
        if (along < 0.01 || along > range ||
            std::fabs(cross) > std::max(0.12, along * widen) + place.radius)
            continue;
        AheadNote note;
        note.minutes = along / m_ship_sog * 60.0;
        note.cross = cross;
        note.title = place.title;
        note.path = place.path;
        m_ahead_notes.push_back(note);
    }
    std::sort(m_ahead_notes.begin(), m_ahead_notes.end(),
              [](const AheadNote &a, const AheadNote &b) { return a.minutes < b.minutes; });
    if (m_ahead_notes.size() > 3)
        m_ahead_notes.resize(3);
    for (const AheadNote &note : m_ahead_notes)
        key << "note:" << note.path << '/' << static_cast<int>(note.minutes) << ';';

    // The VHF areas the course enters in the same half hour, up to two:
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
            const double lat = m_ship_lat + along * std::cos(course) / 60.0;
            const double lon = m_ship_lon + along * std::sin(course) / (60.0 * cosLat);
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
    ShowVhfNotice("Geen positie van de boot",
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
    m_note_places = ReadNotePlaces(paths);
    m_note_selected = -1;
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
    if (m_vhf_notice && m_vhf_notice->IsShown())
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
    ShowVhfNotice(m_pending_note_title, m_pending_note_text, true, wxColour(186, 117, 23),
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

// A click on a line of the "Vooruit" block: that object on the chart.
void legend_pi::ShowAheadItem(size_t index)
{
    if (index >= m_ahead.size())
        return;
    const AheadItem item = m_ahead[index];
    if (item.group >= m_dynamic_groups.size())
        return;
    DynamicGroup &group = m_dynamic_groups[item.group];
    if (item.position >= group.positions.size())
        return;

    group.current = static_cast<int>(item.position);
    m_active_group_key = group.key;
    m_active_light_key.Clear();

    const auto position = group.positions[item.position];
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
        if (!m_vhf_highlight.empty())
            StopVhfHighlight();
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

    ShowVhfHighlight(lit);
    return true;
}

// Lights up these areas: for six seconds, or until the next click on the
// chart when the setting says so.
void legend_pi::ShowVhfHighlight(const std::vector<VhfArea> &areas)
{
    m_vhf_highlight = areas;
    m_vhf_highlight_started = wxGetUTCTimeMillis();
    m_vhf_highlight_alpha = 1.0;
    if (m_vhf_keep_lit)
        m_vhf_highlight_timer.Stop();
    else
        m_vhf_highlight_timer.Start(50);
    RefreshAllCanvases();
}

// The areas at a spot, with every other part of each of them.
std::vector<VhfArea> legend_pi::VhfAreasWithParts(double lat, double lon)
{
    std::vector<VhfArea> result;
    std::set<wxString> taken;
    for (const VhfArea *area : m_vhf_areas.At(lat, lon)) {
        for (const VhfArea &part : m_vhf_areas.All()) {
            if (part.description == area->description &&
                taken.insert(part.name + "|" + part.description + "|" +
                             wxString::Format("%zu", part.polygon.size()) +
                             wxString::Format("%.6f", part.latMin))
                    .second)
                result.push_back(part);
        }
    }
    return result;
}

// One line about an area: "kanaal 18 · Ketelbrug · verkeerspost · melden".
wxString legend_pi::VhfAreaLine(const VhfArea &area) const
{
    const bool nl = m_dutch_names;
    wxString line = (nl ? "kanaal " : "channel ") + area.channel;
    const wxString who = area.callName.IsEmpty() ? area.name : area.callName;
    if (!who.IsEmpty())
        line << wxString::FromUTF8(" \u00b7 ") << who;
    if (!area.type.IsEmpty())
        line << wxString::FromUTF8(" \u00b7 ") << VhfTypeWord(area.type, nl);
    if (!area.usage.IsEmpty())
        line << wxString::FromUTF8(" \u00b7 ") << VhfUsageWord(area.usage, nl);
    return line;
}

// "Marifoon hier" from the chart's menu.
void legend_pi::ShowVhfHere(double lat, double lon)
{
    EnsureVhfAreas();
    const std::vector<VhfArea> parts = VhfAreasWithParts(lat, lon);
    wxString text;
    std::set<wxString> said;
    for (const VhfArea *area : m_vhf_areas.At(lat, lon)) {
        const wxString line = VhfAreaLine(*area);
        if (said.insert(line).second)
            text << (text.IsEmpty() ? "" : "\n") << line;
    }
    if (m_vhf_areas.IsEmpty())
        text = "Geen marifoongebieden geladen (laag VHFinfo Nederland).";
    else if (text.IsEmpty())
        text = "Hier geldt geen marifoongebied uit VHFinfo Nederland.";
    ShowVhfNotice("Marifoon hier", text, false);
    if (!parts.empty())
        ShowVhfHighlight(parts);
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
        text << (text.IsEmpty() ? "" : "\n") << VhfAreaLine(*area);
    ShowVhfNotice("Nieuw marifoongebied", text, true);
    ShowVhfHighlight(VhfAreasWithParts(m_ship_lat, m_ship_lon));
}

// False while OpenCPN is still busy with the previous sound: on macOS it
// plays one at a time, through a system command that takes a moment.
bool legend_pi::PlayVhfSound()
{
    wxString *shared = GetpSharedDataLocation();
    if (!shared)
        return true;
    wxString file = *shared + "sounds" + wxFileName::GetPathSeparator() + "beep1.wav";
    if (!wxFileExists(file))
        return true;
    wxLogNull quiet; // a refused try is not worth a warning in the log
    return PlugInPlaySoundEx(file, -1);
}

// A small window at the top of the chart that goes away on its own after a
// while, or with a click.
void legend_pi::ShowVhfNotice(const wxString &title, const wxString &text, bool sound,
                              const wxColour &titleColour, std::function<void()> action)
{
    m_notice_action = action;
    wxWindow *canvas = GetOCPNCanvasWindow();
    if (!canvas)
        return;

    if (!m_vhf_notice) {
        m_vhf_notice = new wxFrame(canvas, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize,
                                   wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT |
                                       wxFRAME_NO_TASKBAR | wxBORDER_SIMPLE);
        auto *panel = new wxPanel(m_vhf_notice);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        m_vhf_notice_title = new wxStaticText(panel, wxID_ANY, wxEmptyString);
        wxFont bold = m_vhf_notice_title->GetFont();
        bold.SetWeight(wxFONTWEIGHT_BOLD);
        bold.SetPointSize(bold.GetPointSize() + 1);
        m_vhf_notice_title->SetFont(bold);
        m_vhf_notice_text = new wxStaticText(panel, wxID_ANY, wxEmptyString);
        sizer->Add(m_vhf_notice_title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        sizer->Add(m_vhf_notice_text, 0, wxALL, 12);
        panel->SetSizer(sizer);
        // A click closes it, and opens what it is about, if anything.
        auto close = [this](wxMouseEvent &) {
            if (m_vhf_notice)
                m_vhf_notice->Hide();
            if (m_notice_action) {
                std::function<void()> action = m_notice_action;
                m_notice_action = std::function<void()>();
                action();
            }
        };
        panel->Bind(wxEVT_LEFT_DOWN, close);
        m_vhf_notice_title->Bind(wxEVT_LEFT_DOWN, close);
        m_vhf_notice_text->Bind(wxEVT_LEFT_DOWN, close);
        m_vhf_notice_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
            if (m_vhf_notice)
                m_vhf_notice->Hide();
        });
    }

    const LegendPalette palette = GetLegendPalette();
    wxWindow *panel = m_vhf_notice->GetChildren().GetFirst()->GetData();
    panel->SetBackgroundColour(palette.background);
    m_vhf_notice_title->SetForegroundColour(titleColour);
    m_vhf_notice_text->SetForegroundColour(palette.text);
    m_vhf_notice_title->SetLabel(title);
    m_vhf_notice_text->SetLabel(text);
    panel->GetSizer()->Fit(panel);
    m_vhf_notice->SetClientSize(panel->GetBestSize());
    panel->SetSize(m_vhf_notice->GetClientSize());

    // At the top of the chart, in the middle.
    const wxRect area = canvas->GetScreenRect();
    const wxSize size = m_vhf_notice->GetSize();
    m_vhf_notice->Move(area.x + (area.width - size.x) / 2, area.y + 16);
    m_vhf_notice->ShowWithoutActivating();
    m_vhf_notice->Raise();
    m_vhf_notice_timer.StartOnce(12000);

    // OpenCPN's short beep six times, so it is heard in the cockpit.
    if (sound) {
        m_vhf_beeps_left = 6;
        if (PlayVhfSound())
            --m_vhf_beeps_left;
        m_vhf_sound_timer.Start(250);
    }
}

void legend_pi::StopVhfHighlight()
{
    m_vhf_highlight_timer.Stop();
    m_vhf_highlight.clear();
    RefreshAllCanvases();
}

// Lit for six seconds, then fading out in one and a half.
void legend_pi::OnVhfHighlightTimer()
{
    if (m_vhf_keep_lit) {
        m_vhf_highlight_timer.Stop();
        return;
    }
    const double elapsed =
        (wxGetUTCTimeMillis() - m_vhf_highlight_started).ToDouble() / 1000.0;
    const double hold = 6.0;
    const double fade = 1.5;
    if (elapsed >= hold + fade) {
        StopVhfHighlight();
        return;
    }
    const double alpha = elapsed <= hold ? 1.0 : 1.0 - (elapsed - hold) / fade;
    if (std::fabs(alpha - m_vhf_highlight_alpha) > 0.02 || alpha == 1.0) {
        const bool changed = alpha != m_vhf_highlight_alpha;
        m_vhf_highlight_alpha = alpha;
        if (changed)
            RefreshAllCanvases();
    }
}

namespace {

// Light red, as the selected area on the map of vhfinfo.org (fill #ff6666
// at 42 %, a red edge); softer in dusk and night.
void VhfHighlightColours(double alpha, wxColour *fill, wxColour *line)
{
    const double dim = IsDimScheme() ? 0.6 : 1.0;
    *fill = wxColour(255, 102, 102, static_cast<unsigned char>(107 * alpha * dim));
    *line = wxColour(255, 0, 0, static_cast<unsigned char>(200 * alpha * dim));
}

} // namespace

bool legend_pi::DrawVhfHighlight(wxDC &dc, PlugIn_ViewPort *vp)
{
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return false;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    wxColour fill;
    wxColour line;
    VhfHighlightColours(m_vhf_highlight_alpha, &fill, &line);
    const double s = std::max(1.0, ViewportPixelsPerWindowPixel());

    for (const VhfArea &area : m_vhf_highlight) {
        wxGraphicsPath path = gc->CreatePath();
        for (size_t i = 0; i < area.polygon.size(); ++i) {
            wxPoint at;
            GetCanvasPixLL(vp, &at, area.polygon[i].first, area.polygon[i].second);
            if (i == 0)
                path.MoveToPoint(at.x, at.y);
            else
                path.AddLineToPoint(at.x, at.y);
        }
        path.CloseSubpath();
        gc->SetBrush(wxBrush(fill));
        gc->FillPath(path, wxODDEVEN_RULE);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(2.0 * s).Join(wxJOIN_ROUND)));
        gc->StrokePath(path);
    }
    return true;
}

bool legend_pi::DrawVhfHighlightGL(PlugIn_ViewPort *vp)
{
    wxColour fill;
    wxColour line;
    VhfHighlightColours(m_vhf_highlight_alpha, &fill, &line);
    const double s = std::max(1.0, ViewportPixelsPerWindowPixel());

    glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_HINT_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    for (const VhfArea &area : m_vhf_highlight) {
        std::vector<wxPoint> points(area.polygon.size());
        for (size_t i = 0; i < area.polygon.size(); ++i)
            GetCanvasPixLL(vp, &points[i], area.polygon[i].first, area.polygon[i].second);

        glColor4ub(fill.Red(), fill.Green(), fill.Blue(), fill.Alpha());
        glBegin(GL_TRIANGLES);
        for (int index : area.triangles)
            glVertex2d(points[index].x, points[index].y);
        glEnd();

        glColor4ub(line.Red(), line.Green(), line.Blue(), line.Alpha());
        glLineWidth(static_cast<float>(2.0 * s));
        glBegin(GL_LINE_LOOP);
        for (size_t i = 0; i + 1 < points.size(); ++i)
            glVertex2d(points[i].x, points[i].y);
        glEnd();
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();
    return true;
}

std::vector<const VhfArea *> legend_pi::VhfAreasHere(bool *fromShip, bool *layerShown)
{
    EnsureVhfAreas();

    // Shown only while a layer with these areas is shown in OpenCPN: one
    // of its waypoints is visible.
    *layerShown = false;
    if (!m_vhf_areas.IsEmpty()) {
        for (const CachedWaypoint &wp : m_waypoint_cache) {
            if (wp.visible && m_vhf_areas.Icons().count(wp.icon) > 0) {
                *layerShown = true;
                break;
            }
        }
    }

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
        m_night_had_all_lights = m_all_lights_on;
        if (!m_all_lights_on)
            SetAllLightsShining(true);
    }
    else if (!m_night_had_all_lights) {
        SetAllLightsShining(false);
    }
    RefreshAllCanvases();
}

namespace {

// Night blue, dark enough that only lights stand out, light enough that
// coast and fairways stay faintly visible.
const wxColour kNightShade(4, 8, 22, 222);

} // namespace

void legend_pi::DrawNightShade(wxDC &dc, PlugIn_ViewPort *vp)
{
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return;
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(kNightShade));
    gc->DrawRectangle(0, 0, vp->pix_width, vp->pix_height);
}

void legend_pi::DrawNightShadeGL(PlugIn_ViewPort *vp)
{
    glPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(kNightShade.Red(), kNightShade.Green(), kNightShade.Blue(),
               kNightShade.Alpha());
    glBegin(GL_QUADS);
    glVertex2d(0, 0);
    glVertex2d(vp->pix_width, 0);
    glVertex2d(vp->pix_width, vp->pix_height);
    glVertex2d(0, vp->pix_height);
    glEnd();
    glColor4ub(255, 255, 255, 255);
    glPopAttrib();
}

void legend_pi::SetAllLightsShining(bool on)
{
    m_all_lights_on = on;

    if (!on) {
        m_all_lights_timer.Stop();
        m_shining_lights.clear();
        m_shining_characters.clear();
        RefreshAllCanvases();
        return;
    }

    m_all_lights_started = wxGetUTCTimeMillis();
    UpdateAllLights();
    m_all_lights_timer.Stop();
    m_all_lights_timer.Start(50);
    OnAllLightsTimer();
}

// The lights of the list, so the shining lights follow the chart. One light
// that is listed twice (a waypoint and a chart light) shines once.
void legend_pi::UpdateAllLights()
{
    m_shining_lights.clear();
    m_shining_characters.clear();

    std::map<wxString, size_t> characters;
    std::set<std::pair<long, long>> taken;

    for (const DynamicGroup &group : m_dynamic_groups) {
        // Only what the chart draws at this scale.
        if (!group.lightCharacter || group.hiddenAtScale)
            continue;

        auto known = characters.find(group.lightText);
        size_t index = 0;
        if (known != characters.end()) {
            index = known->second;
        }
        else {
            LightCharacter light;
            if (!light.Parse(group.lightText))
                continue;
            ShiningCharacter character;
            character.phases = light.Phases();
            character.colours = light.Colours();
            character.rhythm = light.RhythmAbbreviation();
            for (const LightPhase &phase : character.phases)
                character.cycle += phase.seconds;
            if (character.cycle <= 0.0)
                continue;
            index = m_shining_characters.size();
            m_shining_characters.push_back(character);
            characters[group.lightText] = index;
        }

        for (const auto &position : group.positions) {
            const auto cell = std::make_pair(
                static_cast<long>(std::floor(position.first / 0.0003)),
                static_cast<long>(std::floor(position.second / 0.0005)));
            if (!taken.insert(cell).second)
                continue;

            ShiningLight shining;
            shining.lat = position.first;
            shining.lon = position.second;
            shining.character = index;
            // A fixed start within the period, taken from the position, so
            // the lights do not flash in step and keep their start when the
            // list is rebuilt.
            shining.offset = std::fmod(
                std::fabs(position.first * 7919.0 + position.second * 104729.0),
                m_shining_characters[index].cycle);
            m_shining_lights.push_back(shining);
        }
    }
}

void legend_pi::OnAllLightsTimer()
{
    if (!m_all_lights_on) {
        m_all_lights_timer.Stop();
        return;
    }

    const double elapsed =
        (wxGetUTCTimeMillis() - m_all_lights_started).ToDouble() / 1000.0;

    bool changed = false;
    for (ShiningLight &light : m_shining_lights) {
        const ShiningCharacter &character = m_shining_characters[light.character];
        const int state = PhaseStateAt(
            character.phases, std::fmod(elapsed + light.offset, character.cycle));
        if (state != light.state) {
            light.state = state;
            changed = true;
        }
    }

    if (changed)
        RefreshAllCanvases();
}

namespace {

// The colour a shining light has now, or false while it is dark.
bool ShiningColour(
    int state,
    const std::vector<wxString> &colours,
    wxColour *colour,
    bool *dimPart)
{
    if (state < 0 || colours.empty())
        return false;
    *dimPart = state >= kDimLight;
    *colour = LightDisplayColour(
        colours[static_cast<size_t>(state % kDimLight) % colours.size()],
        std::max(0.6, SchemeDim()));
    return true;
}

} // namespace

// All lights in view: glow, rays and centre as for a chosen light, smaller
// and without the ring, so the chart stays readable.
bool legend_pi::DrawAllLights(wxDC &dc, PlugIn_ViewPort *vp)
{
    if (m_shining_lights.empty())
        return false;

    wxColour outline;
    wxColour highlight;
    MarkerColours(outline, highlight);
    const double pixels = ViewportPixelsPerWindowPixel() * 0.65;
    const double kPi = 3.14159265358979323846;

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (!gc)
        return false;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    bool drew = false;
    for (const ShiningLight &light : m_shining_lights) {
        wxColour colour;
        bool dimPart = false;
        if (!ShiningColour(light.state, m_shining_characters[light.character].colours,
                           &colour, &dimPart))
            continue;

        wxPoint centre;
        GetCanvasPixLL(vp, &centre, light.lat, light.lon);
        if (centre.x < -40 || centre.y < -40 || centre.x > vp->pix_width + 40 ||
            centre.y > vp->pix_height + 40)
            continue;

        const LightMarkerLook look = MakeLightMarkerLook(pixels, dimPart);
        const double s = std::max(0.65, pixels);
        const double cx = centre.x;
        const double cy = centre.y;

        const std::vector<LightSector> sectors = SectorsAt(
            light.lat, light.lon, m_shining_characters[light.character].rhythm);
        if (!sectors.empty())
            DrawSectorsGC(gc.get(), vp, light.lat, light.lon, cx, cy, sectors,
                          60.0 * s, true, outline);

        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(gc->CreateRadialGradientBrush(
            cx, cy, cx, cy, look.glow,
            wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 120 : 210),
            wxColour(colour.Red(), colour.Green(), colour.Blue(), 0)));
        gc->DrawEllipse(cx - look.glow, cy - look.glow, 2 * look.glow, 2 * look.glow);

        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                gc->SetPen(gc->CreatePen(
                    wxGraphicsPenInfo(pass == 0 ? outline : colour)
                        .Width((pass == 0 ? 4.0 : 2.5) * s)
                        .Cap(wxCAP_ROUND)));
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    gc->StrokeLine(cx + look.rayFrom * std::cos(angle),
                                   cy + look.rayFrom * std::sin(angle),
                                   cx + look.rayTo * std::cos(angle),
                                   cy + look.rayTo * std::sin(angle));
                }
            }
        }

        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(outline).Width(1.5 * s)));
        gc->SetBrush(wxBrush(
            wxColour(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255)));
        gc->DrawEllipse(cx - look.core, cy - look.core, 2 * look.core, 2 * look.core);
        drew = true;
    }
    return drew;
}

bool legend_pi::DrawAllLightsGL(PlugIn_ViewPort *vp)
{
    if (m_shining_lights.empty())
        return false;

    wxColour outline;
    wxColour highlight;
    MarkerColours(outline, highlight);
    const double pixels = ViewportPixelsPerWindowPixel() * 0.65;
    const double s = std::max(0.65, pixels);
    const int segments = 40;
    const double kPi = 3.14159265358979323846;

    glPushAttrib(
        GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
        GL_HINT_BIT | GL_LIGHTING_BIT);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    bool drew = false;
    for (const ShiningLight &light : m_shining_lights) {
        wxColour colour;
        bool dimPart = false;
        if (!ShiningColour(light.state, m_shining_characters[light.character].colours,
                           &colour, &dimPart))
            continue;

        wxPoint centre;
        GetCanvasPixLL(vp, &centre, light.lat, light.lon);
        if (centre.x < -40 || centre.y < -40 || centre.x > vp->pix_width + 40 ||
            centre.y > vp->pix_height + 40)
            continue;

        const LightMarkerLook look = MakeLightMarkerLook(pixels, dimPart);
        const double cx = centre.x;
        const double cy = centre.y;

        const std::vector<LightSector> sectors = SectorsAt(
            light.lat, light.lon, m_shining_characters[light.character].rhythm);
        if (!sectors.empty())
            DrawSectorsGL(vp, light.lat, light.lon, cx, cy, sectors, 60.0 * s, true,
                          outline);

        glBegin(GL_TRIANGLE_FAN);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 120 : 210);
        glVertex2d(cx, cy);
        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 0);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(cx + look.glow * std::cos(angle), cy + look.glow * std::sin(angle));
        }
        glEnd();

        if (!dimPart) {
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 0)
                    glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
                else
                    glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 255);
                glLineWidth(static_cast<float>((pass == 0 ? 4.0 : 2.5) * s));
                glBegin(GL_LINES);
                for (int r = 0; r < look.rays; ++r) {
                    const double angle = kPi / 8.0 + 2.0 * kPi * r / look.rays;
                    glVertex2d(cx + look.rayFrom * std::cos(angle),
                               cy + look.rayFrom * std::sin(angle));
                    glVertex2d(cx + look.rayTo * std::cos(angle),
                               cy + look.rayTo * std::sin(angle));
                }
                glEnd();
            }
        }

        glColor4ub(colour.Red(), colour.Green(), colour.Blue(), dimPart ? 170 : 255);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2d(cx, cy);
        for (int i = 0; i <= segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(cx + look.core * std::cos(angle), cy + look.core * std::sin(angle));
        }
        glEnd();
        glColor4ub(outline.Red(), outline.Green(), outline.Blue(), 255);
        glLineWidth(static_cast<float>(1.5 * s));
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(cx + look.core * std::cos(angle), cy + look.core * std::sin(angle));
        }
        glEnd();
        drew = true;
    }

    glColor4ub(255, 255, 255, 255);
    glLineWidth(1.0f);
    glPopAttrib();
    return drew;
}
