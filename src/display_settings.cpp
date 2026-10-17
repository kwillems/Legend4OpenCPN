// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "display_settings.h"

#include <cstring>

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/log.h>

// The API declares PI_S57Obj but OpenCPN does not export its constructor;
// plugins that use the class define it themselves, as the chart plugins
// do. Everything starts empty; the destructor owns nothing.
PI_S57Obj::PI_S57Obj()
{
    std::memset(FeatureName, 0, sizeof(FeatureName));
    Primitive_type = 0;
    att_array = nullptr;
    attVal = nullptr;
    n_attr = 0;
    iOBJL = -1;
    Index = -1;
    x = y = z = 0.0;
    npt = 0;
    geoPt = nullptr;
    geoPtz = nullptr;
    geoPtMulti = nullptr;
    pPolyTessGeo = nullptr;
    m_lat = m_lon = 0.0;
    chart_ref_lat = chart_ref_lon = 0.0;
    lat_min = lat_max = lon_min = lon_max = 0.0;
    Scamin = 1000000000;
    bIsClone = false;
    nRef = 0;
    bIsAton = false;
    bIsAssociable = false;
    m_n_lsindex = 0;
    m_lsindex_array = nullptr;
    m_n_edge_max_points = 0;
    m_chart_context = nullptr;
    m_DisplayCat = PI_STANDARD;
    S52_Context = nullptr;
    child = nullptr;
    next = nullptr;
    x_rate = y_rate = 1.0;
    x_origin = y_origin = 0.0;
    auxParm0 = auxParm1 = auxParm2 = auxParm3 = 0;
    m_ls_list = nullptr;
    m_bcategory_mutable = false;
    m_DPRI = -1;
}

PI_S57Obj::~PI_S57Obj()
{
}

bool DisplaySettings::ChartShows(
    const wxString &className,
    int scamin,
    const PlugIn_ViewPort &vp,
    bool *answered) const
{
    *answered = false;

    // A point object of the class at the centre of the view, without
    // attributes: the default lookup of the class decides.
    PI_S57Obj probe;
    const wxCharBuffer ascii = className.ToAscii();
    if (!ascii.data() || !*ascii.data())
        return true;
    std::strncpy(probe.FeatureName, ascii.data(), sizeof(probe.FeatureName) - 1);
    probe.Primitive_type = 0; // point
    probe.m_lat = probe.lat_min = probe.lat_max = probe.chart_ref_lat = vp.clat;
    probe.m_lon = probe.lon_min = probe.lon_max = probe.chart_ref_lon = vp.clon;
    probe.Scamin = scamin > 0 ? scamin : 1000000000;

    if (!PI_PLIBSetContext(&probe) || !probe.S52_Context)
        return true;

    PlugIn_ViewPort view = vp;
    const bool shown = PI_PLIBObjectRenderCheck(&probe, &view);

    PI_PLIBFreeContext(probe.S52_Context);
    probe.S52_Context = nullptr;

    *answered = true;
    return shown;
}

bool DisplaySettings::Refresh(bool force)
{
    if (m_path.IsEmpty() || !wxFileName::FileExists(m_path))
        return false;

    const wxDateTime modified = wxFileName(m_path).GetModificationTime();
    if (!force && m_modified.IsValid() && modified.IsValid() &&
        modified == m_modified)
        return false;
    m_modified = modified;

    // Read only; the file belongs to OpenCPN. Its warnings stay quiet, our
    // own message below does not.
    wxLogNull *quiet = new wxLogNull;
    wxFileConfig config(
        wxEmptyString, wxEmptyString, m_path, wxEmptyString,
        wxCONFIG_USE_LOCAL_FILE);

    // [Settings/GlobalState] nDisplayCategory: the ASCII code of 'D', 'S',
    // 'O' or 'M'.
    long category = 0;
    config.SetPath("/Settings/GlobalState");
    config.Read("nDisplayCategory", &category, static_cast<long>('S'));
    const wxChar newCategory =
        (category == 'D' || category == 'S' || category == 'O' || category == 'M')
            ? static_cast<wxChar>(category)
            : wxChar('S');

    // [Settings/ObjectFilter] vizBOYLAT=0: the mariner's standard list.
    std::map<wxString, bool> visible;
    config.SetPath("/Settings/ObjectFilter");
    wxString name;
    long index = 0;
    for (bool more = config.GetFirstEntry(name, index); more;
         more = config.GetNextEntry(name, index)) {
        if (!name.StartsWith("viz"))
            continue;
        long value = 1;
        config.Read(name, &value, 1L);
        visible[name.Mid(3).Upper()] = value != 0;
    }

    delete quiet;

    const bool changed = newCategory != m_category || visible != m_classVisible;
    m_category = newCategory;
    m_classVisible.swap(visible);

    if (changed) {
        size_t hidden = 0;
        for (const auto &entry : m_classVisible)
            hidden += entry.second ? 0 : 1;
        wxLogMessage(
            "Legend: OpenCPN display category %c, %zu classes in filter, %zu hidden",
            m_category, m_classVisible.size(), hidden);
    }
    return changed;
}

bool DisplaySettings::Shows(const wxString &className, const wxString &displayCat) const
{
    switch (m_category) {
    case 'D':
        return displayCat == "Displaybase";

    case 'O':
        return true;

    case 'M': {
        // Display base is always drawn; the rest follows the user's list.
        // Classes missing from the list are shown.
        if (displayCat == "Displaybase")
            return true;
        auto found = m_classVisible.find(className.Upper());
        return found == m_classVisible.end() || found->second;
    }

    default:
        return displayCat == "Displaybase" || displayCat == "Standard";
    }
}
