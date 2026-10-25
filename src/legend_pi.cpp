// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "legend_pi.h"

#include "config.h"
#include "legend_html_window.h"
#include "legend_shared.h"

#include <wx/bmpbndl.h>
#include <wx/dir.h>
#include <wx/display.h>
#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filesys.h>
#include <wx/fs_mem.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <memory>
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

// wxHTML knows no check boxes. A list of tasks becomes an indented list
// without bullets (<dl>), each item starting with an empty or a ticked box.
std::string ShowTaskLists(const std::string &html)
{
    static const std::string kTaskItem = "<li class=\"task-list-item\">";
    static const std::string kBox =
        "<input type=\"checkbox\" class=\"task-list-item-checkbox\" disabled";
    if (html.find(kTaskItem) == std::string::npos)
        return html;

    std::string out;
    out.reserve(html.size() + 64);
    std::vector<bool> lists; // per open <ul>: shown as <dl>
    std::vector<bool> items; // per open <li>: shown as <dd>
    bool dropParagraphEnd = false;
    size_t pos = 0;
    while (pos < html.size()) {
        if (html.compare(pos, 4, "<ul>") == 0) {
            const size_t next = html.find('<', pos + 4);
            const bool tasks =
                next != std::string::npos && html.compare(next, kTaskItem.size(), kTaskItem) == 0;
            lists.push_back(tasks);
            out += tasks ? "<dl>" : "<ul>";
            pos += 4;
        }
        else if (html.compare(pos, 5, "</ul>") == 0) {
            const bool tasks = !lists.empty() && lists.back();
            if (!lists.empty())
                lists.pop_back();
            out += tasks ? "</dl>" : "</ul>";
            pos += 5;
        }
        else if (html.compare(pos, kTaskItem.size(), kTaskItem) == 0) {
            pos += kTaskItem.size();
            bool checked = false;
            if (html.compare(pos, kBox.size(), kBox) == 0) {
                const size_t end = html.find('>', pos);
                checked = html.substr(pos, end - pos).find("checked") != std::string::npos;
                pos = end == std::string::npos ? html.size() : end + 1;
            }
            const bool asTerm = !lists.empty() && lists.back();
            items.push_back(asTerm);
            out += asTerm ? "<dd>" : "<li>";
            out += checked ? "<font size=\"+1\">\xE2\x98\x91</font>&nbsp;"
                           : "<font size=\"+1\">\xE2\x98\x90</font>&nbsp;";
            // In a list with empty lines between the items the text is a
            // paragraph; it stays on the line of its box.
            if (html.compare(pos, 3, "<p>") == 0) {
                pos += 3;
                dropParagraphEnd = true;
            }
        }
        else if (dropParagraphEnd && html.compare(pos, 4, "</p>") == 0) {
            dropParagraphEnd = false;
            pos += 4;
        }
        else if (html.compare(pos, 4, "<li>") == 0 || html.compare(pos, 4, "<li ") == 0) {
            items.push_back(false);
            const size_t end = html.find('>', pos);
            out.append(html, pos, end - pos + 1);
            pos = end + 1;
        }
        else if (html.compare(pos, 5, "</li>") == 0) {
            const bool asTerm = !items.empty() && items.back();
            if (!items.empty())
                items.pop_back();
            out += asTerm ? "</dd>" : "</li>";
            pos += 5;
        }
        else {
            out += html[pos++];
        }
    }
    return out;
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
      m_loading(false),
      m_loading_reload(false),
      m_loading_next(0),
      m_loading_fetched(0),
      m_prefetch_scheduled(false),
      m_showing_loading_page(false),
      m_collected_viewport_valid(false),
      m_marker_active(false),
      m_marker_phase(0),
      m_window_x(-1),
      m_window_y(-1),
      m_window_width(560),
      m_window_height(460)
{
    m_viewport = PlugIn_ViewPort();
    m_collected_viewport = PlugIn_ViewPort();
    m_pending_viewport = PlugIn_ViewPort();

    m_marker_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnMarkerTimer(); });

    m_auto_refresh_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnAutoRefreshTimer(); });

    m_loading_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnLoadingTimer(); });

    m_visibility_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { CheckVisibilityChanges(); });

    m_flash_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { OnFlashTimer(); });

    m_filter_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) {
            if (m_search)
                SetFilter(m_search->GetValue());
        });

    m_tap_clock_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) { UpdateTapClock(); });

    m_cell_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OpenNextChartCell(); });

    m_fill_window_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        if (!m_legend_window || !m_legend_window->IsShown())
            return;
        wxStopWatch watch;
        RefreshLegendFiles();
        wxLogMessage("Legend: window filled in %ld ms", watch.Time());
    });

    // Fill the cache once OpenCPN has settled after starting, so the first
    // look at the dynamic legend does not have to wait for it.
    m_prefetch_timer.Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) {
            if (!m_loading && m_waypoint_cache.empty())
                StartWaypointLoading(false);
        });
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

    RefreshNotePlaces();
    m_note_notice_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { ShowPendingNoteNotice(); });

    // In the chart's right-click menu: "Toon in legenda" and "Welk
    // marifoonkanaal hier?", and "Notitie" with a submenu that
    // PrepareContextMenu() fills each time for the spot and the state.
    m_menu_reveal = AddCanvasContextMenuItem(
        new wxMenuItem(nullptr, wxID_ANY, wxString::FromUTF8("Toon in legenda")), this);
    m_menu_actions.push_back(m_menu_reveal);
    m_menu_vhf = AddCanvasContextMenuItem(
        new wxMenuItem(nullptr, wxID_ANY, wxString::FromUTF8("Welk marifoonkanaal hier?")), this);
    m_menu_actions.push_back(m_menu_vhf);

    // OpenCPN tells a plugin of this API version which submenu was used,
    // not which item in it. So every note action is also an item of its
    // own, hidden, whose id the submenu item carries: OpenCPN calls back
    // with that id too (it looks through all items, hidden ones as well).
    auto action = [this](const char *label) {
        const int id = AddCanvasContextMenuItem(
            new wxMenuItem(nullptr, wxID_ANY, wxString::FromUTF8(label)), this);
        SetCanvasContextMenuItemViz(id, false);
        m_menu_actions.push_back(id);
        return id;
    };
    m_menu_new_note = action("Nieuwe notitie hier");
    m_menu_open_note = action("Notitie openen");
    m_menu_delete_note = action("Notitie verwijderen");
    m_menu_undo_move = action("Notitie terugzetten op de oude plek");
    m_menu_link_place = action("Link naar deze plek in de tekst");
    m_menu_pin_note = action("Notitie hier vastpinnen op de kaart");
    m_menu_new_note_boat = action("Nieuwe notitie bij de boot");
    m_menu_link_boat = action("Positie van de boot in de tekst");
    m_menu_pin_boat = action("Vastpinnen op de positie van de boot");
    m_context_submenu = new wxMenu();
    m_menu_parent = AddCanvasContextMenuItem(
        new wxMenuItem(nullptr, wxID_ANY, "Notitie", wxEmptyString, wxITEM_NORMAL,
                       m_context_submenu),
        this);
    PrepareContextMenu(0);

    m_ocharts.SetCacheFile(
        GetConfigDirectory() + wxFileName::GetPathSeparator() +
        "ocharts-cells.txt");
    m_s57.SetCacheFile(
        GetConfigDirectory() + wxFileName::GetPathSeparator() +
        "s57-cells.txt");
    m_s57.SetCatalogue(&m_chart_symbols);

    // OpenCPN's own configuration file, read for its vector display settings.
    wxString *privateDir = GetpPrivateApplicationDataLocation();
    if (privateDir && !privateDir->IsEmpty()) {
        m_display_settings.SetFile(
            *privateDir + wxFileName::GetPathSeparator() + "opencpn.ini");
    }

    return WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
           WANTS_ONPAINT_VIEWPORT | WANTS_OVERLAY_CALLBACK |
           WANTS_OPENGL_OVERLAY_CALLBACK |
           WANTS_DYNAMIC_OPENGL_OVERLAY_CALLBACK | WANTS_MOUSE_EVENTS |
           WANTS_CURSOR_LATLON | INSTALLS_CONTEXTMENU_ITEMS | WANTS_NMEA_EVENTS;
}

