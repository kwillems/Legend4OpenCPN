// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "chart_symbols.h"

#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/tokenzr.h>
#include <wx/xml/xml.h>

namespace {

// Contents of a text file, UTF-8 or, when that fails, Latin-1.
wxString ReadTextFile(const wxString &path)
{
    wxFFile file(path, "rb");
    if (!file.IsOpened())
        return wxString();

    wxString text;
    if (!file.ReadAll(&text, wxConvUTF8) || text.IsEmpty()) {
        file.Seek(0);
        wxString latin;
        if (file.ReadAll(&latin, wxConvISO8859_1))
            text = latin;
    }
    return text;
}

// Fields of one CSV line; quoted fields may contain commas.
wxArrayString SplitCsvLine(const wxString &line)
{
    wxArrayString fields;
    wxString field;
    bool quoted = false;

    for (size_t i = 0; i < line.length(); ++i) {
        const wxUniChar c = line[i];

        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.length() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                }
                else {
                    quoted = false;
                }
            }
            else {
                field += c;
            }
        }
        else if (c == '"') {
            quoted = true;
        }
        else if (c == ',') {
            fields.Add(field);
            field.Clear();
        }
        else {
            field += c;
        }
    }

    fields.Add(field);
    return fields;
}

// Text of the first child element with this name, or empty.
wxString ChildText(const wxXmlNode *node, const wxString &name)
{
    for (const wxXmlNode *child = node->GetChildren(); child;
         child = child->GetNext()) {
        if (child->GetType() == wxXML_ELEMENT_NODE && child->GetName() == name) {
            wxString text = child->GetNodeContent();
            return text.Trim(true).Trim(false);
        }
    }
    return wxString();
}

const wxXmlNode *ChildElement(const wxXmlNode *node, const wxString &name)
{
    for (const wxXmlNode *child = node->GetChildren(); child;
         child = child->GetNext()) {
        if (child->GetType() == wxXML_ELEMENT_NODE && child->GetName() == name)
            return child;
    }
    return nullptr;
}

long AttributeLong(const wxXmlNode *node, const wxString &name)
{
    long value = 0;
    node->GetAttribute(name, "0").ToLong(&value);
    return value;
}

// List values without spaces, so "3, 4, 3" compares equal to "3,4,3".
wxString NormalizeValue(wxString value)
{
    value.Replace(" ", "");
    return value;
}

// Topmark symbols by TOPSHP value, as S-52 procedure TOPMAR01 chooses them
// for floating marks (buoys) and for rigid marks (beacons).
const char *const kTopmarkFloating[] = {
    nullptr,    "TOPMAR02", "TOPMAR04", "TOPMAR10", "TOPMAR12", "TOPMAR13",
    "TOPMAR14", "TOPMAR65", "TOPMAR17", "TOPMAR16", "TOPMAR08", "TOPMAR07",
    "TOPMAR14", "TOPMAR05", "TOPMAR06", "TMARDEF2", "TMARDEF2", "TMARDEF2",
    "TOPMAR10", "TOPMAR13", "TOPMAR14", "TOPMAR13", "TOPMAR14", "TOPMAR14",
    "TOPMAR02", "TOPMAR04", "TOPMAR10", "TOPMAR17", "TOPMAR18", "TOPMAR02",
    "TOPMAR17", "TOPMAR14", "TOPMAR10", "TMARDEF2"};

const char *const kTopmarkRigid[] = {
    nullptr,    "TOPMAR22", "TOPMAR24", "TOPMAR30", "TOPMAR32", "TOPMAR33",
    "TOPMAR34", "TOPMAR85", "TOPMAR86", "TOPMAR36", "TOPMAR28", "TOPMAR27",
    "TOPMAR14", "TOPMAR25", "TOPMAR26", "TOPMAR88", "TOPMAR87", "TMARDEF1",
    "TOPMAR30", "TOPMAR33", "TOPMAR34", "TOPMAR33", "TOPMAR34", "TOPMAR34",
    "TOPMAR22", "TOPMAR24", "TOPMAR30", "TOPMAR86", "TOPMAR89", "TOPMAR22",
    "TOPMAR86", "TOPMAR14", "TOPMAR30", "TMARDEF1"};

}  // namespace

