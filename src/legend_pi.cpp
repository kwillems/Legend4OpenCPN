#include "legend_pi.h"

#include <wx/button.h>
#include <wx/dir.h>
#include <wx/display.h>
#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/statline.h>
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
      m_legend_position(nullptr),
      m_legend_choice(nullptr),
      m_content_book(nullptr),
      m_image_page(nullptr),
      m_legend_bitmap(nullptr),
      m_markdown_view(nullptr),
      m_legend_index(0),
      m_window_x(-1),
      m_window_y(-1),
      m_window_width(560),
      m_window_height(460)
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
    LoadConfig();

    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL;
}

bool legend_pi::DeInit()
{
    SaveWindowState();
    SaveConfig();

    if (m_legend_window) {
        m_legend_window->Destroy();
        m_legend_window = nullptr;
        m_legend_position = nullptr;
        m_legend_choice = nullptr;
        m_content_book = nullptr;
        m_image_page = nullptr;
        m_legend_bitmap = nullptr;
        m_markdown_view = nullptr;
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

void legend_pi::LoadConfig()
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

    config.Read("WindowX", &m_window_x, -1);
    config.Read("WindowY", &m_window_y, -1);
    config.Read("WindowWidth", &m_window_width, 560);
    config.Read("WindowHeight", &m_window_height, 460);

    if (m_window_width < 380)
        m_window_width = 380;

    if (m_window_height < 300)
        m_window_height = 300;
}

void legend_pi::SaveConfig() const
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

    config.Write("LastLegend", m_last_legend_name);

    config.Write("WindowX", m_window_x);
    config.Write("WindowY", m_window_y);
    config.Write("WindowWidth", m_window_width);
    config.Write("WindowHeight", m_window_height);

    config.Flush();
}

void legend_pi::RestoreWindowState()
{
    if (!m_legend_window)
        return;

    m_legend_window->SetMinSize(wxSize(380, 300));

    wxSize size(m_window_width, m_window_height);

    if (m_window_x >= 0 && m_window_y >= 0) {
        wxPoint pos(m_window_x, m_window_y);

        if (wxDisplay::GetFromPoint(pos) != wxNOT_FOUND) {
            m_legend_window->SetSize(
                m_window_x,
                m_window_y,
                size.GetWidth(),
                size.GetHeight());
            return;
        }
    }

    m_legend_window->SetSize(size);
    m_legend_window->CentreOnParent();
}

