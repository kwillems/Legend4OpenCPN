// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _CHART_SYMBOLS_H_
#define _CHART_SYMBOLS_H_

#include <cstdint>  // ocpn_plugin.h (api-18) uses uint64_t without it
#include "ocpn_plugin.h"

#include <map>
#include <utility>
#include <vector>

#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/string.h>

// One object of a vector chart, independent of where it came from: the
// o-charts plugin or an S-57 file read by this plugin.
struct ChartObject
{
    enum Primitive { Point = 0, Line = 1, Area = 2 };

    wxString className;   // S-57 acronym, e.g. BOYLAT
    int primitive = Point;
    double lat = 0.0;
    double lon = 0.0;
    int scamin = 0;       // 0 = no scale limit

    // Acronym and value as text, in chart order. List values are written
    // as "3,4,3", the way the S-52 lookup tables write them.
    std::vector<std::pair<wxString, wxString>> attributes;

    // Value of an attribute, or nullptr when the object does not have it.
    const wxString *Attribute(const wxString &acronym) const;
};

// OpenCPN's presentation data for vector charts, read from its s57data
// folder: the S-52 lookup tables and symbols (chartsymbols.xml), the raster
// symbol sheets for day, dusk and night, and the S-57 object catalogue
// (s57objectclasses.csv, s57attributes.csv, s57expectedinput.csv).
//
// It answers three questions about a chart object: which symbol does it
// get, what does that symbol look like, and what is it called.
class ChartSymbols
{
public:
    struct Resolved
    {
        wxString symbol;      // e.g. BOYLAT14; empty when no symbol applies
        wxString displayCat;  // Displaybase, Standard, Other or Mariners
        wxString table;       // lookup table used: Simplified, Paper, Lines
        bool fromDefault = false; // no lookup matched the attributes
        bool conditional = false; // symbol chosen by a CS() procedure here
    };

    // Reads all files; true when the lookup tables could be read. Safe to
    // call again: a loaded instance is left alone.
    bool Load(const wxString &s57dataDir);
    bool IsLoaded() const { return m_loaded; }

    // The S-52 symbol for an object. simplified selects the Simplified
    // point table of OpenCPN's "simplified" symbol style, otherwise the
    // Paper chart table.
    bool Resolve(const ChartObject &obj, bool simplified, Resolved *out) const;

    // The symbol's picture from the sheet of the colour scheme, with alpha.
    // An invalid image when the symbol has no raster picture.
    wxImage SymbolImage(const wxString &symbol, PI_ColorScheme scheme);

    // Texts from the catalogue; empty when unknown.
    wxString SymbolDescription(const wxString &symbol) const;
    wxString ClassDescription(const wxString &className) const;
    wxString AttributeName(const wxString &acronym) const;
    // Acronyms for the numeric codes used inside S-57 files.
    wxString ClassAcronym(int code) const;
    wxString AttributeAcronym(int code) const;
    // Meaning of an enumerated value ("1" of BOYSHP -> "conical (nun,
    // ogival)"); list values are translated one by one.
    wxString AttributeValueMeaning(
        const wxString &acronym, const wxString &value) const;

    size_t LookupCount() const { return m_lookup_count; }
    size_t SymbolCount() const { return m_symbols.size(); }

private:
    struct Lookup
    {
        wxString table;
        std::vector<std::pair<wxString, wxString>> attribs; // acronym, value
        wxString instruction;
        wxString displayCat;
    };

    struct Symbol
    {
        wxString description;
        wxRect bitmap;        // position and size on the raster sheet
        bool hasBitmap = false;
    };

    bool LoadChartSymbols(const wxString &path);
    void LoadObjectClasses(const wxString &path);
    void LoadAttributes(const wxString &path);
    void LoadExpectedInput(const wxString &path);

    const Lookup *FindLookup(
        const ChartObject &obj, const wxString &table, bool *fromDefault) const;
    static wxString FirstSymbolInInstruction(const wxString &instruction);
    static wxString ConditionalProcedure(const wxString &instruction);
    wxString ConditionalSymbol(
        const wxString &procedure, const ChartObject &obj) const;

    bool m_loaded = false;
    wxString m_dir;
    size_t m_lookup_count = 0;

    std::map<wxString, std::vector<Lookup>> m_lookups; // class -> file order
    std::map<wxString, Symbol> m_symbols;
    std::map<wxString, wxString> m_class_descriptions;
    std::map<int, wxString> m_class_acronyms;
    std::map<wxString, int> m_attribute_codes;
    std::map<int, wxString> m_attribute_acronyms;
    std::map<wxString, wxString> m_attribute_names;
    std::map<int, std::map<wxString, wxString>> m_expected_input;
    std::map<int, wxImage> m_sheets; // colour scheme -> raster sheet
};

#endif