// ---------------------------------------------------------------------------
// ChartObject
// ---------------------------------------------------------------------------

const wxString *ChartObject::Attribute(const wxString &acronym) const
{
    for (const auto &entry : attributes) {
        if (entry.first == acronym)
            return &entry.second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ChartSymbols: loading
// ---------------------------------------------------------------------------

bool ChartSymbols::Load(const wxString &s57dataDir)
{
    if (m_loaded)
        return true;

    m_dir = s57dataDir;

    wxFileName file(s57dataDir, "chartsymbols.xml");
    if (!LoadChartSymbols(file.GetFullPath()))
        return false;

    file.SetFullName("s57objectclasses.csv");
    LoadObjectClasses(file.GetFullPath());
    file.SetFullName("s57attributes.csv");
    LoadAttributes(file.GetFullPath());
    file.SetFullName("s57expectedinput.csv");
    LoadExpectedInput(file.GetFullPath());

    m_loaded = true;
    return true;
}

bool ChartSymbols::LoadChartSymbols(const wxString &path)
{
    wxLogNull quiet;
    wxXmlDocument doc;
    if (!doc.Load(path) || !doc.GetRoot())
        return false;

    for (const wxXmlNode *section = doc.GetRoot()->GetChildren(); section;
         section = section->GetNext()) {
        if (section->GetType() != wxXML_ELEMENT_NODE)
            continue;

        if (section->GetName() == "lookups") {
            for (const wxXmlNode *node = section->GetChildren(); node;
                 node = node->GetNext()) {
                if (node->GetType() != wxXML_ELEMENT_NODE ||
                    node->GetName() != "lookup")
                    continue;

                Lookup lookup;
                lookup.table = ChildText(node, "table-name");
                lookup.instruction = ChildText(node, "instruction");
                lookup.displayCat = ChildText(node, "display-cat");

                for (const wxXmlNode *child = node->GetChildren(); child;
                     child = child->GetNext()) {
                    if (child->GetType() != wxXML_ELEMENT_NODE ||
                        child->GetName() != "attrib-code")
                        continue;

                    wxString code = child->GetNodeContent();
                    code.Trim(true).Trim(false);
                    if (code.length() < 6)
                        continue;

                    lookup.attribs.emplace_back(
                        code.Left(6), NormalizeValue(code.Mid(6)));
                }

                m_lookups[node->GetAttribute("name")].push_back(lookup);
                ++m_lookup_count;
            }
        }
        else if (section->GetName() == "symbols") {
            for (const wxXmlNode *node = section->GetChildren(); node;
                 node = node->GetNext()) {
                if (node->GetType() != wxXML_ELEMENT_NODE ||
                    node->GetName() != "symbol")
                    continue;

                Symbol symbol;
                symbol.description = ChildText(node, "description");

                if (const wxXmlNode *bitmap = ChildElement(node, "bitmap")) {
                    const wxXmlNode *location =
                        ChildElement(bitmap, "graphics-location");
                    if (location) {
                        symbol.bitmap = wxRect(
                            AttributeLong(location, "x"),
                            AttributeLong(location, "y"),
                            AttributeLong(bitmap, "width"),
                            AttributeLong(bitmap, "height"));
                        symbol.hasBitmap =
                            symbol.bitmap.width > 0 && symbol.bitmap.height > 0;
                    }
                }

                m_symbols[ChildText(node, "name")] = symbol;
            }
        }
    }

    return m_lookup_count > 0;
}

void ChartSymbols::LoadObjectClasses(const wxString &path)
{
    // "Code","ObjectClass","Acronym",...
    wxStringTokenizer lines(ReadTextFile(path), "\r\n", wxTOKEN_STRTOK);
    while (lines.HasMoreTokens()) {
        const wxArrayString fields = SplitCsvLine(lines.GetNextToken());
        long code = 0;
        if (fields.GetCount() >= 3 && !fields[2].IsEmpty() &&
            fields[0].ToLong(&code)) {
            m_class_descriptions[fields[2]] = fields[1];
            m_class_acronyms[static_cast<int>(code)] = fields[2];
        }
    }
}

void ChartSymbols::LoadAttributes(const wxString &path)
{
    // "Code","Attribute","Acronym","Attributetype","Class"
    wxStringTokenizer lines(ReadTextFile(path), "\r\n", wxTOKEN_STRTOK);
    while (lines.HasMoreTokens()) {
        const wxArrayString fields = SplitCsvLine(lines.GetNextToken());
        long code = 0;
        if (fields.GetCount() >= 3 && fields[0].ToLong(&code) &&
            !fields[2].IsEmpty()) {
            m_attribute_codes[fields[2]] = static_cast<int>(code);
            m_attribute_acronyms[static_cast<int>(code)] = fields[2];
            m_attribute_names[fields[2]] = fields[1];
        }
    }
}

void ChartSymbols::LoadExpectedInput(const wxString &path)
{
    // "Code","ID","Meaning": attribute code, enumerated value, text
    wxStringTokenizer lines(ReadTextFile(path), "\r\n", wxTOKEN_STRTOK);
    while (lines.HasMoreTokens()) {
        const wxArrayString fields = SplitCsvLine(lines.GetNextToken());
        long code = 0;
        if (fields.GetCount() >= 3 && fields[0].ToLong(&code))
            m_expected_input[static_cast<int>(code)][fields[1]] = fields[2];
    }
}

// ---------------------------------------------------------------------------
// ChartSymbols: resolving
// ---------------------------------------------------------------------------

// S-52 lookup selection: of the lookups of the object's class in the given
// table, the one whose attribute conditions all hold and that has the most
// of them. Without a match the lookup without conditions is the default.
const ChartSymbols::Lookup *ChartSymbols::FindLookup(
    const ChartObject &obj,
    const wxString &table,
    bool *fromDefault) const
{
    *fromDefault = false;

    auto found = m_lookups.find(obj.className);
    if (found == m_lookups.end())
        return nullptr;

    const Lookup *best = nullptr;
    bool conditionalExists = false;

    for (const Lookup &lookup : found->second) {
        if (lookup.table != table)
            continue;

        if (lookup.attribs.empty()) {
            if (!best)
                best = &lookup;
            continue;
        }
        conditionalExists = true;

        bool matches = true;
        for (const auto &condition : lookup.attribs) {
            const wxString *value = obj.Attribute(condition.first);

            // "?" in the table means: the attribute is missing or has no
            // value. An empty condition value means: present, any value.
            if (condition.second == "?") {
                if (value && !value->IsEmpty()) {
                    matches = false;
                    break;
                }
                continue;
            }
            if (!value) {
                matches = false;
                break;
            }
            if (!condition.second.IsEmpty() &&
                NormalizeValue(*value) != condition.second) {
                matches = false;
                break;
            }
        }

        if (matches &&
            (!best || lookup.attribs.size() > best->attribs.size()))
            best = &lookup;
    }

    if (!best) {
        // No lookup of this table at all: take the first of any table, so
        // at least the class is recognised.
        if (!found->second.empty()) {
            *fromDefault = true;
            return &found->second.front();
        }
        return nullptr;
    }

    // The unconditional lookup while conditional ones exist: the object's
    // attributes matched none of them.
    *fromDefault = best->attribs.empty() && conditionalExists;
    return best;
}

// The symbol name of the first SY() in an instruction, e.g. "BOYLAT14" of
// "SY(BOYLAT14);TE(...)". Rotation parameters are dropped.
wxString ChartSymbols::FirstSymbolInInstruction(const wxString &instruction)
{
    int at = instruction.Find("SY(");
    while (at != wxNOT_FOUND) {
        // Not the tail of another word such as "CSY(".
        if (at == 0 || !wxIsalpha(instruction[at - 1])) {
            const wxString rest = instruction.Mid(at + 3);
            wxString name;
            for (wxUniChar c : rest) {
                if (c == ')' || c == ',')
                    break;
                name += c;
            }
            return name.Trim(true).Trim(false);
        }
        const int next = instruction.Mid(at + 3).Find("SY(");
        at = next == wxNOT_FOUND ? wxNOT_FOUND : at + 3 + next;
    }
    return wxString();
}

// The procedure name of a CS() in an instruction, e.g. "LIGHTS05".
wxString ChartSymbols::ConditionalProcedure(const wxString &instruction)
{
    const int at = instruction.Find("CS(");
    if (at == wxNOT_FOUND)
        return wxString();

    wxString name;
    for (wxUniChar c : instruction.Mid(at + 3)) {
        if (c == ')')
            break;
        name += c;
    }
    return name.Trim(true).Trim(false);
}

// Symbols that S-52 chooses in code instead of in the tables. Only the
// procedures that matter for point symbols in a legend are covered; the
// rest gives no symbol and falls back to the class.
wxString ChartSymbols::ConditionalSymbol(
    const wxString &procedure,
    const ChartObject &obj) const
{
    if (procedure.StartsWith("LIGHTS")) {
        // Red, green, otherwise white/yellow/other.
        const wxString *colour = obj.Attribute("COLOUR");
        const wxString value = colour ? NormalizeValue(*colour) : wxString();
        wxStringTokenizer parts(value, ",", wxTOKEN_STRTOK);
        bool red = false;
        bool green = false;
        while (parts.HasMoreTokens()) {
            const wxString part = parts.GetNextToken();
            red = red || part == "3";
            green = green || part == "4";
        }
        if (red && !green)
            return "LIGHTS11";
        if (green && !red)
            return "LIGHTS12";
        return "LIGHTS13";
    }

    if (procedure.StartsWith("TOPMAR")) {
        // Floating marks are far more common than rigid ones; whether the
        // topmark sits on a buoy or a beacon is not known from the topmark
        // itself.
        const wxString *shape = obj.Attribute("TOPSHP");
        long value = 0;
        if (!shape || !shape->ToLong(&value))
            return "TMARDEF2";
        const long count =
            static_cast<long>(sizeof(kTopmarkFloating) / sizeof(kTopmarkFloating[0]));
        if (value <= 0 || value >= count || !kTopmarkFloating[value])
            return "TMARDEF2";
        return kTopmarkFloating[value];
    }

    return wxString();
}

bool ChartSymbols::Resolve(
    const ChartObject &obj,
    bool simplified,
    Resolved *out) const
{
    *out = Resolved();

    wxString table;
    switch (obj.primitive) {
    case ChartObject::Point:
        table = simplified ? "Simplified" : "Paper";
        break;
    case ChartObject::Line:
        table = "Lines";
        break;
    default:
        table = "Plain";
        break;
    }

    bool fromDefault = false;
    const Lookup *lookup = FindLookup(obj, table, &fromDefault);
    if (!lookup)
        return false;

    out->table = lookup->table;
    out->displayCat = lookup->displayCat;
    out->fromDefault = fromDefault;

    wxString symbol = FirstSymbolInInstruction(lookup->instruction);

    if (symbol.IsEmpty()) {
        const wxString procedure = ConditionalProcedure(lookup->instruction);
        if (!procedure.IsEmpty()) {
            symbol = ConditionalSymbol(procedure, obj);
            out->conditional = !symbol.IsEmpty();
        }
    }

    if (symbol.IsEmpty()) {
        // Any lookup of this class with a plain symbol, so the class still
        // gets a recognisable picture.
        auto found = m_lookups.find(obj.className);
        if (found != m_lookups.end()) {
            for (const Lookup &other : found->second) {
                symbol = FirstSymbolInInstruction(other.instruction);
                if (!symbol.IsEmpty()) {
                    out->fromDefault = true;
                    break;
                }
            }
        }
    }

    out->symbol = symbol;
    return true;
}

// ---------------------------------------------------------------------------
// ChartSymbols: pictures and texts
// ---------------------------------------------------------------------------

wxImage ChartSymbols::SymbolImage(const wxString &symbol, PI_ColorScheme scheme)
{
    auto found = m_symbols.find(symbol);
    if (found == m_symbols.end() || !found->second.hasBitmap)
        return wxImage();

    // The sheets of OpenCPN: day, dusk and night versions of every symbol.
    const int key = static_cast<int>(scheme);
    auto sheet = m_sheets.find(key);
    if (sheet == m_sheets.end()) {
        wxString name = "rastersymbols-day.png";
        if (scheme == PI_GLOBAL_COLOR_SCHEME_DUSK)
            name = "rastersymbols-dusk.png";
        else if (scheme == PI_GLOBAL_COLOR_SCHEME_NIGHT)
            name = "rastersymbols-dark.png";

        wxLogNull quiet;
        wxImage image;
        image.LoadFile(wxFileName(m_dir, name).GetFullPath(), wxBITMAP_TYPE_PNG);
        sheet = m_sheets.insert(std::make_pair(key, image)).first;
    }

    const wxImage &image = sheet->second;
    if (!image.IsOk())
        return wxImage();

    wxRect rect = found->second.bitmap;
    rect.Intersect(wxRect(0, 0, image.GetWidth(), image.GetHeight()));
    if (rect.width <= 0 || rect.height <= 0)
        return wxImage();

    return image.GetSubImage(rect);
}

wxString ChartSymbols::SymbolDescription(const wxString &symbol) const
{
    auto found = m_symbols.find(symbol);
    if (found == m_symbols.end())
        return wxString();

    // "can buoy,yellow,full-chart" or "tower, black, full-chart and
    // simplified": the parts that name the symbol table say nothing about
    // the object and are dropped; the rest is joined again with ", ".
    wxString text;
    wxStringTokenizer parts(found->second.description, ",", wxTOKEN_STRTOK);
    while (parts.HasMoreTokens()) {
        wxString part = parts.GetNextToken();
        part.Trim(true).Trim(false);
        if (part.IsEmpty() || part == "general" ||
            part.Contains("full-chart") || part.Contains("paper-chart") ||
            part.Contains("simplified"))
            continue;
        if (!text.IsEmpty())
            text << ", ";
        text << part;
    }
    return text;
}

wxString ChartSymbols::ClassDescription(const wxString &className) const
{
    auto found = m_class_descriptions.find(className);
    return found == m_class_descriptions.end() ? wxString() : found->second;
}

wxString ChartSymbols::ClassAcronym(int code) const
{
    auto found = m_class_acronyms.find(code);
    return found == m_class_acronyms.end() ? wxString() : found->second;
}

wxString ChartSymbols::AttributeAcronym(int code) const
{
    auto found = m_attribute_acronyms.find(code);
    return found == m_attribute_acronyms.end() ? wxString() : found->second;
}

wxString ChartSymbols::AttributeName(const wxString &acronym) const
{
    auto found = m_attribute_names.find(acronym);
    return found == m_attribute_names.end() ? wxString() : found->second;
}

wxString ChartSymbols::AttributeValueMeaning(
    const wxString &acronym,
    const wxString &value) const
{
    auto code = m_attribute_codes.find(acronym);
    if (code == m_attribute_codes.end())
        return wxString();

    auto values = m_expected_input.find(code->second);
    if (values == m_expected_input.end())
        return wxString();

    wxString result;
    wxStringTokenizer parts(NormalizeValue(value), ",", wxTOKEN_STRTOK);
    while (parts.HasMoreTokens()) {
        const wxString part = parts.GetNextToken();
        auto meaning = values->second.find(part);
        if (meaning == values->second.end())
            return wxString();
        if (!result.IsEmpty())
            result << ", ";
        result << meaning->second;
    }
    return result;
}
