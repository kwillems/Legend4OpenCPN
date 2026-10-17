// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _S57_SOURCE_H_
#define _S57_SOURCE_H_

#include "chart_symbols.h"
#include "ocpn_plugin.h"

#include <map>
#include <vector>

#include <wx/string.h>

// Chart objects of unencrypted ENC cells (S-57, .000 with .001... updates)
// in OpenCPN's chart folders, read by this plugin itself with a small ISO
// 8211 reader. Only point objects are produced; the catalogue translates
// the numeric object and attribute codes of the files.
class S57Source
{
public:
    // Catalogue for class and attribute codes; must be loaded before the
    // first query.
    void SetCatalogue(const ChartSymbols *catalogue) { m_catalogue = catalogue; }

    // File that remembers extent and scale of every cell.
    void SetCacheFile(const wxString &path) { m_cache_file = path; }

    // The point objects of the cells that cover the viewport, within its
    // bounds. Cells far more detailed than the view are left out.
    std::vector<ChartObject> PointObjectsInView(const PlugIn_ViewPort &vp);

    // Forget the inventory; the next query scans the chart folders again.
    void Rescan() { m_inventoried = false; }

    size_t CellCount() const { return m_cells.size(); }
    wxString LastDiagnostics() const { return m_diagnostics; }

    // One parsed cell: its point objects, extent and compilation scale.
    struct ParsedCell
    {
        std::vector<ChartObject> points;
        double latMin = 0, lonMin = 0, latMax = 0, lonMax = 0;
        int scale = 0;
        bool valid = false;
    };

    // Reads a base cell and its update files. Public for tests and tools.
    ParsedCell ParseCell(const wxString &basePath) const;

private:
    struct Cell
    {
        wxString path;
        wxString signature; // base and update files with their times
        double latMin, lonMin, latMax, lonMax;
        int scale;
    };

    struct Loaded
    {
        ParsedCell cell;
        unsigned lastUse;
    };

    void Inventory();
    void LoadCache(std::map<wxString, Cell> *cache) const;
    void SaveCache() const;
    const ParsedCell *Load(const Cell &cell);
    static wxString Signature(const wxString &basePath);

    const ChartSymbols *m_catalogue = nullptr;
    wxString m_cache_file;
    bool m_inventoried = false;
    std::vector<Cell> m_cells;
    std::map<wxString, Loaded> m_loaded; // path -> parsed cell
    unsigned m_use_counter = 0;
    wxString m_diagnostics;
};

#endif
