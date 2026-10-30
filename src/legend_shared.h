// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _LEGEND_SHARED_H_
#define _LEGEND_SHARED_H_

#include <cstdint>  // ocpn_plugin.h (api-18) uses uint64_t without it
#include "ocpn_plugin.h"

#include <functional>

#include <wx/colour.h>
#include <wx/image.h>
#include <wx/string.h>

class wxMemoryFSHandler;

// Constants, colours and drawing helpers that several parts of the
// legend use.

// Pseudo-path of the dynamic legend entry in the list of legend items.
const wxString kDynamicGpxId = "@gpx-legend";

// Steps of the fade of a row found from the chart (150 ms each).
const int kFadeSteps = 8;

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

// Ids of the move buttons of the blocks at the top (Marifoon, Uitkijk,
// Notities, Licht herkennen), followed by their place in that order.
const wxString kLegendMoveBlockUpIdPrefix = "legend-move-block-up-";
const wxString kLegendMoveBlockDownIdPrefix = "legend-move-block-down-";

// Id of the cells of the header icons, for their tooltips: 0 settings,
// 1 Ververs.
const wxString kLegendTipIdPrefix = "legend-tip-";
const wxString kRefreshIconId = "legend-refreshicon";

// Id of the pad of "Licht herkennen", and of its lines with matching
// lights, followed by their number.
const wxString kLegendTapIdPrefix = "legend-tap-";
const wxString kTapClockId = "legend-tapclock";
const wxString kLegendMatchIdPrefix = "legend-match-";
// Buttons of "Licht herkennen", whole cells to click without a link line:
// 0 clears the taps, 1 closes, 10 to 14 choose a colour.
const wxString kLegendButtonIdPrefix = "legend-button-";

// Id of a line of the "Uitkijk" block, followed by its number.
const wxString kLegendAheadIdPrefix = "legend-ahead-";
const wxString kLegendAheadVhfIdPrefix = "legend-aheadvhf-";
const wxString kLegendAheadNoteIdPrefix = "legend-aheadnote-";
const wxString kLegendNoteIdPrefix = "legend-note-";
const wxString kLegendVhfAreaIdPrefix = "legend-vhfarea-";

// The "memory:" file system handler for the icons of the dynamic legend.
// Its code is compiled into this plugin, so it must be removed again in
// DeInit: wxWidgets deletes registered handlers only when OpenCPN exits,
// long after the plugin library has been unloaded.
extern wxMemoryFSHandler *g_memoryHandler;

// OpenCPN's colour scheme (day, dusk, night), set through SetColorScheme().
extern PI_ColorScheme g_color_scheme;

// Nachtvoorbeeld is on: the legend takes the night colours, whatever the
// scheme of OpenCPN is.
extern bool g_night_preview;

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

// Draw an image (with or without alpha channel or mask) on a background
// colour and return the opaque result.
wxImage FlattenImage(const wxImage &image, const wxColour &background);

// The scheme the legend window is drawn in.
PI_ColorScheme LegendScheme();

bool IsDimScheme();

// Brightness of icons and images in the current scheme. OpenCPN has darker
// symbol sets for dusk and night; user icons only exist in one version, so
// they are dimmed instead.
double SchemeDim();

wxString SchemeName(PI_ColorScheme scheme);

wxColour BlendColour(const wxColour &a, const wxColour &b, double amount);

// Darkens the colours of an image, keeping its transparency.
wxImage DimImage(const wxImage &image, double dim);

// Flattens an icon on a background with only the icon itself dimmed.
// flatten() draws the icon on a given colour. Drawing it on black and on
// white gives its colour and its coverage, from which the dimmed icon on
// the real background follows.
wxImage FlattenDimmed(
    const std::function<wxImage(const wxColour &)> &flatten,
    const wxColour &background,
    double dim);

// Marker on the chart: bright by day, subdued in dusk and night so it does
// not spoil night vision, but still distinct from the chart.
void MarkerColours(wxColour &outline, wxColour &highlight);

// A chart scale in a few characters: "1:22k", "1:1,5M".
wxString FormatScaleShort(int scale);

LegendPalette GetLegendPalette();

wxString HtmlColour(const wxColour &colour);

// Opening body tag with the palette, so wxHtmlWindow does not fall back to
// its own default colours.
wxString LegendBodyTag(bool plainLinks = false);

// The abbreviation of a light character without its colours.
wxString RhythmOf(const wxString &abbreviation);

#endif
