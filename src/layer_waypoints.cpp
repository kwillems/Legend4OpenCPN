// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "layer_waypoints.h"

#include <map>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/xml/xml.h>

namespace {

wxString ChildText(const wxXmlNode *node, const wxString &name)
{
    for (const wxXmlNode *child = node->GetChildren(); child; child = child->GetNext()) {
        if (child->GetName() == name)
            return child->GetNodeContent();
    }
    return wxString();
}

bool ReadLayerFile(const wxString &path, std::vector<LayerWaypoint> *points)
{
    wxXmlDocument document;
    {
        wxLogNull quiet;
        if (!document.Load(path) || !document.GetRoot())
            return false;
    }

    for (const wxXmlNode *node = document.GetRoot()->GetChildren(); node;
         node = node->GetNext()) {
        if (node->GetName() != "wpt")
            continue;

        LayerWaypoint point;
        if (!node->GetAttribute("lat").ToCDouble(&point.lat) ||
            !node->GetAttribute("lon").ToCDouble(&point.lon))
            return false;

        for (const wxXmlNode *child = node->GetChildren(); child; child = child->GetNext()) {
            const wxString &name = child->GetName();
            if (name == "sym")
                point.icon = child->GetNodeContent();
            else if (name == "name")
                point.name = child->GetNodeContent();
            else if (name == "desc")
                point.description = child->GetNodeContent();
            else if (name == "extensions") {
                for (const wxXmlNode *extension = child->GetChildren(); extension;
                     extension = extension->GetNext()) {
                    if (extension->GetName() != "opencpn:scale_min_max")
                        continue;
                    point.useScamin = extension->GetAttribute("UseScale") == "true";
                    extension->GetAttribute("ScaleMin").ToCDouble(&point.scamin);
                }
            }
        }
        points->push_back(point);
    }
    return true;
}

} // namespace

void LayerWaypoints::Load(const wxString &layersDir)
{
    std::map<wxString, LayerFile> before;
    for (LayerFile &file : m_files)
        before[file.path] = std::move(file);
    m_files.clear();

    if (!wxDirExists(layersDir))
        return;

    wxArrayString paths;
    wxDir::GetAllFiles(layersDir, &paths, "*.gpx", wxDIR_FILES);

    for (const wxString &path : paths) {
        const wxDateTime modified = wxFileName(path).GetModificationTime();
        auto known = before.find(path);
        if (known != before.end() && known->second.modified == modified) {
            m_files.push_back(std::move(known->second));
            continue;
        }

        LayerFile file;
        file.path = path;
        file.modified = modified;
        if (ReadLayerFile(path, &file.points) && !file.points.empty())
            m_files.push_back(std::move(file));
    }
}
