// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "gles_picture.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPixmap>

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/dcmemory.h>
#include <wx/dc.h>
#include <wx/log.h>
#include <wx/string.h>

#include <algorithm>
#include <cmath>

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

// Triangles in one colour, with their corners in pixels of the view.
const char *kFillVertexShader =
    "attribute vec2 position;\n"
    "uniform vec2 view;\n"
    "void main() {\n"
    "    gl_Position = vec4(2.0 * position.x / view.x - 1.0,\n"
    "                       1.0 - 2.0 * position.y / view.y, 0.0, 1.0);\n"
    "}\n";

const char *kFillFragmentShader =
    "precision mediump float;\n"
    "uniform vec4 colour;\n"
    "void main() {\n"
    "    gl_FragColor = colour;\n"
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

// The GL state the picture changes, given back to OpenCPN afterwards.
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

GLuint LinkProgram(const char *vertexSource, const char *fragmentSource)
{
    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment)
        return 0;
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
        return 0;
    }
    return program;
}

} // namespace

void GlesFillTriangles(const wxColour &colour, const std::vector<wxPoint> &corners)
{
    if (corners.size() < 3 || colour.Alpha() == 0)
        return;

    // One program per GL context, which OpenCPN may make anew.
    static EGLContext programContext = EGL_NO_CONTEXT;
    static GLuint program = 0;
    static GLint position = -1, view = -1, colourAt = -1;
    const EGLContext current = eglGetCurrentContext();
    if (!program || current != programContext) {
        program = LinkProgram(kFillVertexShader, kFillFragmentShader);
        if (!program)
            return;
        programContext = current;
        position = glGetAttribLocation(program, "position");
        view = glGetUniformLocation(program, "view");
        colourAt = glGetUniformLocation(program, "colour");
    }

    std::vector<GLfloat> xy;
    xy.reserve(corners.size() * 2);
    for (const wxPoint &corner : corners) {
        xy.push_back(static_cast<GLfloat>(corner.x));
        xy.push_back(static_cast<GLfloat>(corner.y));
    }
    GLint viewport[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, viewport);

    SavedState saved;
    glUseProgram(program);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUniform2f(view, static_cast<GLfloat>(viewport[2]), static_cast<GLfloat>(viewport[3]));
    glUniform4f(colourAt, colour.Red() / 255.0f, colour.Green() / 255.0f, colour.Blue() / 255.0f,
                colour.Alpha() / 255.0f);
    glEnableVertexAttribArray(position);
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, xy.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(corners.size() / 3 * 3));
    glDisableVertexAttribArray(position);
}

// The shader program and the texture, made once per GL context.
bool GlesPicture::Prepare(wxGLContext *context)
{
    if (m_program && context == m_context)
        return true;
    m_program = 0;
    m_texture = 0;
    m_context = nullptr;

    const GLuint program = LinkProgram(kVertexShader, kFragmentShader);
    if (!program)
        return false;

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

bool GlesPicture::Draw(wxGLContext *context, int viewWidth, int viewHeight, const wxRect &place,
                       const std::function<void(wxDC &)> &paint)
{
    if (viewWidth <= 0 || viewHeight <= 0 || place.width <= 0 || place.height <= 0)
        return false;

    // The picture, transparent where nothing is drawn.
    QPixmap pixmap(place.width, place.height);
    pixmap.fill(Qt::transparent);
    wxBitmap bitmap(pixmap);
    {
        wxMemoryDC dc(bitmap);
        paint(dc);
        dc.SelectObject(wxNullBitmap);
    }
    const QImage image =
        bitmap.GetHandle()->toImage().convertToFormat(QImage::Format_RGBA8888_Premultiplied);

    SavedState saved;
    if (!Prepare(context))
        return false;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.constBits());

    // From pixels, y down, to OpenGL's -1..1, y up; the first row of the
    // picture is its top.
    const GLfloat left = 2.0f * place.x / viewWidth - 1.0f;
    const GLfloat right = 2.0f * (place.x + place.width) / viewWidth - 1.0f;
    const GLfloat top = 1.0f - 2.0f * place.y / viewHeight;
    const GLfloat bottom = 1.0f - 2.0f * (place.y + place.height) / viewHeight;
    const GLfloat positions[] = {left, top, right, top, left, bottom, right, bottom};
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

namespace {

QFont LabelFont(int pixels)
{
    QFont font;
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(std::max(1, pixels));
    return font;
}

} // namespace

wxSize GlesTextSize(const wxString &text, int pixels)
{
    const QFontMetricsF metrics(LabelFont(pixels));
    return wxSize(static_cast<int>(std::ceil(
                      metrics.horizontalAdvance(QString::fromUtf8(text.utf8_str())))),
                  static_cast<int>(std::ceil(metrics.height())));
}

void GlesDrawText(wxDC &dc, const wxString &text, int pixels, const wxColour &colour, int x,
                  int y)
{
    QPainter *painter = static_cast<QPainter *>(dc.GetHandle());
    if (!painter || !painter->isActive())
        return;
    const QFont font = LabelFont(pixels);
    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setFont(font);
    painter->setPen(QColor(colour.Red(), colour.Green(), colour.Blue(), colour.Alpha()));
    painter->drawText(QPointF(x, y + QFontMetricsF(font).ascent()),
                      QString::fromUtf8(text.utf8_str()));
    painter->restore();
}
