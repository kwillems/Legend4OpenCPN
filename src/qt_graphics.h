// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _QT_GRAPHICS_H_
#define _QT_GRAPHICS_H_

// The wxWidgets of OpenCPN's Android libraries is built without
// wxGraphicsContext. These classes stand in for the part of it the plugin
// uses and draw with the QPainter behind the wxDC (or on a QImage), so
// lines keep their anti-aliasing, fractional widths and gradients.
//
// Angles follow wxWidgets: radians, with positive angles turning clockwise
// on screen. Qt measures arcs in degrees the other way round.

#include <wx/brush.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/font.h>
#include <wx/image.h>
#include <wx/pen.h>
#include <wx/string.h>

#include <QBrush>
#include <QColor>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QString>

#include <cmath>
#include <memory>

enum wxAntialiasMode
{
    wxANTIALIAS_NONE,
    wxANTIALIAS_DEFAULT
};

namespace qt_graphics {

const double kPi = 3.14159265358979323846;

inline QColor ToQt(const wxColour &colour)
{
    if (!colour.IsOk())
        return QColor(0, 0, 0);
    return QColor(colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());
}

inline QString ToQt(const wxString &text)
{
    return QString::fromUtf8(text.utf8_str());
}

inline QPen MakePen(const wxColour &colour, double width, wxPenStyle style, wxPenCap cap,
                    wxPenJoin join)
{
    if (style == wxPENSTYLE_TRANSPARENT || !colour.IsOk())
        return QPen(Qt::NoPen);
    QPen pen(ToQt(colour));
    pen.setWidthF(width);
    switch (style) {
    case wxPENSTYLE_DOT:
        pen.setStyle(Qt::DotLine);
        break;
    case wxPENSTYLE_LONG_DASH:
    case wxPENSTYLE_SHORT_DASH:
        pen.setStyle(Qt::DashLine);
        break;
    case wxPENSTYLE_DOT_DASH:
        pen.setStyle(Qt::DashDotLine);
        break;
    default:
        pen.setStyle(Qt::SolidLine);
        break;
    }
    switch (cap) {
    case wxCAP_BUTT:
        pen.setCapStyle(Qt::FlatCap);
        break;
    case wxCAP_PROJECTING:
        pen.setCapStyle(Qt::SquareCap);
        break;
    default:
        pen.setCapStyle(Qt::RoundCap);
        break;
    }
    switch (join) {
    case wxJOIN_BEVEL:
        pen.setJoinStyle(Qt::BevelJoin);
        break;
    case wxJOIN_MITER:
        pen.setJoinStyle(Qt::MiterJoin);
        break;
    default:
        pen.setJoinStyle(Qt::RoundJoin);
        break;
    }
    return pen;
}

inline QPen MakePen(const wxPen &pen)
{
    if (!pen.IsOk())
        return QPen(Qt::NoPen);
    return MakePen(pen.GetColour(), pen.GetWidth(), pen.GetStyle(), pen.GetCap(),
                   pen.GetJoin());
}

inline QBrush MakeBrush(const wxBrush &brush)
{
    if (!brush.IsOk() || brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT)
        return QBrush(Qt::NoBrush);
    return QBrush(ToQt(brush.GetColour()));
}

} // namespace qt_graphics

class wxGraphicsPenInfo
{
public:
    explicit wxGraphicsPenInfo(const wxColour &colour = wxColour(), double width = 1.0,
                               wxPenStyle style = wxPENSTYLE_SOLID)
        : m_colour(colour), m_width(width), m_style(style)
    {
    }

    wxGraphicsPenInfo &Width(double width)
    {
        m_width = width;
        return *this;
    }
    wxGraphicsPenInfo &Style(wxPenStyle style)
    {
        m_style = style;
        return *this;
    }
    wxGraphicsPenInfo &Cap(wxPenCap cap)
    {
        m_cap = cap;
        return *this;
    }
    wxGraphicsPenInfo &Join(wxPenJoin join)
    {
        m_join = join;
        return *this;
    }

    QPen ToQt() const { return qt_graphics::MakePen(m_colour, m_width, m_style, m_cap, m_join); }

private:
    wxColour m_colour;
    double m_width;
    wxPenStyle m_style;
    wxPenCap m_cap = wxCAP_ROUND;
    wxPenJoin m_join = wxJOIN_ROUND;
};

class wxGraphicsPen
{
public:
    wxGraphicsPen() : m_pen(Qt::NoPen) {}
    explicit wxGraphicsPen(const QPen &pen) : m_pen(pen) {}

    const QPen &ToQt() const { return m_pen; }

private:
    QPen m_pen;
};

class wxGraphicsBrush
{
public:
    wxGraphicsBrush() : m_brush(Qt::NoBrush) {}
    explicit wxGraphicsBrush(const QBrush &brush) : m_brush(brush) {}

