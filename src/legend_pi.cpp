// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifdef __WXOSX__
#define GL_SILENCE_DEPRECATION
#endif

#include "legend_pi.h"

#include "config.h"

#include <wx/glcanvas.h>

#include <wx/button.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/dir.h>
#include <wx/display.h>
#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filesys.h>
#include <wx/fs_mem.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/panel.h>
#include <wx/statline.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>

extern "C" {
#include "md4c-html.h"
}

namespace {

// Pseudo-path of the dynamic legend entry in the list of legend items.
const wxString kDynamicGpxId = "@gpx-legend";

class LegendHtmlWindow : public wxHtmlWindow
{
public:
    LegendHtmlWindow(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint &pos,
        const wxSize &size,
        long style)
        : wxHtmlWindow(parent, id, pos, size, style)
    {
    }

    void SetMarkdownPage(
        const wxString &html,
        const wxString &markdownPath)
    {
        wxFileName markdownFile(markdownPath);
        m_baseDirectory = markdownFile.GetPath();
        SetPage(html);
    }

    void SetDynamicPage(const wxString &html)
    {
        // The dynamic page only uses memory: images, no relative paths.
        m_baseDirectory.Clear();
        SetPage(html);
    }

protected:
    wxHtmlOpeningStatus OnOpeningURL(
        wxHtmlURLType type,
        const wxString &url,
        wxString *redirect) const override
    {
        if (type != wxHTML_URL_IMAGE || m_baseDirectory.IsEmpty())
            return wxHTML_OPEN;

        // Leave remote and already-qualified resources alone.
        if (url.StartsWith("http://") ||
            url.StartsWith("https://") ||
            url.StartsWith("file:") ||
            url.StartsWith("memory:") ||
            url.StartsWith("data:")) {
            return wxHTML_OPEN;
        }

        wxString imagePath = url;

        // A query or fragment is meaningful in a URL but not in a local
        // filename used by wxFileSystem.
        int queryPos = imagePath.Find('?');
        if (queryPos != wxNOT_FOUND)
            imagePath = imagePath.Left(queryPos);

        int fragmentPos = imagePath.Find('#');
        if (fragmentPos != wxNOT_FOUND)
            imagePath = imagePath.Left(fragmentPos);

        wxFileName imageFile(imagePath);

        if (!imageFile.IsAbsolute())
            imageFile.MakeAbsolute(m_baseDirectory);

        imageFile.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);

        *redirect = wxFileSystem::FileNameToURL(imageFile);
        return wxHTML_REDIRECT;
    }

private:
    wxString m_baseDirectory;
};

void Md4cOutputCallback(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    auto *out = static_cast<std::string *>(userdata);
    out->append(text, size);
}

// Draw an image (with or without alpha channel or mask) on a background
// colour and return the opaque result.
wxImage FlattenImage(const wxImage &image, const wxColour &background)
{
    if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0)
        return wxImage();

    const int width = image.GetWidth();
    const int height = image.GetHeight();
    const size_t pixels = static_cast<size_t>(width) * height;

    const unsigned char *src = image.GetData();
    const unsigned char *alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;

    const bool hasMask = !alpha && image.HasMask();
    const unsigned char maskR = hasMask ? image.GetMaskRed() : 0;
    const unsigned char maskG = hasMask ? image.GetMaskGreen() : 0;
    const unsigned char maskB = hasMask ? image.GetMaskBlue() : 0;

    wxImage flat(width, height, false);
    unsigned char *dst = flat.GetData();

    for (size_t i = 0; i < pixels; ++i) {
        const unsigned char r = src[i * 3];
        const unsigned char g = src[i * 3 + 1];
        const unsigned char b = src[i * 3 + 2];

        int a = alpha ? alpha[i] : 255;

        if (hasMask && r == maskR && g == maskG && b == maskB)
            a = 0;

        dst[i * 3] = static_cast<unsigned char>(
            (r * a + background.Red() * (255 - a)) / 255);
        dst[i * 3 + 1] = static_cast<unsigned char>(
            (g * a + background.Green() * (255 - a)) / 255);
        dst[i * 3 + 2] = static_cast<unsigned char>(
            (b * a + background.Blue() * (255 - a)) / 255);
    }

    return flat;
}

// OpenCPN keeps waypoint icons as PNG files: user icons in the private data
// directory (UserIcons) and the built-in ones in the shared data directory.
// Reading the file ourselves is more reliable than asking OpenCPN for a
// bitmap, which can come back empty on macOS.
wxImage LoadIconFromFile(const wxString &key, const wxColour &background)
{
    if (key.IsEmpty() || key.Find('/') != wxNOT_FOUND ||
        key.Find('\\') != wxNOT_FOUND || key.Find("..") != wxNOT_FOUND)
        return wxImage();

    wxArrayString candidates;

    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (privateDir && !privateDir->IsEmpty()) {
        candidates.Add(
            *privateDir + wxFileName::GetPathSeparator() +
            "UserIcons" + wxFileName::GetPathSeparator() + key + ".png");
    }

    wxString *sharedDir = GetpSharedDataLocation();
    if (sharedDir && !sharedDir->IsEmpty()) {
        candidates.Add(
            *sharedDir + wxFileName::GetPathSeparator() +
            "uidata" + wxFileName::GetPathSeparator() +
            "markicons" + wxFileName::GetPathSeparator() + key + ".png");
        candidates.Add(
            *sharedDir + wxFileName::GetPathSeparator() +
            "uidata" + wxFileName::GetPathSeparator() + key + ".png");
    }

    for (size_t i = 0; i < candidates.GetCount(); ++i) {
        if (!wxFileName::FileExists(candidates[i]))
            continue;

        wxImage image;
        wxLogNull quiet;

        if (image.LoadFile(candidates[i], wxBITMAP_TYPE_PNG) && image.IsOk())
            return FlattenImage(image, background);
    }

    return wxImage();
}

