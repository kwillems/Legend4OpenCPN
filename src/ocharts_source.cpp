// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "ocharts_source.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stopwatch.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

namespace {

// Chart classes of the o-charts plugin for .oesu cells; the first one that
// exists is used.
const char *const kChartClasses[] = {"oesuChart", "oeuSENCChart"};

// Cells queried for one view at most, the most detailed first.
const size_t kMaxCellsPerView = 12;

// Opened charts kept at the same time. Every open chart holds a decrypted
// cell in memory, and opening one takes up to a quarter of a second. More
// than one view's worth, so panning between views does not keep reopening
// the same cells.
const size_t kMaxOpenCharts = 16;

// Cells more detailed than this factor times the view scale are skipped:
// OpenCPN's quilt does not show them either, and their objects carry scale
// limits that hide them anyway.
const double kDetailFactor = 4.0;

// Layout of an attribute value inside OpenCPN (s52s57.h). The API only
// declares the array type; the o-charts plugin fills it with this layout.
struct S57AttrValue
{
    void *value;
    char valType;
};
WX_DEFINE_ARRAY_PTR(S57AttrValue *, S57AttrValueArray);

// Value of an attribute as text. Lists come as strings ("3,4,3").
wxString AttributeText(const S57AttrValue *val)
{
    if (!val || !val->value)
        return wxString();

    switch (val->valType) {
    case 0:
    case 'I':
        return wxString::Format("%d", *static_cast<const int *>(val->value));
    case 2:
    case 'R':
        return wxString::FromCDouble(*static_cast<const double *>(val->value));
    case 4:
    case 'S': {
        const char *text = static_cast<const char *>(val->value);
        wxString out = wxString::FromUTF8(text);
        if (out.IsEmpty() && *text)
            out = wxString::From8BitData(text);
        return out;
    }
    default:
        return wxString();
    }
}

ChartObject ToChartObject(const PI_S57Obj *obj)
{
    ChartObject object;
    object.className = wxString::FromAscii(obj->FeatureName);
    object.primitive = obj->Primitive_type;
    object.lat = obj->m_lat;
    object.lon = obj->m_lon;

    if (obj->att_array && obj->attVal && obj->n_attr > 0) {
        const auto *values =
            reinterpret_cast<const S57AttrValueArray *>(obj->attVal);
        const int count =
            std::min(obj->n_attr, static_cast<int>(values->GetCount()));

        for (int i = 0; i < count; ++i) {
            wxString acronym = wxString::From8BitData(obj->att_array + 6 * i, 6);
            acronym.Trim(true);
            const wxString value = AttributeText(values->Item(i));
            object.attributes.emplace_back(acronym, value);

            // The Scamin field of the object is a sentinel; the attribute
            // carries the real limit.
            long scamin = 0;
            if (acronym == "SCAMIN" && value.ToLong(&scamin) && scamin > 0)
                object.scamin = static_cast<int>(scamin);
        }
    }

    return object;
}

// Chart folders of OpenCPN, without the "^hash" suffix of the database.
wxArrayString ChartDirectories()
{
    wxArrayString result;
    const wxArrayString raw = GetChartDBDirArrayString();
    for (size_t i = 0; i < raw.GetCount(); ++i) {
        wxString dir = raw[i].BeforeFirst('^');
        dir.Trim(true).Trim(false);
        if (!dir.IsEmpty() && wxFileName::DirExists(dir))
            result.Add(dir);
    }
    return result;
}

}  // namespace

OChartsSource::OChartsSource()
{
}

OChartsSource::~OChartsSource()
{
    Close();
}

void OChartsSource::SetCacheFile(const wxString &path)
{
    m_cache_file = path;
}

PlugInChartBaseExtendedPlus2 *OChartsSource::Create()
{
    for (const char *name : kChartClasses) {
        wxObject *object = wxCreateDynamicObject(name);
        if (!object)
            continue;

        auto *chart = dynamic_cast<PlugInChartBaseExtendedPlus2 *>(object);
        if (!chart)
            chart = static_cast<PlugInChartBaseExtendedPlus2 *>(object);
        return chart;
    }
    return nullptr;
}

