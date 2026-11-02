// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// legend_pi, the window: building the legend window, its clicks, keys
// and colours, the settings menu, headings, search and reordering.

#include "legend_pi.h"

#include "geo.h"
#include "legend_html_window.h"
#include "legend_shared.h"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/gdicmn.h>
#include <wx/html/winpars.h>
#include <wx/file.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/statline.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/textfile.h>
#include <wx/wrapsizer.h>
#include <wx/tokenzr.h>
#include <wx/translation.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <functional>

#ifdef __WXOSX__
#include <objc/message.h>
#include <objc/runtime.h>

// AppKit constant (macOS 10.14 and later); weakly linked so the plugin
// still loads where it is missing.
extern "C" void *const NSAppearanceNameDarkAqua __attribute__((weak_import));
#endif

namespace {

// Text prepared for searching: lower case and without accents, so that
// "waterskien" finds "Waterskiën" and capitals do not matter.
wxString FoldForSearch(const wxString &text)
{
    const wxString lower = text.Lower();
    wxString folded;
    folded.reserve(lower.length());

    for (const wxUniChar ch : lower) {
        switch (ch.GetValue()) {
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
            folded << 'a';
            break;
        case 0xE6:
            folded << "ae";
            break;
        case 0xE7:
            folded << 'c';
            break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            folded << 'e';
            break;
        case 0xEC: case 0xED: case 0xEE: case 0xEF:
            folded << 'i';
            break;
        case 0xF1:
            folded << 'n';
            break;
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8:
            folded << 'o';
            break;
        case 0x153:
            folded << "oe";
            break;
        case 0xF9: case 0xFA: case 0xFB: case 0xFC:
            folded << 'u';
            break;
        case 0xFD: case 0xFF:
            folded << 'y';
            break;
        case 0xDF:
            folded << "ss";
            break;
        default:
            folded << ch;
            break;
        }
    }
    return folded;
}

// Native controls on macOS (buttons, choice) ignore colours set by the
// program; they follow the appearance of their window. In dusk and night
// the legend window gets the dark appearance; by day it follows the system
// again (nil appearance).
void SetWindowDarkAppearance(wxTopLevelWindow *window, bool dark)
{
#ifdef __WXOSX__
    if (!window)
        return;

    void *nsWindow = window->GetWXWindow();
    if (!nsWindow)
        return;

    id appearance = nil;
    if (dark && &NSAppearanceNameDarkAqua != nullptr) {
        appearance = reinterpret_cast<id (*)(id, SEL, id)>(objc_msgSend)(
            reinterpret_cast<id>(objc_getClass("NSAppearance")),
            sel_registerName("appearanceNamed:"),
            reinterpret_cast<id>(NSAppearanceNameDarkAqua));
    }

    reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
        reinterpret_cast<id>(nsWindow),
        sel_registerName("setAppearance:"),
        appearance);
#else
    (void)window;
    (void)dark;
#endif
}

// The window's colour by day. On Android a window without a colour of its
// own is not painted at all: the chart shows through, and buttons that were
// hidden stay visible.
wxColour DayBackground()
{
#ifdef __ANDROID__
    return wxColour(240, 240, 240);
#else
    return wxNullColour;
#endif
}

}

