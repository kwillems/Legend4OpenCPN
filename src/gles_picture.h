// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _GLES_PICTURE_H_
#define _GLES_PICTURE_H_

#include <functional>
#include <vector>

#include <wx/gdicmn.h>

class wxColour;
class wxDC;
class wxGLContext;
class wxString;

// A small picture made with a wxDC, laid on the chart with OpenGL ES
// (Android), for what piDC cannot draw there: text. piDC measures and
// draws text on a wxScreenDC, which has no active painter in OpenCPN's
// wxWidgets on Qt, and its DrawBitmap does nothing with OpenGL ES.
class GlesPicture
{
public:
    // paint draws on a transparent wxDC of place's size; the picture is
    // laid at place, in pixels of a view of viewWidth by viewHeight.
    bool Draw(wxGLContext *context, int viewWidth, int viewHeight, const wxRect &place,
              const std::function<void(wxDC &)> &paint);

private:
    bool Prepare(wxGLContext *context);

    wxGLContext *m_context = nullptr; // the context the ids below belong to
    unsigned m_program = 0;
    unsigned m_texture = 0;
    int m_position = -1; // attribute locations
    int m_texcoord = -1;
    int m_sampler = -1;
};

// Fills triangles (three corners each, in pixels of the view) in one
// colour with OpenGL ES. piDC's fills are not reliable there: its
// tessellated polygon takes a transformation left by an earlier call, and
// its triangle overwrites its own corners with its colour.
void GlesFillTriangles(const wxColour &colour, const std::vector<wxPoint> &corners);

// Text for such a picture, in a sans-serif font `pixels` high. OpenCPN's
// wxWidgets on Qt does not pass a wxFont's size in pixels on to Qt (the
// text came out a few pixels high), so this measures and draws with Qt.
wxSize GlesTextSize(const wxString &text, int pixels);
void GlesDrawText(wxDC &dc, const wxString &text, int pixels, const wxColour &colour, int x,
                  int y);

#endif