bool OChartsSource::Available()
{
    PlugInChartBaseExtendedPlus2 *chart = Create();
    if (!chart)
        return false;
    delete chart;
    return true;
}

void OChartsSource::Rescan()
{
    m_inventoried = false;
}

void OChartsSource::SetColorScheme(PI_ColorScheme scheme)
{
    m_scheme = scheme;
    for (auto &entry : m_open)
        entry.second.chart->SetColorScheme(static_cast<int>(scheme), true);
}

void OChartsSource::Close()
{
    for (auto &entry : m_open)
        delete entry.second.chart;
    m_open.clear();
}

// ---------------------------------------------------------------------------
// Inventory of cells
// ---------------------------------------------------------------------------

void OChartsSource::LoadCache(std::map<wxString, Cell> *cache) const
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
        wxLongLong_t modified = 0;
        if (!fields.GetNextToken().ToLongLong(&modified))
            continue;
        cell.modified = modified;

        double *parts[] = {&cell.extent.SLAT, &cell.extent.WLON,
                           &cell.extent.NLAT, &cell.extent.ELON};
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

void OChartsSource::SaveCache() const
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
        line << cell.path << "\t" << cell.modified.ToString() << "\t"
             << wxString::FromCDouble(cell.extent.SLAT, 6) << "\t"
             << wxString::FromCDouble(cell.extent.WLON, 6) << "\t"
             << wxString::FromCDouble(cell.extent.NLAT, 6) << "\t"
             << wxString::FromCDouble(cell.extent.ELON, 6) << "\t"
             << cell.scale;
        file.AddLine(line);
    }

    file.Write(wxTextFileType_None, wxConvUTF8);
}

// Finds every .oesu cell in OpenCPN's chart folders. Extent and scale come
// from the cache, or from a header-only init of the cell (5-10 ms each).
void OChartsSource::Inventory()
{
    wxStopWatch watch;
    std::map<wxString, Cell> cache;
    LoadCache(&cache);

    m_cells.clear();
    int headerReads = 0;
    int failures = 0;

    for (const wxString &dir : ChartDirectories()) {
        wxArrayString files;
        wxDir::GetAllFiles(dir, &files, "*.oesu", wxDIR_FILES | wxDIR_DIRS);

        for (size_t i = 0; i < files.GetCount(); ++i) {
            const wxString &path = files[i];
            const wxDateTime stamp = wxFileName(path).GetModificationTime();
            const wxLongLong modified =
                stamp.IsValid() ? stamp.GetValue() : wxLongLong(0);

            auto cached = cache.find(path);
            if (cached != cache.end() && cached->second.modified == modified) {
                m_cells.push_back(cached->second);
                continue;
            }

            PlugInChartBaseExtendedPlus2 *chart = Create();
            if (!chart)
                return;
            ++headerReads;

            Cell cell;
            cell.path = path;
            cell.modified = modified;
            cell.extent = ExtentPI();
            const int result = chart->Init(path, PI_HEADER_ONLY);
            const bool ok = result == 0 && chart->GetChartExtent(&cell.extent);
            cell.scale = ok ? chart->GetNativeScale() : 0;
            delete chart;

            if (!ok) {
                ++failures;
                continue;
            }
            m_cells.push_back(cell);
        }
    }

    if (headerReads > 0)
        SaveCache();

    m_inventoried = true;
    wxLogMessage(
        "Legend: o-charts inventory: %zu cells, %d header reads, %d unreadable, "
        "%ld ms",
        m_cells.size(), headerReads, failures, watch.Time());
}

// ---------------------------------------------------------------------------
// Opening cells and reading objects
// ---------------------------------------------------------------------------