// The legend window: the header with the choice of item and the note
// buttons, the bars for editing notes, the search field, the pages
// (image, legend or note, note editor) and the navigation at the foot.
void legend_pi::CreateLegendWindow()
{
    if (m_legend_window)
        return;

    m_legend_window = new wxFrame(
        GetOCPNCanvasWindow(),
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxSize(560, 460),
        wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);

    auto *panel = new wxPanel(m_legend_window);
    auto *mainSizer = new wxBoxSizer(wxVERTICAL);

#ifdef __ANDROID__
    mainSizer->Add(CreateTitleBar(panel), 0, wxEXPAND);
#endif

    mainSizer->Add(
        CreateHeaderRow(panel),
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    mainSizer->Add(
        CreateFormatBar(panel),
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    mainSizer->Add(
        CreatePlaceBar(panel),
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    mainSizer->Add(
        CreateSearchField(panel),
        0,
        wxEXPAND | wxTOP | wxLEFT | wxRIGHT,
        10);

    m_content_book = new wxSimplebook(
        panel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize);

    CreateImagePage();
    CreateMarkdownView(panel->GetFont());
    CreateNoteEditor();

    m_content_book->AddPage(m_image_page, "Afbeelding");
    m_content_book->AddPage(m_markdown_view, "Markdown");
    m_content_book->AddPage(m_editor, "Notitie");

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

    mainSizer->Add(
        CreateNavigation(panel),
        0,
        wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    mainSizer->Add(
        CreateEditFooter(panel),
        0,
        wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
        10);

    m_save_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { SaveNote(); });

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
#ifdef __ANDROID__
    CreateResizeGrip(panel);
#endif
    SetWindowTexts();

    RestoreWindowState();
    BindWindowEvents();
}

// The header row: the choice of item, with the buttons for notes.
wxSizer *legend_pi::CreateHeaderRow(wxPanel *panel)
{
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

    m_legend_choice->Bind(
        wxEVT_CHOICE,
        &legend_pi::OnLegendSelected,
        this);

#ifdef __ANDROID__
    // The buttons are big enough for a finger, so they get a row of their
    // own under the choice of item; beside it they pushed it out.
    auto *header = new wxBoxSizer(wxVERTICAL);
    header->Add(m_header_book, 0, wxEXPAND);
    auto *headerRow = new wxBoxSizer(wxHORIZONTAL);
    header->Add(headerRow, 0, wxALIGN_RIGHT | wxTOP, 4);
#else
    auto *headerRow = new wxBoxSizer(wxHORIZONTAL);
    headerRow->Add(m_header_book, 1, wxALIGN_CENTER_VERTICAL);
#endif

    AddNoteButtons(panel, headerRow);

#ifdef __ANDROID__
    return header;
#else
    return headerRow;
#endif
}

// Search field of the dynamic legend; hidden for other legend items. The
// list follows the text after a short pause in typing, Enter applies it at
// once.
wxWindow *legend_pi::CreateSearchField(wxPanel *panel)
{
#ifdef __ANDROID__
    m_search = new wxTextCtrl(
#else
    m_search = new wxSearchCtrl(
#endif
        panel,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_PROCESS_ENTER);
#ifndef __ANDROID__
    m_search->ShowCancelButton(true);
#endif
    m_search->Hide();

    m_search->Bind(
        wxEVT_TEXT,
        [this](wxCommandEvent &event) {
            event.Skip();
            m_filter_timer.StartOnce(250);
        });

    m_search->Bind(
        wxEVT_TEXT_ENTER,
        [this](wxCommandEvent &) { SetFilter(m_search->GetValue()); });

#ifndef __ANDROID__
    m_search->Bind(
        wxEVT_SEARCHCTRL_SEARCH_BTN,
        [this](wxCommandEvent &) { SetFilter(m_search->GetValue()); });

    m_search->Bind(
        wxEVT_SEARCHCTRL_CANCEL_BTN,
        [this](wxCommandEvent &) { SetFilter(wxEmptyString); });
#endif

    return m_search;
}

// The page of a PNG legend: the image, scaled to the window.
void legend_pi::CreateImagePage()
{
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
}

// The page of Markdown notes and of the dynamic legend, in the font of the
// surrounding OpenCPN user interface.
void legend_pi::CreateMarkdownView(const wxFont &uiFont)
{
    TapClockModule clockModule;
    wxHtmlWinParser::AddModule(&clockModule);
    m_markdown_view = new LegendHtmlWindow(
        m_content_book,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxHW_SCROLLBAR_AUTO);
    wxHtmlWinParser::RemoveModule(&clockModule);

    // The rhythms of lights are images; in a narrow window they are drawn
    // narrower, so a row never needs more width than the window has.
    m_markdown_view->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent &event) {
            event.Skip();
            if (IsDynamicLegendVisible() && RhythmWidth() != m_rhythm_width)
                m_markdown_view->CallAfter([this]() { RenderDynamicLegend(true); });
        });

    // Give Markdown some breathing room without using extra layout rows.
    m_markdown_view->SetBorders(12);

    // Use the surrounding OpenCPN UI font as the basis for Markdown.
    // The seven values correspond to HTML font sizes 1 through 7.
    const int baseFontSize =
        wxMax(11, uiFont.GetPointSize());

    int htmlFontSizes[7] = {
        wxMax(9, baseFontSize - 2),
        wxMax(10, baseFontSize - 1),
        baseFontSize,
        baseFontSize + 2,
        baseFontSize + 4,
        baseFontSize + 7,
        baseFontSize + 10
    };

    wxString normalFace = uiFont.GetFaceName();

    wxFont fixedFont(
        baseFontSize,
        wxFONTFAMILY_TELETYPE,
        wxFONTSTYLE_NORMAL,
        wxFONTWEIGHT_NORMAL);

    m_markdown_view->SetFonts(
        normalFace,
        fixedFont.GetFaceName(),
        htmlFontSizes);

    ApplyViewColours();

    m_markdown_view->Bind(wxEVT_HTML_LINK_CLICKED, &legend_pi::OnLegendLink, this);
    BindLegendMouse();
}

// Links in the page: the commands of the dynamic legend, places in notes,
// and normal external links, which open in the system browser.
void legend_pi::OnLegendLink(wxHtmlLinkEvent &event)
{
    wxString href = event.GetLinkInfo().GetHref();

    if (href.StartsWith("legend-reorder-done:")) {
        m_markdown_view->CallAfter([this]() { SetReorderMode(false); });
        return;
    }

    if (href.StartsWith("legend-identify-")) {
        const wxString what = href.Mid(16).BeforeFirst(':');
        m_markdown_view->CallAfter([this, what]() { RunIdentifyCommand(what); });
        return;
    }

    if (href.StartsWith("legend-settings:")) {
        m_markdown_view->CallAfter([this]() { ShowSettingsMenu(); });
        return;
    }

    // The settings, also reachable as links: "legend-<command>:".
    for (const char *command : {"reorder", "collapse-all", "expand-all",
                                "lights", "all-lights", "hidden", "distance"}) {
        if (href == wxString("legend-") + command + ":") {
            const wxString name(command);
            m_markdown_view->CallAfter([this, name]() { RunLegendCommand(name); });
            return;
        }
    }

    if (href.StartsWith("legend-refresh:")) {
        // The page is not replaced inside its own link click; the empty list
        // shows before the work starts.
        m_markdown_view->CallAfter([this]() {
            StartRefreshSpin();
            m_markdown_view->CallAfter([this]() {
                m_ocharts.Rescan();
                m_s57.Rescan();
                m_ahead_area_valid = false;
                m_ahead_cells_tried.clear();
                m_vhf_loaded = false;
                LoadChartNames();
                ShowDynamicLegend(true);
            });
        });
        return;
    }

    // A place in a note: the chart goes there and marks it.
    if (href.StartsWith("geo:")) {
        double lat = 0.0;
        double lon = 0.0;
        if (ParseGeoUri(href, &lat, &lon))
            m_markdown_view->CallAfter([this, lat, lon]() { GoToPlace(lat, lon); });
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
}

// What is at a point of the dynamic legend: the cells carry ids by kind.
legend_pi::Hit legend_pi::HitAt(const wxPoint &pos) const
{
    Hit hit;
    if (m_current_legend_path != kDynamicGpxId)
        return hit;

    auto *legendView = static_cast<LegendHtmlWindow *>(m_markdown_view);
    const std::pair<HitKind, int> candidates[] = {
        {HitKind::MoveUp, legendView->IdNumberAt(pos, kLegendMoveUpIdPrefix)},
        {HitKind::MoveDown,
         legendView->IdNumberAt(pos, kLegendMoveDownIdPrefix)},
        {HitKind::MoveBlockUp,
         legendView->IdNumberAt(pos, kLegendMoveBlockUpIdPrefix)},
        {HitKind::MoveBlockDown,
         legendView->IdNumberAt(pos, kLegendMoveBlockDownIdPrefix)},
        {HitKind::Tap, legendView->IdNumberAt(pos, kLegendTapIdPrefix)},
        {HitKind::Match, legendView->IdNumberAt(pos, kLegendMatchIdPrefix)},
        {HitKind::Button, legendView->IdNumberAt(pos, kLegendButtonIdPrefix)},
        {HitKind::Tip, legendView->IdNumberAt(pos, kLegendTipIdPrefix)},
        {HitKind::AheadVhf, legendView->IdNumberAt(pos, kLegendAheadVhfIdPrefix)},
        {HitKind::AheadNote, legendView->IdNumberAt(pos, kLegendAheadNoteIdPrefix)},
        {HitKind::Note, legendView->IdNumberAt(pos, kLegendNoteIdPrefix)},
        {HitKind::VhfArea, legendView->IdNumberAt(pos, kLegendVhfAreaIdPrefix)},
        {HitKind::Ahead, legendView->IdNumberAt(pos, kLegendAheadIdPrefix)},
        {HitKind::All, legendView->IdNumberAt(pos, kLegendAllIdPrefix)},
        {HitKind::Row, legendView->RowAt(pos)},
        {HitKind::Section, legendView->SectionAt(pos)}};

    for (const auto &candidate : candidates) {
        if (candidate.second >= 0) {
            hit.kind = candidate.first;
            hit.index = candidate.second;
            break;
        }
    }
    return hit;
}

// Whole rows of the dynamic legend are clickable, not only the text. A
// press and release on the same row counts as a click; dragging to select
// text does not. Headings work the same way and fold their group in or
// out; in the order edit mode the arrow buttons move a heading.
void legend_pi::BindLegendMouse()
{
    auto *legendView = static_cast<LegendHtmlWindow *>(m_markdown_view);

    m_markdown_view->Bind(
        wxEVT_LEFT_DOWN,
        [this](wxMouseEvent &event) {
            m_pressed_hit = HitAt(event.GetPosition());
            // The pad of "Licht herkennen": the light shines while held.
            if (m_pressed_hit.kind == HitKind::Tap) {
                TapDown();
                m_pressed_hit = Hit();
                return;
            }
            event.Skip();
        });

    m_markdown_view->Bind(
        wxEVT_LEFT_UP,
        [this](wxMouseEvent &event) {
            event.Skip();

            if (m_tap_down) {
                TapUp();
                return;
            }

            const Hit hit = m_pressed_hit;
            m_pressed_hit = Hit();

            if (hit.kind == HitKind::None || !(HitAt(event.GetPosition()) == hit))
                return;

            m_markdown_view->CallAfter([this, hit]() { OnLegendClick(hit); });
        });

    // Tooltips explain what a click does; they change only when the mouse
    // moves to another kind of target, so they do not flicker.
    m_markdown_view->Bind(
        wxEVT_MOTION,
        [this, legendView](wxMouseEvent &event) {
            const Hit hit = HitAt(event.GetPosition());
            legendView->SetRowCursor(hit.kind != HitKind::None);

            const wxString tip = LegendTip(hit);
            if (tip != m_shown_tip) {
                m_shown_tip = tip;
                if (tip.IsEmpty())
                    m_markdown_view->UnsetToolTip();
                else
                    m_markdown_view->SetToolTip(tip);
            }

            event.Skip();
        });

    m_markdown_view->Bind(
        wxEVT_LEAVE_WINDOW,
        [this, legendView](wxMouseEvent &event) {
            // A press on the pad that ends outside the window ends there.
            if (m_tap_down)
                TapUp();
            legendView->SetRowCursor(false);
            if (!m_shown_tip.IsEmpty()) {
                m_shown_tip.Clear();
                m_markdown_view->UnsetToolTip();
            }
            event.Skip();
        });
}

// A click on something in the dynamic legend.
void legend_pi::OnLegendClick(const Hit &hit)
{
    switch (hit.kind) {
    case HitKind::Row:
        GoToDynamicGroup(static_cast<size_t>(hit.index));
        break;
    case HitKind::All:
        MarkAllOfDynamicGroup(static_cast<size_t>(hit.index));
        break;
    case HitKind::Section:
        ToggleSection(static_cast<size_t>(hit.index));
        break;
    case HitKind::MoveUp:
        MoveSection(hit.index, -1);
        break;
    case HitKind::MoveDown:
        MoveSection(hit.index, +1);
        break;
    case HitKind::MoveBlockUp:
        MoveTopBlock(hit.index, -1);
        break;
    case HitKind::MoveBlockDown:
        MoveTopBlock(hit.index, +1);
        break;
    case HitKind::Ahead:
        ShowAheadItem(static_cast<size_t>(hit.index));
        break;
    case HitKind::AheadNote:
        if (static_cast<size_t>(hit.index) < m_ahead_notes.size())
            OpenNoteOrShowSign(m_ahead_notes[hit.index].path);
        break;
    case HitKind::Note:
        if (static_cast<size_t>(hit.index) < m_note_rows.size() &&
            m_note_rows[hit.index] < m_note_places.size())
            OpenNoteOrShowSign(m_note_places[m_note_rows[hit.index]].path);
        break;
    case HitKind::AheadVhf:
        if (static_cast<size_t>(hit.index) < m_ahead_vhf.size()) {
            const AheadVhf &next = m_ahead_vhf[hit.index];
            ShowVhfAreas(m_vhf_areas.WithPartsAt(next.lat, next.lon));
        }
        break;
    case HitKind::VhfArea:
        if (static_cast<size_t>(hit.index) < m_vhf_rows.size())
            ShowVhfAreas(m_vhf_areas.PartsOf(m_vhf_rows[hit.index].description));
        break;
    case HitKind::Match:
        if (static_cast<size_t>(hit.index) < m_identify_matches.size())
            GoToDynamicGroup(m_identify_matches[hit.index]);
        break;
    case HitKind::Button: {
        static const char *const colours[] = {"any", "W", "R", "G", "Y"};
        if (hit.index == 0)
            RunIdentifyCommand("reset");
        else if (hit.index == 1)
            RunIdentifyCommand("stop");
        else if (hit.index >= 10 && hit.index < 15)
            RunIdentifyCommand(wxString("colour-") + colours[hit.index - 10]);
        break;
    }
    case HitKind::Tap:
    case HitKind::Tip: // the link inside does the work
    case HitKind::None:
        break;
    }
}

// What a click on something in the dynamic legend does, as a tooltip.
wxString legend_pi::LegendTip(const Hit &hit) const
{
    wxString tip;
    switch (hit.kind) {
    case HitKind::All:
        tip = _("Point out all instances in view at once");
        if (hit.index >= 0 &&
            static_cast<size_t>(hit.index) < m_dynamic_groups.size() &&
            m_dynamic_groups[hit.index].lightCharacter)
            tip = _("Make all lights in view with this characteristic shine, in their rhythm");
        break;
    case HitKind::Row:
        tip = _("To the nearest instance; another click goes to the next one");
        // The symbol name is what symbolen.txt needs for a chart
        // symbol; the list itself shows only the description.
        if (hit.index >= 0 &&
            static_cast<size_t>(hit.index) < m_dynamic_groups.size()) {
            const DynamicGroup &group = m_dynamic_groups[hit.index];
            if (group.hiddenAtScale) {
                tip = _("Not drawn at this scale");
                if (group.visibleFrom > 0)
                    tip << _(", visible from ")
                        << FormatScaleShort(group.visibleFrom, m_dutch_names);
                tip << _("; a click zooms in until the nearest instance is visible");
            }
            if (group.chartSymbol)
                tip = _("Chart symbol ") + group.symbol + ". " + tip;
            if (group.lightCharacter)
                tip = _("The nearest light with this characteristic shines on "
                         "the chart in its rhythm; another click goes to the "
                         "next one. Period and groups come from the chart; "
                         "the length of a flash or eclipse is a common value");
        }
        break;
    case HitKind::Section:
        tip = hit.index >= 0 &&
                      static_cast<size_t>(hit.index) <
                          m_rendered_headings.size() &&
                      m_collapsed_sections.count(
                          m_rendered_headings[hit.index]) > 0
                  ? _("Unfold group")
                  : _("Fold group");
        break;
    case HitKind::MoveUp:
    case HitKind::MoveBlockUp:
        tip = _("Heading up");
        break;
    case HitKind::MoveDown:
    case HitKind::MoveBlockDown:
        tip = _("Heading down");
        break;
    case HitKind::Ahead:
        tip = _("Point out on the chart");
        break;
    case HitKind::AheadVhf:
    case HitKind::VhfArea:
        tip = _("Highlight the VHF area on the chart");
        break;
    case HitKind::AheadNote:
    case HitKind::Note:
        tip = _("Open the note");
        break;
    case HitKind::Tap:
        tip = _("Hold down while the light is on (or the space bar)");
        break;
    case HitKind::Match:
        tip = _("Make this light shine on the chart");
        break;
    case HitKind::Button:
        tip = hit.index == 0   ? _("Discard the tapped rhythm and start again")
              : hit.index == 1 ? _("Close Identify a light; the menu opens it again")
                               : _("Compare only lights of this colour");
        break;
    case HitKind::Tip:
        tip = hit.index == 0 ? _("Settings: what the legend shows")
                             : _("Refresh: read charts and layers again");
        break;
    case HitKind::None:
        break;
    }
    return tip;
}

// Vorige / Volgende at the foot of the window.
wxWindow *legend_pi::CreateNavigation(wxPanel *panel)
{
    auto *buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    m_nav_panel = new wxPanel(panel);

    m_previous_button = new wxButton(m_nav_panel, wxID_ANY, wxEmptyString);
    m_next_button = new wxButton(m_nav_panel, wxID_ANY, wxEmptyString);

    m_previous_button->SetMinSize(wxSize(110, 36));
    m_next_button->SetMinSize(wxSize(110, 36));

    buttonSizer->Add(m_previous_button, 0, wxRIGHT, 8);
    buttonSizer->Add(m_next_button, 0, wxLEFT, 8);
    m_nav_panel->SetSizer(buttonSizer);
    return m_nav_panel;
}

#ifdef __ANDROID__
// The bar at the top: the name of the window and a close button. Dragging
// the bar moves the window.
wxWindow *legend_pi::CreateTitleBar(wxPanel *panel)
{
    m_title_bar = new wxPanel(panel);
    auto *barSizer = new wxBoxSizer(wxHORIZONTAL);

    m_title_text = new wxStaticText(m_title_bar, wxID_ANY, wxEmptyString);
    wxFont titleFont = m_title_text->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_title_text->SetFont(titleFont);

    const int barHeight = 2 * m_title_bar->GetCharHeight() + 8;
    m_close_button = new wxButton(m_title_bar, wxID_ANY, wxString::FromUTF8("\u00d7"),
                                  wxDefaultPosition, wxSize(barHeight, barHeight),
                                  wxBU_EXACTFIT);
    m_close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { m_legend_window->Close(); });

    barSizer->Add(m_title_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    barSizer->Add(m_close_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_title_bar->SetSizer(barSizer);
    m_title_bar->SetMinSize(wxSize(-1, barHeight + 4));

    BindWindowDrag(m_title_bar, false);
    BindWindowDrag(m_title_text, false);
    return m_title_bar;
}

// Three short diagonal lines in the bottom right corner; dragging them
// resizes the window. The grip lies over the panel, outside the sizers.
void legend_pi::CreateResizeGrip(wxPanel *panel)
{
    const int side = 2 * panel->GetCharHeight();
    m_resize_grip = new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxSize(side, side));
    m_resize_grip->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxPaintDC dc(m_resize_grip);
        const wxSize size = m_resize_grip->GetClientSize();
        dc.SetPen(wxPen(GetLegendPalette().muted, 2));
        for (int i = 1; i <= 3; ++i) {
            const int offset = i * size.x / 4;
            dc.DrawLine(size.x - 3, size.y - 3 - offset, size.x - 3 - offset, size.y - 3);
        }
    });

    auto place = [this, panel]() {
        const wxSize panelSize = panel->GetClientSize();
        const wxSize gripSize = m_resize_grip->GetSize();
        m_resize_grip->Move(panelSize.x - gripSize.x, panelSize.y - gripSize.y);
        m_resize_grip->Raise();
    };
    panel->Bind(wxEVT_SIZE, [place](wxSizeEvent &event) {
        event.Skip();
        place();
    });
    place();

    BindWindowDrag(m_resize_grip, true);
}

// Moving or resizing the window by dragging a handle. The window stays
// on the screen with enough of it to drag it back. It follows the finger
// at most ten times a second: each change lays out the list again and
// redraws the chart, and following every move piled up work for minutes.
void legend_pi::BindWindowDrag(wxWindow *handle, bool resize)
{
    if (!m_drag_timer.GetOwner()) {
        m_drag_timer.SetOwner(m_legend_window);
        m_legend_window->Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
            if (m_legend_window->GetRect() != m_drag_target)
                m_legend_window->SetSize(m_drag_target);
        }, m_drag_timer.GetId());
    }

    handle->Bind(wxEVT_LEFT_DOWN, [this, handle](wxMouseEvent &event) {
        m_drag_start = handle->ClientToScreen(event.GetPosition());
        m_drag_rect = m_legend_window->GetRect();
        m_drag_handle = handle;
    });

    handle->Bind(wxEVT_MOTION, [this, handle, resize](wxMouseEvent &event) {
        // The finger keeps sending its moves to the handle it went down on;
        // CaptureMouse does nothing on Android.
        if (m_drag_handle != handle || !event.LeftIsDown())
            return;
        const wxPoint delta = handle->ClientToScreen(event.GetPosition()) - m_drag_start;
        const wxRect area = wxGetClientDisplayRect();
        const int margin = m_title_bar ? m_title_bar->GetSize().y : 40;
        wxRect rect = m_drag_rect;

        if (resize) {
            const wxSize minimum = m_legend_window->GetMinSize();
            rect.width = std::max(minimum.x, std::min(rect.width + delta.x,
                                                      area.GetRight() + 1 - rect.x));
            rect.height = std::max(minimum.y, std::min(rect.height + delta.y,
                                                       area.GetBottom() + 1 - rect.y));
        } else {
            rect.x = std::max(area.x - rect.width + 2 * margin,
                              std::min(rect.x + delta.x, area.GetRight() - 2 * margin));
            rect.y = std::max(area.y, std::min(rect.y + delta.y, area.GetBottom() - margin));
        }
        m_drag_target = rect;
        if (!m_drag_timer.IsRunning())
            m_drag_timer.StartOnce(100);
    });

    handle->Bind(wxEVT_LEFT_UP, [this, handle](wxMouseEvent &) {
        if (m_drag_handle != handle)
            return;
        m_drag_handle = nullptr;
        if (m_drag_timer.IsRunning()) {
            m_drag_timer.Stop();
            m_legend_window->SetSize(m_drag_target);
        }
        SaveWindowState();
    });
}
#endif

