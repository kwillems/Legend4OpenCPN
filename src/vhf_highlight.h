// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _VHF_HIGHLIGHT_H_
#define _VHF_HIGHLIGHT_H_

#include "vhf_areas.h"

#include <functional>
#include <vector>

#include <wx/longlong.h>
#include <wx/timer.h>

class wxDC;
class PlugIn_ViewPort;

// VHF areas lit on the chart in light red: for six seconds, then fading
// out, or until the next click on the chart when keepLit is set.
class VhfHighlight
{
public:
    // refresh redraws the charts, as the light changes.
    explicit VhfHighlight(std::function<void()> refresh);

    void Show(const std::vector<VhfArea> &areas);
    // The setting changed: a lit area now stays, or starts to fade.
    void SetKeepLit(bool keepLit);
    // Goes out, and the charts are redrawn.
    void Stop();
    // Goes out without a redraw, before the plugin goes.
    void Clear();
    bool IsEmpty() const { return m_areas.empty(); }

    // lineScale: viewport pixels per window pixel; dim: dusk or night.
    bool Draw(wxDC &dc, PlugIn_ViewPort *vp, double lineScale, bool dim) const;
    bool DrawGL(PlugIn_ViewPort *vp, double lineScale, bool dim) const;

private:
    void OnTimer();
    void StartTiming();

    std::function<void()> m_refresh;
    std::vector<VhfArea> m_areas; // copies: a reload frees the areas
    bool m_keep_lit = false;
    wxLongLong m_started = 0;
    double m_alpha = 1.0;
    wxTimer m_timer;
};

#endif
