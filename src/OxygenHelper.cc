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

#include <QGuiApplication>
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
    // Same contrast as the Oxygen style (read through oxygenrc like Oxygen's DecoHelper).
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("oxygenrc"));
    m_contrast = KColorScheme::contrastF(config);
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
    return highThreshold(color) ? color : KColorScheme::shade(color, KColorScheme::LightShade, m_contrast);
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
    return KStatefulBrush(KColorScheme::View, KColorScheme::FocusColor, KSharedConfig::openConfig(QStringLiteral("oxygenrc"))).brush(palette).color();
}

QColor OxygenHelper::hoverColor(const QPalette &palette) const
{
    return KStatefulBrush(KColorScheme::View, KColorScheme::HoverColor, KSharedConfig::openConfig(QStringLiteral("oxygenrc"))).brush(palette).color();
}

QColor OxygenHelper::negativeTextColor(const QPalette &palette) const
{
    return KStatefulBrush(KColorScheme::View, KColorScheme::NegativeText, KSharedConfig::openConfig(QStringLiteral("oxygenrc"))).brush(palette).color();
}

void OxygenHelper::renderWindowBackground(QPainter *painter, const QRect &clipRect, const QRect &windowRect, const QColor &color, int yShift) const
{
    if (!windowRect.isValid()) {
        return;
    }

    painter->save();
    if (clipRect.isValid()) {
        painter->setClipRect(clipRect, Qt::IntersectClip);
    }

    // upper linear gradient
    const int splitY = qMin(300.0, (3.0 * windowRect.height()) / 4.0);
    if (splitY + yShift > 0) {
        QRect upperRect = windowRect;
        upperRect.setHeight(splitY + yShift);
        QLinearGradient gradient(0, windowRect.top() + yShift, 0, windowRect.top() + splitY + yShift);
        gradient.setColorAt(0.0, backgroundTopColor(color));
        gradient.setColorAt(0.5, color);
        gradient.setColorAt(1.0, backgroundBottomColor(color));
        painter->fillRect(upperRect, gradient);
    }

    // lower flat part
    const QRect lowerRect = windowRect.adjusted(0, splitY + yShift, 0, 0);
    if (lowerRect.isValid()) {
        painter->fillRect(lowerRect, backgroundBottomColor(color));
    }

    // upper radial gradient
    const int radialW = qMin(600, windowRect.width());
    const int radialH = 64 + yShift;
    const QRect radialRect((windowRect.width() - radialW) / 2 + windowRect.x(), windowRect.y(), radialW, radialH);
    if (radialH > 0 && (!clipRect.isValid() || clipRect.intersects(radialRect))) {
        QRadialGradient gradient(64, radialH - 64, 64);
        QColor radialColor = backgroundRadialColor(color);
        radialColor.setAlpha(255);
        gradient.setColorAt(0, radialColor);
        radialColor.setAlpha(101);
        gradient.setColorAt(0.5, radialColor);
        radialColor.setAlpha(37);
        gradient.setColorAt(0.75, radialColor);
        radialColor.setAlpha(0);
        gradient.setColorAt(1, radialColor);

        // the gradient is defined in a 128 units wide window, stretched to the radial width
        painter->translate(radialRect.topLeft());
        painter->scale(qreal(radialW) / 128.0, 1.0);
        painter->fillRect(QRect(0, 0, 128, radialH), gradient);
    }

    painter->restore();
}

void OxygenHelper::drawShadow(QPainter &painter, const QColor &color, int size) const
{
    const qreal m = qreal(size - 2) * 0.5;
    const qreal offset = 0.8;
    const qreal k0 = (m - 4.0) / m;

    QRadialGradient shadowGradient(m + 1.0, m + offset + 1.0, m);
    for (int i = 0; i < 8; i++) {
        // sinusoidal gradient
        const qreal k1 = (k0 * qreal(8 - i) + qreal(i)) * 0.125;
        const qreal a = (std::cos(M_PI * i * 0.125) + 1.0) * 0.30;
        shadowGradient.setColorAt(k1, alphaColor(color, a * s_shadowGain));
    }

    shadowGradient.setColorAt(1.0, alphaColor(color, 0.0));
    painter.save();
    painter.setBrush(shadowGradient);
    painter.drawEllipse(QRectF(0, 0, size, size));
    painter.restore();
}