// The tooltip of a button in the header or the format bar, by its icon.
wxString legend_pi::ButtonTip(const wxString &icon) const
{
    if (icon == "edit")
        return _("Edit this note");
    if (icon == "rename")
        return _("Give this note another name");
    if (icon == "trash")
        return wxString::Format(_("Delete this note (into the folder %s)"), _("Deleted"));
    if (icon == "new")
        return _("New note");
    if (icon == "heading")
        return _("Heading level: heading 1 to 6, or plain text");
    if (icon == "bold")
        return _("Bold (Cmd+B)");
    if (icon == "italic")
        return _("Italic (Cmd+I)");
    if (icon == "bullets")
        return _("Bulleted list on or off");
    if (icon == "numbers")
        return _("Numbered list on or off");
    if (icon == "tasks")
        return _("Checklist on or off; click a box to tick it");
    if (icon == "link")
        return _("Make a link of the selected text");
    if (icon == "image")
        return _("An image from the Legend folder or elsewhere");
    if (icon == "place")
        return _("A place on the chart: as a link in the text, or pin the whole note to "
                  "the chart. For a place other than the centre: right-click the chart");
    return wxEmptyString;
}

void legend_pi::SetWindowTexts()
{
    if (!m_legend_window)
        return;
    m_legend_window->SetTitle(_("Legend"));
#ifdef __ANDROID__
    m_title_text->SetLabel(_("Legend"));
    m_close_button->SetToolTip(_("Close"));
#endif
    for (const auto &button : m_format_icons)
        button.first->SetToolTip(ButtonTip(button.second));
#ifdef __ANDROID__
    m_move_button->SetToolTip(_("Move this note: tap the chart where it should go"));
#endif
    m_save_button->SetLabel(_("Save"));
    m_save_button->SetToolTip(_("Save and finish editing (Esc)"));
    m_radius_button->SetLabel(_("Radius"));
    m_radius_button->SetToolTip(_("Within what distance you get a notice under way"));
    m_loose_button->SetLabel(_("Unpin"));
    m_loose_button->SetToolTip(_("No longer pin the note to the chart"));
#ifdef __ANDROID__
    m_search->SetHint(_("Search the legend"));
#else
    m_search->SetDescriptiveText(_("Search the legend"));
#endif
    m_search->SetToolTip(_("Shows only the symbols whose name, heading or icon name "
                            "contains all typed words (Cmd+F)"));
    m_previous_button->SetLabel(_("\u2039  Previous"));
    m_next_button->SetLabel(_("Next  \u203a"));
    m_previous_button->SetToolTip(_("Show the previous legend or note"));
    m_next_button->SetToolTip(_("Show the next legend or note"));
    if (!m_editing)
        m_save_status->SetLabel(_("Saved"));
    if (wxWindow *panel = m_save_button->GetParent())
        panel->Layout();
}

