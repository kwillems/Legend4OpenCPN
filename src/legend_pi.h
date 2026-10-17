// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _LEGEND_PI_H_
#define _LEGEND_PI_H_

#include "ocpn_plugin.h"
#include "chart_symbols.h"
#include "ocharts_source.h"
#include "s57_source.h"
#include "display_settings.h"

#include <map>
#include <set>
#include <vector>

#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/frame.h>
#include <wx/html/htmlwin.h>
#include <wx/panel.h>
#include <wx/image.h>
#include <wx/simplebook.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/timer.h>

// One group of identical waypoint symbols in the dynamic legend.
struct DynamicGroup
{
    wxString key;     // waypoint icon name
    wxString label;   // text shown in the legend
    bool labelIsName = false; // no meaning known: label is the icon name
    int section = -1;  // heading in symbolen.txt, -1 = none ("Overig")
    int order = 1 << 30; // line in symbolen.txt; not listed = last
    wxString imageName; // name in the in-memory file system (may be empty)
    std::vector<std::pair<double, double>> positions; // lat, lon; nearest first
    int current = -1; // last position shown by a click, -1 = none yet

    // Chart objects (o-charts) are grouped by S-52 symbol instead of by
    // waypoint icon; the picture then comes from OpenCPN's symbol sheet.
    bool chartSymbol = false;
    wxString symbol;
    wxString iconName; // waypoint icon to load; key may carry a suffix

    // Members of this group are not drawn at the current scale (SCAMIN);
    // visibleFrom is the chart scale at which the first one appears.
    bool hiddenAtScale = false;
    int visibleFrom = 0;
};

// One line of symbolen.txt: icon name (may hold one '*'), meaning (may be
// empty: the line then only places the icon under its heading).
struct SymbolMeaning
{
    wxString name;
    wxString meaning;
    int section = -1; // index in the list of headings, -1 = before any
    int order = 0;    // position in the file
};

// What the dynamic legend needs of one waypoint. Asking OpenCPN for a
// waypoint by GUID is slow with many thousands of layer waypoints, so the
// answers are kept between refreshes.
struct CachedWaypoint
{
    double lat = 0.0;
    double lon = 0.0;
    wxString icon;
    wxString description;
    wxString firstLine; // first line of the waypoint's own description
    bool visible = false;
    bool useScamin = false;
    double scamin = 0.0;
};

class legend_pi : public opencpn_plugin_118
{
public:
    explicit legend_pi(void *ppimgr);
    ~legend_pi() override;

    int Init() override;
    bool DeInit() override;

    int GetAPIVersionMajor() override;
    int GetAPIVersionMinor() override;

    int GetPlugInVersionMajor() override;
    int GetPlugInVersionMinor() override;

    wxString GetCommonName() override;
    wxString GetShortDescription() override;
    wxString GetLongDescription() override;

    void OnToolbarToolCallback(int id) override;
    void SetColorScheme(PI_ColorScheme cs) override;
    void SetCurrentViewPort(PlugIn_ViewPort &vp) override;

    using opencpn_plugin_118::RenderOverlayMultiCanvas;
    using opencpn_plugin_118::RenderGLOverlayMultiCanvas;

    bool RenderGLOverlayMultiCanvas(
        wxGLContext *pcontext,
        PlugIn_ViewPort *vp,
        int canvasIndex,
        int priority) override;

    bool RenderOverlayMultiCanvas(
        wxDC &dc,
        PlugIn_ViewPort *vp,
        int canvasIndex,
        int priority) override;

private:
    void CreateLegendWindow();
    void ToggleLegendWindow();

    void ScanLegendFiles();
    void RebuildLegendChoice();
    void RefreshLegendFiles();
    void ShowLegend(size_t index);
    void ShowEmptyState();
    void UpdateImageScale();
    void SaveCurrentMarkdownScroll();
    void RestoreMarkdownScroll(const wxString &path);

    // Dynamic legend (waypoints visible on screen)
    void ShowDynamicLegend(bool reloadWaypoints = false);
    void CollectDynamicLegend();
    void RefreshDynamicLegendKeepingState();
    void RenderLoadingPage();
    void ApplyViewColours();

    // Filling the waypoint cache in small steps keeps OpenCPN responsive.
    bool StartWaypointLoading(bool reloadAll); // true: cache is up to date
    void OnLoadingTimer();
    void FinishWaypointLoading();

    // Layers can be hidden or shown without the waypoint list changing.
    void CheckVisibilityChanges();
    void RenderDynamicLegend(bool keepScroll = false);
    void GoToDynamicGroup(size_t index);
    void MarkAllOfDynamicGroup(size_t index);
    // View scale (pixels per metre) at which a scale limit is just passed,
    // or 0 when it cannot be computed.
    double ViewScaleForLimit(int limit);
    void JumpTo(double lat, double lon, double viewScale);

    // Chart objects of the o-charts cells in view, grouped by symbol.
    void EnsureChartSymbols();
    void CollectChartObjects(
        const PlugIn_ViewPort &vp, std::map<wxString, DynamicGroup> *groups);
    void ClearDynamicImages();

    // Meaning of a symbol shown in the legend, see ResolveLabel().
    wxString GetMeaningsFile() const;
    void LoadMeanings();
    void BuildCommonLines();

    // Headings of the dynamic legend can be collapsed; the state is kept by
    // heading name in legend.ini.
    wxString SectionName(int section) const;
    void ToggleSection(size_t renderedIndex);
    void SetAllSectionsCollapsed(bool collapsed);

    // Order edit mode: headings only, with buttons to move a heading up or
    // down. The new order is written to symbolen.txt.
    void SetReorderMode(bool on);
    void MoveSection(int section, int delta);
    void ResolveLabel(
        const wxString &key,
        const wxString &iconDescription,
        DynamicGroup &group) const;
    bool IsDynamicLegendVisible() const;
    void OnAutoRefreshTimer();

