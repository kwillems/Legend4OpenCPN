// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "gles_overlay.h"

#include <GLES2/gl2.h>

#include <QImage>
#include <QPixmap>

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/log.h>

namespace {

const char *kVertexShader =
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    uv = texcoord;\n"
    "}\n";

const char *kFragmentShader =
    "precision mediump float;\n"
    "uniform sampler2D picture;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(picture, uv);\n"
    "}\n";

GLuint CompileShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        wxLogMessage("Legend: shader not compiled: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// The GL state the overlay changes, given back to OpenCPN afterwards.
struct SavedState
{
    GLint program = 0;
    GLint activeTexture = 0;
    GLint texture = 0;
    GLint arrayBuffer = 0;
    GLboolean blend = GL_FALSE;
    GLint blendSrcRgb = 0;
    GLint blendDstRgb = 0;
    GLint blendSrcAlpha = 0;
    GLint blendDstAlpha = 0;

    SavedState()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        blend = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    }

    ~SavedState()
    {
        glUseProgram(program);
        glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(activeTexture);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        if (blend)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
    }
};

}

void GlesOverlay::Release()
{
    m_program = 0;
    m_texture = 0;
    m_context = nullptr;
}

// The shader program and the texture, made once per GL context.
bool GlesOverlay::Prepare(wxGLContext *context)
{
    if (m_program && context == m_context)
        return true;
    Release();

    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertex || !fragment)
        return false;

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        wxLogMessage("Legend: shader program not linked");
        glDeleteProgram(program);
        return false;
    }

    m_program = program;
    m_position = glGetAttribLocation(program, "position");
    m_texcoord = glGetAttribLocation(program, "texcoord");
    m_sampler = glGetUniformLocation(program, "picture");

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_texture = texture;
    m_context = context;
    return true;
}

bool GlesOverlay::Draw(wxGLContext *context, int width, int height,
                       const std::function<bool(wxDC &)> &paint)
{
    if (width <= 0 || height <= 0)
        return false;

    // The picture, transparent where nothing is drawn.
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::transparent);
    wxBitmap bitmap(pixmap);
    bool drew = false;
    {
        wxMemoryDC dc(bitmap);
        drew = paint(dc);
        dc.SelectObject(wxNullBitmap);
    }
    if (!drew)
        return false;

    const QImage image =
        bitmap.GetHandle()->toImage().convertToFormat(QImage::Format_RGBA8888_Premultiplied);

    SavedState saved;
    if (!Prepare(context))
        return false;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.constBits());

    // The whole viewport; the first row of the picture is the top.
    const GLfloat positions[] = {-1.f, 1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f};
    const GLfloat texcoords[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};

    glUseProgram(m_program);
    glUniform1i(m_sampler, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(m_position);
    glEnableVertexAttribArray(m_texcoord);
    glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointer(m_texcoord, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // the picture is premultiplied
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(m_position);
    glDisableVertexAttribArray(m_texcoord);
    return true;
}