// Convert an OpenCPN icon bitmap to an opaque image by drawing it on the
// background colour. Transparency (alpha channel or mask) is resolved here,
// because it is lost or turns black when the image is shown by wxHtmlWindow.
wxImage FlattenIcon(
    const wxBitmap &bitmap,
    const wxColour &background,
    const wxString &key)
{
    // First attempt: let the platform draw the bitmap on a canvas with the
    // background colour. On some platforms reading the pixels of a bitmap
    // directly (ConvertToImage) gives an empty result while drawing works.
    {
        const int bw = bitmap.GetWidth();
        const int bh = bitmap.GetHeight();

        if (bw > 0 && bh > 0) {
            wxBitmap canvas(bw, bh, 24);

            {
                wxMemoryDC dc;
                dc.SelectObject(canvas);
                dc.SetBackground(wxBrush(background));
                dc.Clear();
                dc.DrawBitmap(bitmap, 0, 0, true);
                dc.SelectObject(wxNullBitmap);
            }

            wxImage drawn = canvas.ConvertToImage();

            if (drawn.IsOk()) {
                const unsigned char *d = drawn.GetData();
                const size_t n =
                    static_cast<size_t>(drawn.GetWidth()) * drawn.GetHeight();

                std::set<unsigned int> seen;
                for (size_t i = 0; i < n; ++i) {
                    seen.insert(
                        (static_cast<unsigned int>(d[i * 3]) << 16) |
                        (static_cast<unsigned int>(d[i * 3 + 1]) << 8) |
                        static_cast<unsigned int>(d[i * 3 + 2]));
                }

                // More than the plain background colour: the drawing worked.
                if (seen.size() > 1)
                    return drawn;
            }
        }
    }

    (void)key;

    return FlattenImage(bitmap.ConvertToImage(), background);
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
      m_header_book(nullptr),
      m_single_title(nullptr),
      m_legend_position(nullptr),
      m_legend_choice(nullptr),
      m_previous_button(nullptr),
      m_next_button(nullptr),
      m_content_book(nullptr),
      m_image_page(nullptr),
      m_legend_bitmap(nullptr),
      m_markdown_view(nullptr),
      m_legend_index(0),
      m_viewport_valid(false),
      m_dynamic_generation(0),
      m_dynamic_total(0),
      m_collected_viewport_valid(false),
      m_marker_active(false),
      m_marker_lat(0.0),
      m_marker_lon(0.0),
      m_marker_phase(0),
      m_window_x(-1),
      m_window_y(-1),
      m_window_width(560),
      m_window_height(460)
{
    m_viewport = PlugIn_ViewPort();
    m_collected_viewport = PlugIn_ViewPort();

    m_marker_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnMarkerTimer(); });

    m_auto_refresh_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnAutoRefreshTimer(); });
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

    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
           WANTS_ONPAINT_VIEWPORT | WANTS_OVERLAY_CALLBACK |
           WANTS_OPENGL_OVERLAY_CALLBACK |
           WANTS_DYNAMIC_OPENGL_OVERLAY_CALLBACK;
}

