#include "legend_pi.h"

#include <wx/button.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>

#include <string>

extern "C" {
#include "md4c-html.h"
}

namespace {

void Md4cOutputCallback(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    auto *out = static_cast<std::string *>(userdata);
    out->append(text, size);
}

}

extern "C" DECL_EXP opencpn_plugin *create_pi(void *ppimgr)
{
    return new legend_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin *p)
{
    delete p;
}

legend_pi::legend_pi(void *ppimgr)
    : opencpn_plugin_118(ppimgr),
      m_toolbar_item_id(-1),
      m_legend_window(nullptr),
      m_legend_bitmap(nullptr),
      m_markdown_view(nullptr),
      m_legend_title(nullptr),
      m_legend_index(0)
{
}

legend_pi::~legend_pi()
{
}

int legend_pi::Init()
{
    wxString data_dir = GetPluginDataDir("legend_pi");

    wxString icon_path =
        data_dir +
        wxFileName::GetPathSeparator() +
        "data" +
        wxFileName::GetPathSeparator() +
        "legend_toolbar.png";

    m_toolbar_bitmap = wxBitmap(icon_path, wxBITMAP_TYPE_PNG);

    m_toolbar_item_id = InsertPlugInTool(
        "",
        &m_toolbar_bitmap,
        &m_toolbar_bitmap,
        wxITEM_NORMAL,
        "Legenda",
        "Toon of verberg de legenda",
        nullptr,
        -1,
        0,
        this);

    EnsureDirectories();
    LoadLastLegend();

    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL;
}

bool legend_pi::DeInit()
{
    SaveLastLegend();

    if (m_legend_window) {
        m_legend_window->Destroy();
        m_legend_window = nullptr;
        m_legend_bitmap = nullptr;
        m_markdown_view = nullptr;
        m_legend_title = nullptr;
    }

    if (m_toolbar_item_id >= 0) {
        RemovePlugInTool(m_toolbar_item_id);
        m_toolbar_item_id = -1;
    }

    return true;
}

int legend_pi::GetAPIVersionMajor()
{
    return 1;
}

int legend_pi::GetAPIVersionMinor()
{
    return 18;
}

int legend_pi::GetPlugInVersionMajor()
{
    return 0;
}

int legend_pi::GetPlugInVersionMinor()
{
    return 1;
}

wxString legend_pi::GetCommonName()
{
    return "Legend";
}

wxString legend_pi::GetShortDescription()
{
    return "Configurable legend for OpenCPN";
}

wxString legend_pi::GetLongDescription()
{
    return "Displays PNG legends and Markdown notes in OpenCPN.";
}

wxString legend_pi::GetLegendDirectory() const
{
    return
        wxStandardPaths::Get().GetDocumentsDir() +
        wxFileName::GetPathSeparator() +
        "OpenCPN" +
        wxFileName::GetPathSeparator() +
        "Legend";
}

wxString legend_pi::GetConfigDirectory() const
{
    return
        wxFileName::GetHomeDir() +
        wxFileName::GetPathSeparator() +
        "Library" +
        wxFileName::GetPathSeparator() +
        "Application Support" +
        wxFileName::GetPathSeparator() +
        "OpenCPN" +
        wxFileName::GetPathSeparator() +
        "Legend";
}

void legend_pi::EnsureDirectories() const
{
    wxFileName::Mkdir(
        GetLegendDirectory(),
        wxS_DIR_DEFAULT,
        wxPATH_MKDIR_FULL);

    wxFileName::Mkdir(
        GetConfigDirectory(),
        wxS_DIR_DEFAULT,
        wxPATH_MKDIR_FULL);
}

wxString legend_pi::MakeDisplayTitle(const wxString &path) const
{
    wxFileName filename(path);
    wxString title = filename.GetName();

    title.Replace("_", " ");
    title.Replace("-", " ");

    return title;
}