void OxygenHelper::drawOuterGlow(QPainter &painter, const QColor &color, int size) const
{
    const QRectF r(0, 0, size, size);
    const qreal m = qreal(size) * 0.5;
    const qreal width = 3;
    const qreal glowBias = 0.6;

    const qreal bias = glowBias * qreal(14) / size;

    // k0 is located at width - bias from the outer edge
    const qreal gm = m + bias - 0.9;
    const qreal k0 = (m - width + bias) / gm;
    QRadialGradient glowGradient(m, m, gm);
    for (int i = 0; i < 8; i++) {
        // k1 grows linearly from k0 to 1.0
        const qreal k1 = k0 + qreal(i) * (1.0 - k0) / 8.0;

        // a follows sqrt curve
        const qreal a = 1.0 - std::sqrt(qreal(i) / 8);
        glowGradient.setColorAt(k1, alphaColor(color, a));
    }

    // glow
    painter.save();
    painter.setBrush(glowGradient);
    painter.drawEllipse(r);

    // inside mask
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.setBrush(Qt::black);
    painter.drawEllipse(r.adjusted(width + 0.5, width + 0.5, -width - 1, -width - 1));
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

namespace
{
struct ShadowConfig {
    bool enabled = true;
    int size = 40;
    qreal verticalOffset = 0;
    QColor innerColor;
    QColor outerColor;
    bool useOuterColor = true;
    bool useAccentColor = false;
};

ShadowConfig loadShadowConfig(bool active)
{
    // same defaults as Oxygen's Active/InactiveShadowConfiguration
    ShadowConfig out;
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("oxygenrc")), active ? QStringLiteral("ActiveShadow") : QStringLiteral("InactiveShadow"));
    out.enabled = group.readEntry("Enabled", true);
    out.size = group.readEntry("ShadowSize", 40);
    out.verticalOffset = group.readEntry("VerticalOffset", active ? 0.0 : 0.2);
    out.innerColor = group.readEntry("InnerColor", active ? QColor(112, 239, 255) : QColor(0, 0, 0));
    out.outerColor = group.readEntry("OuterColor", active ? QColor(84, 167, 240) : QColor(0, 0, 0));
    out.useOuterColor = group.readEntry("UseOuterColor", active);
    out.useAccentColor = group.readEntry("UseAccentColor", active);
    return out;
}

QColor accentColor()
{
    return QGuiApplication::palette().color(QPalette::Active, QPalette::Accent);
}

qreal square(qreal x)
{
    return x * x;
}

qreal parabolic(qreal amplitude, qreal width, qreal x)
{
    return qMax(0.0, amplitude * (1.0 - square(x / width)));
}

qreal gaussian(qreal amplitude, qreal width, qreal x)
{
    return qMax(0.0, amplitude * (std::exp(-square(x / width) - 0.05)));
}
} // namespace

void OxygenHelper::renderShadowGradient(QPainter &painter, const QRectF &rect, const QRadialGradient &radialGradient, bool hasBorder) const
{
    if (hasBorder) {
        painter.setBrush(radialGradient);
        painter.drawRect(rect);
        return;
    }

    const qreal size = rect.width() / 2.0;
    const qreal hoffset = radialGradient.center().x() - size;
    const qreal voffset = radialGradient.center().y() - size;
    const qreal radius = radialGradient.radius();

    // load gradient stops
    const QGradientStops stops(radialGradient.stops());

    // draw ellipse for the upper rect
    {
        const QRectF r(hoffset, voffset, 2 * size - hoffset, size);
        painter.setBrush(radialGradient);
        painter.drawRect(r);
    }

    // draw square gradients for the lower rect
    {
        // vertical lines
        const QRectF r(hoffset, size + voffset, 2 * size - hoffset, 4);
        QLinearGradient lg(hoffset, 0.0, 2 * size + hoffset, 0.0);
        for (int i = 0; i < stops.size(); i++) {
            const QColor c(stops[i].second);
            const qreal xx(stops[i].first * radius);
            lg.setColorAt((size - xx) / (2.0 * size), c);
            lg.setColorAt((size + xx) / (2.0 * size), c);
        }

        painter.setBrush(lg);
        painter.drawRect(r);
    }

    {
        // horizontal line
        const QRectF r(size - 4 + hoffset, size + voffset, 8, size);
        QLinearGradient lg(0, voffset, 0, 2 * size + voffset);
        for (int i = 0; i < stops.size(); i++) {
            const QColor c(stops[i].second);
            const qreal xx(stops[i].first * radius);
            lg.setColorAt((size + xx) / (2.0 * size), c);
        }

        painter.setBrush(lg);
        painter.drawRect(r);
    }

    // bottom corners
    for (int side = 0; side < 2; ++side) {
        const bool left = (side == 0);
        const QRectF r(left ? hoffset : size + hoffset + 4, size + voffset + 4, size - 4, size);
        QRadialGradient corner(left ? size + hoffset - 4 : size + hoffset + 4, size + voffset + 4, radius);
        for (int i = 0; i < stops.size(); i++) {
            QColor c(stops[i].second);
            qreal xx(stops[i].first - 4.0 / radius);
            if (xx < 0) {
                if (i < stops.size() - 1) {
                    const qreal x1(stops[i + 1].first - 4.0 / radius);
                    c = KColorUtils::mix(c, stops[i + 1].second, -xx / (x1 - xx));
                }
                xx = 0;
            }

            corner.setColorAt(xx, c);
        }

        painter.setBrush(corner);
        painter.drawRect(r);
    }
}