bool legend_pi::DeInit()
{
    SaveCurrentMarkdownScroll();
    SaveWindowState();
    SaveConfig();
    StopMarker();
    m_auto_refresh_timer.Stop();
    ClearDynamicImages();

    if (m_legend_window) {
        m_legend_window->Destroy();
        m_legend_window = nullptr;
        m_header_book = nullptr;
        m_single_title = nullptr;
        m_legend_position = nullptr;
        m_legend_choice = nullptr;
        m_previous_button = nullptr;
        m_next_button = nullptr;
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
    return PLUGIN_VERSION_MAJOR;
}

int legend_pi::GetPlugInVersionMinor()
{
    return PLUGIN_VERSION_MINOR;
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
    return "Displays PNG legends and Markdown notes in OpenCPN, plus a dynamic legend of the waypoint symbols visible on screen.";
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
    // OpenCPN's own private data directory is platform specific and known
    // to OpenCPN itself; use it instead of a hardcoded macOS path.
    wxString base;
    wxString *privateDir = GetpPrivateApplicationDataLocation();

    if (privateDir && !privateDir->IsEmpty())
        base = *privateDir;
    else
        base = wxStandardPaths::Get().GetUserDataDir();

    return
        base +
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
    if (path == kDynamicGpxId)
        return wxString::FromUTF8("Kaartlegenda (waypoints in beeld)");

    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    // For Markdown, use the first non-empty H1 as the visible title.
    // If the document does not start with an H1, fall back to the filename.
    if (ext == "md" || ext == "markdown") {
        wxTextFile markdownFile;

        if (markdownFile.Open(path)) {
            for (size_t i = 0; i < markdownFile.GetLineCount(); ++i) {
                wxString line = markdownFile.GetLine(i);
                line.Trim(true);
                line.Trim(false);

                if (line.IsEmpty())
                    continue;

                if (line.StartsWith("# ") && !line.StartsWith("##")) {
                    wxString title = line.Mid(2);
                    title.Trim(true);
                    title.Trim(false);

                    if (!title.IsEmpty()) {
                        markdownFile.Close();
                        return title;
                    }
                }

                // Only the first non-empty line can act as the document title.
                break;
            }

            markdownFile.Close();
        }
    }

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

    if (dir.IsOpened()) {
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

    // The dynamic legend is always the first item.
    m_legend_files.Insert(kDynamicGpxId, 0);
}

void legend_pi::RebuildLegendChoice()
{
    if (!m_header_book || !m_legend_choice ||
        !m_legend_position || !m_single_title)
        return;

    m_legend_choice->Clear();

    const size_t count = m_legend_files.GetCount();
    const bool canNavigate = count > 1;

    if (m_previous_button)
        m_previous_button->Enable(canNavigate);

    if (m_next_button)
        m_next_button->Enable(canNavigate);

    if (count == 0) {
        // No separate header is useful when the folder is empty.
        m_header_book->Hide();

        if (m_header_book->GetParent())
            m_header_book->GetParent()->Layout();

        return;
    }

    m_header_book->Show();

    if (count == 1) {
        // One item: show only its title. No selector and no "1 van 1".
        m_single_title->SetLabel(MakeDisplayTitle(m_legend_files[0]));
        m_header_book->SetSelection(1);

        if (m_header_book->GetParent())
            m_header_book->GetParent()->Layout();

        return;
    }

    // Multiple items: show the selector and position counter.
    for (size_t i = 0; i < count; ++i)
        m_legend_choice->Append(MakeDisplayTitle(m_legend_files[i]));

    m_legend_choice->Enable();
    m_header_book->SetSelection(2);

    if (m_header_book->GetParent())
        m_header_book->GetParent()->Layout();
}

void legend_pi::RefreshLegendFiles()
{
    SaveCurrentMarkdownScroll();

    wxString preferred = m_last_legend_name;

    if (!m_legend_files.IsEmpty() &&
        m_legend_index < m_legend_files.GetCount()) {
        wxFileName current(m_legend_files[m_legend_index]);
        preferred = current.GetFullName();
    }

    ScanLegendFiles();
    RebuildLegendChoice();

    if (m_legend_window)
        m_legend_window->Layout();

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

    // If the first non-empty Markdown line is an H1, it is already used
    // as the document title in the selector/header. Remove that one line
    // from the rendered body to avoid showing the title twice.
    {
        size_t pos = 0;

        // Skip UTF-8 BOM if present.
        if (markdown.size() >= 3 &&
            static_cast<unsigned char>(markdown[0]) == 0xEF &&
            static_cast<unsigned char>(markdown[1]) == 0xBB &&
            static_cast<unsigned char>(markdown[2]) == 0xBF) {
            pos = 3;
        }

        while (pos < markdown.size()) {
            size_t lineEnd = markdown.find('\n', pos);
            size_t contentEnd =
                (lineEnd == std::string::npos) ? markdown.size() : lineEnd;

            size_t trimmedStart = pos;
            while (trimmedStart < contentEnd &&
                   (markdown[trimmedStart] == ' ' ||
                    markdown[trimmedStart] == '\t' ||
                    markdown[trimmedStart] == '\r')) {
                ++trimmedStart;
            }

            size_t trimmedEnd = contentEnd;
            while (trimmedEnd > trimmedStart &&
                   (markdown[trimmedEnd - 1] == ' ' ||
                    markdown[trimmedEnd - 1] == '\t' ||
                    markdown[trimmedEnd - 1] == '\r')) {
                --trimmedEnd;
            }

            if (trimmedStart == trimmedEnd) {
                if (lineEnd == std::string::npos)
                    break;

                pos = lineEnd + 1;
                continue;
            }

            const bool isH1 =
                (trimmedEnd - trimmedStart >= 3) &&
                markdown[trimmedStart] == '#' &&
                markdown[trimmedStart + 1] == ' ';

            if (isH1) {
                size_t eraseEnd =
                    (lineEnd == std::string::npos) ? contentEnd : lineEnd + 1;

                markdown.erase(pos, eraseEnd - pos);
            }

            break;
        }
    }

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

void legend_pi::SaveCurrentMarkdownScroll()
{
    if (!m_markdown_view || m_current_legend_path.IsEmpty())
        return;

    wxFileName currentFile(m_current_legend_path);
    wxString ext = currentFile.GetExt().Lower();

    if (ext != "md" && ext != "markdown")
        return;

    int x = 0;
    int y = 0;
    m_markdown_view->GetViewStart(&x, &y);

    m_markdown_scroll_positions[m_current_legend_path] =
        wxPoint(x, y);
}

void legend_pi::RestoreMarkdownScroll(const wxString &path)
{
    if (!m_markdown_view)
        return;

    wxPoint position(0, 0);

    auto it = m_markdown_scroll_positions.find(path);
    if (it != m_markdown_scroll_positions.end())
        position = it->second;

    // SetPage() rebuilds the HTML contents. Restore the scroll position
    // after wxWidgets has had a chance to lay out the new page.
    m_markdown_view->CallAfter(
        [this, path, position]() {
            if (m_markdown_view &&
                m_current_legend_path == path) {
                m_markdown_view->Scroll(
                    position.x,
                    position.y);
            }
        });
}

void legend_pi::ShowEmptyState()
{
    SaveCurrentMarkdownScroll();

    m_legend_index = 0;
    m_last_legend_name.Clear();
    m_current_legend_path.Clear();

    if (m_markdown_view) {
        wxString legend_dir = GetLegendDirectory();
        legend_dir.Replace("&", "&amp;");
        legend_dir.Replace("<", "&lt;");
        legend_dir.Replace(">", "&gt;");

        wxString html;
        html << "<html><body>"
             << "<font size=\"+1\">"
             << "<p><b>Geen legenda's of notities gevonden.</b></p>"
             << "<p>Plaats een PNG- of Markdown-bestand in:</p>"
             << "<p><code>" << legend_dir << "</code></p>"
             << "</font>"
             << "</body></html>";

        m_markdown_view->SetPage(html);
    }

    if (m_content_book && m_markdown_view)
        m_content_book->SetSelection(1);
}

void legend_pi::UpdateImageScale()
{
    if (!m_image_page || !m_legend_bitmap || !m_current_image.IsOk())
        return;

    wxSize client = m_image_page->GetClientSize();

    // Leave some breathing room around the image.
    const int availableWidth = client.GetWidth() - 20;
    const int availableHeight = client.GetHeight() - 20;

    if (availableWidth <= 0 || availableHeight <= 0)
        return;

    const int imageWidth = m_current_image.GetWidth();
    const int imageHeight = m_current_image.GetHeight();

    if (imageWidth <= 0 || imageHeight <= 0)
        return;

    double scale = 1.0;

    if (imageWidth > availableWidth || imageHeight > availableHeight) {
        const double scaleX =
            static_cast<double>(availableWidth) / imageWidth;
        const double scaleY =
            static_cast<double>(availableHeight) / imageHeight;

        scale = wxMin(scaleX, scaleY);
    }

    int displayWidth =
        static_cast<int>(imageWidth * scale);
    int displayHeight =
        static_cast<int>(imageHeight * scale);

    displayWidth = wxMax(1, displayWidth);
    displayHeight = wxMax(1, displayHeight);

    wxImage displayImage = m_current_image;

    if (displayWidth != imageWidth || displayHeight != imageHeight) {
        displayImage = m_current_image.Scale(
            displayWidth,
            displayHeight,
            wxIMAGE_QUALITY_HIGH);
    }

    m_legend_bitmap->SetBitmap(wxBitmap(displayImage));

    // The image page is a normal panel, not a scrolled window. The bitmap
    // is therefore always constrained to the available view.
    m_image_page->Layout();
}

void legend_pi::ShowLegend(size_t index)
{
    SaveCurrentMarkdownScroll();

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

    if (m_legend_files.GetCount() > 1) {
        if (m_legend_position) {
            m_legend_position->SetLabel(
                wxString::Format(
                    "%zu van %zu",
                    m_legend_index + 1,
                    m_legend_files.GetCount()));
        }

        if (m_legend_choice)
            m_legend_choice->SetSelection(static_cast<int>(m_legend_index));
    }

    if (path == kDynamicGpxId) {
        m_current_image = wxImage();
        m_current_legend_path = path;
        ShowDynamicLegend();
    }
    else if (ext == "png") {
        wxImage image;

        if (!image.LoadFile(path, wxBITMAP_TYPE_PNG))
            return;

        m_current_legend_path = path;
        m_current_image = image.Copy();

        if (m_content_book)
            m_content_book->SetSelection(0);

        UpdateImageScale();
    }
    else if (ext == "md" || ext == "markdown") {
        m_current_image = wxImage();
        m_current_legend_path = path;

        if (m_markdown_view) {
            auto *markdownWindow =
                static_cast<LegendHtmlWindow *>(m_markdown_view);

            markdownWindow->SetMarkdownPage(
                RenderMarkdownFile(path),
                path);
        }

        if (m_content_book)
            m_content_book->SetSelection(1);

        RestoreMarkdownScroll(path);
    }

    if (m_legend_window)
        m_legend_window->Layout();

    SaveConfig();
}

void legend_pi::OnPreviousLegend(wxCommandEvent &event)
{
    if (m_legend_files.IsEmpty())
        return;

    size_t index =
        (m_legend_index == 0)
            ? m_legend_files.GetCount() - 1
            : m_legend_index - 1;

    ShowLegend(index);
}

void legend_pi::OnNextLegend(wxCommandEvent &event)
{
    if (m_legend_files.IsEmpty())
        return;

    size_t index = m_legend_index + 1;

    if (index >= m_legend_files.GetCount())
        index = 0;

    ShowLegend(index);
}

void legend_pi::OnLegendSelected(wxCommandEvent &event)
{
    if (!m_legend_choice)
        return;

    int selection = m_legend_choice->GetSelection();

    if (selection == wxNOT_FOUND)
        return;

    ShowLegend(static_cast<size_t>(selection));

    // Keep keyboard focus on the selector after a choice has been made.
    // This allows Up/Down to continue moving through the list immediately.
    m_legend_choice->SetFocus();
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

    m_header_book = new wxSimplebook(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    // Page 0: empty state. The whole book is hidden when there are no items.
    auto *emptyHeaderPage = new wxPanel(m_header_book);

    // Page 1: exactly one item -> title only.
    auto *singleHeaderPage = new wxPanel(m_header_book);
    auto *singleHeaderSizer = new wxBoxSizer(wxHORIZONTAL);

    m_single_title = new wxStaticText(
        singleHeaderPage,
        wxID_ANY,
        "");

    wxFont singleTitleFont = m_single_title->GetFont();
    singleTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_single_title->SetFont(singleTitleFont);

    singleHeaderSizer->Add(
        m_single_title,
        1,
        wxALIGN_CENTER_VERTICAL);

    singleHeaderPage->SetSizer(singleHeaderSizer);

    // Page 2: multiple items -> selector plus "x van y".
    auto *multiHeaderPage = new wxPanel(m_header_book);
    auto *multiHeaderSizer = new wxBoxSizer(wxHORIZONTAL);

    m_legend_choice = new wxChoice(
        multiHeaderPage,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_legend_choice->SetMinSize(wxSize(160, -1));

    wxFont choiceFont = m_legend_choice->GetFont();
    choiceFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_legend_choice->SetFont(choiceFont);

    m_legend_position = new wxStaticText(
        multiHeaderPage,
        wxID_ANY,
        "");

    // Keep the position counter visible on the right, even in a narrow window.
    m_legend_position->SetMinSize(wxSize(52, -1));

    multiHeaderSizer->Add(
        m_legend_choice,
        1,
        wxRIGHT | wxEXPAND | wxALIGN_CENTER_VERTICAL,
        10);

    multiHeaderSizer->Add(
        m_legend_position,
        0,
        wxALIGN_CENTER_VERTICAL);

    multiHeaderPage->SetSizer(multiHeaderSizer);

    m_header_book->AddPage(emptyHeaderPage, "Leeg");
    m_header_book->AddPage(singleHeaderPage, "Een");
    m_header_book->AddPage(multiHeaderPage, "Meerdere");
    m_header_book->SetSelection(0);
    m_header_book->Hide();

    mainSizer->Add(
        m_header_book,
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

    m_image_page = new wxPanel(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    m_image_page->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent &event) {
            event.Skip();
            UpdateImageScale();
        });

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

    m_markdown_view = new LegendHtmlWindow(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxHW_SCROLLBAR_AUTO);

    // Give Markdown some breathing room without using extra layout rows.
    m_markdown_view->SetBorders(12);

    // Use the surrounding OpenCPN UI font as the basis for Markdown.
    // The seven values correspond to HTML font sizes 1 through 7.
    const int baseFontSize =
        wxMax(11, panel->GetFont().GetPointSize());

    int htmlFontSizes[7] = {
        wxMax(9, baseFontSize - 2),
        wxMax(10, baseFontSize - 1),
        baseFontSize,
        baseFontSize + 2,
        baseFontSize + 4,
        baseFontSize + 7,
        baseFontSize + 10
    };

    wxString normalFace = panel->GetFont().GetFaceName();

    wxFont fixedFont(
        baseFontSize,
        wxFONTFAMILY_TELETYPE,
        wxFONTSTYLE_NORMAL,
        wxFONTWEIGHT_NORMAL);

    m_markdown_view->SetFonts(
        normalFace,
        fixedFont.GetFaceName(),
        htmlFontSizes);

    // Make the Markdown view visually belong to the plugin window.
    m_markdown_view->SetBackgroundColour(panel->GetBackgroundColour());
    m_markdown_view->SetForegroundColour(panel->GetForegroundColour());

    // Open normal external Markdown links in the system browser.
    m_markdown_view->Bind(
        wxEVT_HTML_LINK_CLICKED,
        [this](wxHtmlLinkEvent &event) {
            wxString href = event.GetLinkInfo().GetHref();

            // Links inside the dynamic legend.
            if (href.StartsWith("legend-goto:")) {
                long index = -1;

                if (href.Mid(12).ToLong(&index) && index >= 0) {
                    m_markdown_view->CallAfter(
                        [this, index]() {
                            GoToDynamicGroup(static_cast<size_t>(index));
                        });
                }
                return;
            }

            if (href.StartsWith("legend-refresh:")) {
                m_markdown_view->CallAfter(
                    [this]() { ShowDynamicLegend(); });
                return;
            }

            if (href.StartsWith("http://") ||
                href.StartsWith("https://") ||
                href.StartsWith("mailto:") ||
                href.StartsWith("tel:")) {
                wxLaunchDefaultBrowser(href);
                return;
            }

            event.Skip();
        });

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

    m_previous_button =
        new wxButton(panel, wxID_ANY, wxString::FromUTF8("‹  Vorige"));

    m_next_button =
        new wxButton(panel, wxID_ANY, wxString::FromUTF8("Volgende  ›"));

    m_previous_button->SetMinSize(wxSize(110, 36));
    m_next_button->SetMinSize(wxSize(110, 36));

    m_previous_button->SetToolTip("Toon de vorige legenda of notitie");
    m_next_button->SetToolTip("Toon de volgende legenda of notitie");

    buttonSizer->Add(m_previous_button, 0, wxRIGHT, 8);
    buttonSizer->Add(m_next_button, 0, wxLEFT, 8);

    mainSizer->Add(
        buttonSizer,
        0,
        wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    m_previous_button->Bind(
        wxEVT_BUTTON,
        &legend_pi::OnPreviousLegend,
        this);

    m_next_button->Bind(
        wxEVT_BUTTON,
        &legend_pi::OnNextLegend,
        this);

    // Until the first scan completes, there is nothing to navigate.
    m_previous_button->Disable();
    m_next_button->Disable();

    panel->SetSizer(mainSizer);

    RestoreWindowState();

    m_legend_window->Bind(
        wxEVT_CLOSE_WINDOW,
        [this](wxCloseEvent &event) {
            SaveCurrentMarkdownScroll();
            SaveWindowState();
            SaveConfig();
            m_legend_window->Hide();
            event.Veto();
        });

    m_legend_window->Bind(
        wxEVT_CHAR_HOOK,
        [this](wxKeyEvent &event) {
            const bool plainKey =
                !event.ControlDown() &&
                !event.AltDown() &&
                !event.MetaDown();

            if (plainKey &&
                event.GetKeyCode() == WXK_ESCAPE) {
                SaveCurrentMarkdownScroll();
                SaveWindowState();
                SaveConfig();
                m_legend_window->Hide();
                return;
            }

            if (plainKey &&
                m_legend_files.GetCount() > 1) {

                if (event.GetKeyCode() == WXK_LEFT) {
                    wxCommandEvent commandEvent;
                    OnPreviousLegend(commandEvent);
                    return;
                }

                if (event.GetKeyCode() == WXK_RIGHT) {
                    wxCommandEvent commandEvent;
                    OnNextLegend(commandEvent);
                    return;
                }
            }

            event.Skip();
        });
}

void legend_pi::ToggleLegendWindow()
{
    CreateLegendWindow();

    if (m_legend_window->IsShown()) {
        SaveCurrentMarkdownScroll();
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


void legend_pi::SetCurrentViewPort(PlugIn_ViewPort &vp)
{
    m_viewport = vp;
    m_viewport_valid = (vp.pix_width > 0 && vp.pix_height > 0);

    if (!m_viewport_valid || !IsDynamicLegendVisible())
        return;

    // OpenCPN calls this on every repaint, also while the marker animates.
    // Only a real change of position, zoom, rotation or window size counts.
    const PlugIn_ViewPort &old = m_collected_viewport;

    const bool changed =
        !m_collected_viewport_valid ||
        std::fabs(vp.clat - old.clat) > 1e-7 ||
        std::fabs(vp.clon - old.clon) > 1e-7 ||
        std::fabs(vp.view_scale_ppm - old.view_scale_ppm) >
            1e-6 * old.view_scale_ppm ||
        std::fabs(vp.rotation - old.rotation) > 1e-4 ||
        vp.pix_width != old.pix_width ||
        vp.pix_height != old.pix_height;

    // Wait until the chart has stopped moving before rebuilding the list.
    if (changed)
        m_auto_refresh_timer.StartOnce(400);
}

bool legend_pi::IsDynamicLegendVisible() const
{
    return m_legend_window &&
           m_legend_window->IsShown() &&
           m_current_legend_path == kDynamicGpxId;
}

void legend_pi::OnAutoRefreshTimer()
{
    if (!IsDynamicLegendVisible())
        return;

    const std::vector<DynamicGroup> previous = m_dynamic_groups;

    CollectDynamicLegend();

    auto sortedPositions = [](const DynamicGroup &group) {
        auto positions = group.positions;
        std::sort(positions.begin(), positions.end());
        return positions;
    };

    std::map<wxString, const DynamicGroup *> previousByKey;
    for (const DynamicGroup &group : previous)
        previousByKey[group.key] = &group;

    bool changed = previous.size() != m_dynamic_groups.size();

    for (DynamicGroup &group : m_dynamic_groups) {
        auto found = previousByKey.find(group.key);
        if (found == previousByKey.end()) {
            changed = true;
            continue;
        }

        const DynamicGroup &old = *found->second;

        // Same waypoints as before: keep the old order and counter, so
        // clicking on continues through the same sequence. Re-sorting
        // around the new centre would send the next click back to the
        // object just visited.
        if (sortedPositions(old) == sortedPositions(group)) {
            group.positions = old.positions;
            group.current = old.current;
            continue;
        }

        changed = true;

        // The set changed; continue from the object shown last if it is
        // still in view.
        if (old.current >= 0) {
            const auto &shown =
                old.positions[static_cast<size_t>(old.current)];

            for (size_t i = 0; i < group.positions.size(); ++i) {
                if (group.positions[i] == shown) {
                    group.current = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // Nothing to show differently: leave the window alone.
    if (!changed) {
        m_dynamic_groups = previous;
        return;
    }

    RenderDynamicLegend(true);
}

void legend_pi::ClearDynamicImages()
{
    for (const wxString &name : m_dynamic_image_names)
        wxMemoryFSHandler::RemoveFile(name);

    m_dynamic_image_names.clear();
}

void legend_pi::CollectDynamicLegend()
{
    m_dynamic_groups.clear();
    m_dynamic_total = 0;

    m_collected_viewport = m_viewport;
    m_collected_viewport_valid = m_viewport_valid;

    if (!m_viewport_valid)
        return;

    const PlugIn_ViewPort vp = m_viewport;

    // Longitudes may be offset by 360 degrees relative to the viewport.
    auto lonInView = [&vp](double lon) {
        for (double offset : {0.0, 360.0, -360.0}) {
            const double l = lon + offset;
            if (l >= vp.lon_min && l <= vp.lon_max)
                return true;
        }
        return false;
    };

    std::map<wxString, DynamicGroup> groups;

    wxArrayString guids = GetWaypointGUIDArray();

    for (size_t i = 0; i < guids.GetCount(); ++i) {
        std::unique_ptr<PlugIn_Waypoint_Ex> wp =
            GetWaypointEx_Plugin(guids[i]);

        if (!wp || !wp->IsVisible)
            continue;

        // Waypoints with a scale limit disappear when zoomed out too far.
        if (wp->b_useScamin && vp.chart_scale > wp->scamin)
            continue;

        if (wp->m_lat < vp.lat_min || wp->m_lat > vp.lat_max)
            continue;

        if (!lonInView(wp->m_lon))
            continue;

        wxString key = wp->IconName;
        if (key.IsEmpty())
            key = "unknown";

        DynamicGroup &group = groups[key];

        if (group.key.IsEmpty()) {
            group.key = key;
            group.label =
                wp->IconDescription.IsEmpty() ? key : wp->IconDescription;
        }

        group.positions.emplace_back(wp->m_lat, wp->m_lon);
        ++m_dynamic_total;
    }

    // Order the positions of every group from near to far, measured from
    // the centre of the screen.
    const double kPi = 3.14159265358979323846;
    const double cosLat = std::cos(vp.clat * kPi / 180.0);

    auto distance = [&](const std::pair<double, double> &p) {
        double dlat = p.first - vp.clat;
        double dlon = p.second - vp.clon;

        while (dlon > 180.0)
            dlon -= 360.0;
        while (dlon < -180.0)
            dlon += 360.0;

        dlon *= cosLat;
        return dlat * dlat + dlon * dlon;
    };

    for (auto &entry : groups) {
        DynamicGroup group = entry.second;

        std::sort(
            group.positions.begin(),
            group.positions.end(),
            [&](const std::pair<double, double> &a,
                const std::pair<double, double> &b) {
                return distance(a) < distance(b);
            });

        m_dynamic_groups.push_back(group);
    }

    std::sort(
        m_dynamic_groups.begin(),
        m_dynamic_groups.end(),
        [](const DynamicGroup &a, const DynamicGroup &b) {
            return a.label.CmpNoCase(b.label) < 0;
        });
}

void legend_pi::RenderDynamicLegend(bool keepScroll)
{
    if (!m_markdown_view)
        return;

    static bool memoryHandlerAdded = false;
    if (!memoryHandlerAdded) {
        wxFileSystem::AddHandler(new wxMemoryFSHandler);
        memoryHandlerAdded = true;
    }

    int scrollX = 0;
    int scrollY = 0;
    if (keepScroll)
        m_markdown_view->GetViewStart(&scrollX, &scrollY);

    ClearDynamicImages();
    ++m_dynamic_generation;

    auto escape = [](wxString text) {
        text.Replace("&", "&amp;");
        text.Replace("<", "&lt;");
        text.Replace(">", "&gt;");
        return text;
    };

    wxString html;
    html << "<html><body>";
    html << "<p><b>Kaartlegenda</b></p>";

    if (!m_viewport_valid) {
        html << "<p>De kaartpositie is nog niet bekend. Beweeg de kaart "
                "een klein stukje en klik daarna op "
                "<a href=\"legend-refresh:\">Ververs</a>.</p>";
    }
    else {
        html << "<p>" << static_cast<int>(m_dynamic_groups.size())
             << " symbolen, "
             << m_dynamic_total << " waypoints in beeld. "
             << "<a href=\"legend-refresh:\">Ververs</a></p>";

        if (m_dynamic_groups.empty()) {
            html << "<p>Geen zichtbare waypoints in dit kaartbeeld.</p>";
        }
        else {
            html << "<table cellpadding=\"4\">";

            for (size_t i = 0; i < m_dynamic_groups.size(); ++i) {
                DynamicGroup &group = m_dynamic_groups[i];
                group.imageName.Clear();

                const wxColour background =
                    m_markdown_view->GetBackgroundColour();

                // First choice: the icon's own PNG file. Fall back to
                // asking OpenCPN for the bitmap.
                wxImage image = LoadIconFromFile(group.key, background);

                if (!image.IsOk()) {
                    wxBitmap bitmap = GetIcon_PlugIn(group.key);

                    if (bitmap.IsOk())
                        image = FlattenIcon(bitmap, background, group.key);
                }

                if (image.IsOk()) {
                    wxString name = wxString::Format(
                        "legend_dyn_%d_%zu.png",
                        m_dynamic_generation,
                        i);

                    wxMemoryFSHandler::AddFile(
                        name, image, wxBITMAP_TYPE_PNG);

                    m_dynamic_image_names.push_back(name);
                    group.imageName = name;
                }

                const size_t count = group.positions.size();

                wxString countText;
                if (group.current >= 0 && count > 1) {
                    countText = wxString::Format(
                        "%d/%zu", group.current + 1, count);
                }
                else {
                    countText = wxString::Format("&times;%zu", count);
                }

                html << "<tr><td>";
                if (!group.imageName.IsEmpty())
                    html << "<img src=\"memory:" << group.imageName << "\">";
                html << "</td><td><a href=\"legend-goto:" << static_cast<int>(i) << "\">"
                     << escape(group.label) << "</a></td><td align=\"right\">"
                     << countText << "</td></tr>";
            }

            html << "</table>";
            html << "<p><font size=\"-1\">Klik op een symbool om naar het "
                    "dichtstbijzijnde te gaan. Nog een klik gaat naar het "
                    "volgende. De lijst past zich vanzelf aan als je de "
                    "kaart verschuift of zoomt.</font></p>";
        }
    }

    html << "</body></html>";

    static_cast<LegendHtmlWindow *>(m_markdown_view)->SetDynamicPage(html);

    if (keepScroll) {
        m_markdown_view->CallAfter(
            [this, scrollX, scrollY]() {
                if (m_markdown_view &&
                    m_current_legend_path == kDynamicGpxId) {
                    m_markdown_view->Scroll(scrollX, scrollY);
                }
            });
    }
}

void legend_pi::ShowDynamicLegend()
{
    CollectDynamicLegend();
    RenderDynamicLegend(false);

    if (m_content_book)
        m_content_book->SetSelection(1);
}

void legend_pi::GoToDynamicGroup(size_t index)
{
    if (index >= m_dynamic_groups.size())
        return;

    DynamicGroup &group = m_dynamic_groups[index];

    if (group.positions.empty())
        return;

    group.current =
        (group.current + 1) % static_cast<int>(group.positions.size());

    const std::pair<double, double> &position =
        group.positions[static_cast<size_t>(group.current)];

    // Keep the current zoom level.
    double scale = m_viewport_valid ? m_viewport.view_scale_ppm : 0.0;
    if (scale <= 0.0)
        scale = 0.1;

    // Only move the chart when the object is not comfortably in view.
    // Otherwise the chart stays where it is and the marker shows the spot.
    bool comfortablyVisible = false;

    if (m_viewport_valid) {
        wxPoint pixel;
        GetCanvasPixLL(&m_viewport, &pixel, position.first, position.second);

        const int marginX = m_viewport.pix_width / 8;
        const int marginY = m_viewport.pix_height / 8;

        comfortablyVisible =
            pixel.x >= marginX &&
            pixel.x <= m_viewport.pix_width - marginX &&
            pixel.y >= marginY &&
            pixel.y <= m_viewport.pix_height - marginY;
    }

    if (!comfortablyVisible) {
        wxWindow *canvas = PluginGetFocusCanvas();
        if (!canvas)
            canvas = GetCanvasByIndex(0);

        if (canvas) {
            CanvasJumpToPosition(
                canvas, position.first, position.second, scale);
        }
        else {
            JumpToPosition(position.first, position.second, scale);
        }
    }

    ShowMarker(position.first, position.second);

    RenderDynamicLegend(true);
}


void legend_pi::RefreshAllCanvases()
{
    const int count = GetCanvasCount();

    for (int i = 0; i < count; ++i) {
        wxWindow *canvas = GetCanvasByIndex(i);

        if (canvas)
            RequestRefresh(canvas);
    }
}

void legend_pi::ShowMarker(double lat, double lon)
{
    m_marker_lat = lat;
    m_marker_lon = lon;
    m_marker_phase = 0;
    m_marker_active = true;

    if (m_marker_timer.IsRunning())
        m_marker_timer.Stop();

    m_marker_timer.Start(120);
    RefreshAllCanvases();
}

void legend_pi::StopMarker()
{
    if (m_marker_timer.IsRunning())
        m_marker_timer.Stop();

    if (m_marker_active) {
        m_marker_active = false;
        RefreshAllCanvases();
    }
}

void legend_pi::OnMarkerTimer()
{
    ++m_marker_phase;

    // About six seconds in total.
    if (m_marker_phase > 50) {
        StopMarker();
        return;
    }

    RefreshAllCanvases();
}

bool legend_pi::RenderOverlayMultiCanvas(
    wxDC &dc,
    PlugIn_ViewPort *vp,
    int canvasIndex,
    int priority)
{
    (void)canvasIndex;

    if (!m_marker_active || !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    wxPoint centre;
    GetCanvasPixLL(vp, &centre, m_marker_lat, m_marker_lon);

    if (centre.x < -60 || centre.y < -60 ||
        centre.x > vp->pix_width + 60 || centre.y > vp->pix_height + 60)
        return false;

    const int radius = 16;
    const int pulse = m_marker_phase % 10;

    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    const wxColour highlight(255, 0, 110);

    // White outline first, so the marker stands out on any chart colour.
    dc.SetPen(wxPen(*wxWHITE, 7));
    dc.DrawCircle(centre, radius);
    dc.DrawLine(centre.x - radius - 12, centre.y, centre.x - radius + 2, centre.y);
    dc.DrawLine(centre.x + radius - 2, centre.y, centre.x + radius + 12, centre.y);
    dc.DrawLine(centre.x, centre.y - radius - 12, centre.x, centre.y - radius + 2);
    dc.DrawLine(centre.x, centre.y + radius - 2, centre.x, centre.y + radius + 12);

    dc.SetPen(wxPen(highlight, 3));
    dc.DrawCircle(centre, radius);
    dc.DrawLine(centre.x - radius - 10, centre.y, centre.x - radius + 2, centre.y);
    dc.DrawLine(centre.x + radius - 2, centre.y, centre.x + radius + 10, centre.y);
    dc.DrawLine(centre.x, centre.y - radius - 10, centre.x, centre.y - radius + 2);
    dc.DrawLine(centre.x, centre.y + radius - 2, centre.x, centre.y + radius + 10);

    // A pulsing ring that draws the eye.
    dc.SetPen(wxPen(highlight, 2));
    dc.DrawCircle(centre, radius + 8 + pulse * 3);

    return true;
}

bool legend_pi::RenderGLOverlayMultiCanvas(
    wxGLContext *pcontext,
    PlugIn_ViewPort *vp,
    int canvasIndex,
    int priority)
{
    (void)pcontext;
    (void)canvasIndex;

    if (!m_marker_active || !vp)
        return false;

    // Draw once per frame.
    if (priority != OVERLAY_LEGACY && priority != -1)
        return false;

    wxPoint centre;
    GetCanvasPixLL(vp, &centre, m_marker_lat, m_marker_lon);

    if (centre.x < -60 || centre.y < -60 ||
        centre.x > vp->pix_width + 60 || centre.y > vp->pix_height + 60)
        return false;

    const double cx = centre.x;
    const double cy = centre.y;
    const double radius = 16.0;
    const int pulse = m_marker_phase % 10;

    glPushAttrib(
        GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    auto circle = [](double x, double y, double r) {
        const int segments = 48;
        const double kPi = 3.14159265358979323846;

        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * kPi * i / segments;
            glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
        }
        glEnd();
    };

    auto ticks = [](double x, double y, double r, double length) {
        glBegin(GL_LINES);
        glVertex2d(x - r - length, y);
        glVertex2d(x - r + 2, y);
        glVertex2d(x + r - 2, y);
        glVertex2d(x + r + length, y);
        glVertex2d(x, y - r - length);
        glVertex2d(x, y - r + 2);
        glVertex2d(x, y + r - 2);
        glVertex2d(x, y + r + length);
        glEnd();
    };

    // White outline first, so the marker stands out on any chart colour.
    glColor4ub(255, 255, 255, 255);
    glLineWidth(7.0f);
    circle(cx, cy, radius);
    ticks(cx, cy, radius, 12.0);

    glColor4ub(255, 0, 110, 255);
    glLineWidth(3.0f);
    circle(cx, cy, radius);
    ticks(cx, cy, radius, 10.0);

    // A pulsing ring that draws the eye.
    glLineWidth(2.0f);
    circle(cx, cy, radius + 8.0 + pulse * 3.0);

    glPopAttrib();

    return true;
}
