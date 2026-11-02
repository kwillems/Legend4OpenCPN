// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _GLES_OVERLAY_H_
#define _GLES_OVERLAY_H_

#include <functional>

class wxDC;
class wxGLContext;

// OpenCPN on Android draws the chart with OpenGL ES, which has none of the
// fixed-function OpenGL the other platforms draw with. There the drawings
// on the chart are made with a wxDC on a transparent picture, which is laid
// over the chart as a texture.
class GlesOverlay
{
public:
    // paint draws on a wxDC of width x height pixels and says whether it
    // drew anything; only then is the picture laid over the chart.
    bool Draw(wxGLContext *context, int width, int height,
              const std::function<bool(wxDC &)> &paint);

private:
    bool Prepare(wxGLContext *context);
    void Release();

    wxGLContext *m_context = nullptr; // the context the ids below belong to
    unsigned m_program = 0;
    unsigned m_texture = 0;
    int m_position = -1; // attribute locations
    int m_texcoord = -1;
    int m_sampler = -1;
};

#endif