// Closing (the window only hides), light and dark mode, and the keys.
void legend_pi::BindWindowEvents()
{
    m_legend_window->Bind(
        wxEVT_CLOSE_WINDOW,
        [this](wxCloseEvent &event) {
            if (m_editing)
                StopEditing();
            SaveCurrentMarkdownScroll();
            SaveWindowState();
            SaveConfig();
            m_legend_window->Hide();
            event.Veto();
        });

    // Switching macOS between light and dark: repaint with the new palette.
    m_legend_window->Bind(
        wxEVT_SYS_COLOUR_CHANGED,
        [this](wxSysColourChangedEvent &event) {
            event.Skip();
            m_legend_window->CallAfter([this]() {
                ApplyViewColours();

                if (m_legend_window && m_legend_window->IsShown() &&
                    !m_legend_files.IsEmpty())
                    ShowLegend(m_legend_index);
            });
        });

    // Letting go of the space bar ends a tap; key-up events go to the window
    // with the focus, so every window of the legend listens.
    auto spaceUp = [this](wxKeyEvent &event) {
        if (m_tap_down && event.GetKeyCode() == WXK_SPACE) {
            TapUp();
            return;
        }
        event.Skip();
    };
    m_markdown_view->Bind(wxEVT_KEY_UP, spaceUp);
    m_legend_choice->Bind(wxEVT_KEY_UP, spaceUp);
    m_legend_window->Bind(wxEVT_KEY_UP, spaceUp);

    m_legend_window->Bind(wxEVT_CHAR_HOOK, &legend_pi::OnLegendKey, this);
}

