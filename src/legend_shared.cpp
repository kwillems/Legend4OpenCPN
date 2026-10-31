// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "legend_shared.h"

#include "light_character.h"

#include <wx/fs_mem.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>

wxMemoryFSHandler *g_memoryHandler = nullptr;
PI_ColorScheme g_color_scheme = PI_GLOBAL_COLOR_SCHEME_DAY;
bool g_night_preview = false;

namespace {

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

} // namespace

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

PI_ColorScheme LegendScheme()
{
    return g_night_preview ? PI_GLOBAL_COLOR_SCHEME_NIGHT : g_color_scheme;
}

bool IsDimScheme()
{
    return LegendScheme() == PI_GLOBAL_COLOR_SCHEME_DUSK ||
           LegendScheme() == PI_GLOBAL_COLOR_SCHEME_NIGHT;
}

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

LegendPalette GetLegendPalette()
{
    if (IsDimScheme())
        return GetDimPalette();

#if wxCHECK_VERSION(3, 1, 3)
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
#endif

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

wxString LegendBodyTag(bool plainLinks)
{
    const LegendPalette palette = GetLegendPalette();

    return wxString::Format(
        "<body bgcolor=\"%s\" text=\"%s\" link=\"%s\">",
        HtmlColour(palette.background),
        HtmlColour(palette.text),
        HtmlColour(plainLinks ? palette.text : palette.link));
}

wxString RhythmOf(const wxString &abbreviation)
{
    LightCharacter light;
    return light.Parse(abbreviation) ? light.RhythmAbbreviation() : wxString();
}
