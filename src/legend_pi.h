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
    void ShowDynamicLegend();
    void CollectDynamicLegend();
    void RenderDynamicLegend(bool keepScroll = false);
    void GoToDynamicGroup(size_t index);
    void ClearDynamicImages();

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