// The keys of the legend window: Escape closes it, the arrows go to the
// previous or next item, Cmd+F to the search field, and the space bar is
// the pad of "Licht herkennen".
void legend_pi::OnLegendKey(wxKeyEvent &event)
{
    const bool plainKey =
        !event.ControlDown() &&
        !event.AltDown() &&
        !event.MetaDown();

    // While a note is edited the keys are for the text: Escape and
    // Cmd+B are the only ones the legend takes.
    if (m_editing) {
        if (event.GetKeyCode() == WXK_ESCAPE && plainKey) {
            StopEditing();
            return;
        }
        if (event.GetModifiers() == wxMOD_CMD && m_editor) {
            if (event.GetKeyCode() == 'B') {
                m_editor->ToggleBold();
                return;
            }
            if (event.GetKeyCode() == 'I') {
                m_editor->ToggleItalic();
                return;
            }
        }
        event.Skip();
        return;
    }

    // The space bar is the pad of "Licht herkennen", outside the
    // search field.
    if (m_identify_on && event.GetKeyCode() == WXK_SPACE && plainKey &&
        !(m_search && wxWindow::FindFocus() == m_search)) {
        if (!m_tap_down)
            TapDown();
        return;
    }

    // Cmd+F (Ctrl+F elsewhere) goes to the search field.
    if (event.GetKeyCode() == 'F' && event.GetModifiers() == wxMOD_CMD &&
        m_search && m_search->IsShown()) {
        m_search->SetFocus();
        m_search->SelectAll();
        return;
    }

    // In the search field the arrows move the cursor, and Escape
    // first empties the field before it closes the window.
    wxWindow *focus = wxWindow::FindFocus();
    const bool inSearch =
        m_search && focus &&
        (focus == m_search || focus->GetParent() == m_search);

    if (inSearch && plainKey &&
        (event.GetKeyCode() == WXK_LEFT || event.GetKeyCode() == WXK_RIGHT)) {
        event.Skip();
        return;
    }

    if (inSearch && plainKey && event.GetKeyCode() == WXK_ESCAPE &&
        !m_search->GetValue().IsEmpty()) {
        SetFilter(wxEmptyString);
        return;
    }

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
}

void legend_pi::ApplyViewColours()
{
    if (!m_markdown_view)
        return;

    const LegendPalette palette = GetLegendPalette();
    const bool dim = IsDimScheme();

    // The window around the list: panels and labels take the colours of
    // the list in dusk and night and get their normal colours back by day.
    // Buttons and the choice follow the window appearance.
    if (m_legend_window) {
        SetWindowDarkAppearance(m_legend_window, dim);

        std::function<void(wxWindow *)> paint = [&](wxWindow *window) {
            if (window == m_markdown_view || window == m_image_page)
                return;

            const bool plain = window == m_legend_window ||
                               wxDynamicCast(window, wxPanel) ||
                               wxDynamicCast(window, wxSimplebook) ||
                               wxDynamicCast(window, wxStaticText);
            if (plain) {
                window->SetBackgroundColour(
                    dim ? palette.background : DayBackground());
                window->SetForegroundColour(dim ? palette.text : wxNullColour);
            }

            for (wxWindow *child : window->GetChildren())
                paint(child);
        };

        paint(m_legend_window);
#ifdef __ANDROID__
        m_title_bar->SetBackgroundColour(dim ? palette.stripe : wxColour(222, 222, 222));
#endif
        m_legend_window->Refresh();
    }
    m_markdown_view->SetBackgroundColour(palette.background);
    m_markdown_view->SetForegroundColour(palette.text);
    m_markdown_view->Refresh();

    for (const auto &icon : m_format_icons)
        icon.first->SetBitmap(NoteToolIcon(icon.second, palette.text));

    if (m_editor) {
        m_editor->SetColours(palette.background, palette.text,
                             palette.link == palette.text ? palette.muted : palette.link);
    }

    // The page of PNG legends darkens along in dusk and night; by day it
    // keeps the normal window colour.
    if (m_image_page) {
        m_image_page->SetBackgroundColour(
            IsDimScheme() ? palette.background : DayBackground());
        m_image_page->Refresh();
    }
}

