// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "s57_source.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>

#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stopwatch.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

namespace {

// Parsed cells kept in memory; a cell is a few thousand objects at most.
const size_t kMaxLoadedCells = 48;

// Cells queried for one view at most, the most detailed first.
const size_t kMaxCellsPerView = 24;

// Cells more detailed than this factor times the view scale are skipped,
// like OpenCPN's quilt does.
const double kDetailFactor = 4.0;

// S-57 record and primitive codes.
const int kRecordFeature = 100;
const int kRecordIsolatedNode = 110;
const int kRecordConnectedNode = 120;
const int kPrimPoint = 1;
const int kAttrScamin = 133;

const unsigned char kFieldTerminator = 0x1E;
const unsigned char kUnitTerminator = 0x1F;

// ---------------------------------------------------------------------------
// ISO 8211: records made of tagged fields with raw contents.
// ---------------------------------------------------------------------------

struct Field
{
    std::string tag;
    std::string data; // without the field terminator
};

struct Record
{
    std::vector<Field> fields;

    const Field *Find(const char *tag) const
    {
        for (const Field &field : fields) {
            if (field.tag == tag)
                return &field;
        }
        return nullptr;
    }
};

int AsciiInt(const std::string &text, size_t at, size_t length)
{
    int value = 0;
    for (size_t i = at; i < at + length && i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9')
            return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

// Reads every record of a file. The first record (the data descriptive
// record) is skipped; S-57 field layouts are fixed and known here.
bool ReadRecords(const wxString &path, std::vector<Record> *records)
{
    wxFFile file(path, "rb");
    if (!file.IsOpened())
        return false;

    std::string data;
    const wxFileOffset size = file.Length();
    if (size <= 24 || size > 512 * 1024 * 1024)
        return false;
    data.resize(static_cast<size_t>(size));
    if (file.Read(&data[0], data.size()) != data.size())
        return false;

    size_t pos = 0;
    bool first = true;

    while (pos + 24 <= data.size()) {
        const int length = AsciiInt(data, pos, 5);
        const int base = AsciiInt(data, pos + 12, 5);
        const int lengthSize = AsciiInt(data, pos + 20, 1);
        const int positionSize = AsciiInt(data, pos + 21, 1);
        const int tagSize = AsciiInt(data, pos + 23, 1);

        if (length < 24 || base < 24 || lengthSize <= 0 || positionSize <= 0 ||
            tagSize <= 0 || pos + static_cast<size_t>(length) > data.size())
            return !records->empty();

        if (!first) {
            Record record;
            const size_t entrySize =
                static_cast<size_t>(tagSize + lengthSize + positionSize);
            size_t entry = pos + 24;

            while (entry + entrySize <= pos + static_cast<size_t>(base) &&
                   static_cast<unsigned char>(data[entry]) != kFieldTerminator) {
                Field field;
                field.tag = data.substr(entry, tagSize);
                const int fieldLength = AsciiInt(data, entry + tagSize, lengthSize);
                const int fieldPos =
                    AsciiInt(data, entry + tagSize + lengthSize, positionSize);
                entry += entrySize;

                if (fieldLength <= 0 || fieldPos < 0)
                    continue;

                const size_t start = pos + base + fieldPos;
                if (start + fieldLength > pos + static_cast<size_t>(length))
                    continue;

                field.data = data.substr(start, fieldLength);
                if (!field.data.empty() &&
                    static_cast<unsigned char>(field.data.back()) == kFieldTerminator)
                    field.data.pop_back();
                record.fields.push_back(field);
            }

            records->push_back(record);
        }

        first = false;
        pos += static_cast<size_t>(length);
    }

    return !records->empty();
}

// Little-endian binary subfields.
unsigned U8(const std::string &d, size_t at)
{
    return at < d.size() ? static_cast<unsigned char>(d[at]) : 0;
}

unsigned U16(const std::string &d, size_t at)
{
    return U8(d, at) | (U8(d, at + 1) << 8);
}

uint32_t U32(const std::string &d, size_t at)
{
    return static_cast<uint32_t>(U8(d, at)) | (static_cast<uint32_t>(U8(d, at + 1)) << 8) |
           (static_cast<uint32_t>(U8(d, at + 2)) << 16) |
           (static_cast<uint32_t>(U8(d, at + 3)) << 24);
}

int32_t I32(const std::string &d, size_t at)
{
    return static_cast<int32_t>(U32(d, at));
}

// ---------------------------------------------------------------------------
// S-57 content: nodes with a coordinate, features with attributes and a
// pointer to their node.
// ---------------------------------------------------------------------------

struct Node
{
    bool hasCoordinate = false;
    double y = 0; // raw YCOO (latitude * COMF)
    double x = 0;
};

struct Feature
{
    int primitive = 0;
    int objectClass = 0;
    std::vector<std::pair<int, std::string>> attributes; // code, value
    bool hasNode = false;
    int nodeRecordName = 0;
    uint32_t nodeId = 0;
};

uint64_t NodeKey(int recordName, uint32_t id)
{
    return (static_cast<uint64_t>(recordName) << 32) | id;
}

// Coordinates of a node record: the first pair of SG2D (or SG3D).
bool NodeCoordinate(const Record &record, Node *node)
{
    if (const Field *sg2d = record.Find("SG2D")) {
        if (sg2d->data.size() >= 8) {
            node->y = I32(sg2d->data, 0);
            node->x = I32(sg2d->data, 4);
            node->hasCoordinate = true;
            return true;
        }
    }
    if (const Field *sg3d = record.Find("SG3D")) {
        if (sg3d->data.size() >= 12) {
            node->y = I32(sg3d->data, 0);
            node->x = I32(sg3d->data, 4);
            node->hasCoordinate = true;
            return true;
        }
    }
    return false;
}

// Attributes of a feature record: repeated ATTL (code) and ATVL (text up
// to the unit terminator).
void ReadAttributes(const Field &attf, std::vector<std::pair<int, std::string>> *out)
{
    size_t at = 0;
    while (at + 2 <= attf.data.size()) {
        const int code = static_cast<int>(U16(attf.data, at));
        at += 2;
        const size_t end = attf.data.find(static_cast<char>(kUnitTerminator), at);
        const std::string value =
            attf.data.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? attf.data.size() : end + 1;
        out->emplace_back(code, value);
    }
}

// The first spatial pointer of a feature: NAME is RCNM (1 byte) and RCID
// (4 bytes), followed by ORNT, USAG and MASK.
bool ReadSpatialPointer(const Field &fspt, Feature *feature)
{
    if (fspt.data.size() < 5)
        return false;
    feature->nodeRecordName = static_cast<int>(U8(fspt.data, 0));
    feature->nodeId = U32(fspt.data, 1);
    feature->hasNode = true;
    return true;
}

// Merges updated attributes into a feature; a value of 0x7F deletes.
void MergeAttributes(Feature *feature, const std::vector<std::pair<int, std::string>> &updates)
{
    for (const auto &update : updates) {
        auto existing = std::find_if(
            feature->attributes.begin(), feature->attributes.end(),
            [&update](const std::pair<int, std::string> &a) {
                return a.first == update.first;
            });

        const bool remove = update.second == "\x7f";
        if (existing != feature->attributes.end()) {
            if (remove)
                feature->attributes.erase(existing);
            else
                existing->second = update.second;
        }
        else if (!remove) {
            feature->attributes.push_back(update);
        }
    }
}

struct CellData
{
    double comf = 10000000.0;
    int scale = 0;
    std::map<uint64_t, Node> nodes;
    std::map<uint32_t, Feature> features; // by RCID
};

// Applies the records of a base cell (all inserts) or of an update file
// (inserts, deletes and modifications by RUIN).
void ApplyRecords(const std::vector<Record> &records, CellData *cell, bool isUpdate)
{
    for (const Record &record : records) {
        if (const Field *dspm = record.Find("DSPM")) {
            // RCNM RCID HDAT VDAT SDAT CSCL DUNI HUNI PUNI COUN COMF SOMF
            if (dspm->data.size() >= 24) {
                const uint32_t cscl = U32(dspm->data, 8);
                const uint32_t comf = U32(dspm->data, 16);
                if (cscl > 0)
                    cell->scale = static_cast<int>(cscl);
                if (comf > 0)
                    cell->comf = comf;
            }
            continue;
        }

        if (const Field *vrid = record.Find("VRID")) {
            // RCNM(1) RCID(4) RVER(2) RUIN(1)
            if (vrid->data.size() < 8)
                continue;
            const int recordName = static_cast<int>(U8(vrid->data, 0));
            const uint32_t id = U32(vrid->data, 1);
            const unsigned ruin = isUpdate ? U8(vrid->data, 7) : 1;

            if (recordName != kRecordIsolatedNode && recordName != kRecordConnectedNode)
                continue;

            const uint64_t key = NodeKey(recordName, id);
            if (ruin == 2) {
                cell->nodes.erase(key);
            }
            else {
                Node node;
                if (NodeCoordinate(record, &node))
                    cell->nodes[key] = node;
                else if (ruin == 1)
                    cell->nodes[key] = node;
            }
            continue;
        }

        if (const Field *frid = record.Find("FRID")) {
            // RCNM(1) RCID(4) PRIM(1) GRUP(1) OBJL(2) RVER(2) RUIN(1)
            if (frid->data.size() < 12 || U8(frid->data, 0) != static_cast<unsigned>(kRecordFeature))
                continue;
            const uint32_t id = U32(frid->data, 1);
            const unsigned ruin = isUpdate ? U8(frid->data, 11) : 1;

            if (ruin == 2) {
                cell->features.erase(id);
                continue;
            }

            Feature fresh;
            fresh.primitive = static_cast<int>(U8(frid->data, 5));
            fresh.objectClass = static_cast<int>(U16(frid->data, 7));

            std::vector<std::pair<int, std::string>> attributes;
            if (const Field *attf = record.Find("ATTF"))
                ReadAttributes(*attf, &attributes);

            if (ruin == 3) {
                auto existing = cell->features.find(id);
                if (existing == cell->features.end())
                    continue;
                MergeAttributes(&existing->second, attributes);
                if (const Field *fspt = record.Find("FSPT"))
                    ReadSpatialPointer(*fspt, &existing->second);
                continue;
            }

            fresh.attributes = attributes;
            if (const Field *fspt = record.Find("FSPT"))
                ReadSpatialPointer(*fspt, &fresh);
            cell->features[id] = fresh;
        }
    }
}

// Update files of a base cell, in order: NAME.001, NAME.002, ...
wxArrayString UpdateFiles(const wxString &basePath)
{
    wxArrayString updates;
    wxFileName base(basePath);
    for (int number = 1; number < 1000; ++number) {
        wxFileName update(base);
        update.SetExt(wxString::Format("%03d", number));
        if (!update.FileExists())
            break;
        updates.Add(update.GetFullPath());
    }
    return updates;
}

}  // namespace

// ---------------------------------------------------------------------------
// S57Source
// ---------------------------------------------------------------------------

S57Source::ParsedCell S57Source::ParseCell(const wxString &basePath) const
{
    ParsedCell parsed;

    std::vector<Record> records;
    if (!ReadRecords(basePath, &records))
        return parsed;

    CellData cell;
    ApplyRecords(records, &cell, false);

    // An ENC without a dataset parameter record is not one we understand
    // (an encrypted S-63 cell, for instance).
    if (cell.features.empty() && cell.nodes.empty())
        return parsed;

    const wxArrayString updates = UpdateFiles(basePath);
    for (size_t i = 0; i < updates.GetCount(); ++i) {
        std::vector<Record> updateRecords;
        if (ReadRecords(updates[i], &updateRecords))
            ApplyRecords(updateRecords, &cell, true);
    }

    // Extent from every node, so the whole coverage counts, not only the
    // point objects.
    bool first = true;
    for (const auto &entry : cell.nodes) {
        const Node &node = entry.second;
        if (!node.hasCoordinate)
            continue;
        const double lat = node.y / cell.comf;
        const double lon = node.x / cell.comf;
        if (first) {
            parsed.latMin = parsed.latMax = lat;
            parsed.lonMin = parsed.lonMax = lon;
            first = false;
        }
        else {
            parsed.latMin = std::min(parsed.latMin, lat);
            parsed.latMax = std::max(parsed.latMax, lat);
            parsed.lonMin = std::min(parsed.lonMin, lon);
            parsed.lonMax = std::max(parsed.lonMax, lon);
        }
    }

    for (const auto &entry : cell.features) {
        const Feature &feature = entry.second;
        if (feature.primitive != kPrimPoint || !feature.hasNode)
            continue;

        auto node = cell.nodes.find(NodeKey(feature.nodeRecordName, feature.nodeId));
        if (node == cell.nodes.end() || !node->second.hasCoordinate)
            continue;

        ChartObject object;
        object.primitive = ChartObject::Point;
        object.lat = node->second.y / cell.comf;
        object.lon = node->second.x / cell.comf;
        object.className =
            m_catalogue ? m_catalogue->ClassAcronym(feature.objectClass) : wxString();
        if (object.className.IsEmpty())
            object.className = wxString::Format("%d", feature.objectClass);

        for (const auto &attribute : feature.attributes) {
            wxString acronym =
                m_catalogue ? m_catalogue->AttributeAcronym(attribute.first) : wxString();
            if (acronym.IsEmpty())
                acronym = wxString::Format("%d", attribute.first);

            wxString value = wxString::FromUTF8(attribute.second.c_str());
            if (value.IsEmpty() && !attribute.second.empty())
                value = wxString::From8BitData(attribute.second.c_str(), attribute.second.size());
            value.Trim(true).Trim(false);
            object.attributes.emplace_back(acronym, value);

            long scamin = 0;
            if (attribute.first == kAttrScamin && value.ToLong(&scamin) && scamin > 0)
                object.scamin = static_cast<int>(scamin);
        }

        parsed.points.push_back(object);
    }

    parsed.scale = cell.scale;
    parsed.valid = !first;
    return parsed;
}

// Base file and update files with their modification times: when any of
// them changes, the cached extent is read again.
wxString S57Source::Signature(const wxString &basePath)
{
    wxString signature;
    wxArrayString files = UpdateFiles(basePath);
    files.Insert(basePath, 0);
    for (size_t i = 0; i < files.GetCount(); ++i) {
        const wxDateTime stamp = wxFileName(files[i]).GetModificationTime();
        signature << (stamp.IsValid() ? stamp.GetValue().ToString() : wxString("0")) << ";";
    }
    return signature;
}

void S57Source::LoadCache(std::map<wxString, Cell> *cache) const
{
    if (m_cache_file.IsEmpty() || !wxFileName::FileExists(m_cache_file))
        return;

    wxTextFile file;
    if (!file.Open(m_cache_file, wxConvUTF8))
        return;

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        wxStringTokenizer fields(file[i], "\t", wxTOKEN_RET_EMPTY);
        if (fields.CountTokens() < 7)
            continue;

        Cell cell;
        cell.path = fields.GetNextToken();
        cell.signature = fields.GetNextToken();

        double *parts[] = {&cell.latMin, &cell.lonMin, &cell.latMax, &cell.lonMax};
        bool ok = true;
        for (double *part : parts)
            ok = ok && fields.GetNextToken().ToCDouble(part);

        long scale = 0;
        ok = ok && fields.GetNextToken().ToLong(&scale);
        if (!ok)
            continue;

        cell.scale = static_cast<int>(scale);
        (*cache)[cell.path] = cell;
    }
}

void S57Source::SaveCache() const
{
    if (m_cache_file.IsEmpty())
        return;

    wxTextFile file;
    if (wxFileName::FileExists(m_cache_file)) {
        if (!file.Open(m_cache_file, wxConvUTF8))
            return;
        file.Clear();
    }
    else if (!file.Create(m_cache_file)) {
        return;
    }

    for (const Cell &cell : m_cells) {
        wxString line;
        line << cell.path << "\t" << cell.signature << "\t"
             << wxString::FromCDouble(cell.latMin, 6) << "\t"
             << wxString::FromCDouble(cell.lonMin, 6) << "\t"
             << wxString::FromCDouble(cell.latMax, 6) << "\t"
             << wxString::FromCDouble(cell.lonMax, 6) << "\t" << cell.scale;
        file.AddLine(line);
    }

    file.Write(wxTextFileType_None, wxConvUTF8);
}

// Finds every .000 cell in OpenCPN's chart folders. Extent and scale come
// from the cache or from reading the cell once.
void S57Source::Inventory()
{
    wxStopWatch watch;
    std::map<wxString, Cell> cache;
    LoadCache(&cache);

    m_cells.clear();
    int reads = 0;
    int unreadable = 0;

    const wxArrayString raw = GetChartDBDirArrayString();
    for (size_t d = 0; d < raw.GetCount(); ++d) {
        wxString dir = raw[d].BeforeFirst('^');
        dir.Trim(true).Trim(false);
        if (dir.IsEmpty() || !wxFileName::DirExists(dir))
            continue;

        wxArrayString files;
        wxDir::GetAllFiles(dir, &files, "*.000", wxDIR_FILES | wxDIR_DIRS);

        for (size_t i = 0; i < files.GetCount(); ++i) {
            const wxString &path = files[i];
            const wxString signature = Signature(path);

            auto cached = cache.find(path);
            if (cached != cache.end() && cached->second.signature == signature) {
                m_cells.push_back(cached->second);
                continue;
            }

            ++reads;
            const ParsedCell parsed = ParseCell(path);
            if (!parsed.valid) {
                ++unreadable;
                continue;
            }

            Cell cell;
            cell.path = path;
            cell.signature = signature;
            cell.latMin = parsed.latMin;
            cell.lonMin = parsed.lonMin;
            cell.latMax = parsed.latMax;
            cell.lonMax = parsed.lonMax;
            cell.scale = parsed.scale;
            m_cells.push_back(cell);

            // Already parsed: keep it for the queries that follow.
            Loaded loaded;
            loaded.cell = parsed;
            loaded.lastUse = ++m_use_counter;
            if (m_loaded.size() < kMaxLoadedCells)
                m_loaded[path] = loaded;
        }
    }

    if (reads > 0)
        SaveCache();

    m_inventoried = true;
    wxLogMessage(
        "Legend: S-57 inventory: %zu cells, %d read, %d unreadable, %ld ms",
        m_cells.size(), reads, unreadable, watch.Time());
}

const S57Source::ParsedCell *S57Source::Load(const Cell &cell)
{
    auto found = m_loaded.find(cell.path);
    if (found != m_loaded.end()) {
        found->second.lastUse = ++m_use_counter;
        return &found->second.cell;
    }

    while (m_loaded.size() >= kMaxLoadedCells) {
        auto oldest = m_loaded.begin();
        for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it) {
            if (it->second.lastUse < oldest->second.lastUse)
                oldest = it;
        }
        m_loaded.erase(oldest);
    }

