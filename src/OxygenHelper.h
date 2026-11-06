/*
 * oxygen-decoration-lim
 *
 * Colour and drawing helpers reproducing the look of the Oxygen window
 * decoration (window background gradient, glossy window buttons, contrast
 * colours). The colour maths follows the one of the Oxygen style helper
 * (SPDX-FileCopyrightText: 2007-2013 Casper Boemann, Matthew Woehlke,
 * Long Huynh Huu, Hugo Pereira Da Costa), which is licensed under the
 * LGPL-2.0-only; this is a re-implementation limited to what the window
 * decoration needs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QColor>
#include <QCache>
#include <QHash>
#include <QObject>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QRect>
#include <QRgb>

#include <memory>

class KStatefulBrush;
class QPainter;
class QRadialGradient;

namespace OxygenLim
{

// Shared helper: derived colours, window background and button rendering, with
// caches. It also watches the application palette (colour scheme, accent
// colour) once for all decorations and notifies them through a signal, after
// its own caches have been dropped.
class OxygenHelper : public QObject
{
    Q_OBJECT

public:
    static OxygenHelper *self();
    ~OxygenHelper() override;

    // Drops every cached colour, brush and button pixmap.
    void invalidate();

    // Re-reads oxygenrc (contrast, shadow settings). Cached shadows must be
    // dropped by the caller.
    void reconfigure();

    // Colour palette derived from a base colour.
    QColor calcLightColor(const QColor &color) const;
    QColor calcDarkColor(const QColor &color) const;
    QColor calcShadowColor(const QColor &color) const;
    QColor backgroundTopColor(const QColor &color) const;
    QColor backgroundBottomColor(const QColor &color) const;
    QColor backgroundRadialColor(const QColor &color) const;
    static QColor alphaColor(QColor color, qreal alpha);

    // Colours taken from the colour scheme.
    QColor focusColor(const QPalette &palette) const;
    QColor hoverColor(const QPalette &palette) const;
    QColor negativeTextColor(const QPalette &palette) const;

    // Paints the Oxygen window background (vertical gradient + radial glow).
    // clipRect and windowRect are in decoration coordinates.
    void renderWindowBackground(QPainter *painter, const QRect &clipRect, const QRect &windowRect, const QColor &color, int yShift) const;

    // Oxygen window shadow (see Oxygen::ShadowCache). Returns a square image
    // of twice shadowSize(); a null image means shadows are disabled for
    // that state. The shadow settings are read from oxygenrc.
    QImage shadowImage(bool active, bool hasBorder) const;

    // Cross-fade between the inactive and active shadows (opacity 0..1).
    // A null image means one of the two shadows is disabled.
    QImage animatedShadowImage(bool hasBorder, qreal opacity) const;

    // Glossy round window button slab (cached). size is the logical size; the
    // returned pixmap has size * devicePixelRatio physical pixels and its
    // device pixel ratio already set.
    QPixmap windecoButton(const QColor &color, const QColor &glow, bool sunken, int size, qreal devicePixelRatio) const;

Q_SIGNALS:
    // Emitted (deferred) after the application palette has changed.
    void applicationPaletteChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    OxygenHelper();

    QColor computeLightColor(const QColor &color) const;
    QColor computeDarkColor(const QColor &color) const;
    QColor computeShadowColor(const QColor &color) const;
    QColor computeBackgroundTopColor(const QColor &color) const;
    QColor computeBackgroundBottomColor(const QColor &color) const;
    QColor computeBackgroundRadialColor(const QColor &color) const;
    QPixmap renderWindecoButton(const QColor &color, const QColor &glow, bool sunken, int physicalSize) const;

    bool lowThreshold(const QColor &color) const;
    bool highThreshold(const QColor &color) const;
    void renderShadowGradient(QPainter &painter, const QRectF &rect, const QRadialGradient &gradient, bool hasBorder) const;
    void drawShadow(QPainter &painter, const QColor &color, int size) const;
    void drawOuterGlow(QPainter &painter, const QColor &color, int size) const;

    qreal m_contrast = 0.7;
    qreal m_bgContrast = 0.9;
    bool m_paletteUpdatePending = false;

    mutable QHash<QRgb, QColor> m_lightCache;
    mutable QHash<QRgb, QColor> m_darkCache;
    mutable QHash<QRgb, QColor> m_shadowColorCache;
    mutable QHash<QRgb, QColor> m_bgTopCache;
    mutable QHash<QRgb, QColor> m_bgBottomCache;
    mutable QHash<QRgb, QColor> m_bgRadialCache;
    mutable QCache<quint64, QPixmap> m_buttonCache;
    mutable std::unique_ptr<KStatefulBrush> m_focusBrush;
    mutable std::unique_ptr<KStatefulBrush> m_hoverBrush;
    mutable std::unique_ptr<KStatefulBrush> m_negativeBrush;

    static constexpr qreal s_shadowGain = 1.5;
    static constexpr int s_shadowOverlap = 4;
};

} // namespace OxygenLim