bool legend_pi::DeInit()
{
    m_save_timer.Stop();
    if (m_editing)
        SaveNote();
    SaveCurrentMarkdownScroll();
    SaveWindowState();
    SaveConfig();
    StopMarker();
    m_auto_refresh_timer.Stop();
    m_loading_timer.Stop();
    m_prefetch_timer.Stop();
    m_fill_window_timer.Stop();
    // A cell opened after m_ocharts.Close() below, or what waits for the
    // cells, would reach into charts and a window that are gone.
    m_cell_timer.Stop();
    m_cells_to_open.clear();
    m_after_cells = std::function<void()>();
    m_visibility_timer.Stop();
    m_flash_timer.Stop();
    m_filter_timer.Stop();
    m_chosen_light.Clear();
    m_all_lights.Clear();
    m_vhf_highlight.Clear();
    m_note_notice_timer.Stop();
    m_night_preview = false;
    g_night_preview = false;
    m_tap_clock_timer.Stop();
    m_loading = false;

    // Charts of the o-charts plugin must go before this plugin is unloaded.
    m_ocharts.Close();
    ClearDynamicImages();

    if (m_legend_window) {
        // Delete right away instead of Destroy(): Destroy() postpones the
        // deletion to a later idle moment, when this plugin's code may
        // already be unloaded. Deleting also drops pending CallAfter calls.
        delete m_legend_window;
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
        m_search = nullptr;
        m_edit_button = nullptr;
        m_new_button = nullptr;
        m_rename_button = nullptr;
        m_delete_button = nullptr;
        m_save_button = nullptr;
        m_format_bar = nullptr;
        m_format_icons.clear();
        m_editor = nullptr;
        m_nav_panel = nullptr;
        m_edit_footer = nullptr;
        m_save_status = nullptr;
        m_place_bar = nullptr;
        m_place_text = nullptr;
    }

    if (g_memoryHandler) {
        wxFileSystem::RemoveHandler(g_memoryHandler);
        delete g_memoryHandler;
        g_memoryHandler = nullptr;
        m_pin_images.clear();
    }

    if (m_toolbar_item_id >= 0) {
        RemovePlugInTool(m_toolbar_item_id);
        m_toolbar_item_id = -1;
    }

    m_notice.Close();
    // OpenCPN owns the menu items it was given, and the submenu with its
    // item; only their places in the menu are given up.
    if (m_menu_parent >= 0) {
        RemoveCanvasContextMenuItem(m_menu_parent);
        m_menu_parent = -1;
    }
    for (int id : m_menu_actions)
        RemoveCanvasContextMenuItem(id);
    m_menu_actions.clear();
    m_context_submenu = nullptr;

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
        return wxString::FromUTF8("Kaartlegenda (in beeld)");

    wxFileName filename(path);
    wxString ext = filename.GetExt().Lower();

    // For Markdown, use the first non-empty H1 as the visible title.
    // If the document does not start with an H1, fall back to the filename.
    if (ext == "md" || ext == "markdown") {
        wxString text;
        wxFile markdownFile(path);
        if (markdownFile.IsOpened() && markdownFile.ReadAll(&text, wxConvUTF8)) {
            // The block of settings at the top (a note's place) is no title.
            const wxArrayString lines = wxSplit(SplitFrontMatter(text).body, '\n', '\0');
            for (wxString line : lines) {
                line.Trim(true);
                line.Trim(false);

                if (line.IsEmpty())
                    continue;

                if (line.StartsWith("# ") && !line.StartsWith("##")) {
                    // Without the backslashes Markdown puts before signs
                    // such as _ and *.
                    const wxString heading = line.Mid(2);
                    wxString title;
                    for (size_t i = 0; i < heading.length(); ++i) {
                        if (heading[i] == '\\' && i + 1 < heading.length() &&
                            wxIspunct(heading[i + 1]))
                            ++i;
                        title << heading[i];
                    }
                    title.Trim(true);
                    title.Trim(false);

                    if (!title.IsEmpty())
                        return title;
                }

                // Only the first non-empty line can act as the document title.
                break;
            }
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

    config.Read("ShowHiddenAtScale", &m_show_hidden_at_scale, false);
    config.Read("ShowDistanceFromBoat", &m_show_distance, false);
    config.Read("AheadFanShown", &m_ahead_fan_shown, true);
    config.Read("AheadAngle", &m_ahead_angle, 30);
    if (m_ahead_angle != 15 && m_ahead_angle != 45)
        m_ahead_angle = 30;
    config.Read("ShowLights", &m_show_lights, true);
    config.Read("VhfKeepLit", &m_vhf_keep_lit, false);
    m_vhf_highlight.SetKeepLit(m_vhf_keep_lit);
    config.Read("VhfNotice", &m_vhf_notice_on, true);
    config.Read("NoteNotice", &m_note_notice_on, true);
    wxString language;
    config.Read("Language", &language, "nl");
    m_dutch_names = language != "en";

    wxString collapsed;
    config.Read("CollapsedSections", &collapsed, "");
    m_collapsed_sections.clear();
    for (const wxString &name : wxSplit(collapsed, '\t', '\0')) {
        if (!name.IsEmpty())
            m_collapsed_sections.insert(name);
    }
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

    wxArrayString collapsed;
    for (const wxString &name : m_collapsed_sections)
        collapsed.Add(name);
    config.Write("CollapsedSections", wxJoin(collapsed, '\t', '\0'));
    config.Write("ShowHiddenAtScale", m_show_hidden_at_scale);
    config.Write("ShowDistanceFromBoat", m_show_distance);
    config.Write("AheadFanShown", m_ahead_fan_shown);
    config.Write("AheadAngle", m_ahead_angle);
    config.Write("ShowLights", m_show_lights);
    config.Write("VhfKeepLit", m_vhf_keep_lit);
    config.Write("VhfNotice", m_vhf_notice_on);
    config.Write("NoteNotice", m_note_notice_on);
    config.Write("Language", m_dutch_names ? "nl" : "en");

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

// A loose PNG in the Legend folder becomes a note: the picture moves into
// the images folder and a note of the same name shows it. An image a note
// already shows stays where it is.
void legend_pi::ConvertLooseImages()
{
    const wxString legendDir = GetLegendDirectory();
    wxDir dir(legendDir);
    if (!dir.IsOpened())
        return;

    wxArrayString images;
    wxString notes;
    wxString filename;
    bool found = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
    while (found) {
        const wxFileName file(legendDir, filename);
        const wxString ext = file.GetExt().Lower();
        if (ext == "png") {
            images.Add(filename);
        }
        else if (ext == "md" || ext == "markdown") {
            wxString text;
            wxFile note(file.GetFullPath());
            if (note.IsOpened() && note.ReadAll(&text, wxConvUTF8))
                notes << text << "\n";
        }
        found = dir.GetNext(&filename);
    }

    for (const wxString &name : images) {
        wxString escaped = name;
        escaped.Replace(" ", "%20");
        if (notes.Contains(name) || notes.Contains(escaped))
            continue;

        const wxFileName source(legendDir, name);
        wxFileName image(legendDir + wxFileName::GetPathSeparator() + "images", name);
        wxFileName::Mkdir(image.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        for (int n = 2; image.FileExists(); ++n)
            image.SetName(wxString::Format("%s %d", source.GetName(), n));
        wxFileName note(legendDir, source.GetName() + ".md");
        for (int n = 2; note.FileExists(); ++n)
            note.SetName(wxString::Format("%s %d", source.GetName(), n));

        if (!wxRenameFile(source.GetFullPath(), image.GetFullPath(), false))
            continue;
        wxFileName relative(image);
        relative.MakeRelativeTo(legendDir);
        wxString path = relative.GetFullPath(wxPATH_UNIX);
        if (path.Contains(" "))
            path = "<" + path + ">";
        const wxString text =
            "# " + note.GetName() + "\n\n![" + source.GetName() + "](" + path + ")\n";
        wxTempFile file(note.GetFullPath());
        if (!file.IsOpened() || !file.Write(text, wxConvUTF8) || !file.Commit())
            wxRenameFile(image.GetFullPath(), source.GetFullPath(), false);
    }
}

void legend_pi::ScanLegendFiles()
{
    ConvertLooseImages();
    m_legend_files.Clear();

    wxString legend_dir = GetLegendDirectory();
    wxDir dir(legend_dir);

    if (dir.IsOpened()) {
        wxString filename;
        bool found = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);

        while (found) {
            wxFileName file(filename);
            wxString ext = file.GetExt().Lower();

            if (ext == "md" || ext == "markdown") {
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

    // Notes may have been added, renamed or removed: their places too.
    RefreshNotePlaces();
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
        return "<html>" + LegendBodyTag() + "<p>Markdown-bestand kon niet worden geopend.</p></body></html>";

    wxFileOffset len = file.Length();

    if (len <= 0)
        return "<html>" + LegendBodyTag() + "<p>Leeg Markdown-bestand.</p></body></html>";

    std::string markdown;
    markdown.resize(static_cast<size_t>(len));

    if (file.Read(&markdown[0], static_cast<size_t>(len)) != len)
        return "<html>" + LegendBodyTag() + "<p>Markdown-bestand kon niet volledig worden gelezen.</p></body></html>";

    // The block with the note's place is not shown as text; a line on top
    // says where the note belongs, as a link that takes the chart there.
    const NoteFrontMatter front = SplitFrontMatter(wxString::FromUTF8(markdown.c_str()));
    if (front.block.IsEmpty())
        return RenderMarkdownText(markdown);

    const wxScopedCharBuffer body = front.body.utf8_str();
    wxString html = RenderMarkdownText(std::string(body.data(), body.length()));
    if (front.hasPlace) {
        const LegendPalette palette = GetLegendPalette();
        wxString radius = wxString::Format("%.2f", front.radius);
        while (radius.EndsWith("0"))
            radius.RemoveLast();
        if (radius.EndsWith("."))
            radius.RemoveLast();
        radius.Replace(".", ",");
        // A tinted bar with a pin, the colours of the bar in the editor: it
        // is about the note, not part of its text.
        const bool dim = IsDimScheme();
        const wxColour fill = dim ? palette.stripe : wxColour(250, 238, 218);
        const wxColour ink = dim ? palette.text : wxColour(99, 56, 6);
        if (!g_memoryHandler) {
            g_memoryHandler = new wxMemoryFSHandler;
            wxFileSystem::AddHandler(g_memoryHandler);
        }
        const wxString pin = "legend_pin_" + HtmlColour(ink).Mid(1) + "_" +
                             HtmlColour(fill).Mid(1) + ".png";
        if (m_pin_images.insert(pin).second) {
            const wxBitmapBundle bundle = NoteToolIcon("place", ink);
            const wxImage small = FlattenImage(bundle.GetBitmap(wxSize(16, 16)).ConvertToImage(), fill);
            const wxImage large = FlattenImage(bundle.GetBitmap(wxSize(32, 32)).ConvertToImage(), fill);
            wxMemoryFSHandler::AddFile(pin, small, wxBITMAP_TYPE_PNG);
            wxMemoryFSHandler::AddFile(pin.Left(pin.length() - 4) + "@2x.png", large,
                                       wxBITMAP_TYPE_PNG);
        }
        wxString line;
        line << "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" bgcolor=\""
             << HtmlColour(fill) << "\"><tr><td width=\"20\" valign=\"middle\"><img src=\"memory:"
             << pin << "\"></td><td valign=\"middle\"><font size=\"-1\" color=\"" << HtmlColour(ink)
             << "\">Deze notitie is vastgepind op de kaart: <a href=\"geo:"
             << wxString::FromCDouble(front.lat, 5) << "," << wxString::FromCDouble(front.lon, 5)
             << "\">" << FormatPosition(front.lat, front.lon) << "</a> &middot; straal " << radius
             << " NM</font></td></tr></table><br>";
        const int bodyTag = html.Find("<body");
        const int bodyEnd = bodyTag == wxNOT_FOUND ? wxNOT_FOUND : html.find('>', bodyTag);
        if (bodyEnd != wxNOT_FOUND)
            html = html.Left(bodyEnd + 1) + line + html.Mid(bodyEnd + 1);
    }
    return html;
}

// Markdown text (UTF-8) as the page of the viewer.
// The note's own "# Title" line is shown too, as in the editor, although
// the choice above the page shows the title as well.
wxString legend_pi::RenderMarkdownText(const std::string &markdown) const
{
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
        return "<html>" + LegendBodyTag() + "<p>Markdown kon niet worden verwerkt.</p></body></html>";

    html = ShowTaskLists(html);

    wxString wrapped;
    wrapped << "<html>" << LegendBodyTag()
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
        html << "<html>" << LegendBodyTag()
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

    UpdateSearchVisibility();
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

    displayImage = DimImage(displayImage, SchemeDim());

    m_legend_bitmap->SetBitmap(wxBitmap(displayImage));

    // The image page is a normal panel, not a scrolled window. The bitmap
    // is therefore always constrained to the available view.
    m_image_page->Layout();
}

void legend_pi::ShowLegend(size_t index)
{
    // While a note is edited the window stays on it; a refresh from
    // elsewhere (colours, the folder) only redraws the preview.
    if (m_editing) {
        UpdateNoteButtons();
        return;
    }

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
        UpdateSearchVisibility();
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

    UpdateSearchVisibility();
    UpdateNoteButtons();

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

void legend_pi::OnToolbarToolCallback(int id)
{
    if (id == m_toolbar_item_id)
        ToggleLegendWindow();
}


void legend_pi::SetCurrentViewPort(PlugIn_ViewPort &vp)
{
    m_viewport = vp;
    m_viewport_valid = (vp.pix_width > 0 && vp.pix_height > 0);

    if (m_log_next_viewport && m_viewport_valid &&
        vp.chart_scale != m_logged_chart_scale) {
        m_log_next_viewport = false;
        wxLogMessage(
            "Legend: viewport after zoom: displayed scale 1:%.0f, chart scale "
            "1:%.0f, ppm %.6f, %dx%d px",
            DisplayedScale(vp), static_cast<double>(vp.chart_scale),
            vp.view_scale_ppm, vp.pix_width, vp.pix_height);
    }

    if (m_viewport_valid && !m_prefetch_scheduled) {
        m_prefetch_scheduled = true;
        m_prefetch_timer.StartOnce(5000);
    }

    if (!m_viewport_valid || !IsDynamicLegendVisible())
        return;

    // OpenCPN calls this on every repaint, also while the marker animates.
    // Only a real change of position, zoom, rotation or window size counts.
    const bool changed =
        !m_collected_viewport_valid || ViewportMoved(vp, m_collected_viewport);

    // Wait until the chart has stopped moving before rebuilding the list.
    // The wait restarts only while the chart keeps moving: repaints of a
    // still chart (the marker animation) must not keep postponing it.
    if (changed) {
        if (!m_auto_refresh_timer.IsRunning() ||
            ViewportMoved(vp, m_pending_viewport)) {
            m_pending_viewport = vp;
            m_auto_refresh_timer.StartOnce(400);
        }
        return;
    }

    // The chart object scale factor was changed in the options: redraw the
    // list so its icons follow.
    const double scale = GetOCPNChartScaleFactor_Plugin();
    if (scale > 0.0 &&
        std::fabs(std::min(4.0, std::max(0.5, static_cast<double>(scale))) -
                  m_rendered_icon_scale) > 1e-3 &&
        !m_showing_loading_page)
        RenderDynamicLegend(true);

    // The vector display settings of OpenCPN changed (category, object
    // classes, symbol style): the chart symbols in the list follow.
    const int plibHash = PI_GetPLIBStateHash();
    if (plibHash != m_plib_state_hash) {
        const bool first = m_plib_state_hash == 0;
        m_plib_state_hash = plibHash;
        if (!first && !m_showing_loading_page) {
            m_display_settings.Refresh(true);
            RefreshDynamicLegendKeepingState();
            return;
        }
    }

    // A repaint without movement may come from hiding or showing a layer.
    // Not restarted on every repaint, so the marker animation cannot keep
    // postponing the check.
    if (!m_visibility_timer.IsRunning())
        m_visibility_timer.StartOnce(700);
}

void legend_pi::CheckVisibilityChanges()
{
    // OpenCPN wrote new display settings to its configuration file.
    if (IsDynamicLegendVisible() && !m_loading && !m_showing_loading_page &&
        m_display_settings.Refresh()) {
        RefreshDynamicLegendKeepingState();
        return;
    }

    if (!IsDynamicLegendVisible() || m_loading || !m_viewport_valid ||
        m_waypoint_cache.empty() ||
        m_waypoint_cache.size() != m_waypoint_guids.GetCount())
        return;

    const PlugIn_ViewPort &vp = m_viewport;

    auto lonInView = [&vp](double lon) {
        for (double offset : {0.0, 360.0, -360.0}) {
            const double l = lon + offset;
            if (l >= vp.lon_min && l <= vp.lon_max)
                return true;
        }
        return false;
    };

    // Waypoints of one layer are next to each other in OpenCPN's list.
    // Probe the first waypoint in view of every unbroken run, visible or
    // not: hiding or showing a layer changes all of its waypoints at once.
    std::vector<size_t> probes;
    size_t previous = static_cast<size_t>(-1);

    for (size_t i = 0; i < m_waypoint_cache.size(); ++i) {
        const CachedWaypoint &wp = m_waypoint_cache[i];

        if (wp.lat < vp.lat_min || wp.lat > vp.lat_max || !lonInView(wp.lon))
            continue;

        if (previous == static_cast<size_t>(-1) || i != previous + 1)
            probes.push_back(i);

        previous = i;
    }

    // Every probe asks OpenCPN for one waypoint; keep the total small.
    const size_t kMaxProbes = 200;
    if (probes.size() > kMaxProbes) {
        std::vector<size_t> spread;
        for (size_t k = 0; k < kMaxProbes; ++k)
            spread.push_back(probes[k * probes.size() / kMaxProbes]);
        probes.swap(spread);
    }

    for (size_t index : probes) {
        std::unique_ptr<PlugIn_Waypoint_Ex> wp =
            GetWaypointEx_Plugin(m_waypoint_guids[index]);

        const bool visible = wp && wp->IsVisible;

        if (visible != m_waypoint_cache[index].visible) {
            wxLogMessage(
                "Legend: waypoint visibility changed, reloading waypoints");

            // The list stays in place while loading; its end refreshes it.
            StartWaypointLoading(true);
            return;
        }
    }
}

bool legend_pi::ViewportMoved(const PlugIn_ViewPort &a, const PlugIn_ViewPort &b)
{
    return std::fabs(a.clat - b.clat) > 1e-7 ||
           std::fabs(a.clon - b.clon) > 1e-7 ||
           std::fabs(a.view_scale_ppm - b.view_scale_ppm) >
               1e-6 * b.view_scale_ppm ||
           std::fabs(a.rotation - b.rotation) > 1e-4 ||
           a.pix_width != b.pix_width || a.pix_height != b.pix_height;
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

    // When new waypoints still have to be fetched, the end of the loading
    // refreshes the list.
    if (!StartWaypointLoading(false))
        return;

    RefreshDynamicLegendKeepingState();
}