void legend_pi::LoadLastLegend()
{
    wxString config_path =
        GetConfigDirectory() +
        wxFileName::GetPathSeparator() +
        "legend.ini";

    wxFileConfig config(
        "Legend",
        wxEmptyString,
        config_path,
        wxEmptyString,
        wxCONFIG_USE_LOCAL_FILE);

    config.Read("LastLegend", &m_last_legend_name, "");
}

void legend_pi::SaveLastLegend() const
{
    if (m_legend_files.IsEmpty() ||
        m_legend_index >= m_legend_files.GetCount())
        return;

    wxFileName filename(m_legend_files[m_legend_index]);

    wxString config_path =
        GetConfigDirectory() +
        wxFileName::GetPathSeparator() +
        "legend.ini";

    wxFileConfig config(
        "Legend",
        wxEmptyString,
        config_path,
        wxEmptyString,
        wxCONFIG_USE_LOCAL_FILE);

    config.Write("LastLegend", filename.GetFullName());
    config.Flush();
}

void legend_pi::ScanLegendFiles()
{
    m_legend_files.Clear();

    wxString legend_dir = GetLegendDirectory();
    wxDir dir(legend_dir);

    if (!dir.IsOpened())
        return;

    wxString filename;
    bool found = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);

    while (found) {
        wxFileName file(filename);
        wxString ext = file.GetExt().Lower();

        if (ext == "png" || ext == "md" || ext == "markdown") {
            m_legend_files.Add(
                legend_dir +
                wxFileName::GetPathSeparator() +
                filename);
        }

        found = dir.GetNext(&filename);
    }

    m_legend_files.Sort();
}

wxString legend_pi::RenderMarkdownFile(const wxString &path) const
{
    wxFile file(path);

    if (!file.IsOpened())
        return "<html><body><p>Markdown-bestand kon niet worden geopend.</p></body></html>";

    wxFileOffset len = file.Length();

    if (len <= 0)
        return "<html><body><p>Leeg Markdown-bestand.</p></body></html>";

    std::string markdown;
    markdown.resize(static_cast<size_t>(len));

    if (file.Read(&markdown[0], static_cast<size_t>(len)) != len)
        return "<html><body><p>Markdown-bestand kon niet volledig worden gelezen.</p></body></html>";

    std::string html;

    unsigned parser_flags =
        MD_DIALECT_GITHUB |
        MD_FLAG_NOHTML;

    int result = md_html(
        markdown.data(),
        static_cast<MD_SIZE>(markdown.size()),
        Md4cOutputCallback,
        &html,
        parser_flags,
        0);

    if (result != 0)
        return "<html><body><p>Markdown kon niet worden verwerkt.</p></body></html>";

    wxString wrapped;
    wrapped << "<html><body>"
            << wxString::FromUTF8(html.c_str())
            << "</body></html>";

    return wrapped;
}

void legend_pi::ShowLegend(size_t index)
{
    if (m_legend_files.IsEmpty())
        return;

    if (index >= m_legend_files.GetCount())
        index = 0;

    m_legend_index = index;

    wxString path = m_legend_files[m_legend_index];
    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    if (m_legend_title)
        m_legend_title->SetLabel(MakeDisplayTitle(path));

    if (ext == "png") {
        wxImage image;

        if (!image.LoadFile(path, wxBITMAP_TYPE_PNG))
            return;

        if (m_markdown_view)
            m_markdown_view->Hide();

        if (m_legend_bitmap) {
            m_legend_bitmap->SetBitmap(wxBitmap(image));
            m_legend_bitmap->Show();
        }
    } else if (ext == "md" || ext == "markdown") {
        if (m_legend_bitmap)
            m_legend_bitmap->Hide();

        if (m_markdown_view) {
            m_markdown_view->SetPage(RenderMarkdownFile(path));
            m_markdown_view->Show();
        }
    }

    SaveLastLegend();

    if (m_legend_window) {
        m_legend_window->Layout();
        m_legend_window->Fit();
    }
}

void legend_pi::OnPreviousLegend(wxCommandEvent &event)
{
    if (m_legend_files.IsEmpty())
        return;

    if (m_legend_index == 0)
        m_legend_index = m_legend_files.GetCount() - 1;
    else
        --m_legend_index;

    ShowLegend(m_legend_index);
}