    const QBrush &ToQt() const { return m_brush; }

private:
    QBrush m_brush;
};

class wxGraphicsPath
{
public:
    void MoveToPoint(double x, double y) { m_path.moveTo(x, y); }
    void AddLineToPoint(double x, double y) { m_path.lineTo(x, y); }
    void AddQuadCurveToPoint(double cx, double cy, double x, double y)
    {
        m_path.quadTo(cx, cy, x, y);
    }
    void AddCurveToPoint(double cx1, double cy1, double cx2, double cy2, double x, double y)
    {
        m_path.cubicTo(cx1, cy1, cx2, cy2, x, y);
    }
    void AddArc(double x, double y, double r, double startAngle, double endAngle,
                bool clockwise)
    {
        using qt_graphics::kPi;
        if (clockwise) {
            while (endAngle < startAngle)
                endAngle += 2.0 * kPi;
        }
        else {
            while (endAngle > startAngle)
                endAngle -= 2.0 * kPi;
        }
        const QPointF start(x + r * std::cos(startAngle), y + r * std::sin(startAngle));
        // Like wxWidgets: a line from the current point to the arc, or a
        // move there when the path is still empty.
        if (m_path.elementCount() == 0)
            m_path.moveTo(start);
        else
            m_path.lineTo(start);
        m_path.arcTo(QRectF(x - r, y - r, 2.0 * r, 2.0 * r), -startAngle * 180.0 / kPi,
                     -(endAngle - startAngle) * 180.0 / kPi);
    }
    void AddCircle(double x, double y, double r) { m_path.addEllipse(QPointF(x, y), r, r); }
    void AddEllipse(double x, double y, double w, double h)
    {
        m_path.addEllipse(QRectF(x, y, w, h));
    }
    void AddRectangle(double x, double y, double w, double h)
    {
        m_path.addRect(QRectF(x, y, w, h));
    }
    void AddRoundedRectangle(double x, double y, double w, double h, double radius)
    {
        m_path.addRoundedRect(QRectF(x, y, w, h), radius, radius);
    }
    void CloseSubpath() { m_path.closeSubpath(); }

    QPainterPath ToQt(wxPolygonFillMode fillStyle) const
    {
        QPainterPath path(m_path);
        path.setFillRule(fillStyle == wxWINDING_RULE ? Qt::WindingFill : Qt::OddEvenFill);
        return path;
    }

private:
    QPainterPath m_path;
};

class wxGraphicsContext
{
public:
    static wxGraphicsContext *Create(const wxDC &dc) { return CreateFromUnknownDC(dc); }

    static wxGraphicsContext *CreateFromUnknownDC(const wxDC &dc)
    {
        QPainter *painter = static_cast<QPainter *>(dc.GetHandle());
        if (!painter || !painter->isActive())
            return nullptr;
        return new wxGraphicsContext(painter, nullptr);
    }

    // Draws on the image, which gets the result when the context is deleted.
    static wxGraphicsContext *Create(wxImage &image)
    {
        if (!image.IsOk())
            return nullptr;
        return new wxGraphicsContext(nullptr, &image);
    }

    ~wxGraphicsContext()
    {
        if (m_image) {
            m_own_painter.reset();
            WriteBack();
        }
        else {
            m_painter->restore();
        }
    }

    wxGraphicsContext(const wxGraphicsContext &) = delete;
    wxGraphicsContext &operator=(const wxGraphicsContext &) = delete;

    bool SetAntialiasMode(wxAntialiasMode mode)
    {
        const bool on = mode != wxANTIALIAS_NONE;
        m_painter->setRenderHint(QPainter::Antialiasing, on);
        m_painter->setRenderHint(QPainter::TextAntialiasing, on);
        return true;
    }

    wxGraphicsPath CreatePath() const { return wxGraphicsPath(); }

    wxGraphicsPen CreatePen(const wxGraphicsPenInfo &info) const
    {
        return wxGraphicsPen(info.ToQt());
    }
    wxGraphicsPen CreatePen(const wxPen &pen) const
    {
        return wxGraphicsPen(qt_graphics::MakePen(pen));
    }

    wxGraphicsBrush CreateRadialGradientBrush(double xo, double yo, double xc, double yc,
                                              double radius, const wxColour &oColour,
                                              const wxColour &cColour) const
    {
        QRadialGradient gradient(QPointF(xc, yc), radius, QPointF(xo, yo));
        gradient.setColorAt(0.0, qt_graphics::ToQt(oColour));
        gradient.setColorAt(1.0, qt_graphics::ToQt(cColour));
        return wxGraphicsBrush(QBrush(gradient));
    }

