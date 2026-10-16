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

#include <map>
#include <vector>

#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/choice.h>
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
    wxString imageName; // name in the in-memory file system (may be empty)
    std::vector<std::pair<double, double>> positions; // lat, lon; nearest first
    int current = -1; // last position shown by a click, -1 = none yet
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
    void RenderDynamicLegend(bool keepScroll = false);
    void GoToDynamicGroup(size_t index);
    void ClearDynamicImages();
    bool IsDynamicLegendVisible() const;
    void OnAutoRefreshTimer();

    // Temporary marker on the chart for the object chosen in the legend
    void ShowMarker(double lat, double lon);
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
    bool m_prefetch_scheduled;
    bool m_showing_loading_page;

    // Viewport the dynamic legend was last collected for; a change starts
    // m_auto_refresh_timer so the list follows the chart.
    PlugIn_ViewPort m_collected_viewport;
    bool m_collected_viewport_valid;
    wxTimer m_auto_refresh_timer;

    bool m_marker_active;
    double m_marker_lat;
    double m_marker_lon;
    int m_marker_phase;
    wxTimer m_marker_timer;

    int m_window_x;
    int m_window_y;
    int m_window_width;
    int m_window_height;
};

#endif
