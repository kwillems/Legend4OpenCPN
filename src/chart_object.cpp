// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "chart_object.h"

const wxString *ChartObject::Attribute(const wxString &acronym) const
{
    for (const auto &entry : attributes) {
        if (entry.first == acronym)
            return &entry.second;
    }
    return nullptr;
}

wxString ChartObjectKey(const ChartObject &object)
{
    wxString key = wxString::Format("%s|%.5f|%.5f", object.className, object.lat, object.lon);
    if (object.className == "LIGHTS") {
        for (const char *acronym :
             {"LITCHR", "SIGGRP", "SIGPER", "COLOUR", "SECTR1", "SECTR2"}) {
            const wxString *value = object.Attribute(acronym);
            key << '|' << (value ? *value : wxString());
        }
    }
    return key;
}
