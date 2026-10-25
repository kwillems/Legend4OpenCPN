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

class wxFrame;
class wxPanel;
class wxStaticText;
class wxWindow;

// The colours of a notice, after those of the legend.
struct NoticeColours
{
    wxColour title;
    wxColour background;
    wxColour text;
    wxColour frame;  // the thin frame of the buttons
    wxColour accent; // behind the action button
};

// A small window at the top of the chart that goes away on its own after a
// while, or with a click: a new VHF area, a note nearby, a moved note.
class ChartNotice
{
public:
    ChartNotice();
    ~ChartNotice();

    // Every notice has a button "Sluiten". With an action, a click
    // elsewhere on the notice runs it; with an action label as well, the
    // action is a button next to "Sluiten" and only that button runs it.
    // With sound, OpenCPN's short beep sounds six times.
    void Show(const wxString &title,
              const wxString &text,
              bool sound,
              const NoticeColours &colours,
              std::function<void()> action = std::function<void()>(),
              const wxString &actionLabel = wxEmptyString);

    bool IsShown() const;
    void Hide();

    // Stops the timers and deletes the window, before the plugin goes.
    void Close();

private:
    // A button drawn as the buttons in the legend: its label in a thin
    // frame. A native button is white on macOS and hardly shows on the
    // white notice.
    struct Button
    {
        wxPanel *frame = nullptr; // one pixel of the frame colour around
        wxPanel *inside = nullptr;
        wxStaticText *label = nullptr;
    };
    Button MakeButton(const wxString &label, std::function<void()> onClick);
    static void ColourButton(const Button &button, const wxColour &frame,
                             const wxColour &fill, const wxColour &text);
    static void LayoutButton(const Button &button);

    void Create(wxWindow *canvas);
    void RunAction();
    // False while OpenCPN is still busy with the previous sound.
    bool PlaySound();

    wxFrame *m_frame = nullptr;
    wxPanel *m_panel = nullptr;
    wxStaticText *m_title = nullptr;
    wxStaticText *m_text = nullptr;
    wxPanel *m_buttons = nullptr; // Sluiten, and the action as a button
    Button m_close_button;
    Button m_action_button;
    std::function<void()> m_action;
    wxTimer m_hide_timer;
    wxTimer m_sound_timer; // the next beep
    int m_beeps_left = 0;
};

#endif