void legend_pi::OnNextLegend(wxCommandEvent &event)
{
    if (m_legend_files.IsEmpty())
        return;

    ++m_legend_index;

    if (m_legend_index >= m_legend_files.GetCount())
        m_legend_index = 0;

    ShowLegend(m_legend_index);
}

void legend_pi::CreateLegendWindow()
{
    if (m_legend_window)
        return;

    ScanLegendFiles();

    m_legend_window = new wxFrame(
        GetOCPNCanvasWindow(),
        wxID_ANY,
        "Legenda",
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);

    auto *panel = new wxPanel(m_legend_window);
    auto *mainSizer = new wxBoxSizer(wxVERTICAL);

    m_legend_title = new wxStaticText(panel, wxID_ANY, "Legenda");

    wxFont titleFont = m_legend_title->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_legend_title->SetFont(titleFont);

    mainSizer->Add(
        m_legend_title,
        0,
        wxALIGN_CENTER | wxTOP | wxLEFT | wxRIGHT,
        10);

    if (m_legend_files.IsEmpty()) {
        auto *text = new wxStaticText(
            panel,
            wxID_ANY,
            "Geen PNG- of Markdown-bestanden gevonden.");

        mainSizer->Add(
            text,
            0,
            wxALL | wxALIGN_CENTER,
            15);
    } else {
        m_legend_bitmap = new wxStaticBitmap(
            panel,
            wxID_ANY,
            wxBitmap(1, 1));

        mainSizer->Add(
            m_legend_bitmap,
            0,
            wxALL | wxALIGN_CENTER,
            10);

        m_markdown_view = new wxHtmlWindow(
            panel,
            wxID_ANY,
            wxDefaultPosition,
            wxSize(480, 320),
            wxHW_SCROLLBAR_AUTO);

        m_markdown_view->Hide();

        mainSizer->Add(
            m_markdown_view,
            1,
            wxEXPAND | wxALL,
            10);

        auto *buttonSizer = new wxBoxSizer(wxHORIZONTAL);

        auto *previousButton =
            new wxButton(panel, wxID_ANY, wxString::FromUTF8("‹"));

        auto *nextButton =
            new wxButton(panel, wxID_ANY, wxString::FromUTF8("›"));

        previousButton->SetMinSize(wxSize(42, -1));
        nextButton->SetMinSize(wxSize(42, -1));

        buttonSizer->Add(previousButton, 0, wxRIGHT, 4);
        buttonSizer->Add(nextButton, 0, wxLEFT, 4);

        mainSizer->Add(
            buttonSizer,
            0,
            wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM,
            10);

        previousButton->Bind(
            wxEVT_BUTTON,
            &legend_pi::OnPreviousLegend,
            this);

        nextButton->Bind(
            wxEVT_BUTTON,
            &legend_pi::OnNextLegend,
            this);
    }

    panel->SetSizer(mainSizer);

    if (!m_legend_files.IsEmpty()) {
        size_t start_index = 0;

        if (!m_last_legend_name.IsEmpty()) {
            for (size_t i = 0; i < m_legend_files.GetCount(); ++i) {
                wxFileName file(m_legend_files[i]);

                if (file.GetFullName() == m_last_legend_name) {
                    start_index = i;
                    break;
                }
            }
        }

        ShowLegend(start_index);
    } else {
        mainSizer->Fit(m_legend_window);
    }

    m_legend_window->Bind(
        wxEVT_CLOSE_WINDOW,
        [this](wxCloseEvent &event) {
            m_legend_window->Hide();
            event.Veto();
        });
}

void legend_pi::ToggleLegendWindow()
{
    CreateLegendWindow();

    if (m_legend_window->IsShown()) {
        m_legend_window->Hide();
    } else {
        m_legend_window->Show();
        m_legend_window->Raise();
    }
}

void legend_pi::OnToolbarToolCallback(int id)
{
    if (id == m_toolbar_item_id)
        ToggleLegendWindow();
}
