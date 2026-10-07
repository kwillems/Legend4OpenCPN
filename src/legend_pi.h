#ifndef _LEGEND_PI_H_
#define _LEGEND_PI_H_

#include "ocpn_plugin.h"

#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/frame.h>
#include <wx/html/htmlwin.h>
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
    void ShowLegend(size_t index);
    void OnPreviousLegend(wxCommandEvent &event);
    void OnNextLegend(wxCommandEvent &event);

    wxString GetLegendDirectory() const;
    wxString GetConfigDirectory() const;
    void EnsureDirectories() const;
    wxString MakeDisplayTitle(const wxString &path) const;
    wxString RenderMarkdownFile(const wxString &path) const;
    void LoadLastLegend();
    void SaveLastLegend() const;

    int m_toolbar_item_id;
    wxBitmap m_toolbar_bitmap;
    wxFrame *m_legend_window;

    wxStaticBitmap *m_legend_bitmap;
    wxHtmlWindow *m_markdown_view;
    wxStaticText *m_legend_title;

    wxArrayString m_legend_files;
    size_t m_legend_index;
    wxString m_last_legend_name;
};

#endif
