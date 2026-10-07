#ifndef _LEGEND_PI_H_
#define _LEGEND_PI_H_

#include "ocpn_plugin.h"

#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/frame.h>
#include <wx/html/htmlwin.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

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

private:
    void CreateLegendWindow();
    void ToggleLegendWindow();

    void ScanLegendFiles();
    void RefreshLegendFiles();
    void ShowLegend(size_t index);
    void ShowEmptyState();

    void OnPreviousLegend(wxCommandEvent &event);
    void OnNextLegend(wxCommandEvent &event);

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

    wxStaticText *m_legend_title;
    wxSimplebook *m_content_book;

    wxScrolledWindow *m_image_page;
    wxStaticBitmap *m_legend_bitmap;

    wxHtmlWindow *m_markdown_view;

    wxArrayString m_legend_files;
    size_t m_legend_index;
    wxString m_last_legend_name;

    int m_window_x;
    int m_window_y;
    int m_window_width;
    int m_window_height;
};

#endif
