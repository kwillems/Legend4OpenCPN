// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _OCHARTS_SOURCE_H_
#define _OCHARTS_SOURCE_H_

#include "chart_symbols.h"
#include "ocpn_plugin.h"

#include <map>
#include <vector>

#include <wx/longlong.h>
#include <wx/string.h>

// Chart objects of o-charts cells (.oesu), read through the chart class of
// the o-charts plugin, the way OpenCPN itself queries objects. The plugin
// keeps doing the decryption and the licence check; nothing is stored or
// exported. Only the extents of the cells are cached on disk.
class OChartsSource
{
public:
    OChartsSource();
    ~OChartsSource();

    // File that remembers the extent and scale of every cell, so later
    // starts need no header reads.
    void SetCacheFile(const wxString &path);

    // True when the o-charts plugin is loaded and its chart class exists.
    bool Available();

    // The point objects of the cells that cover the viewport, within its
    // bounds, with duplicates between overlapping cells removed. Cells far
    // more detailed than the view are left out, like OpenCPN's quilt does.
    // With ignoreScaleLimits the cells are asked as if the view were at a
    // very large scale, so objects hidden by their scale limit come too.
    std::vector<ChartObject> PointObjectsInView(
        const PlugIn_ViewPort &vp, bool ignoreScaleLimits = false);

    // The point objects in an area for the Uitkijk, scale limits ignored,
    // from the cells already open that overlap it and are not coarser than
    // maxScale. The cells still to open come back in `unopened`, the cells
    // used in `used`.
    // For every point of `samples` the most detailed cell that covers it,
    // so no part of the area goes without a chart; then other cells, the
    // most detailed first, up to maxCells.
    std::vector<ChartObject> PointObjectsInArea(const PlugIn_ViewPort &area, int maxScale,
                                                size_t maxCells,
                                                const std::vector<std::pair<double, double>> &samples,
                                                std::vector<wxString> *unopened,
                                                std::vector<CellExtent> *used);

    // The cells of this view that are not open yet, and one of them opened:
    // each takes a fifth of a second, so the legend opens them one by one,
    // with room for OpenCPN in between, before it asks for the objects.
    std::vector<wxString> UnopenedCells(const PlugIn_ViewPort &vp);
    void OpenCell(const wxString &path) { Open(path); }

    // Forget the cell inventory; the next query scans the chart folders
    // again (new or removed cells).
    void Rescan();

    // Frees every opened chart. Must run before the plugin is unloaded.
    void Close();

    // OpenCPN's colour scheme (day, dusk, night). The o-charts plugin has
    // its own presentation library whose scheme is set through its chart
    // objects; a freshly initialised chart resets it to day, so every chart
    // opened here gets the current scheme at once, and all open charts get
    // a change.
    void SetColorScheme(PI_ColorScheme scheme);

    size_t CellCount() const { return m_cells.size(); }
    wxString LastDiagnostics() const { return m_diagnostics; }

private:
    struct Cell
    {
        wxString path;
        wxLongLong modified;
        ExtentPI extent;
        int scale;
    };

    struct OpenChart
    {
        PlugInChartBaseExtendedPlus2 *chart;
        unsigned lastUse;
    };

    void Inventory();
    void LoadCache(std::map<wxString, Cell> *cache) const;
    void SaveCache() const;
    std::vector<const Cell *> Candidates(const PlugIn_ViewPort &vp);
    PlugInChartBaseExtendedPlus2 *Create();
    PlugInChartBaseExtendedPlus2 *Open(const wxString &path);

    wxString m_cache_file;
    bool m_inventoried = false;
    std::vector<Cell> m_cells;
    std::map<wxString, OpenChart> m_open; // path -> chart
    unsigned m_use_counter = 0;
    wxString m_diagnostics;
    PI_ColorScheme m_scheme = PI_GLOBAL_COLOR_SCHEME_DAY;
};

#endif