PlugInChartBaseExtendedPlus2 *OChartsSource::Open(const wxString &path)
{
    auto found = m_open.find(path);
    if (found != m_open.end()) {
        found->second.lastUse = ++m_use_counter;
        return found->second.chart;
    }

    while (m_open.size() >= kMaxOpenCharts) {
        auto oldest = m_open.begin();
        for (auto it = m_open.begin(); it != m_open.end(); ++it) {
            if (it->second.lastUse < oldest->second.lastUse)
                oldest = it;
        }
        delete oldest->second.chart;
        m_open.erase(oldest);
    }

    PlugInChartBaseExtendedPlus2 *chart = Create();
    if (!chart)
        return nullptr;

    wxStopWatch watch;
    if (chart->Init(path, PI_FULL_INIT) != 0) {
        wxLogMessage("Legend: o-charts cell %s could not be opened",
                     wxFileName(path).GetFullName());
        delete chart;
        return nullptr;
    }

    // Init leaves the o-charts presentation library in its day scheme;
    // put the current scheme back before the chart draws again.
    chart->SetColorScheme(static_cast<int>(m_scheme), true);

    wxLogMessage("Legend: o-charts cell %s opened in %ld ms",
                 wxFileName(path).GetFullName(), watch.Time());

    OpenChart entry;
    entry.chart = chart;
    entry.lastUse = ++m_use_counter;
    m_open[path] = entry;
    return chart;
}

std::vector<ChartObject> OChartsSource::PointObjectsInView(
    const PlugIn_ViewPort &vp,
    bool ignoreScaleLimits)
{
    std::vector<ChartObject> objects;
    m_diagnostics.Clear();

    if (vp.pix_width <= 0 || vp.pix_height <= 0 || vp.view_scale_ppm <= 0.0)
        return objects;

    if (!m_inventoried)
        Inventory();

    // Cells that overlap the view, the most detailed first; those far more
    // detailed than the view are skipped.
    std::vector<const Cell *> candidates;
    for (const Cell &cell : m_cells) {
        if (cell.extent.NLAT < vp.lat_min || cell.extent.SLAT > vp.lat_max ||
            cell.extent.ELON < vp.lon_min || cell.extent.WLON > vp.lon_max)
            continue;
        if (cell.scale > 0 && cell.scale * kDetailFactor < vp.chart_scale)
            continue;
        candidates.push_back(&cell);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Cell *a, const Cell *b) { return a->scale < b->scale; });
    if (candidates.size() > kMaxCellsPerView)
        candidates.resize(kMaxCellsPerView);

    // Query radius: half the diagonal of the view, in degrees.
    const double kPi = 3.14159265358979323846;
    const double degPerPixelLat = 1.0 / (vp.view_scale_ppm * 1852.0 * 60.0);
    const double cosLat = std::max(0.2, std::cos(vp.clat * kPi / 180.0));
    const double halfLat = vp.pix_height / 2.0 * degPerPixelLat;
    const double halfLon = vp.pix_width / 2.0 * degPerPixelLat / cosLat;
    const float radius = static_cast<float>(std::sqrt(halfLat * halfLat + halfLon * halfLon) * 1.05);

    std::set<wxString> seen;
    int cellsRead = 0;
    int rawCount = 0;

    for (const Cell *cell : candidates) {
        PlugInChartBaseExtendedPlus2 *chart = Open(cell->path);
        if (!chart)
            continue;
        ++cellsRead;

        PlugIn_ViewPort query = vp;
        if (ignoreScaleLimits)
            query.chart_scale = 1.0f;
        ListOfPI_S57Obj *list = chart->GetObjRuleListAtLatLon(
            static_cast<float>(vp.clat), static_cast<float>(vp.clon), radius, &query);
        if (!list)
            continue;

        for (ListOfPI_S57Obj::Node *node = list->GetFirst(); node;
             node = node->GetNext()) {
            const PI_S57Obj *obj = node->GetData();
            if (!obj || obj->Primitive_type != ChartObject::Point)
                continue;
            ++rawCount;

            if (obj->m_lat < vp.lat_min || obj->m_lat > vp.lat_max ||
                obj->m_lon < vp.lon_min || obj->m_lon > vp.lon_max)
                continue;

            // The same object in two overlapping cells counts once.
            const wxString key = wxString::Format(
                "%s|%.5f|%.5f", obj->FeatureName, obj->m_lat, obj->m_lon);
            if (!seen.insert(key).second)
                continue;

            objects.push_back(ToChartObject(obj));
        }

        // The objects belong to the chart; only the list is ours.
        list->Clear();
        delete list;
    }

    m_diagnostics = wxString::Format(
        "%zu cells overlap, %d read, %d points found, %zu in view",
        candidates.size(), cellsRead, rawCount, objects.size());
    return objects;
}
