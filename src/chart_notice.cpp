// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "chart_notice.h"

#include <cstdint>  // ocpn_plugin.h (api-18) uses uint64_t without it
#include "ocpn_plugin.h"

#include <wx/button.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/log.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

ChartNotice::ChartNotice()
{
    m_hide_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { Hide(); });

    // Tries again until OpenCPN takes the next beep.
    m_sound_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        if (m_beeps_left <= 0) {
            m_sound_timer.Stop();
            return;
        }
        if (PlaySound())
            --m_beeps_left;
    });
}

ChartNotice::~ChartNotice()
{
    Close();
}

void ChartNotice::Create(wxWindow *canvas)
{
    m_frame = new wxFrame(canvas, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                          wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT | wxFRAME_NO_TASKBAR |
                              wxBORDER_SIMPLE);
    m_panel = new wxPanel(m_frame);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    m_title = new wxStaticText(m_panel, wxID_ANY, wxEmptyString);
    wxFont bold = m_title->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    bold.SetPointSize(bold.GetPointSize() + 1);
    m_title->SetFont(bold);
    m_text = new wxStaticText(m_panel, wxID_ANY, wxEmptyString);
    sizer->Add(m_title, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    sizer->Add(m_text, 0, wxALL, 12);
    // With an action label: the action as a button, and one that closes.
    m_buttons = new wxPanel(m_panel);
    auto *buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    m_action_button = new wxButton(m_buttons, wxID_ANY, wxEmptyString);
    auto *closeButton = new wxButton(m_buttons, wxID_ANY, "Sluiten");
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add(closeButton, 0, wxRIGHT, 8);
    buttonSizer->Add(m_action_button, 0);
    m_buttons->SetSizer(buttonSizer);
    sizer->Add(m_buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    m_panel->SetSizer(sizer);
    m_action_button->SetDefault();
    m_action_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        Hide();
        RunAction();
    });
    closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_action = std::function<void()>();
        Hide();
    });
    // A click closes it, and opens what it is about, if anything; with
    // buttons only those do the action.
    auto close = [this](wxMouseEvent &) {
        Hide();
        if (m_buttons && m_buttons->IsShown())
            return;
        RunAction();
    };
    m_panel->Bind(wxEVT_LEFT_DOWN, close);
    m_title->Bind(wxEVT_LEFT_DOWN, close);
    m_text->Bind(wxEVT_LEFT_DOWN, close);
}

void ChartNotice::Show(const wxString &title,
                       const wxString &text,
                       bool sound,
                       const wxColour &titleColour,
                       const wxColour &background,
                       const wxColour &textColour,
                       std::function<void()> action,
                       const wxString &actionLabel)
{
    m_action = action;
    wxWindow *canvas = GetOCPNCanvasWindow();
    if (!canvas)
        return;
    if (!m_frame)
        Create(canvas);

    m_panel->SetBackgroundColour(background);
    m_title->SetForegroundColour(titleColour);
    m_text->SetForegroundColour(textColour);
    m_title->SetLabel(title);
    m_text->SetLabel(text);
    m_buttons->SetBackgroundColour(background);
    m_action_button->SetLabel(actionLabel);
    m_buttons->Show(!actionLabel.IsEmpty());
    m_panel->GetSizer()->Layout();
    m_panel->GetSizer()->Fit(m_panel);
    m_frame->SetClientSize(m_panel->GetBestSize());
    m_panel->SetSize(m_frame->GetClientSize());

    // At the top of the chart, in the middle.
    const wxRect area = canvas->GetScreenRect();
    const wxSize size = m_frame->GetSize();
    m_frame->Move(area.x + (area.width - size.x) / 2, area.y + 16);
    m_frame->ShowWithoutActivating();
    m_frame->Raise();
    m_hide_timer.StartOnce(12000);

    // OpenCPN's short beep six times, so it is heard in the cockpit.
    if (sound) {
        m_beeps_left = 6;
        if (PlaySound())
            --m_beeps_left;
        m_sound_timer.Start(250);
    }
}

bool ChartNotice::IsShown() const
{
    return m_frame && m_frame->IsShown();
}

void ChartNotice::Hide()
{
    if (m_frame)
        m_frame->Hide();
}

void ChartNotice::Close()
{
    m_hide_timer.Stop();
    m_sound_timer.Stop();
    m_action = std::function<void()>();
    delete m_frame;
    m_frame = nullptr;
    m_panel = nullptr;
    m_title = nullptr;
    m_text = nullptr;
    m_buttons = nullptr;
    m_action_button = nullptr;
}

void ChartNotice::RunAction()
{
    if (!m_action)
        return;
    std::function<void()> action = m_action;
    m_action = std::function<void()>();
    action();
}

// On macOS OpenCPN plays one sound at a time, through a system command that
// takes a moment.
bool ChartNotice::PlaySound()
{
    wxString *shared = GetpSharedDataLocation();
    if (!shared)
        return true;
    wxString file = *shared + "sounds" + wxFileName::GetPathSeparator() + "beep1.wav";
    if (!wxFileExists(file))
        return true;
    wxLogNull quiet; // a refused try is not worth a warning in the log
    return PlugInPlaySoundEx(file, -1);
}
