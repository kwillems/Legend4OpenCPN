// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _LAYER_WAYPOINTS_H_
#define _LAYER_WAYPOINTS_H_

#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

// The waypoints of the layer files (GPX) in OpenCPN's layers folder, read
// straight from the files. OpenCPN hands a plugin its waypoints one by one,
// some 0.13 ms each; for many thousands of buoys reading the files is far
// quicker. OpenCPN keeps the waypoints of one layer together and in file
// order, so a layer is found in its list by its first and last point.
struct LayerWaypoint
{
    double lat = 0.0;
    double lon = 0.0;
    wxString icon;         // <sym>
    wxString name;         // <name>
    wxString description;  // <desc>
    bool useScamin = false;
    double scamin = 0.0;
};

struct LayerFile
{
    wxString path;
    wxDateTime modified;
    std::vector<LayerWaypoint> points;
};

class LayerWaypoints
{
public:
    // Reads the layer files of this folder; a file read before and not
    // changed since is kept as it is.
    void Load(const wxString &layersDir);

    const std::vector<LayerFile> &Files() const { return m_files; }

private:
    std::vector<LayerFile> m_files;
};

#endif
