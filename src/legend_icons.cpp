// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "legend_icons.h"

#include <cstdint>  // ocpn_plugin.h (api-18) uses uint64_t without it
#include "ocpn_plugin.h"

#include "legend_shared.h"

#include <wx/bmpbndl.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

bool IsSafeIconKey(const wxString &key)
{
    return !key.IsEmpty() && key.Find('/') == wxNOT_FOUND &&
           key.Find('\\') == wxNOT_FOUND && key.Find("..") == wxNOT_FOUND;
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

// A colour darkened for dusk and night (dim 1 = as it is).
wxColour Dimmed(const wxColour &colour, double dim)
{
    return wxColour(static_cast<unsigned char>(colour.Red() * dim),
                    static_cast<unsigned char>(colour.Green() * dim),
                    static_cast<unsigned char>(colour.Blue() * dim), colour.Alpha());
}

} // namespace

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

wxImage LoadIconFromFile(const wxString &key, const wxColour &background,
                         int scale)
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

wxImage NormalizeIcon(
    const wxImage &image,
    const wxColour &background,
    int box,
    const wxColour *tile,
    int padding)
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

wxImage EyeIcon(
    bool open, const wxColour &colour, const wxColour &background, int scale)
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
