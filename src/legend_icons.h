// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _LEGEND_ICONS_H_
#define _LEGEND_ICONS_H_

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/image.h>
#include <wx/string.h>

// The icons of the legend: waypoint icons read from their files, and the
// small signs and eyes drawn for rows and the header.

// A user icon can also be an SVG file (UserIcons/<key>.svg); OpenCPN then
// uses it instead of a PNG of the same name.
wxString SvgIconPath(const wxString &key);

// OpenCPN keeps waypoint icons as PNG or SVG files: user icons in the
// private data directory (UserIcons) and the built-in ones in the shared
// data directory. Reading the file ourselves is more reliable than asking
// OpenCPN for a bitmap, which can come back empty on macOS. An SVG icon is
// drawn at `scale` times its size, so a Retina screen gets it sharp.
wxImage LoadIconFromFile(const wxString &key, const wxColour &background,
                         int scale = 1);

// Convert an OpenCPN icon bitmap to an opaque image by drawing it on the
// background colour. Transparency (alpha channel or mask) is resolved here,
// because it is lost or turns black when the image is shown by wxHtmlWindow.
wxImage FlattenIcon(
    const wxBitmap &bitmap,
    const wxColour &background,
    const wxString &key);

// Every icon gets a square of the same size, so all rows in the dynamic
// legend are equally high. Icons keep their own size and are centred; only
// icons larger than the square are scaled down. With a tile colour the icon
// sits on a rounded tile of that colour, padding pixels wider than the box.
wxImage NormalizeIcon(
    const wxImage &image,
    const wxColour &background,
    int box,
    const wxColour *tile = nullptr,
    int padding = 0);

// Blends an image towards a colour: faded icons for rows the chart does
// not draw at this scale.
wxImage FadeImage(const wxImage &image, const wxColour &towards, double amount);

// A small eye, closed (a stroke through it) for rows hidden at this
// scale, drawn on the row colour.
wxImage EyeIcon(
    bool open, const wxColour &colour, const wxColour &background, int scale = 1);

// The sign of a note for a row of the legend: a sheet with a folded corner,
// darkened in dusk and night like the other icons (the row stays as it is).
wxImage NoteSignImage(const wxColour &background, int size);

// A small VHF sign for a row: the channel in a red-edged rounded square,
// like the icons of the VHFinfo layer.
wxImage VhfBadgeImage(const wxString &channel, const wxColour &background, int size);

// The icons of "Instellingen" (a gear) and "Ververs" (a circling arrow),
// drawn rather than taken from a font, so their size is certain.
wxImage HeaderIcon(bool gear, const wxColour &colour, const wxColour &background, int size);

#endif
