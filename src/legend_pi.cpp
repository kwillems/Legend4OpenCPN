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
#include <wx/dcmemory.h>
#include <wx/dir.h>
#include <wx/display.h>
#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filesys.h>
#include <wx/fs_mem.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/statline.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <wx/xml/xml.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <memory>
#include <set>
#include <string>

extern "C" {
#include "md4c-html.h"
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

// The "memory:" file system handler for the icons of the dynamic legend.
// Its code is compiled into this plugin, so it must be removed again in
// DeInit: wxWidgets deletes registered handlers only when OpenCPN exits,
// long after the plugin library has been unloaded.
wxMemoryFSHandler *g_memoryHandler = nullptr;

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

    wxString m_baseDirectory;
    bool m_overRow = false;
};

void Md4cOutputCallback(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    auto *out = static_cast<std::string *>(userdata);
    out->append(text, size);
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

// OpenCPN keeps waypoint icons as PNG files: user icons in the private data
// directory (UserIcons) and the built-in ones in the shared data directory.
// Reading the file ourselves is more reliable than asking OpenCPN for a
// bitmap, which can come back empty on macOS.
wxImage LoadIconFromFile(const wxString &key, const wxColour &background)
{
    if (key.IsEmpty() || key.Find('/') != wxNOT_FOUND ||
        key.Find('\\') != wxNOT_FOUND || key.Find("..") != wxNOT_FOUND)
        return wxImage();

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
    "# De icoonnaam is de bestandsnaam van het icoon zonder .png, zoals\n"
    "# de legenda hem in grijs toont zolang er geen betekenis bekend is.\n"
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
    "# Regels die met # beginnen worden overgeslagen. Wijzigingen zijn\n"
    "# zichtbaar na Ververs of zodra de kaart verschuift.\n"
    "#\n"
    "# Voorbeeld:\n"
    "# [Marifoon]\n"
    "# vhf_b11b_*    = Meldplicht marifoon, kanaal *\n"
    "# vhf_*         =\n"
    "#\n"
    "# [Betonning]\n"
    "# 022           = Groene spitse ton\n"
    "\n";

// OpenCPN's colour scheme (day, dusk, night), set through SetColorScheme().
PI_ColorScheme g_color_scheme = PI_GLOBAL_COLOR_SCHEME_DAY;

bool IsDimScheme()
{
    return g_color_scheme == PI_GLOBAL_COLOR_SCHEME_DUSK ||
           g_color_scheme == PI_GLOBAL_COLOR_SCHEME_NIGHT;
}

// Brightness of icons and images in the current scheme. OpenCPN has darker
// symbol sets for dusk and night; user icons only exist in one version, so
// they are dimmed instead.
double SchemeDim()
{
    switch (g_color_scheme) {
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
    const bool night = g_color_scheme == PI_GLOBAL_COLOR_SCHEME_NIGHT;

    wxColour background;
    wxColour text;
    const bool fromOpenCPN =
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

    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
           WANTS_ONPAINT_VIEWPORT | WANTS_OVERLAY_CALLBACK |
           WANTS_OPENGL_OVERLAY_CALLBACK |
           WANTS_DYNAMIC_OPENGL_OVERLAY_CALLBACK;
}

bool legend_pi::DeInit()
{
    SaveCurrentMarkdownScroll();
    SaveWindowState();
    SaveConfig();
    StopMarker();
    m_auto_refresh_timer.Stop();
    m_loading_timer.Stop();
    m_prefetch_timer.Stop();
    m_visibility_timer.Stop();
    m_loading = false;
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
    }

    if (g_memoryHandler) {
        wxFileSystem::RemoveHandler(g_memoryHandler);
        delete g_memoryHandler;
        g_memoryHandler = nullptr;
    }

    if (m_toolbar_item_id >= 0) {
        RemovePlugInTool(m_toolbar_item_id);
        m_toolbar_item_id = -1;
    }

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
        return wxString::FromUTF8("Kaartlegenda (waypoints in beeld)");

    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    // For Markdown, use the first non-empty H1 as the visible title.
    // If the document does not start with an H1, fall back to the filename.
    if (ext == "md" || ext == "markdown") {
        wxTextFile markdownFile;

        if (markdownFile.Open(path)) {
            for (size_t i = 0; i < markdownFile.GetLineCount(); ++i) {
                wxString line = markdownFile.GetLine(i);
                line.Trim(true);
                line.Trim(false);

                if (line.IsEmpty())
                    continue;

                if (line.StartsWith("# ") && !line.StartsWith("##")) {
                    wxString title = line.Mid(2);
                    title.Trim(true);
                    title.Trim(false);

                    if (!title.IsEmpty()) {
                        markdownFile.Close();
                        return title;
                    }
                }

                // Only the first non-empty line can act as the document title.
                break;
            }

            markdownFile.Close();
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

    // If the first non-empty Markdown line is an H1, it is already used
    // as the document title in the selector/header. Remove that one line
    // from the rendered body to avoid showing the title twice.
    {
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

    mainSizer->Add(
        m_header_book,
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    m_legend_choice->Bind(
        wxEVT_CHOICE,
        &legend_pi::OnLegendSelected,
        this);

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

    m_markdown_view = new LegendHtmlWindow(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxHW_SCROLLBAR_AUTO);

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

            if (href.StartsWith("legend-reorder:") ||
                href.StartsWith("legend-reorder-done:")) {
                const bool on = !href.StartsWith("legend-reorder-done:");
                m_markdown_view->CallAfter([this, on]() { SetReorderMode(on); });
                return;
            }

            if (href.StartsWith("legend-collapse-all:") ||
                href.StartsWith("legend-expand-all:")) {
                const bool collapse = href.StartsWith("legend-collapse-all:");
                m_markdown_view->CallAfter([this, collapse]() {
                    SetAllSectionsCollapsed(collapse);
                });
                return;
            }

            if (href.StartsWith("legend-refresh:")) {
                m_markdown_view->CallAfter([this]() {
                    ProbeChartObjects();
                    ShowDynamicLegend(true);
                });
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
    enum class HitKind { None, Row, All, Section, MoveUp, MoveDown };
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
        [hitAt, pressed](wxMouseEvent &event) {
            *pressed = hitAt(event.GetPosition());
            event.Skip();
        });

    m_markdown_view->Bind(
        wxEVT_LEFT_UP,
        [this, hitAt, pressed](wxMouseEvent &event) {
            event.Skip();

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
                break;
            case HitKind::Row:
                tip = "Naar het dichtstbijzijnde exemplaar; nog een klik gaat "
                      "naar het volgende";
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
            legendView->SetRowCursor(false);
            if (!shownTip->IsEmpty()) {
                shownTip->Clear();
                m_markdown_view->UnsetToolTip();
            }
            event.Skip();
        });

    m_content_book->AddPage(m_image_page, "Afbeelding");
    m_content_book->AddPage(m_markdown_view, "Markdown");

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

    m_previous_button =
        new wxButton(panel, wxID_ANY, wxString::FromUTF8("‹  Vorige"));

    m_next_button =
        new wxButton(panel, wxID_ANY, wxString::FromUTF8("Volgende  ›"));

    m_previous_button->SetMinSize(wxSize(110, 36));
    m_next_button->SetMinSize(wxSize(110, 36));

    m_previous_button->SetToolTip("Toon de vorige legenda of notitie");
    m_next_button->SetToolTip("Toon de volgende legenda of notitie");

    buttonSizer->Add(m_previous_button, 0, wxRIGHT, 8);
    buttonSizer->Add(m_next_button, 0, wxLEFT, 8);

    mainSizer->Add(
        buttonSizer,
        0,
        wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

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

    m_legend_window->Bind(
        wxEVT_CHAR_HOOK,
        [this](wxKeyEvent &event) {
            const bool plainKey =
                !event.ControlDown() &&
                !event.AltDown() &&
                !event.MetaDown();

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

void legend_pi::OnToolbarToolCallback(int id)
{
    if (id == m_toolbar_item_id)
        ToggleLegendWindow();
}


void legend_pi::SetCurrentViewPort(PlugIn_ViewPort &vp)
{
    m_viewport = vp;
    m_viewport_valid = (vp.pix_width > 0 && vp.pix_height > 0);

    if (m_viewport_valid && !m_prefetch_scheduled) {
        m_prefetch_scheduled = true;
        m_prefetch_timer.StartOnce(5000);
    }

    if (!m_viewport_valid || !IsDynamicLegendVisible())
        return;

    // OpenCPN calls this on every repaint, also while the marker animates.
    // Only a real change of position, zoom, rotation or window size counts.
    const PlugIn_ViewPort &old = m_collected_viewport;

    const bool changed =
        !m_collected_viewport_valid ||
        std::fabs(vp.clat - old.clat) > 1e-7 ||
        std::fabs(vp.clon - old.clon) > 1e-7 ||
        std::fabs(vp.view_scale_ppm - old.view_scale_ppm) >
            1e-6 * old.view_scale_ppm ||
        std::fabs(vp.rotation - old.rotation) > 1e-4 ||
        vp.pix_width != old.pix_width ||
        vp.pix_height != old.pix_height;

    // Wait until the chart has stopped moving before rebuilding the list.
    if (changed) {
        m_auto_refresh_timer.StartOnce(400);
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

    // A repaint without movement may come from hiding or showing a layer.
    // Not restarted on every repaint, so the marker animation cannot keep
    // postponing the check.
    if (!m_visibility_timer.IsRunning())
        m_visibility_timer.StartOnce(700);
}

void legend_pi::CheckVisibilityChanges()
{
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
        meaning = entry->meaning;
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
            meaning = pattern.meaning;
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

wxString legend_pi::SectionName(int section) const
{
    if (section >= 0 && section < static_cast<int>(m_meaning_sections.size()))
        return m_meaning_sections[static_cast<size_t>(section)];

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
        m_collapsed_sections.insert(SectionName(-1));
    }

    SaveConfig();
    RenderDynamicLegend(true);
}

void legend_pi::SetReorderMode(bool on)
{
    m_reorder_mode = on;
    RenderDynamicLegend(!on);
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

void legend_pi::CollectDynamicLegend()
{
    m_dynamic_groups.clear();
    m_dynamic_total = 0;

    m_collected_viewport = m_viewport;
    m_collected_viewport_valid = m_viewport_valid;

    if (!m_viewport_valid)
        return;

    LoadMeanings();

    const PlugIn_ViewPort vp = m_viewport;

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

        // Waypoints with a scale limit disappear when zoomed out too far.
        if (wp.useScamin && vp.chart_scale > wp.scamin)
            continue;

        if (wp.lat < vp.lat_min || wp.lat > vp.lat_max)
            continue;

        if (!lonInView(wp.lon))
            continue;

        wxString key = wp.icon;
        if (key.IsEmpty())
            key = "unknown";

        DynamicGroup &group = groups[key];

        if (group.key.IsEmpty()) {
            group.key = key;
            ResolveLabel(key, wp.description, group);
        }

        group.positions.emplace_back(wp.lat, wp.lon);
        ++m_dynamic_total;
    }

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

    for (auto &entry : groups) {
        DynamicGroup group = entry.second;

        std::sort(
            group.positions.begin(),
            group.positions.end(),
            [&](const std::pair<double, double> &a,
                const std::pair<double, double> &b) {
                return distance(a) < distance(b);
            });

        m_dynamic_groups.push_back(group);
    }

    std::sort(
        m_dynamic_groups.begin(),
        m_dynamic_groups.end(),
        [](const DynamicGroup &a, const DynamicGroup &b) {
            // Headings in file order, "Overig" (-1) last; within a heading
            // the order of symbolen.txt, then the icon name.
            const int sa = a.section < 0 ? 1 << 30 : a.section;
            const int sb = b.section < 0 ? 1 << 30 : b.section;
            if (sa != sb)
                return sa < sb;
            if (a.order != b.order)
                return a.order < b.order;
            return a.key.CmpNoCase(b.key) < 0;
        });
}

void legend_pi::RenderDynamicLegend(bool keepScroll)
{
    if (!m_markdown_view)
        return;

    if (!g_memoryHandler) {
        g_memoryHandler = new wxMemoryFSHandler;
        wxFileSystem::AddHandler(g_memoryHandler);
    }

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
    std::vector<wxColour> rowColours(m_dynamic_groups.size());
    int iconBox = 16;

    // Headings only when symbolen.txt defines them; a collapsed heading
    // hides its rows.
    const bool showHeadings = !m_meaning_sections.empty();
    std::vector<bool> collapsed(m_dynamic_groups.size(), false);
    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        // In the order edit mode only the headings are shown.
        collapsed[i] =
            showHeadings &&
            (m_reorder_mode ||
             m_collapsed_sections.count(
                 SectionName(m_dynamic_groups[i].section)) > 0);
    }

    // Stripes start again under every heading.
    std::vector<size_t> stripeIndex(m_dynamic_groups.size());
    for (size_t i = 0, n = 0; i < m_dynamic_groups.size(); ++i) {
        if (i > 0 &&
            m_dynamic_groups[i].section != m_dynamic_groups[i - 1].section)
            n = 0;
        stripeIndex[i] = n++;
    }

    for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
        const DynamicGroup &group = m_dynamic_groups[i];

        const bool isActive =
            !m_active_group_key.IsEmpty() && group.key == m_active_group_key;

        rowColours[i] =
            isActive ? active
                     : (stripeIndex[i] % 2 == 1 ? stripe : background);

        // Transparency is resolved against what ends up behind the icon:
        // the tile in dark mode, otherwise the row itself. Icons of a
        // collapsed group appear small next to the heading instead.
        if (collapsed[i])
            rowColours[i] = background;

        const wxColour iconBackground =
            palette.iconTiles ? palette.tile : rowColours[i];

        // First choice: the icon's own PNG file. Fall back to asking
        // OpenCPN for the bitmap. In dusk and night the icon is dimmed, the
        // background it sits on is not.
        const wxString key = group.key;
        auto flatten = [&key](const wxColour &colour) {
            wxImage drawn = LoadIconFromFile(key, colour);

            if (!drawn.IsOk()) {
                wxBitmap bitmap = GetIcon_PlugIn(key);

                if (bitmap.IsOk())
                    drawn = FlattenIcon(bitmap, colour, key);
            }
            return drawn;
        };

        wxImage image = FlattenDimmed(flatten, iconBackground, SchemeDim());

        if (image.IsOk() && std::fabs(iconScale - 1.0) > 1e-3) {
            const int width = std::max(
                1, static_cast<int>(std::lround(image.GetWidth() * iconScale)));
            const int height = std::max(
                1, static_cast<int>(std::lround(image.GetHeight() * iconScale)));
            image = image.Scale(width, height, wxIMAGE_QUALITY_HIGH);
        }

        if (image.IsOk() && !collapsed[i]) {
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

    // Title with Ververs on the right, the summary below it and, with
    // headings, the fold links on a line of their own. Nothing here is kept
    // on one line by force, so a narrow window never scrolls sideways.
    // The block sits on its own tint with a firmer line under it, so it
    // stands apart from the list.
    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\" "
         << "bgcolor=\"" << hex(palette.header) << "\">"
         << "<tr><td valign=\"bottom\"><font size=\"+1\"><b>Kaartlegenda"
         << "</b></font></td><td align=\"right\" valign=\"bottom\">"
         << "<a href=\"legend-refresh:\">Ververs</a></td></tr>"
         << "<tr><td colspan=\"2\"><font color=\"" << hex(muted) << "\">";

    if (!m_viewport_valid) {
        html << "De kaartpositie is nog niet bekend. Beweeg de kaart "
                "een klein stukje en klik daarna op Ververs.";
    }
    else {
        html << static_cast<int>(m_dynamic_groups.size()) << " symbolen"
             << " &middot; " << m_dynamic_total << " waypoints in beeld";
    }

    html << "</font></td></tr>";

    if (showHeadings && m_reorder_mode) {
        html << "<tr><td colspan=\"2\"><a href=\"legend-reorder-done:\">"
             << "<b>Klaar</b></a></td></tr>";
    }
    else if (showHeadings && m_viewport_valid) {
        html << "<tr><td colspan=\"2\"><font size=\"-1\">";
        if (!m_dynamic_groups.empty()) {
            // One link that does what makes sense now: fold everything in
            // while any group in view is open, otherwise fold out.
            bool anyOpen = false;
            for (const DynamicGroup &group : m_dynamic_groups) {
                if (m_collapsed_sections.count(SectionName(group.section)) ==
                    0) {
                    anyOpen = true;
                    break;
                }
            }

            if (anyOpen)
                html << "<a href=\"legend-collapse-all:\">Alles inklappen</a>";
            else
                html << "<a href=\"legend-expand-all:\">Alles uitklappen</a>";
            html << " &middot; ";
        }
        html << "<a href=\"legend-reorder:\">Volgorde wijzigen</a>"
             << "</font></td></tr>";
    }

    html << "</table>";

    html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
         << "<tr><td bgcolor=\"" << hex(palette.headerRule)
         << "\" height=\"2\"></td></tr>"
         << "<tr><td height=\"6\"></td></tr>"
         << "</table>";

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

                const wxString name = wxString::Format(
                    "legend_dyn_%d_order_%zu.png", m_dynamic_generation, j);

                wxMemoryFSHandler::AddFile(name, small, wxBITMAP_TYPE_PNG);
                m_dynamic_image_names.push_back(name);
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
                 << escape(m_meaning_sections[static_cast<size_t>(section)])
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
    else if (m_viewport_valid) {
        html << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"5\">";

        m_rendered_headings.clear();

        for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
            DynamicGroup &group = m_dynamic_groups[i];

            if (showHeadings &&
                (i == 0 || group.section != m_dynamic_groups[i - 1].section)) {
                const wxString heading = SectionName(group.section);
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

                    const wxString name = wxString::Format(
                        "legend_dyn_%d_strip_%zu.png",
                        m_dynamic_generation,
                        j);

                    wxMemoryFSHandler::AddFile(name, small, wxBITMAP_TYPE_PNG);
                    m_dynamic_image_names.push_back(name);
                    strip << "<img src=\"memory:" << name << "\"> ";
                }

                if (folded && symbols > kStripIcons) {
                    strip << "<font color=\"" << hex(muted) << "\">+"
                          << symbols - kStripIcons << "</font>";
                }

                // Every cell of the heading carries its id, so a click
                // anywhere on it folds the group in or out.
                const wxString sectionId = wxString::Format(
                    "%s%zu", kLegendSectionIdPrefix, m_rendered_headings.size());
                m_rendered_headings.push_back(heading);

                // Every group gets its own table; between them the heading
                // with a thin line under it. Inside the row table the
                // padding would turn the line into a thick bar.
                html << "</table>"
                     << "<table width=\"100%\" cellspacing=\"0\" "
                        "cellpadding=\"0\">"
                     << "<tr><td colspan=\"2\" height=\"" << (i == 0 ? 6 : 14)
                     << "\"></td></tr>"
                     << "<tr><td id=\"" << sectionId << "\" valign=\"middle\">"
                     << "<font size=\"+3\">"
                     << (folded ? "&#9654;" : "&#9660;") << "</font>&nbsp;<b>"
                     << escape(heading) << "</b> <font color=\"" << hex(muted)
                     << "\">&middot; " << symbols
                     << (symbols == 1 ? " symbool, " : " symbolen, ")
                     << waypoints << " in beeld</font></td>"
                     << "<td id=\"" << sectionId
                     << "\" align=\"right\" valign=\"middle\">"
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

            group.imageName.Clear();

            const bool isActive =
                !m_active_group_key.IsEmpty() &&
                group.key == m_active_group_key;

            const wxColour &rowColour = rowColours[i];
            wxImage image = icons[i];

            if (image.IsOk()) {
                image = NormalizeIcon(
                    image,
                    rowColour,
                    iconBox,
                    palette.iconTiles ? &palette.tile : nullptr,
                    tilePadding);
            }

            if (image.IsOk()) {
                wxString name = wxString::Format(
                    "legend_dyn_%d_%zu.png",
                    m_dynamic_generation,
                    i);

                wxMemoryFSHandler::AddFile(name, image, wxBITMAP_TYPE_PNG);

                m_dynamic_image_names.push_back(name);
                group.imageName = name;
            }

            const size_t count = group.positions.size();

            wxString countText;
            if (group.current >= 0 && count > 1) {
                countText = wxString::Format(
                    "%d/%zu", group.current + 1, count);
            }
            else {
                countText = wxString::Format("&times;%zu", count);
            }

            wxString label = escape(group.label);
            if (group.labelIsName) {
                // No meaning known yet: the icon name, in grey, as it must
                // be written in symbolen.txt.
                label = "<font color=\"" + hex(muted) + "\">" + label +
                        "</font>";
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
                 << "\" align=\"center\" valign=\"middle\">";
            if (!group.imageName.IsEmpty())
                html << "<img src=\"memory:" << group.imageName << "\">";
            html << "</td><td id=\"" << rowId << "\" valign=\"middle\">"
                 << label << "</td>"
                 << "<td id=\"" << allId
                 << "\" align=\"right\" valign=\"middle\" nowrap>"
                 << "<font color=\"" << hex(isActive ? foreground : muted)
                 << "\">" << countText << "&nbsp;<font size=\"+1\">&#9678;"
                 << "</font></font></td></tr>";
        }

        html << "</table>";
        html << "<p><font size=\"-1\" color=\"" << hex(muted) << "\">"
                "Klik op een rij om naar het dichtstbijzijnde symbool te gaan; "
                "nog een klik gaat naar het volgende. Klik op &#9678; om alle "
                "exemplaren in beeld tegelijk aan te wijzen. De lijst past zich "
                "vanzelf aan als je de kaart verschuift of zoomt.";
        if (showHeadings)
            html << " Klik op een kopje om de groep in of uit te klappen.";
        html << "</font></p>";
    }

    html << "</body></html>";

    static_cast<LegendHtmlWindow *>(m_markdown_view)->SetDynamicPage(html);
    m_showing_loading_page = false;

    if (keepScroll) {
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

    const std::pair<double, double> &position =
        group.positions[static_cast<size_t>(group.current)];

    // Keep the current zoom level.
    double scale = m_viewport_valid ? m_viewport.view_scale_ppm : 0.0;
    if (scale <= 0.0)
        scale = 0.1;

    // Only move the chart when the object is not comfortably in view.
    // Otherwise the chart stays where it is and the marker shows the spot.
    bool comfortablyVisible = false;

    if (m_viewport_valid) {
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

    if (!comfortablyVisible) {
        wxWindow *canvas = PluginGetFocusCanvas();
        if (!canvas)
            canvas = GetCanvasByIndex(0);

        if (canvas) {
            CanvasJumpToPosition(
                canvas, position.first, position.second, scale);
        }
        else {
            JumpToPosition(position.first, position.second, scale);
        }
    }

    ShowMarker(position.first, position.second);

    RenderDynamicLegend(true);
}


// Marks every object of a symbol in view at once. The chart does not move:
// they are all in view already. The counter of the row stays as it is.
void legend_pi::MarkAllOfDynamicGroup(size_t index)
{
    if (index >= m_dynamic_groups.size())
        return;

    const DynamicGroup &group = m_dynamic_groups[index];
    if (group.positions.empty())
        return;

    m_active_group_key = group.key;
    ShowMarkers(group.positions);
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

// Screen positions of the marked objects on a canvas, leaving out those
// far outside it.
std::vector<wxPoint> legend_pi::MarkerCentres(PlugIn_ViewPort *vp) const
{
    std::vector<wxPoint> centres;

    for (const auto &position : m_marker_positions) {
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

    if (!m_marker_active || !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    const std::vector<wxPoint> centres = MarkerCentres(vp);
    if (centres.empty())
        return false;

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

    if (!m_marker_active || !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    const std::vector<wxPoint> centres = MarkerCentres(vp);
    if (centres.empty())
        return false;

    const double radius = 16.0;
    const int pulse = m_marker_phase % 10;

    glPushAttrib(
        GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT);

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

    glPopAttrib();

    return true;
}

// ---------------------------------------------------------------------------
// Phase 0 probe: can this plugin read the objects of an o-charts cell through
// the chart class of the o-charts plugin, the way OpenCPN does for "Chart
// object query"? Everything goes to the log; nothing is shown or stored.
// ---------------------------------------------------------------------------

namespace {

// All element contents and attributes of an XML tree as "path=value".
void FlattenXml(
    const wxXmlNode *node,
    const wxString &prefix,
    wxArrayString &out,
    int depth)
{
    if (!node || depth > 6)
        return;

    for (const wxXmlNode *child = node->GetChildren(); child;
         child = child->GetNext()) {
        if (child->GetType() != wxXML_ELEMENT_NODE)
            continue;

        const wxString name =
            prefix.IsEmpty() ? child->GetName() : prefix + "/" + child->GetName();

        wxString content = child->GetNodeContent();
        content.Trim(true).Trim(false);
        if (!content.IsEmpty())
            out.Add(name + "=" + content.Left(160));

        for (const wxXmlAttribute *attr = child->GetAttributes(); attr;
             attr = attr->GetNext())
            out.Add(name + "@" + attr->GetName() + "=" + attr->GetValue().Left(160));

        FlattenXml(child, name, out, depth + 1);
    }
}

struct ProbeCell
{
    wxString path;
    bool hasBox = false;
    double latMin = 0, latMax = 0, lonMin = 0, lonMax = 0;
    int nativeScale = 0;
};

// Extents found with a header-only init, kept between runs.
struct ProbeExtent
{
    ExtentPI extent;
    int nativeScale;
};
std::map<wxString, ProbeExtent> g_probe_extents;

// Layout of an attribute value inside OpenCPN (s52s57.h). The API header
// only declares the array type; the o-charts plugin fills it with this
// layout, and OpenCPN reads it the same way.
struct LegendS57attVal
{
    void *value;
    char valType;
};
WX_DEFINE_ARRAY_PTR(LegendS57attVal *, LegendS57attValArray);

// Value of an attribute as text. Only known type codes are decoded; the
// raw code is shown for anything else, so the encoding can be learned.
wxString AttributeValueText(const LegendS57attVal *val)
{
    if (!val || !val->value)
        return "<null>";

    switch (val->valType) {
    case 0:
    case 'I':
        return wxString::Format("%d", *static_cast<const int *>(val->value));
    case 2:
    case 'R':
        return wxString::Format("%g", *static_cast<const double *>(val->value));
    case 4:
    case 'S': {
        const char *text = static_cast<const char *>(val->value);
        wxString out = wxString::FromUTF8(text);
        if (out.IsEmpty() && *text)
            out = wxString::From8BitData(text);
        return out;
    }
    default:
        return wxString::Format("<type %d>", static_cast<int>(val->valType));
    }
}

// "ACRONYM=value; ..." for all attributes of an object.
wxString AttributesText(const PI_S57Obj *obj)
{
    wxString out;
    if (!obj->att_array || !obj->attVal || obj->n_attr <= 0)
        return out;

    const auto *values =
        reinterpret_cast<const LegendS57attValArray *>(obj->attVal);
    const int count =
        std::min(obj->n_attr, static_cast<int>(values->GetCount()));

    for (int i = 0; i < count; ++i) {
        if (!out.IsEmpty())
            out << "; ";
        out << wxString::From8BitData(obj->att_array + 6 * i, 6).Trim()
            << "=" << AttributeValueText(values->Item(i)).Left(60);
    }
    return out;
}

// Creates a chart object of the o-charts plugin. The class is registered
// with wxWidgets by that plugin, like OpenCPN itself uses it.
PlugInChartBaseExtendedPlus2 *CreateOChart(wxString &usedClass)
{
    for (const char *name : {"oesuChart", "oeuSENCChart"}) {
        wxObject *object = wxCreateDynamicObject(name);
        if (!object)
            continue;

        usedClass = name;
        auto *chart = dynamic_cast<PlugInChartBaseExtendedPlus2 *>(object);
        if (!chart) {
            wxLogMessage("Legend probe: %s created but dynamic_cast failed; "
                         "using static_cast", name);
            chart = static_cast<PlugInChartBaseExtendedPlus2 *>(object);
        }
        return chart;
    }
    return nullptr;
}

}  // namespace

void legend_pi::ProbeChartObjects()
{
    if (!m_viewport_valid) {
        wxLogMessage("Legend probe: no viewport yet");
        return;
    }

    wxStopWatch total;
    const double lat = m_viewport.clat;
    const double lon = m_viewport.clon;

    wxLogMessage(
        "Legend probe: start, centre %.5f %.5f, chart scale 1:%.0f, "
        "%dx%d px, %.4f ppm",
        lat, lon, static_cast<double>(m_viewport.chart_scale),
        m_viewport.pix_width, m_viewport.pix_height,
        m_viewport.view_scale_ppm);

    // 1. The chart database: which entries exist, and what does an entry
    //    look like? The first entry and the first .oesu entry are logged in
    //    full, so the format is known for the next step.
    std::vector<ProbeCell> cells;
    int entries = 0;
    bool loggedOesu = false;

    for (int index = 0; index < 20000; ++index) {
        wxXmlDocument doc = GetChartDatabaseEntryXML(index, false);
        if (!doc.IsOk() || !doc.GetRoot())
            break;
        ++entries;

        wxArrayString lines;
        FlattenXml(doc.GetRoot(), "", lines, 0);

        wxString path;
        double box[4] = {0, 0, 0, 0};
        bool have[4] = {false, false, false, false};

        for (const wxString &line : lines) {
            const wxString key = line.BeforeFirst('=').Lower();
            const wxString value = line.AfterFirst('=');

            if (path.IsEmpty() && value.Contains("/") &&
                (key.Contains("path") || key.Contains("file")))
                path = value;

            double d = 0;
            if (!value.ToCDouble(&d) && !value.ToDouble(&d))
                continue;
            if (key.Contains("lat") && key.Contains("min")) { box[0] = d; have[0] = true; }
            else if (key.Contains("lat") && key.Contains("max")) { box[1] = d; have[1] = true; }
            else if (key.Contains("lon") && key.Contains("min")) { box[2] = d; have[2] = true; }
            else if (key.Contains("lon") && key.Contains("max")) { box[3] = d; have[3] = true; }
        }

        const bool isOesu = path.Lower().EndsWith(".oesu");

        if (index == 0 || (isOesu && !loggedOesu)) {
            wxLogMessage("Legend probe: database entry %d (%s):", index,
                         isOesu ? "oesu" : "first");
            for (const wxString &line : lines)
                wxLogMessage("Legend probe:   %s", line);
            if (isOesu)
                loggedOesu = true;
        }

        if (!isOesu)
            continue;

        ProbeCell cell;
        cell.path = path;
        cell.hasBox = have[0] && have[1] && have[2] && have[3];
        cell.latMin = box[0]; cell.latMax = box[1];
        cell.lonMin = box[2]; cell.lonMax = box[3];
        cells.push_back(cell);
    }

    wxLogMessage("Legend probe: %d database entries, %zu .oesu cells, %ld ms",
                 entries, cells.size(), total.Time());

    if (cells.empty()) {
        wxLogMessage("Legend probe: no .oesu cells in the chart database; stop");
        return;
    }

    // 2. Which cells cover the centre? Use the box from the database when it
    //    was found, otherwise a header-only init of the cell gives its extent.
    wxString usedClass;
    std::vector<ProbeCell> covering;
    int headerInits = 0;

    for (const ProbeCell &cell : cells) {
        if (cell.hasBox) {
            if (lat < cell.latMin || lat > cell.latMax ||
                lon < cell.lonMin || lon > cell.lonMax)
                continue;
        }

        ExtentPI extent = {0, 0, 0, 0};
        int scale = 0;

        auto known = g_probe_extents.find(cell.path);
        if (known != g_probe_extents.end()) {
            extent = known->second.extent;
            scale = known->second.nativeScale;
        }
        else {
            PlugInChartBaseExtendedPlus2 *chart = CreateOChart(usedClass);
            if (!chart) {
                wxLogMessage("Legend probe: cannot create o-charts chart class; stop");
                return;
            }
            ++headerInits;

            const int result = chart->Init(cell.path, PI_HEADER_ONLY);
            const bool hasExtent = result == 0 && chart->GetChartExtent(&extent);
            scale = result == 0 ? chart->GetNativeScale() : 0;
            delete chart;

            if (result != 0 || !hasExtent) {
                wxLogMessage("Legend probe: header %s -> init %d, no extent",
                             wxFileName(cell.path).GetFullName(), result);
                continue;
            }

            g_probe_extents[cell.path] = {extent, scale};
        }

        if (lat >= extent.SLAT && lat <= extent.NLAT &&
            lon >= extent.WLON && lon <= extent.ELON) {
            ProbeCell found = cell;
            found.nativeScale = scale;
            covering.push_back(found);
        }
    }

    wxLogMessage(
        "Legend probe: %d header inits, %zu extents known, %zu cells cover "
        "the centre (class %s), %ld ms so far",
        headerInits, g_probe_extents.size(), covering.size(), usedClass,
        total.Time());
    for (const ProbeCell &cell : covering)
        wxLogMessage("Legend probe:   covering %s 1:%d",
                     wxFileName(cell.path).GetFullName(), cell.nativeScale);

    if (covering.empty())
        return;

    // 3. The most detailed covering cell: full init and an object query
    //    around the centre, like OpenCPN's object query but with a radius
    //    of a quarter of the screen width.
    std::sort(covering.begin(), covering.end(),
              [](const ProbeCell &a, const ProbeCell &b) {
                  return a.nativeScale < b.nativeScale;
              });
    const ProbeCell &best = covering.front();

    wxStopWatch watch;
    PlugInChartBaseExtendedPlus2 *chart = CreateOChart(usedClass);
    const int result = chart->Init(best.path, PI_FULL_INIT);
    wxLogMessage("Legend probe: full init %s -> %d, %ld ms",
                 wxFileName(best.path).GetFullName(), result, watch.Time());

    if (result != 0) {
        delete chart;
        return;
    }

    PlugIn_ViewPort vp = m_viewport;
    const float radiusDeg = static_cast<float>(
        (vp.pix_width / 4.0) / (vp.view_scale_ppm * 1852.0 * 60.0));

    watch.Start();
    ListOfPI_S57Obj *objects =
        chart->GetObjRuleListAtLatLon(
            static_cast<float>(lat), static_cast<float>(lon), radiusDeg, &vp);
    wxLogMessage("Legend probe: query radius %.4f deg -> %d objects, %ld ms",
                 radiusDeg, objects ? static_cast<int>(objects->GetCount()) : -1,
                 watch.Time());

    // CreateObjDescriptions() of the o-charts plugin crashes when called on
    // a chart it did not render itself, so the attributes are read directly.
    if (objects) {
        std::map<wxString, int> perClass;
        std::map<int, int> perPrim;
        int shown = 0;
        for (ListOfPI_S57Obj::Node *node = objects->GetFirst(); node;
             node = node->GetNext()) {
            PI_S57Obj *obj = node->GetData();
            if (!obj)
                continue;
            const wxString feature = wxString::FromAscii(obj->FeatureName);
            ++perClass[wxString::Format("%s/prim%d", feature, obj->Primitive_type)];
            ++perPrim[obj->Primitive_type];

            // Areas are of no use to the legend; show the others.
            if (shown < 25 && obj->Primitive_type != 2) {
                ++shown;
                wxLogMessage(
                    "Legend probe:   %s prim %d cat %c at %.5f %.5f scamin %d: %s",
                    feature, obj->Primitive_type,
                    static_cast<char>(obj->m_DisplayCat ? obj->m_DisplayCat : '?'),
                    obj->m_lat, obj->m_lon, obj->Scamin, AttributesText(obj));
            }
        }
        for (const auto &entry : perPrim)
            wxLogMessage("Legend probe:   prim %d: %d objects", entry.first, entry.second);
        for (const auto &entry : perClass)
            wxLogMessage("Legend probe:   %4d x %s", entry.second, entry.first);

        objects->Clear();
        delete objects;
    }

    delete chart;
    wxLogMessage("Legend probe: done in %ld ms", total.Time());
}
