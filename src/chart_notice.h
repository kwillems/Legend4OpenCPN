// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _CHART_NOTICE_H_
#define _CHART_NOTICE_H_

#include <functional>

#include <wx/colour.h>
#include <wx/string.h>
#include <wx/timer.h>

class wxButton;
class wxFrame;
class wxPanel;
class wxStaticText;

// A small window at the top of the chart that goes away on its own after a
// while, or with a click: a new VHF area, a note nearby, a moved note.
class ChartNotice
{
public:
    ChartNotice();
    ~ChartNotice();

    // With an action, a click on the notice runs it; with an action label
    // as well, the action is a button next to "Sluiten" and only that
    // button runs it. With sound, OpenCPN's short beep sounds six times.
    void Show(const wxString &title,
              const wxString &text,
              bool sound,
              const wxColour &titleColour,
              const wxColour &background,
              const wxColour &textColour,
              std::function<void()> action = std::function<void()>(),
              const wxString &actionLabel = wxEmptyString);

    bool IsShown() const;
    void Hide();

    // Stops the timers and deletes the window, before the plugin goes.
    void Close();

private:
    void Create(wxWindow *canvas);
    void RunAction();
    // False while OpenCPN is still busy with the previous sound.
    bool PlaySound();

    wxFrame *m_frame = nullptr;
    wxPanel *m_panel = nullptr;
    wxStaticText *m_title = nullptr;
    wxStaticText *m_text = nullptr;
    wxPanel *m_buttons = nullptr; // the action as a button, and Sluiten
    wxButton *m_action_button = nullptr;
    std::function<void()> m_action;
    wxTimer m_hide_timer;
    wxTimer m_sound_timer; // the next beep
    int m_beeps_left = 0;
};

#endif
