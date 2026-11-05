/*
 * oxygen-decoration-lim
 *
 * See OxygenHelper.h for the licensing notes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "OxygenHelper.h"

#include <KColorScheme>
#include <KColorUtils>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KStatefulBrush>

#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

#include <cmath>

namespace OxygenLim
{

OxygenHelper *OxygenHelper::self()
{
    static OxygenHelper s_helper;
    return &s_helper;
}

OxygenHelper::OxygenHelper()
{
    // Same contrast setting as the Oxygen style (KDE/contrast, 0..10).
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("KDE"));
    m_contrast = qBound(0.0, group.readEntry("contrast", 7) / 10.0, 1.0);
    m_bgContrast = qMin(1.0, 0.9 * m_contrast / 0.7);
}

QColor OxygenHelper::alphaColor(QColor color, qreal alpha)
{
    if (alpha >= 0 && alpha < 1.0) {
        color.setAlphaF(alpha * color.alphaF());
    }
    return color;
}

bool OxygenHelper::lowThreshold(const QColor &color) const
{
    const QColor darker(KColorScheme::shade(color, KColorScheme::MidShade, 0.5));
    return KColorUtils::luma(darker) > KColorUtils::luma(color);
}

bool OxygenHelper::highThreshold(const QColor &color) const
{
    const QColor lighter(KColorScheme::shade(color, KColorScheme::LightShade, 0.5));
    return KColorUtils::luma(lighter) < KColorUtils::luma(color);
}

QColor OxygenHelper::calcLightColor(const QColor &color) const
{
    return KColorScheme::shade(color, KColorScheme::LightShade, m_contrast);
}

QColor OxygenHelper::calcDarkColor(const QColor &color) const
{
    return lowThreshold(color) ? KColorUtils::mix(calcLightColor(color), color, 0.3 + 0.7 * m_contrast)
                               : KColorScheme::shade(color, KColorScheme::MidShade, m_contrast);
}

QColor OxygenHelper::calcShadowColor(const QColor &color) const
{
    return lowThreshold(color) ? KColorUtils::mix(Qt::black, color, color.alphaF())
                               : KColorScheme::shade(KColorUtils::mix(Qt::black, color, color.alphaF()), KColorScheme::ShadowShade, m_contrast);
}

QColor OxygenHelper::backgroundTopColor(const QColor &color) const
{
    if (lowThreshold(color)) {
        return KColorScheme::shade(color, KColorScheme::MidlightShade, 0.0);
    }
    const qreal my = KColorUtils::luma(KColorScheme::shade(color, KColorScheme::LightShade, 0.0));
    const qreal by = KColorUtils::luma(color);
    return KColorUtils::shade(color, (my - by) * m_bgContrast);
}

QColor OxygenHelper::backgroundBottomColor(const QColor &color) const
{
    const QColor midColor(KColorScheme::shade(color, KColorScheme::MidShade, 0.0));
    if (lowThreshold(color)) {
        return midColor;
    }
    const qreal by = KColorUtils::luma(color);
    const qreal my = KColorUtils::luma(midColor);
    return KColorUtils::shade(color, (my - by) * m_bgContrast);
}

QColor OxygenHelper::backgroundRadialColor(const QColor &color) const
{
    if (lowThreshold(color)) {
        return KColorScheme::shade(color, KColorScheme::LightShade, 0.0);
    }
    if (highThreshold(color)) {
        return color;
    }
    return KColorScheme::shade(color, KColorScheme::LightShade, m_bgContrast);
}

QColor OxygenHelper::focusColor(const QPalette &palette) const
{
    return KStatefulBrush(KColorScheme::View, KColorScheme::FocusColor).brush(palette).color();
}

QColor OxygenHelper::hoverColor(const QPalette &palette) const
{
    return KStatefulBrush(KColorScheme::View, KColorScheme::HoverColor).brush(palette).color();
}

QColor OxygenHelper::negativeTextColor(const QPalette &palette) const
{
    return KStatefulBrush(KColorScheme::View, KColorScheme::NegativeText).brush(palette).color();
}

void OxygenHelper::renderWindowBackground(QPainter *painter, const QRect &clipRect, const QRect &windowRect, const QColor &color, int yShift) const
{
    Q_UNUSED(yShift)
    if (!windowRect.isValid()) {
        return;
    }

    // The gradient spans the upper part of the window, the rest is flat.
    const int splitY = qMin(200, (3 * windowRect.height()) / 4);
    const QRect r = (clipRect.isValid() ? clipRect.intersected(windowRect) : windowRect);
    if (!r.isValid()) {
        return;
    }

    painter->save();
    painter->setClipRect(r, Qt::IntersectClip);

    // vertical gradient
    const int top = windowRect.top();
    const int gradientTop = top;
    const int gradientBottom = top + splitY;
    if (r.top() < gradientBottom) {
        QLinearGradient gradient(0, gradientTop, 0, gradientBottom);
        gradient.setColorAt(0.0, backgroundTopColor(color));
        gradient.setColorAt(1.0, backgroundBottomColor(color));
        painter->fillRect(QRect(r.left(), r.top(), r.width(), qMin(gradientBottom, r.bottom() + 1) - r.top()), gradient);
    }

    // lower part
    if (r.bottom() + 1 > gradientBottom) {
        const int y = qMax(r.top(), gradientBottom);
        painter->fillRect(QRect(r.left(), y, r.width(), r.bottom() + 1 - y), backgroundBottomColor(color));
    }

    // radial glow at the top centre of the window
    const int radialWidth = qMin(600, windowRect.width());
    const int radialHeight = 64;
    const QRect radialRect(windowRect.center().x() - radialWidth / 2, windowRect.top(), radialWidth, radialHeight);
    if (radialRect.intersects(r)) {
        QColor radialColor = backgroundRadialColor(color);
        QRadialGradient gradient(0.5 * radialWidth, 0, 0.5 * radialWidth);
        radialColor.setAlpha(255);
        gradient.setColorAt(0.0, radialColor);
        radialColor.setAlpha(101);
        gradient.setColorAt(0.5, radialColor);
        radialColor.setAlpha(37);
        gradient.setColorAt(0.75, radialColor);
        radialColor.setAlpha(0);
        gradient.setColorAt(1.0, radialColor);

        painter->translate(radialRect.topLeft());
        // flatten the circle into an ellipse of the requested height
        painter->scale(1.0, qreal(radialHeight) / (0.5 * radialWidth));
        painter->fillRect(QRectF(0, 0, radialWidth, 0.5 * radialWidth), gradient);
    }

    painter->restore();
}

void OxygenHelper::drawShadow(QPainter &painter, const QColor &color, int size) const
{
    const qreal m = qreal(size) * 0.5;
    const qreal offset = 0.8;
    const qreal k0 = (m - 4.0) / m;
    QRadialGradient shadowGradient(m, m + offset, m);
    for (int i = 0; i < 8; i++) {
        // sinusoidal gradient
        const qreal k1 = (qreal(k0 * i) + qreal(8 - i)) * 0.125;
        const qreal a = (std::cos(M_PI * i * 0.125) + 1.0) * 0.30;
        shadowGradient.setColorAt(k1, alphaColor(color, a * m_shadowGain));
    }
    shadowGradient.setColorAt(1.0, alphaColor(color, 0.0));
    painter.setBrush(shadowGradient);
    painter.drawEllipse(QRectF(0, 0, size, size));
}

void OxygenHelper::drawOuterGlow(QPainter &painter, const QColor &color, int size) const
{
    const QRectF r(0, 0, size, size);
    const qreal m = qreal(size) * 0.5;
    const qreal width = 3;
    const qreal bias = 0.6;
    const qreal k0 = (m - width + bias) / m;
    QRadialGradient glowGradient(m, m, m - 0.5);
    for (int i = 0; i < 8; i++) {
        // parabolic gradient
        const qreal k1 = k0 + qreal(i) * (1.0 - k0) / 8.0;
        const qreal a = 1.0 - std::sqrt(qreal(i) / 8);
        glowGradient.setColorAt(k1, alphaColor(color, a));
    }
    glowGradient.setColorAt(1.0, alphaColor(color, 0.0));

    painter.save();
    painter.setBrush(glowGradient);
    painter.drawEllipse(r);
    painter.restore();
}

QPixmap OxygenHelper::windecoButton(const QColor &color, const QColor &glow, bool sunken, int size, int physicalSize) const
{
    Q_UNUSED(size)
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHints(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setWindow(0, 0, 21, 21);

    // button shadow
    if (color.isValid()) {
        p.save();
        p.translate(0, -0.2);
        drawShadow(p, calcShadowColor(color), 21);
        p.restore();
    }

    // button glow
    if (glow.isValid()) {
        p.save();
        p.translate(0, -0.2);
        drawOuterGlow(p, glow, 21);
        p.restore();
    }

    // button slab
    p.translate(0, 1);
    p.setWindow(0, 0, 18, 18);
    if (color.isValid()) {
        p.translate(0, (0.5 - 0.668));

        const QColor light(calcLightColor(color));
        const QColor dark(calcDarkColor(color));

        {
            // plain background
            QLinearGradient lg(0, 1.665, 0, (12.33 + 1.665));
            if (sunken) {
                lg.setColorAt(1, light);
                lg.setColorAt(0, dark);
            } else {
                lg.setColorAt(0, light);
                lg.setColorAt(1, dark);
            }

            const QRectF r(0.5 * (18 - 12.33), 1.665, 12.33, 12.33);
            p.setBrush(lg);
            p.drawEllipse(r);
        }

        {
            // outline circle
            const qreal penWidth = 0.7;
            QLinearGradient lg(0, 1.665, 0, (2.0 * 12.33 + 1.665));
            lg.setColorAt(0, light);
            lg.setColorAt(1, dark);
            const QRectF r(0.5 * (18 - 12.33 + penWidth), (1.665 + penWidth), (12.33 - penWidth), (12.33 - penWidth));
            p.setPen(QPen(lg, penWidth));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r);
        }
    }

    p.end();
    return pixmap;
}

} // namespace OxygenLim