    // Temporary marker on the chart for the object chosen in the legend
    void ShowMarker(double lat, double lon);
    void ShowMarkers(const std::vector<std::pair<double, double>> &positions);
    std::vector<wxPoint> MarkerCentres(PlugIn_ViewPort *vp) const;
    void StopMarker();
    void OnMarkerTimer();
    void RefreshAllCanvases();

    void OnPreviousLegend(wxCommandEvent &event);
    void OnNextLegend(wxCommandEvent &event);
    void OnLegendSelected(wxCommandEvent &event);

    wxString GetLegendDirectory() const;
    wxString GetConfigDirectory() const;
    void EnsureDirectories() const;

    wxString MakeDisplayTitle(const wxString &path) const;
    wxString RenderMarkdownFile(const wxString &path) const;

    void LoadConfig();
    void SaveConfig() const;
    void RestoreWindowState();
    void SaveWindowState();

    int m_toolbar_item_id;
    wxBitmap m_toolbar_bitmap;
    wxFrame *m_legend_window;

    wxSimplebook *m_header_book;
    wxStaticText *m_single_title;
    wxStaticText *m_legend_position;
    wxChoice *m_legend_choice;
    wxButton *m_previous_button;
    wxButton *m_next_button;
    wxSimplebook *m_content_book;

    wxPanel *m_image_page;
    wxStaticBitmap *m_legend_bitmap;
    wxImage m_current_image;

    wxHtmlWindow *m_markdown_view;

    wxArrayString m_legend_files;
    size_t m_legend_index;
    wxString m_last_legend_name;
    wxString m_current_legend_path;
    std::map<wxString, wxPoint> m_markdown_scroll_positions;

    PlugIn_ViewPort m_viewport;
    bool m_viewport_valid;
    std::vector<DynamicGroup> m_dynamic_groups;
    std::vector<wxString> m_dynamic_image_names;
    int m_dynamic_generation;
    int m_dynamic_total;
    wxString m_active_group_key; // symbol clicked last, highlighted

    // symbolen.txt: headings, lines in file order, and lookups into them.
    std::vector<wxString> m_meaning_sections;
    std::vector<SymbolMeaning> m_meanings;
    std::map<wxString, size_t> m_meanings_exact;
    std::vector<size_t> m_meanings_patterns;
    wxDateTime m_meanings_modified;

    std::set<wxString> m_collapsed_sections;
    bool m_reorder_mode = false;

    // Chart object scale factor of OpenCPN the list was last drawn with;
    // row icons follow it, so they match the size on the chart.
    double m_rendered_icon_scale = 1.0;
    std::vector<wxString> m_rendered_headings; // index = id in the page

    // Icon name -> first description line shared by all its waypoints.
    std::map<wxString, wxString> m_common_lines;

    wxArrayString m_waypoint_guids;
    std::vector<CachedWaypoint> m_waypoint_cache; // same order as the GUIDs

    bool m_loading;
    bool m_loading_reload;
    wxArrayString m_loading_guids;
    std::vector<CachedWaypoint> m_loading_cache;
    std::map<wxString, size_t> m_loading_known; // GUID -> m_waypoint_cache
    size_t m_loading_next;
    int m_loading_fetched;
    wxLongLong m_loading_started;
    wxTimer m_loading_timer;
    wxTimer m_prefetch_timer;
    wxTimer m_visibility_timer;

    ChartSymbols m_chart_symbols; // S-52 symbols and S-57 catalogue
    bool m_chart_symbols_tried = false;
    OChartsSource m_ocharts;
    S57Source m_s57;
    DisplaySettings m_display_settings; // category and class filter of OpenCPN
    int m_plib_state_hash = 0;          // OpenCPN's vector display settings
    bool m_log_next_viewport = false;   // diagnostics after a zoom
    float m_logged_chart_scale = 0.0f;

    // The scale OpenCPN shows in its status bar and compares waypoint
    // scale limits with: physical screen pixels per metre divided by the
    // pixels per true metre at the view centre. The plugin viewport's
    // chart_scale is a different, nominal figure.
    double DisplayedScale(const PlugIn_ViewPort &vp) const;
    double m_displayed_scale = 0.0; // for the viewport last collected
    bool m_ocharts_available = false;
    bool m_chart_sources_available = false; // any chart source has cells
    bool m_show_chart_objects = true; // setting, kept in legend.ini
    bool m_show_hidden_at_scale = false; // setting: list scale-hidden symbols too
    int m_dynamic_hidden = 0;            // waypoints in view hidden by scale
    int m_chart_objects_scale_hidden = 0; // chart objects in view hidden by scale
    int m_chart_object_total = 0;     // listed chart objects in view
    int m_chart_objects_hidden = 0;   // in view, but not shown at this scale
    wxString m_chart_objects_logged;  // last log line, to log changes only
    bool m_prefetch_scheduled;
    bool m_showing_loading_page;

    // Viewport the dynamic legend was last collected for; a change starts
    // m_auto_refresh_timer so the list follows the chart.
    PlugIn_ViewPort m_collected_viewport;
    bool m_collected_viewport_valid;
    wxTimer m_auto_refresh_timer;
    PlugIn_ViewPort m_pending_viewport; // view that last (re)started the timer
    static bool ViewportMoved(const PlugIn_ViewPort &a, const PlugIn_ViewPort &b);

    bool m_marker_active;
    std::vector<std::pair<double, double>> m_marker_positions; // lat, lon
    int m_marker_phase;
    wxTimer m_marker_timer;

    int m_window_x;
    int m_window_y;
    int m_window_width;
    int m_window_height;
};

#endif
