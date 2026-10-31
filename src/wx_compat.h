// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _WX_COMPAT_H_
#define _WX_COMPAT_H_

#include <wx/graphics.h>
#include <wx/pen.h>
#include <wx/version.h>

// The Android build of OpenCPN has wxWidgets 3.1.0, before
// wxGraphicsPenInfo (3.1.1). This stand-in builds a wxPen instead, which
// wxGraphicsContext::CreatePen takes as well; the width is then rounded to
// whole pixels.
#if !wxCHECK_VERSION(3, 1, 1)
#include <algorithm>
#include <cmath>

class wxGraphicsPenInfo
{
public:
    explicit wxGraphicsPenInfo(const wxColour &colour = wxColour(), double width = 1.0,
                               wxPenStyle style = wxPENSTYLE_SOLID)
        : m_pen(colour, Pixels(width), style)
    {
    }

    wxGraphicsPenInfo &Width(double width)
    {
        m_pen.SetWidth(Pixels(width));
        return *this;
    }
    wxGraphicsPenInfo &Style(wxPenStyle style)
    {
        m_pen.SetStyle(style);
        return *this;
    }
    wxGraphicsPenInfo &Cap(wxPenCap cap)
    {
        m_pen.SetCap(cap);
        return *this;
    }
    wxGraphicsPenInfo &Join(wxPenJoin join)
    {
        m_pen.SetJoin(join);
        return *this;
    }

    operator const wxPen &() const { return m_pen; }

private:
    static int Pixels(double width)
    {
        return std::max(1, static_cast<int>(std::lround(width)));
    }

    wxPen m_pen;
};
#endif

#endif