    wxStopWatch watch;
    Loaded loaded;
    loaded.cell = ParseCell(cell.path);
    loaded.lastUse = ++m_use_counter;
    if (!loaded.cell.valid)
        return nullptr;

    wxLogMessage("Legend: S-57 cell %s read in %ld ms, %zu points",
                 wxFileName(cell.path).GetFullName(), watch.Time(),
                 loaded.cell.points.size());

    return &(m_loaded[cell.path] = loaded).cell;
}

std::vector<const S57Source::Cell *> S57Source::Candidates(const PlugIn_ViewPort &vp)
{
    std::vector<const Cell *> candidates;
    if (vp.pix_width <= 0 || vp.pix_height <= 0)
        return candidates;

    if (!m_inventoried)
        Inventory();

    for (const Cell &cell : m_cells) {
        if (cell.latMax < vp.lat_min || cell.latMin > vp.lat_max ||
            cell.lonMax < vp.lon_min || cell.lonMin > vp.lon_max)
            continue;
        if (cell.scale > 0 && cell.scale * kDetailFactor < vp.chart_scale)
            continue;
        candidates.push_back(&cell);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Cell *a, const Cell *b) { return a->scale < b->scale; });
    if (candidates.size() > kMaxCellsPerView)
        candidates.resize(kMaxCellsPerView);
    return candidates;
}