void legend_pi::SaveWindowState()
{
    if (!m_legend_window)
        return;

    wxPoint pos = m_legend_window->GetPosition();
    wxSize size = m_legend_window->GetSize();

    m_window_x = pos.x;
    m_window_y = pos.y;
    m_window_width = size.GetWidth();
    m_window_height = size.GetHeight();
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

void legend_pi::RebuildLegendChoice()
{
    if (!m_legend_choice)
        return;

    m_legend_choice->Clear();

    if (m_legend_files.IsEmpty()) {
        m_legend_choice->Append("Geen legenda's of notities");
        m_legend_choice->SetSelection(0);
        m_legend_choice->Disable();
        return;
    }

    for (size_t i = 0; i < m_legend_files.GetCount(); ++i)
        m_legend_choice->Append(MakeDisplayTitle(m_legend_files[i]));

    m_legend_choice->Enable();
}

void legend_pi::RefreshLegendFiles()
{
    wxString preferred = m_last_legend_name;

    if (!m_legend_files.IsEmpty() &&
        m_legend_index < m_legend_files.GetCount()) {
        wxFileName current(m_legend_files[m_legend_index]);
        preferred = current.GetFullName();
    }

    ScanLegendFiles();
    RebuildLegendChoice();

    if (m_legend_files.IsEmpty()) {
        ShowEmptyState();
        return;
    }

    size_t index = 0;

    if (!preferred.IsEmpty()) {
        for (size_t i = 0; i < m_legend_files.GetCount(); ++i) {
            wxFileName file(m_legend_files[i]);

            if (file.GetFullName() == preferred) {
                index = i;
                break;
            }
        }
    }

    ShowLegend(index);
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

void legend_pi::ShowEmptyState()
{
    m_legend_index = 0;
    m_last_legend_name.Clear();

    if (m_legend_position)
        m_legend_position->SetLabel("0 van 0");

    if (m_markdown_view) {
        m_markdown_view->SetPage(
            "<html><body><p>Geen PNG- of Markdown-bestanden gevonden.</p></body></html>");
    }

    if (m_content_book && m_markdown_view)
        m_content_book->SetSelection(1);
}

void legend_pi::ShowLegend(size_t index)
{
    if (m_legend_files.IsEmpty()) {
        ShowEmptyState();
        return;
    }

    if (index >= m_legend_files.GetCount())
        index = 0;

    m_legend_index = index;

    wxString path = m_legend_files[m_legend_index];
    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    m_last_legend_name = filename.GetFullName();

    if (m_legend_position) {
        m_legend_position->SetLabel(
            wxString::Format(
                "%zu van %zu",
                m_legend_index + 1,
                m_legend_files.GetCount()));
    }

    if (m_legend_choice)
        m_legend_choice->SetSelection(static_cast<int>(m_legend_index));

    if (ext == "png") {
        wxImage image;

        if (!image.LoadFile(path, wxBITMAP_TYPE_PNG))
            return;

        if (m_legend_bitmap)
            m_legend_bitmap->SetBitmap(wxBitmap(image));

        if (m_image_page) {
            m_image_page->SetVirtualSize(
                image.GetWidth() + 20,
                image.GetHeight() + 20);
            m_image_page->Layout();
        }

        if (m_content_book)
            m_content_book->SetSelection(0);
    }
    else if (ext == "md" || ext == "markdown") {
        if (m_markdown_view)
            m_markdown_view->SetPage(RenderMarkdownFile(path));

        if (m_content_book)
            m_content_book->SetSelection(1);
    }

    SaveConfig();
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

void legend_pi::OnLegendSelected(wxCommandEvent &event)
{
    if (!m_legend_choice)
        return;

    int selection = m_legend_choice->GetSelection();

    if (selection == wxNOT_FOUND)
        return;

    ShowLegend(static_cast<size_t>(selection));
}

void legend_pi::CreateLegendWindow()
{
    if (m_legend_window)
        return;

    m_legend_window = new wxFrame(
        GetOCPNCanvasWindow(),
        wxID_ANY,
        "Legenda",
        wxDefaultPosition,
        wxSize(560, 460),
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);

    auto *panel = new wxPanel(m_legend_window);
    auto *mainSizer = new wxBoxSizer(wxVERTICAL);

    auto *headerSizer = new wxBoxSizer(wxHORIZONTAL);

    m_legend_choice = new wxChoice(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_legend_choice->SetMinSize(wxSize(260, -1));
    m_legend_choice->Disable();

    wxFont choiceFont = m_legend_choice->GetFont();
    choiceFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_legend_choice->SetFont(choiceFont);

    m_legend_position = new wxStaticText(
        panel,
        wxID_ANY,
        "0 van 0");

    headerSizer->Add(
        m_legend_choice,
        1,
        wxRIGHT | wxALIGN_CENTER_VERTICAL,
        10);

    headerSizer->Add(
        m_legend_position,
        0,
        wxALIGN_CENTER_VERTICAL);

    mainSizer->Add(
        headerSizer,
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    m_legend_choice->Bind(
        wxEVT_CHOICE,
        &legend_pi::OnLegendSelected,
        this);

    m_content_book = new wxSimplebook(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_image_page = new wxScrolledWindow(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxHSCROLL | wxVSCROLL);

    m_image_page->SetScrollRate(10, 10);

    auto *imageSizer = new wxBoxSizer(wxVERTICAL);

    m_legend_bitmap = new wxStaticBitmap(
        m_image_page,
        wxID_ANY,
        wxBitmap(1, 1));

    imageSizer->Add(
        m_legend_bitmap,
        0,
        wxALL | wxALIGN_CENTER,
        10);

    m_image_page->SetSizer(imageSizer);

    m_markdown_view = new wxHtmlWindow(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxHW_SCROLLBAR_AUTO);

    m_content_book->AddPage(m_image_page, "Afbeelding");
    m_content_book->AddPage(m_markdown_view, "Markdown");

    mainSizer->Add(
        m_content_book,
        1,
        wxEXPAND | wxALL,
        8);

    auto *separator = new wxStaticLine(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxLI_HORIZONTAL);

    mainSizer->Add(
        separator,
        0,
        wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    auto *buttonSizer = new wxBoxSizer(wxHORIZONTAL);

    auto *previousButton =
        new wxButton(panel, wxID_ANY, wxString::FromUTF8("‹  Vorige"));

    auto *nextButton =
        new wxButton(panel, wxID_ANY, wxString::FromUTF8("Volgende  ›"));

    previousButton->SetMinSize(wxSize(110, 36));
    nextButton->SetMinSize(wxSize(110, 36));

    previousButton->SetToolTip("Toon de vorige legenda of notitie");
    nextButton->SetToolTip("Toon de volgende legenda of notitie");

    buttonSizer->Add(previousButton, 0, wxRIGHT, 8);
    buttonSizer->Add(nextButton, 0, wxLEFT, 8);

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

    panel->SetSizer(mainSizer);

    RestoreWindowState();

    m_legend_window->Bind(
        wxEVT_CLOSE_WINDOW,
        [this](wxCloseEvent &event) {
            SaveWindowState();
            SaveConfig();
            m_legend_window->Hide();
            event.Veto();
        });
}

void legend_pi::ToggleLegendWindow()
{
    CreateLegendWindow();

    if (m_legend_window->IsShown()) {
        SaveWindowState();
        SaveConfig();
        m_legend_window->Hide();
    } else {
        RefreshLegendFiles();
        m_legend_window->Show();
        m_legend_window->Raise();
    }
}

void legend_pi::OnToolbarToolCallback(int id)
{
    if (id == m_toolbar_item_id)
        ToggleLegendWindow();
}