void legend_pi::ToggleLegendWindow()
{
    CreateLegendWindow();

    if (m_legend_window->IsShown()) {
        if (m_editing)
            StopEditing();
        SaveCurrentMarkdownScroll();
        SaveWindowState();
        SaveConfig();
        m_legend_window->Hide();
    } else {
        // The window first, then what is in it: building the chart legend
        // can take a second, and a window that does not come looks broken.
        if (m_legend_index == 0 && m_markdown_view && m_content_book) {
            m_content_book->SetSelection(1);
            RenderProgressPage(_("Building the chart legend"), 0, 0);
        }
        m_legend_window->Show();
        m_legend_window->Raise();
        // macOS puts the window on screen once OpenCPN is back in its event
        // loop; a moment later the content follows.
        wxLogMessage("Legend: window shown");
        m_fill_window_timer.StartOnce(60);
    }
}

void legend_pi::SetColorScheme(PI_ColorScheme cs)
{
    if (cs == g_color_scheme)
        return;

    g_color_scheme = cs;
    m_ocharts.SetColorScheme(cs);

    const LegendPalette palette = GetLegendPalette();
    wxLogMessage(
        "Legend: colour scheme %s, background %s, text %s",
        SchemeName(cs),
        HtmlColour(palette.background),
        HtmlColour(palette.text));

    ApplyViewColours();

    if (m_legend_window && m_legend_window->IsShown() &&
        !m_legend_files.IsEmpty())
        ShowLegend(m_legend_index);

    if (m_marker_active)
        RefreshAllCanvases();
}

wxString legend_pi::PickLanguage(const wxString &text) const
{
    return TextInLanguage(text, m_dutch_names);
}

wxString legend_pi::SectionTitle(int section) const
{
    if (section >= 0 && section < static_cast<int>(m_symbol_meanings.Sections().size()))
        return PickLanguage(m_symbol_meanings.Sections()[static_cast<size_t>(section)]);
    if (section == -2)
        return _("Chart symbols");
    if (section == -3)
        return _("Light characteristics");
    return _("Other");
}

wxString legend_pi::SectionName(int section) const
{
    if (section >= 0 && section < static_cast<int>(m_symbol_meanings.Sections().size()))
        return m_symbol_meanings.Sections()[static_cast<size_t>(section)];

    // Chart symbols not mentioned in symbolen.txt have a heading of their
    // own, before the waypoints without heading.
    if (section == -2)
        return "Kaartsymbolen";

    // Light characters of waypoints and chart lights in view.
    if (section == -3)
        return "Lichtkarakters";

    return "Overig";
}

void legend_pi::ToggleSection(size_t renderedIndex)
{
    if (renderedIndex >= m_rendered_headings.size())
        return;

    const wxString &name = m_rendered_headings[renderedIndex];

    if (m_collapsed_sections.erase(name) == 0)
        m_collapsed_sections.insert(name);

    SaveConfig();
    RenderDynamicLegend(true);
}

void legend_pi::SetAllSectionsCollapsed(bool collapsed)
{
    m_collapsed_sections.clear();

    if (collapsed) {
        for (const wxString &name : m_symbol_meanings.Sections())
            m_collapsed_sections.insert(name);
        m_collapsed_sections.insert(SectionName(-2));
        m_collapsed_sections.insert(SectionName(-3));
        m_collapsed_sections.insert(SectionName(-1));
        m_collapsed_sections.insert("@marifoon");
        m_collapsed_sections.insert("@vooruit");
        m_collapsed_sections.insert("@notities");
        m_collapsed_sections.insert("@herkennen");
    }

    SaveConfig();
    RenderDynamicLegend(true);
}

void legend_pi::RunLegendCommand(const wxString &command)
{
    if (command == "reorder") {
        SetReorderMode(true);
    }
    else if (command == "collapse-all" || command == "expand-all") {
        SetAllSectionsCollapsed(command == "collapse-all");
    }
    else if (command == "lights") {
        m_show_lights = !m_show_lights;
        if (!m_show_lights) {
            SetNightPreview(false);
            SetAllLightsShining(false);
        }
        SaveConfig();
        RefreshDynamicLegendKeepingState();
        RenderDynamicLegend(true);
    }
    else if (command == "all-lights") {
        if (m_night_preview)
            SetNightPreview(false);
        SetAllLightsShining(!m_all_lights.IsOn());
        RenderDynamicLegend(true);
    }
    else if (command == "night") {
        SetNightPreview(!m_night_preview);
        RenderDynamicLegend(true);
    }
    else if (command == "hidden") {
        m_show_hidden_at_scale = !m_show_hidden_at_scale;
        SaveConfig();
        RefreshDynamicLegendKeepingState();
        RenderDynamicLegend(true);
    }
    else if (command == "distance") {
        m_show_distance = !m_show_distance;
        SaveConfig();
        RenderDynamicLegend(true);
    }
}

// "Uitkijkbundel: breedte": the list and the fan on the chart follow at once.
void legend_pi::SetAheadAngle(int angle)
{
    m_ahead_angle = angle;
    SaveConfig();
    RenderDynamicLegend(true);
    RefreshAllCanvases();
}

const char *const legend_pi::kAheadColourNames[kAheadColourCount] = {
    "amber", "oranje", "grijs"};

// "Uitkijkbundel: kleur": the fan on the chart changes at once.
void legend_pi::SetAheadColour(int colour)
{
    m_ahead_colour = colour;
    SaveConfig();
    RefreshAllCanvases();
}

// "Uitkijkbundel: lengte": how many sea miles ahead the list and the fan reach.
void legend_pi::SetAheadMiles(double miles)
{
    m_ahead_miles = miles;
    SaveConfig();
    RenderDynamicLegend(true);
    RefreshAllCanvases();
}

// "Uitkijkbundel: lengte ▸ Anders…": a length of the user's own, in sea
// miles, rounded to half a mile; a comma or a point both do.
void legend_pi::AskAheadMiles()
{
    wxString text = FormatMiles(m_ahead_miles, m_dutch_names);
    text.Replace(" NM", "");
    for (;;) {
        wxTextEntryDialog dialog(m_legend_window,
                                 _("Length of the lookout beam in nautical miles (0.5 to 50):"),
                                 _("Lookout beam: length"), text);
        if (dialog.ShowModal() != wxID_OK)
            return;
        text = dialog.GetValue();
        wxString value = text;
        value.Trim(true).Trim(false);
        value.Replace(",", ".");
        if (value.Lower().EndsWith("nm"))
            value = value.Left(value.length() - 2).Trim(true);
        double miles = 0.0;
        if (value.ToCDouble(&miles)) {
            miles = std::round(miles * 2.0) / 2.0;
            if (miles >= kAheadMinMiles && miles <= kAheadMaxMiles) {
                SetAheadMiles(miles);
                return;
            }
        }
        wxMessageBox(_("Enter a length from 0.5 to 50 nautical miles, for example 3 or 12.5."),
                     _("Lookout beam: length"), wxOK | wxICON_INFORMATION,
                     m_legend_window);
    }
}