    void SetPen(const wxGraphicsPen &pen) { m_pen = pen.ToQt(); }
    void SetPen(const wxPen &pen) { m_pen = qt_graphics::MakePen(pen); }
    void SetBrush(const wxGraphicsBrush &brush) { m_brush = brush.ToQt(); }
    void SetBrush(const wxBrush &brush) { m_brush = qt_graphics::MakeBrush(brush); }

    void SetFont(const wxFont &font, const wxColour &colour)
    {
        m_font = font.GetHandle();
        m_text_colour = qt_graphics::ToQt(colour);
    }

    void GetTextExtent(const wxString &text, double *width, double *height,
                       double *descent = nullptr, double *externalLeading = nullptr) const
    {
        const QFontMetricsF metrics(m_font, m_painter->device());
        if (width)
            *width = metrics.horizontalAdvance(qt_graphics::ToQt(text));
        if (height)
            *height = metrics.height();
        if (descent)
            *descent = metrics.descent();
        if (externalLeading)
            *externalLeading = metrics.leading();
    }

    // Like wxWidgets, (x, y) is the top left corner of the text.
    void DrawText(const wxString &text, double x, double y)
    {
        m_painter->setFont(m_font);
        m_painter->setPen(QPen(m_text_colour));
        const QFontMetricsF metrics(m_font, m_painter->device());
        m_painter->drawText(QPointF(x, y + metrics.ascent()), qt_graphics::ToQt(text));
    }

    void StrokePath(const wxGraphicsPath &path)
    {
        m_painter->strokePath(path.ToQt(wxODDEVEN_RULE), m_pen);
    }
    void FillPath(const wxGraphicsPath &path, wxPolygonFillMode fillStyle = wxODDEVEN_RULE)
    {
        m_painter->fillPath(path.ToQt(fillStyle), m_brush);
    }
    void DrawPath(const wxGraphicsPath &path, wxPolygonFillMode fillStyle = wxODDEVEN_RULE)
    {
        Apply();
        m_painter->drawPath(path.ToQt(fillStyle));
    }

    void StrokeLine(double x1, double y1, double x2, double y2)
    {
        m_painter->setPen(m_pen);
        m_painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }
    void DrawRectangle(double x, double y, double w, double h)
    {
        Apply();
        m_painter->drawRect(QRectF(x, y, w, h));
    }
    void DrawRoundedRectangle(double x, double y, double w, double h, double radius)
    {
        Apply();
        m_painter->drawRoundedRect(QRectF(x, y, w, h), radius, radius);
    }
    void DrawEllipse(double x, double y, double w, double h)
    {
        Apply();
        m_painter->drawEllipse(QRectF(x, y, w, h));
    }

    void Translate(double dx, double dy) { m_painter->translate(dx, dy); }
    void Rotate(double angle) { m_painter->rotate(angle * 180.0 / qt_graphics::kPi); }
    void Scale(double xScale, double yScale) { m_painter->scale(xScale, yScale); }

private:
    wxGraphicsContext(QPainter *painter, wxImage *image) : m_image(image)
    {
        if (image) {
            ReadImage();
            m_own_painter.reset(new QPainter(&m_qimage));
            m_painter = m_own_painter.get();
        }
        else {
            m_painter = painter;
            m_painter->save();
        }
        SetAntialiasMode(wxANTIALIAS_DEFAULT);
    }

    void Apply()
    {
        m_painter->setPen(m_pen);
        m_painter->setBrush(m_brush);
    }

    void ReadImage()
    {
        const int w = m_image->GetWidth(), h = m_image->GetHeight();
        const unsigned char *rgb = m_image->GetData();
        const unsigned char *alpha = m_image->HasAlpha() ? m_image->GetAlpha() : nullptr;
        m_qimage = QImage(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(m_qimage.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int i = y * w + x;
                line[x] = qRgba(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2],
                                alpha ? alpha[i] : 255);
            }
        }
    }

    void WriteBack()
    {
        const int w = m_image->GetWidth(), h = m_image->GetHeight();
        if (!m_image->HasAlpha())
            m_image->InitAlpha();
        unsigned char *rgb = m_image->GetData();
        unsigned char *alpha = m_image->GetAlpha();
        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(m_qimage.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const int i = y * w + x;
                rgb[3 * i] = qRed(line[x]);
                rgb[3 * i + 1] = qGreen(line[x]);
                rgb[3 * i + 2] = qBlue(line[x]);
                alpha[i] = qAlpha(line[x]);
            }
        }
    }

    QPainter *m_painter = nullptr;
    std::unique_ptr<QPainter> m_own_painter;
    wxImage *m_image;
    QImage m_qimage;
    QPen m_pen{Qt::NoPen};
    QBrush m_brush{Qt::NoBrush};
    QFont m_font;
    QColor m_text_colour{Qt::black};
};

#endif