QImage OxygenHelper::shadowImage(bool active, bool hasBorder) const
{
    static const qreal fixedSize = 25.5;

    const ShadowConfig activeConfig = loadShadowConfig(true);
    const ShadowConfig inactiveConfig = loadShadowConfig(false);
    const ShadowConfig &config = active ? activeConfig : inactiveConfig;
    if (!config.enabled || config.size <= 0) {
        return QImage();
    }

    // the image size is the one of the largest enabled shadow, like Oxygen
    qreal size = qMax(activeConfig.enabled ? activeConfig.size : 0, inactiveConfig.enabled ? inactiveConfig.size : 0);
    qreal shadowSize = config.size;

    // add overlap
    size += s_shadowOverlap;
    shadowSize += s_shadowOverlap;

    QPixmap shadow(int(size * 2), int(size * 2));
    shadow.fill(Qt::transparent);

    QPainter painter(&shadow);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    if (active) {
        {
            // inner (sharp) gradient
            const qreal gradientSize = qMin(shadowSize, (shadowSize + fixedSize) / 2);
            const qreal voffset = qMin(12.0 * (gradientSize * config.verticalOffset) / fixedSize, 4.0);

            QRadialGradient radialGradient(size, size + voffset, gradientSize);
            radialGradient.setColorAt(1, Qt::transparent);

            // gaussian shadow is used
            const int nPoints = int((10 * gradientSize) / fixedSize);
            QColor innerC = config.useAccentColor ? KColorUtils::lighten(accentColor(), 0.4) : config.innerColor;
            for (int i = 0; i < nPoints; i++) {
                const qreal x = qreal(i) / nPoints;
                innerC.setAlphaF(gaussian(0.85, 0.17, x));
                radialGradient.setColorAt(x, innerC);
            }

            painter.setBrush(radialGradient);
            renderShadowGradient(painter, shadow.rect(), radialGradient, hasBorder);
        }

        {
            // outer (spread) gradient
            const qreal gradientSize = shadowSize;
            const qreal voffset = qMin(12.0 * (gradientSize * config.verticalOffset) / fixedSize, 4.0);

            QRadialGradient radialGradient(size, size + voffset, gradientSize);
            radialGradient.setColorAt(1, Qt::transparent);

            // gaussian shadow is used
            const int nPoints = int((10 * gradientSize) / fixedSize);
            QColor outerC = config.useAccentColor ? accentColor() : (config.useOuterColor ? config.outerColor : config.innerColor);
            for (int i = 0; i < nPoints; i++) {
                const qreal x = qreal(i) / nPoints;
                outerC.setAlphaF(gaussian(0.46, 0.34, x));
                radialGradient.setColorAt(x, outerC);
            }

            painter.setBrush(radialGradient);
            painter.drawRect(shadow.rect());
        }
    } else {
        const QColor baseColor = config.useOuterColor ? config.outerColor : config.innerColor;
        {
            // inner (sharp gradient)
            const qreal gradientSize = qMin(shadowSize, fixedSize);
            const qreal voffset = 0.2;

            QRadialGradient radialGradient(size, size + voffset, gradientSize);
            radialGradient.setColorAt(1, Qt::transparent);

            // parabolic shadow is used
            const int nPoints = int((10 * gradientSize) / fixedSize);
            QColor c = baseColor;
            for (int i = 0; i < nPoints; i++) {
                const qreal x = qreal(i) / nPoints;
                c.setAlphaF(parabolic(1.0, 0.22, x));
                radialGradient.setColorAt(x, c);
            }

            painter.setBrush(radialGradient);
            renderShadowGradient(painter, shadow.rect(), radialGradient, hasBorder);
        }

        {
            // mid gradient
            const qreal gradientSize = qMin(shadowSize, (shadowSize + 2 * fixedSize) / 3);
            const qreal voffset = qMin(8.0 * (gradientSize * config.verticalOffset) / fixedSize, 4.0);

            QRadialGradient radialGradient(size, size + voffset, gradientSize);
            radialGradient.setColorAt(1, Qt::transparent);

            const int nPoints = int((10 * gradientSize) / fixedSize);
            QColor c = baseColor;
            for (int i = 0; i < nPoints; i++) {
                const qreal x = qreal(i) / nPoints;
                c.setAlphaF(gaussian(0.54, 0.21, x));
                radialGradient.setColorAt(x, c);
            }

            painter.setBrush(radialGradient);
            painter.drawRect(shadow.rect());
        }

        {
            // outer (spread) gradient
            const qreal gradientSize = shadowSize;
            const qreal voffset = qMin(20.0 * (gradientSize * config.verticalOffset) / fixedSize, 4.0);

            QRadialGradient radialGradient(size, size + voffset, gradientSize);
            radialGradient.setColorAt(1, Qt::transparent);

            const int nPoints = int((20 * gradientSize) / fixedSize);
            QColor c = baseColor;
            for (int i = 0; i < nPoints; i++) {
                const qreal x = qreal(i) / nPoints;
                c.setAlphaF(gaussian(0.155, 0.445, x));
                radialGradient.setColorAt(x, c);
            }

            painter.setBrush(radialGradient);
            painter.drawRect(shadow.rect());
        }
    }

    // mask
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.setBrush(Qt::black);
    painter.drawEllipse(QRectF(size - 3, size - 3, 6, 6));

    painter.end();
    return shadow.toImage();
}

} // namespace OxygenLim