bool legend_pi::DutchInterface()
{
    const wxTranslations *translations = wxTranslations::Get();
    return GetLocaleCanonicalName().StartsWith("nl") && translations &&
           translations->IsLoaded("opencpn-legend_pi");
}

// The menu behind "Instellingen": what is shown, with a tick when it is on,
// and the actions on the list.
void legend_pi::ShowSettingsMenu()
{
    if (!m_markdown_view)
        return;

    enum
    {
        kLights = 1000, kHidden, kDistance, kAllLights, kNight, kIdentify, kVhfKeep,
        kVhfNotice, kNotes, kNoteNotice, kAheadFan, kAhead15, kAhead30, kAhead45,
        kAheadNm1, kAheadNm2, kAheadNm5, kAheadNm10, kAheadNm20, kAheadOwn,
        kAheadColour0, kAheadColourLast = kAheadColour0 + kAheadColourCount - 1,
        kFold, kReorder
    };

    wxMenu menu;
    menu.AppendCheckItem(kLights, _("Lights"))->Check(m_show_lights);
    menu.AppendCheckItem(kHidden, _("Also show hidden at this scale"))
        ->Check(m_show_hidden_at_scale);
    menu.AppendCheckItem(kDistance, _("Distance and bearing from the boat"))
        ->Check(m_show_distance);

    menu.AppendSeparator();
    if (m_show_lights && (m_menu_has_lights || m_all_lights.IsOn()))
        menu.AppendCheckItem(kAllLights, _("Make all lights shine"))
            ->Check(m_all_lights.IsOn());
    if (m_show_lights && (m_menu_has_lights || m_night_preview))
        menu.AppendCheckItem(kNight, _("Night preview"))->Check(m_night_preview);
    if (m_show_lights && (m_menu_has_lights || m_identify_on))
        menu.AppendCheckItem(kIdentify, _("Identify a light"))
            ->Check(m_identify_on);
    EnsureVhfAreas();
    if (!m_vhf_areas.IsEmpty()) {
        menu.AppendSeparator();
        menu.AppendCheckItem(kVhfKeep, _("VHF area stays highlighted"))
            ->Check(m_vhf_keep_lit);
        menu.AppendCheckItem(kVhfNotice, _("Notice on entering a VHF area"))
            ->Check(m_vhf_notice_on);
    }
    menu.AppendSeparator();
    menu.AppendCheckItem(kNotes, _("Notes: write, read and show on the chart"))->Check(m_notes_on);
    if (!m_note_places.empty())
        menu.AppendCheckItem(kNoteNotice, _("Notice when near a note"))
            ->Check(m_note_notice_on);

    menu.AppendSeparator();
    menu.AppendCheckItem(kAheadFan, _("Lookout beam on the chart"))
        ->Check(m_ahead_fan_shown);
    auto *width = new wxMenu;
    width->AppendRadioItem(kAhead15, _("Narrow (15\u00b0)"))
        ->Check(m_ahead_angle == 15);
    width->AppendRadioItem(kAhead30, _("Medium (30\u00b0)"))
        ->Check(m_ahead_angle == 30);
    width->AppendRadioItem(kAhead45, _("Wide (45\u00b0)"))
        ->Check(m_ahead_angle == 45);
    menu.AppendSubMenu(width, _("Lookout beam: width"));
    auto *ahead = new wxMenu;
    // Each length with the time it takes at the present speed.
    // A length of the user's own is ticked at "Anders…", with its length.
    const double lengths[] = {1.0, 2.0, 5.0, 10.0, 20.0};
    bool own = true;
    for (int n = 0; n < 5; ++n) {
        wxString label = FormatMiles(lengths[n], m_dutch_names);
        const wxString time = AheadTimeText(lengths[n]);
        if (!time.IsEmpty())
            label << "  (" << time << ")";
        const bool chosen = m_ahead_miles == lengths[n];
        own = own && !chosen;
        ahead->AppendRadioItem(kAheadNm1 + n, label)->Check(chosen);
    }
    wxString ownLabel = _("Other\u2026");
    if (own) {
        ownLabel << " (" << FormatMiles(m_ahead_miles, m_dutch_names);
        const wxString time = AheadTimeText(m_ahead_miles);
        if (!time.IsEmpty())
            ownLabel << ", " << time;
        ownLabel << ")";
    }
    ahead->AppendRadioItem(kAheadOwn, ownLabel)->Check(own);
    menu.AppendSubMenu(ahead, _("Lookout beam: length"));
    auto *colours = new wxMenu;
    const wxString colourLabels[kAheadColourCount] = {
        _("Amber"), _("Orange (searchlight)"),
        _("Grey")};
    for (int n = 0; n < kAheadColourCount; ++n)
        colours->AppendRadioItem(kAheadColour0 + n, colourLabels[n])->Check(m_ahead_colour == n);
    menu.AppendSubMenu(colours, _("Lookout beam: colour"));
    if (m_menu_has_headings) {
        menu.AppendSeparator();
        menu.Append(kFold, m_menu_any_open ? _("Fold all")
                                           : _("Unfold all"));
        menu.Append(kReorder, _("Change order\u2026"));
    }

    const int selection = m_markdown_view->GetPopupMenuSelectionFromUser(menu);
    if (selection >= kAheadColour0 && selection <= kAheadColourLast) {
        SetAheadColour(selection - kAheadColour0);
        return;
    }
    switch (selection) {
    case kLights:
        RunLegendCommand("lights");
        break;
    case kHidden:
        RunLegendCommand("hidden");
        break;
    case kDistance:
        RunLegendCommand("distance");
        break;
    case kAllLights:
        RunLegendCommand("all-lights");
        break;
    case kNight:
        RunLegendCommand("night");
        break;
    case kIdentify:
        SetIdentify(!m_identify_on);
        break;
    case kVhfKeep:
        m_vhf_keep_lit = !m_vhf_keep_lit;
        SaveConfig();
        m_vhf_highlight.SetKeepLit(m_vhf_keep_lit);
        break;
    case kVhfNotice:
        m_vhf_notice_on = !m_vhf_notice_on;
        SaveConfig();
        break;
    case kNotes:
        SetNotesOn(!m_notes_on);
        break;
    case kNoteNotice:
        m_note_notice_on = !m_note_notice_on;
        SaveConfig();
        break;
    case kAheadFan:
        m_ahead_fan_shown = !m_ahead_fan_shown;
        SaveConfig();
        RefreshAllCanvases();
        break;
    case kAhead15:
    case kAhead30:
    case kAhead45:
        SetAheadAngle(selection == kAhead15 ? 15 : selection == kAhead30 ? 30 : 45);
        break;
    case kAheadNm1:
    case kAheadNm2:
    case kAheadNm5:
    case kAheadNm10:
    case kAheadNm20:
        SetAheadMiles(selection == kAheadNm1    ? 1.0
                      : selection == kAheadNm2  ? 2.0
                      : selection == kAheadNm5  ? 5.0
                      : selection == kAheadNm10 ? 10.0
                                                : 20.0);
        break;
    case kAheadOwn:
        AskAheadMiles();
        break;
    case kFold:
        RunLegendCommand(m_menu_any_open ? "collapse-all" : "expand-all");
        break;
    case kReorder:
        RunLegendCommand("reorder");
        break;
    default:
        break;
    }
}

