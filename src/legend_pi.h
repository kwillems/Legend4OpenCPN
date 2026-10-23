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
#include "chart_names.h"
#include "light_character.h"
#include "note_editor.h"
#include "note_places.h"
#include "vhf_areas.h"

#include <map>
#include <string>
#include <set>
#include <vector>

#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/bmpbuttn.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/frame.h>
#include <wx/html/htmlwin.h>
#include <wx/panel.h>
#include <wx/image.h>
#include <wx/simplebook.h>
#include <wx/srchctrl.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/textctrl.h>

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

    // A light character ("Fl(2)R.6s") of waypoints and chart lights in
    // view; label holds it in words. With a parentKey it is a sub-row of
    // the buoy or beacon row it stands on; lights that stand on nothing
    // listed have the heading "Lichtkarakters".
    bool lightCharacter = false;
    wxString lightText;
    wxString parentKey;

    // Row of the light flares of the chart (S-57 class LIGHTS).
    bool lightSymbol = false;
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
    wxString light;     // light character found in its name or description
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

    // From the chart to the legend: a click on a symbol, or "Toon in
    // legenda" in the chart's context menu, reveals the symbol's row.
    bool MouseEventHook(wxMouseEvent &event) override;
    void SetCursorLatLon(double lat, double lon) override;
    void OnContextMenuItemCallback(int id) override;
    void SetCurrentViewPort(PlugIn_ViewPort &vp) override;
    void SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix) override;

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
    void ConvertLooseImages();
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
    // The row of the object nearest to a position, within tolerancePx
    // viewport pixels. With openWindow the legend is opened first.
    bool RevealObjectAt(double lat, double lon, double tolerancePx, bool openWindow);
    void RevealGroupPosition(size_t groupIndex, size_t positionIndex);
    double ViewportPixelsPerWindowPixel() const;
    // View scale (pixels per metre) at which a scale limit is just passed,
    // or 0 when it cannot be computed.
    double ViewScaleForLimit(int limit);
    void JumpTo(double lat, double lon, double viewScale);

    // Chart objects of the o-charts cells in view, grouped by symbol.
    void EnsureChartSymbols();
    void CollectChartObjects(
        const PlugIn_ViewPort &vp, std::map<wxString, DynamicGroup> *groups);
    // Adds one light to the row of its light character under the row
    // parentKey (empty: under "Lichtkarakters"). A light under a row is
    // hidden at this scale when its row is; hiddenFrom is the scale limit
    // of a light on its own that the chart does not draw at this scale. A light within about
    // 30 metres of one already in the row counts once.
    void AddLight(
        std::map<wxString, DynamicGroup> &groups,
        const LightCharacter &light,
        double lat,
        double lon,
        const wxString &parentKey,
        int hiddenFrom = 0) const;
    void ClearDynamicImages();

    // Meaning of a symbol shown in the legend, see ResolveLabel().
    wxString GetMeaningsFile() const;
    void LoadMeanings();
    void BuildCommonLines();

    // Headings of the dynamic legend can be collapsed; the state is kept by
    // heading name in legend.ini.
    // Key of a heading (its text in symbolen.txt, both languages); also the
    // key under which its folded state is kept.
    wxString SectionName(int section) const;
    // The heading as shown, in the chosen language.
    wxString SectionTitle(int section) const;
    // "Nederlands | English" texts of symbolen.txt: the part for the chosen
    // language, or the other part when that one is empty.
    wxString PickLanguage(const wxString &text) const;
    void LoadChartNames();
    void ToggleSection(size_t renderedIndex);
    void SetAllSectionsCollapsed(bool collapsed);

    // Order edit mode: headings only, with buttons to move a heading up or
    // down. The new order is written to symbolen.txt.
    void SetReorderMode(bool on);

    // "Kaartsymbolen" (-2) and "Lichtkarakters" (-3) are headings of the
    // plugin; a heading of that name in symbolen.txt takes their place, so
    // they can be moved like the others. BuiltInSection() gives the heading
    // in the file, or the built-in number when there is none.
    int BuiltInSection(int builtIn) const;
    // Adds those headings to symbolen.txt when missing (for the order
    // edit mode).
    void AddBuiltInHeadings();

    // The settings of the dynamic legend, from the menu behind
    // "Instellingen" or from a link: "lights", "hidden",
    // "language", "all-lights", "collapse-all", "expand-all", "reorder".
    void RunLegendCommand(const wxString &command);
    void ShowSettingsMenu();
    bool m_menu_has_headings = false; // what the menu offers, from the list
    bool m_menu_any_open = false;
    bool m_menu_has_lights = false;
    void MoveSection(int section, int delta);
    void ResolveLabel(
        const wxString &key,
        const wxString &iconDescription,
        DynamicGroup &group) const;
    bool IsDynamicLegendVisible() const;
    void OnAutoRefreshTimer();

    // Search field above the dynamic legend: only rows whose text holds
    // every word typed stay in the list. The text of a row is its name,
    // its heading and its icon or chart symbol name.
    void UpdateSearchVisibility();
    void SetFilter(const wxString &text);
    bool GroupMatchesFilter(const DynamicGroup &group) const;

    // Temporary marker on the chart for the object chosen in the legend
    void ShowMarker(double lat, double lon);
    void ShowMarkers(const std::vector<std::pair<double, double>> &positions);
    std::vector<wxPoint> MarkerCentres(
        PlugIn_ViewPort *vp,
        const std::vector<std::pair<double, double>> &positions) const;
    void StopMarker();
    void OnMarkerTimer();
    void RefreshAllCanvases();

    // A light character chosen in the legend shines on the chart in its
    // own rhythm for a few periods, at one light or at all in view.
    void ShowLightMarkers(
        const LightCharacter &light,
        const std::vector<std::pair<double, double>> &positions);
    void StopLightMarker();
    void OnLightTimer();

    // All lights in view shining on the chart at once, each in its own
    // rhythm, until switched off. Follows the chart as the list does.
    void SetAllLightsShining(bool on);
    void SetNightPreview(bool on);
    void DrawNightShade(wxDC &dc, PlugIn_ViewPort *vp);
    void DrawNightShadeGL(PlugIn_ViewPort *vp);
    void UpdateAllLights();
    void OnAllLightsTimer();
    bool DrawAllLights(wxDC &dc, PlugIn_ViewPort *vp);
    bool DrawAllLightsGL(PlugIn_ViewPort *vp);
    bool DrawLightMarkers(wxDC &dc, PlugIn_ViewPort *vp);
    bool DrawLightMarkersGL(PlugIn_ViewPort *vp);
    // The light's colour (index) at this moment, -1 while dark; plus
    // kDimLight for the steady part of a fixed and flashing light.
    int LightStateAt(double seconds) const;

    void OnPreviousLegend(wxCommandEvent &event);
    void OnNextLegend(wxCommandEvent &event);
    void OnLegendSelected(wxCommandEvent &event);

    wxString GetLegendDirectory() const;
    wxString GetConfigDirectory() const;
    void EnsureDirectories() const;

    wxString MakeDisplayTitle(const wxString &path) const;
    wxString RenderMarkdownFile(const wxString &path) const;
    // dropTitle leaves out a first "# Title" line, which the choice shows.
    wxString RenderMarkdownText(std::string markdown, bool dropTitle = true) const;

    // Writing notes: the current Markdown item in a text box, saved on its
    // own a moment after typing stops.
    bool IsMarkdownItem(const wxString &path) const;
    void StartEditing();
    void StopEditing();
    void SaveNote();
    bool NewNote();
    void NewNoteAt(double lat, double lon);
    void RenameNote();
    void DeleteNote();
    wxString NoteFileName(const wxString &title) const;
    void ShowNoteNamed(const wxString &fullName);
    void UpdateNoteButtons();
    void InsertLink();
    // The chart's right-click menu: "Toon in legenda", "Marifoon hier", and
    // "Notitie" with a submenu whose actions hidden items carry (see Init).
    int m_menu_parent = -1;
    wxMenu *m_context_submenu = nullptr;
    std::vector<int> m_menu_actions;
    int m_menu_reveal = -1;
    int m_menu_vhf = -1;
    int m_menu_new_note = -1;
    int m_menu_open_note = -1;
    int m_menu_delete_note = -1;
    int m_menu_link_place = -1;
    int m_menu_pin_note = -1;
    int m_menu_new_note_boat = -1;
    int m_menu_link_boat = -1;
    int m_menu_pin_boat = -1;
    wxString m_menu_note_path;           // note whose sign was under the mouse
    void PrepareContextMenu(int canvasIndex) override;
    void DeleteNoteFile(const wxString &path);
    wxPanel *m_place_bar = nullptr;      // "Hoort bij ..." while editing
    wxStaticText *m_place_text = nullptr;
    wxString m_note_block;               // settings block of the note being edited
    mutable std::set<wxString> m_pin_images; // pins in the memory file system
    bool m_note_has_place = false;
    double m_note_lat = 0.0;
    double m_note_lon = 0.0;
    double m_note_radius = 0.5;
    void SetNotePlace(double lat, double lon);
    void AskNoteRadius();
    void NotePlaceChanged();
    void UpdatePlaceBar();
    wxString FormatPosition(double lat, double lon) const;
    bool ParseGeoUri(const wxString &uri, double *lat, double *lon) const;
    void GoToPlace(double lat, double lon);
    void InsertPlace(double lat, double lon, const wxString &text = wxString());
    bool BoatPositionKnown() const;
    void InsertBoatPosition();
    void PinNoteAtBoat();
    void NewNoteAtBoat();
    void ShowNoGpsNotice();
    void InsertImage();

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

    wxButton *m_edit_button = nullptr;    // pencil: edit this note
    wxButton *m_new_button = nullptr;     // plus: a new note
    wxButton *m_rename_button = nullptr;  // another name for this note
    wxButton *m_delete_button = nullptr;  // into the folder "Verwijderd"
    wxButton *m_save_button = nullptr;    // while editing: save and stop
    wxPanel *m_format_bar = nullptr;      // buttons that format the note
    std::vector<std::pair<wxBitmapButton *, wxString>> m_format_icons; // and their icons
    NoteEditor *m_editor = nullptr;
    wxPanel *m_nav_panel = nullptr;       // Vorige / Volgende
    wxPanel *m_edit_footer = nullptr;     // Bewaard om ...
    wxStaticText *m_save_status = nullptr;
    bool m_editing = false;
    bool m_note_dirty = false;
    wxString m_editing_path;
    wxTimer m_save_timer;

    wxSearchCtrl *m_search = nullptr;
    wxString m_filter;                    // search text, trimmed
    std::vector<wxString> m_filter_words; // its words, folded for matching
    wxTimer m_filter_timer;               // waits for a pause in typing

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
    ChartNames m_chart_names;     // names of chart objects (s57-nl.txt)
    bool m_dutch_names = true;    // setting: names in Dutch or English
    bool m_chart_symbols_tried = false;
    OChartsSource m_ocharts;
    S57Source m_s57;
    DisplaySettings m_display_settings; // category and class filter of OpenCPN
    int m_plib_state_hash = 0;          // OpenCPN's vector display settings
    bool m_log_next_viewport = false;   // diagnostics after a zoom

    double m_cursor_lat = 0.0;
    double m_cursor_lon = 0.0;
    bool m_cursor_valid = false;
    wxPoint m_mouse_down;
    bool m_mouse_down_valid = false;
    int m_scroll_to_row = -1;          // row to scroll to after rendering

    // A row found from the chart lights up in the marker's colour and fades
    // to the normal highlight colour.
    wxString m_flash_key;
    wxString m_flash_light_key; // its light sub-row, lit along with it
    wxString m_active_light_key; // light sub-row highlighted with its row
    int m_flash_phase = 0;
    wxTimer m_flash_timer;
    void OnFlashTimer();
    bool m_pending_reveal = false;      // reveal once loading has finished
    double m_pending_reveal_lat = 0.0;
    double m_pending_reveal_lon = 0.0;
    double m_pending_reveal_tolerance = 0.0;
    float m_logged_chart_scale = 0.0f;

    // The scale OpenCPN shows in its status bar and compares waypoint
    // scale limits with: physical screen pixels per metre divided by the
    // pixels per true metre at the view centre. The plugin viewport's
    // chart_scale is a different, nominal figure.
    double DisplayedScale(const PlugIn_ViewPort &vp) const;
    double m_displayed_scale = 0.0; // for the viewport last collected
    bool m_ocharts_available = false;
    bool m_chart_sources_available = false; // any chart source has cells
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

    struct ShiningCharacter
    {
        std::vector<LightPhase> phases;
        std::vector<wxString> colours;
        double cycle = 0.0;
        wxString rhythm; // the abbreviation without colours, for its sectors
    };
    struct ShiningLight
    {
        double lat = 0.0;
        double lon = 0.0;
        size_t character = 0;  // index in m_shining_characters
        double offset = 0.0;   // seconds; lights do not flash in step
        int state = -2;        // as LightStateAt(), last drawn
    };
    // Sectors of the sector lights in view, by position (grid of about
    // two metres), from the chart objects last collected.
    // Each sector with the rhythm of its light ("Iso.4s"): two lights at
    // one spot keep their own sectors.
    std::map<std::pair<long, long>, std::vector<std::pair<wxString, LightSector>>>
        m_light_sectors;
    std::vector<LightSector> SectorsAt(double lat, double lon, const wxString &rhythm) const;
    wxString m_light_rhythm; // rhythm of the light shining for a click

    // "Marifoon hier": the VHF areas (VHFinfo layer) the boat is in, or
    // the middle of the chart without a recent position fix.
    VhfAreas m_vhf_areas;
    bool m_vhf_loaded = false;
    void EnsureVhfAreas();

    // The area of a clicked VHF icon, lit on the chart for a few seconds.
    std::vector<VhfArea> m_vhf_highlight; // copies: a reload frees the areas
    wxLongLong m_vhf_highlight_started = 0;
    double m_vhf_highlight_alpha = 1.0;
    wxTimer m_vhf_highlight_timer;
    bool m_vhf_keep_lit = false;        // setting: lit until the next click
    bool m_vhf_notice_on = true;        // setting: notice on a new area
    std::set<wxString> m_vhf_areas_here; // areas the boat is in
    bool m_vhf_areas_known = false;     // m_vhf_areas_here is from a fix
    wxFrame *m_vhf_notice = nullptr;
    wxStaticText *m_vhf_notice_title = nullptr;
    wxStaticText *m_vhf_notice_text = nullptr;
    wxTimer m_vhf_notice_timer;
    wxTimer m_vhf_sound_timer;          // the next beep
    int m_vhf_beeps_left = 0;
    bool PlayVhfSound();
    void ShowVhfHighlight(const std::vector<VhfArea> &areas);
    std::vector<VhfArea> VhfAreasWithParts(double lat, double lon);
    wxString VhfAreaLine(const VhfArea &area) const;
    void ShowVhfHere(double lat, double lon);
    bool VhfLayerShown() const;
    void CheckVhfAreaChange();
    void ShowVhfNotice(const wxString &title, const wxString &text, bool sound,
                       const wxColour &titleColour = wxColour(220, 40, 40),
                       std::function<void()> action = std::function<void()>());
    std::function<void()> m_notice_action; // what a click on the notice opens

    // Notes that belong to a place: a sign on the chart, a heading in the
    // legend, and a notice nearby.
    std::vector<NotePlace> m_note_places;
    int m_note_selected = -1;            // clicked sign: its radius shows
    int m_note_hover = -1;               // sign under the mouse
    std::set<wxString> m_notes_near;     // notes the boat is within the radius of
    bool m_note_places_known = false;
    bool m_note_notice_on = true;        // setting
    wxTimer m_note_notice_timer;         // waits for a VHF notice to go
    wxString m_pending_note_title;
    wxString m_pending_note_text;
    wxString m_pending_note_path;
    std::vector<size_t> m_note_rows;     // rows of the heading "Notities"
    void RefreshNotePlaces();
    double SeaMiles(double lat1, double lon1, double lat2, double lon2) const;
    void OpenNote(const wxString &path);
    int NotePlaceAt(const wxPoint &pixel, double tolerancePx);
    double CurrentDisplayedScale();
    bool NoteHiddenAtScale(const NotePlace &place);
    double m_scale_cache = 0.0;         // displayed scale for the zoom below
    double m_scale_cache_ppm = -1.0;
    double m_scale_cache_lat = 0.0;
    bool DrawNoteSigns(wxDC &dc, PlugIn_ViewPort *vp);
    bool DrawNoteSignsGL(PlugIn_ViewPort *vp);
    void CheckNotePlaces();
    void ShowPendingNoteNotice();
    bool HighlightVhfAreaAt(const wxPoint &pixel, double tolerancePx);
    bool HighlightVhfAreas(const wxString &icon,
                           const std::vector<std::pair<double, double>> &positions);
    void StopVhfHighlight();
    void OnVhfHighlightTimer();
    bool DrawVhfHighlight(wxDC &dc, PlugIn_ViewPort *vp);
    bool DrawVhfHighlightGL(PlugIn_ViewPort *vp);
    double m_ship_lat = 0.0;
    double m_ship_lon = 0.0;
    wxLongLong m_ship_time = 0;   // UTC milliseconds of the last fix, 0 = none
    wxString m_vhf_shown_key;     // areas in the banner last drawn
    double m_ship_cog = NAN;      // course over ground, degrees true
    double m_ship_sog = NAN;      // speed over ground, knots

    // "Vooruit": what the boat meets in the coming half hour along its
    // course, among the objects of the list, in order of arrival.
    struct AheadItem
    {
        size_t group = 0;        // index in m_dynamic_groups
        size_t position = 0;     // index in its positions
        double minutes = 0.0;
        double cross = 0.0;      // sea miles to the right of the course
        wxString light;          // its light character, if any
    };
    std::vector<AheadItem> m_ahead;
    struct AheadVhf
    {
        double minutes = 0.0;
        double lat = 0.0;   // a point on the course inside the area
        double lon = 0.0;
        VhfArea area;
    };
    std::vector<AheadVhf> m_ahead_vhf;  // VHF areas the course enters
    struct AheadNote
    {
        double minutes = 0.0;
        double cross = 0.0;
        wxString title;
        wxString path;
    };
    std::vector<AheadNote> m_ahead_notes; // notes whose place lies ahead
    std::vector<VhfArea> m_vhf_rows;    // areas of the Marifoon block
    wxString m_ahead_key;         // what ComputeAhead() found last
    wxString m_ahead_drawn_key;   // what the list shows
    wxLongLong m_ahead_rendered = 0;
    bool m_ahead_sailing = false; // a recent fix and some speed
    void ComputeAhead();
    void ShowAheadItem(size_t index);

    // "Licht herkennen": the user holds a pad or the space bar while a light
    // shines; the rhythm is compared with the lights in view.
    bool m_identify_on = false;
    std::vector<std::pair<double, double>> m_taps; // seconds since the first
    wxLongLong m_tap_origin = 0;  // UTC milliseconds of the first tap
    wxLongLong m_tap_last = 0;    // UTC milliseconds of the last release
    bool m_tap_down = false;
    double m_tap_start = 0.0;
    wxString m_identify_colour;   // "W", "R", "G", "Y" or empty: any
    std::vector<size_t> m_identify_matches; // rows shown as matches
    wxTimer m_tap_clock_timer;    // the seconds of light and dark in the pad
    int m_rhythm_width = 180;     // pixels, as RhythmWidth() at the last render
    int RhythmWidth() const;
    void SetIdentify(bool on);
    void TapDown();
    void TapUp();
    wxString TapClockText() const;
    void RunIdentifyCommand(const wxString &what);
    void UpdateTapClock();
    // The areas at the position the banner uses; fromShip tells which.
    std::vector<const VhfArea *> VhfAreasHere(bool *fromShip, bool *layerShown);

    bool m_show_lights = true;     // setting: light characters in the list
    bool m_all_lights_on = false;
    bool m_night_preview = false;        // dark chart with the lights shining
    bool m_night_had_all_lights = false; // all lights were on before it
    std::vector<ShiningCharacter> m_shining_characters;
    std::vector<ShiningLight> m_shining_lights;
    wxLongLong m_all_lights_started;
    wxTimer m_all_lights_timer;

    bool m_light_active = false;
    std::vector<std::pair<double, double>> m_light_positions; // lat, lon
    std::vector<LightPhase> m_light_phases;
    std::vector<wxString> m_light_colours;
    double m_light_cycle = 0.0;     // seconds of the phases together
    double m_light_duration = 0.0;  // seconds the light shines on the chart
    wxLongLong m_light_started;     // UTC milliseconds
    int m_light_state = -1;         // as LightStateAt(), last drawn
    wxTimer m_light_timer;

    int m_window_x;
    int m_window_y;
    int m_window_width;
    int m_window_height;
};

#endif