std::vector<wxString> S57Source::UnreadCells(const PlugIn_ViewPort &vp)
{
    std::vector<wxString> paths;
    for (const Cell *cell : Candidates(vp)) {
        if (m_loaded.find(cell->path) == m_loaded.end())
            paths.push_back(cell->path);
    }
    return paths;
}

void S57Source::ReadCell(const wxString &path)
{
    for (const Cell &cell : m_cells) {
        if (cell.path == path) {
            Load(cell);
            return;
        }
    }
}

std::vector<ChartObject> S57Source::PointObjectsInView(const PlugIn_ViewPort &vp)
{
    std::vector<ChartObject> objects;
    m_diagnostics.Clear();

    if (vp.pix_width <= 0 || vp.pix_height <= 0)
        return objects;
    const std::vector<const Cell *> candidates = Candidates(vp);

    std::set<wxString> seen;
    int cellsRead = 0;

    for (const Cell *cell : candidates) {
        const ParsedCell *parsed = Load(*cell);
        if (!parsed)
            continue;
        ++cellsRead;

        for (const ChartObject &object : parsed->points) {
            if (object.lat < vp.lat_min || object.lat > vp.lat_max ||
                object.lon < vp.lon_min || object.lon > vp.lon_max)
                continue;

            const wxString key = wxString::Format(
                "%s|%.5f|%.5f", object.className, object.lat, object.lon);
            if (!seen.insert(key).second)
                continue;

            objects.push_back(object);
        }
    }

    m_diagnostics = wxString::Format(
        "S-57: %zu cells overlap, %d read, %zu points in view",
        candidates.size(), cellsRead, objects.size());
    return objects;
}