int legend_pi::BuiltInSection(int builtIn) const
{
    const wxString name = builtIn == -2 ? "kaartsymbolen" : "lichtkarakters";
    for (size_t i = 0; i < m_symbol_meanings.Sections().size(); ++i) {
        wxString first = m_symbol_meanings.Sections()[i].BeforeFirst('|');
        first.Trim(true).Trim(false);
        if (first.Lower() == name)
            return static_cast<int>(i);
    }
    return builtIn;
}

void legend_pi::AddBuiltInHeadings()
{
    wxString missing;
    if (m_chart_sources_available && BuiltInSection(-2) == -2)
        missing << "\n[Kaartsymbolen | Chart symbols]\n";
    if (m_show_lights && BuiltInSection(-3) == -3)
        missing << "\n[Lichtkarakters | Light characteristics]\n";
    if (missing.IsEmpty())
        return;

    wxFile file;
    if (!file.Open(GetMeaningsFile(), wxFile::write_append))
        return;
    file.Write(missing, wxConvUTF8);
    file.Close();

    // Read again even when the file time did not change within a second.
    m_meanings_modified = wxDateTime();
    LoadMeanings();
    CollectDynamicLegend();
}

void legend_pi::SetReorderMode(bool on)
{
    if (on)
        AddBuiltInHeadings();
    m_reorder_mode = on;
    UpdateSearchVisibility();
    RenderDynamicLegend(!on);
}

void legend_pi::UpdateSearchVisibility()
{
    if (!m_search)
        return;

    // Only the dynamic legend can be searched; the order edit mode shows
    // every heading and has no use for it.
    const bool show = m_current_legend_path == kDynamicGpxId && !m_reorder_mode;
    if (m_search->IsShown() == show)
        return;

    m_search->Show(show);
    m_search->GetParent()->Layout();
}

void legend_pi::SetFilter(const wxString &text)
{
    m_filter_timer.Stop();

    if (m_search && m_search->GetValue() != text)
        m_search->ChangeValue(text);

    wxString trimmed = text;
    trimmed.Trim(true).Trim(false);

    std::vector<wxString> words;
    wxStringTokenizer tokens(FoldForSearch(trimmed), " \t", wxTOKEN_STRTOK);
    while (tokens.HasMoreTokens())
        words.push_back(tokens.GetNextToken());

    if (words == m_filter_words)
        return;

    m_filter = trimmed;
    m_filter_words = words;

    // A new search starts at the top of the list. While the list is still
    // being filled it is drawn with the filter once loading has finished.
    if (IsDynamicLegendVisible() && !m_loading && !m_showing_loading_page)
        RenderDynamicLegend(false);
}

bool legend_pi::GroupMatchesFilter(const DynamicGroup &group) const
{
    if (m_filter_words.empty())
        return true;

    wxString text = group.label;
    text << ' '
         << (group.chartSymbol      ? group.symbol
             : group.lightCharacter ? group.lightText
                                    : group.iconName)
         << ' ' << SectionTitle(group.section);
    const wxString folded = FoldForSearch(text);

    for (const wxString &word : m_filter_words) {
        if (folded.Find(word) == wxNOT_FOUND)
            return false;
    }
    return true;
}

// Moves one of the blocks at the top of the list (Marifoon, Uitkijk,
// Notities, Licht herkennen) among the others; the order is kept in
// legend.ini.
void legend_pi::MoveTopBlock(int index, int delta)
{
    const int target = index + delta;
    const int count = static_cast<int>(m_top_block_order.size());

    if (index < 0 || index >= count || target < 0 || target >= count)
        return;

    std::swap(m_top_block_order[static_cast<size_t>(index)],
              m_top_block_order[static_cast<size_t>(target)]);
    SaveConfig();
    RenderDynamicLegend(true);
}

wxString legend_pi::TopBlockTitle(const wxString &key) const
{
    if (key == "@marifoon")
        return _("VHF");
    if (key == "@vooruit")
        return _("Lookout");
    if (key == "@notities")
        return _("Notes");
    if (key == "@herkennen")
        return _("Identify a light");
    return key;
}

// Moves the block of a heading in symbolen.txt: the [Heading] line and
// everything up to the next heading, comments included. Text before the
// first heading stays in place.
void legend_pi::MoveSection(int section, int delta)
{
    const int target = section + delta;
    const int count = static_cast<int>(m_symbol_meanings.Sections().size());

    if (section < 0 || section >= count || target < 0 || target >= count)
        return;

    const wxString path = GetMeaningsFile();

    wxTextFile file;
    if (!file.Open(path, wxConvUTF8))
        return;

    auto isHeading = [](wxString line) {
        line.Trim(true).Trim(false);
        return line.StartsWith("[") && line.EndsWith("]");
    };

    std::vector<wxString> preamble;
    std::vector<std::vector<wxString>> blocks;

    for (size_t i = 0; i < file.GetLineCount(); ++i) {
        const wxString &line = file[i];

        if (isHeading(line))
            blocks.emplace_back();

        if (blocks.empty())
            preamble.push_back(line);
        else
            blocks.back().push_back(line);
    }

    // The blocks must match the headings that were read; otherwise the
    // file changed in the meantime and nothing is moved.
    if (static_cast<int>(blocks.size()) != count) {
        m_meanings_modified = wxDateTime();
        RefreshDynamicLegendKeepingState();
        RenderDynamicLegend(true);
        return;
    }

    std::swap(blocks[static_cast<size_t>(section)],
              blocks[static_cast<size_t>(target)]);

    // One empty line between blocks, whatever order they end up in.
    auto trimEnd = [](std::vector<wxString> &lines) {
        while (!lines.empty() && wxString(lines.back()).Trim().IsEmpty())
            lines.pop_back();
    };

    trimEnd(preamble);

    file.Clear();
    for (const wxString &line : preamble)
        file.AddLine(line);

    for (auto &block : blocks) {
        trimEnd(block);
        if (file.GetLineCount() > 0)
            file.AddLine(wxEmptyString);
        for (const wxString &line : block)
            file.AddLine(line);
    }

    file.Write(wxTextFileType_None, wxConvUTF8);
    file.Close();

    // Read again even if the modification time did not visibly change.
    m_meanings_modified = wxDateTime();
    RefreshDynamicLegendKeepingState();
    RenderDynamicLegend(true);
}
