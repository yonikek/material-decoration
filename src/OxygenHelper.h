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
#include <QHash>
#include <QPalette>
#include <QPixmap>
#include <QRect>

class QPainter;

namespace OxygenLim
{

class OxygenHelper
{
public:
    static OxygenHelper *self();

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

    // Glossy round window button slab. size is the logical size, physicalSize
    // the size in device pixels.
    QPixmap windecoButton(const QColor &color, const QColor &glow, bool sunken, int size, int physicalSize) const;

private:
    OxygenHelper();

    bool lowThreshold(const QColor &color) const;
    bool highThreshold(const QColor &color) const;
    void drawShadow(QPainter &painter, const QColor &color, int size) const;
    void drawOuterGlow(QPainter &painter, const QColor &color, int size) const;

    qreal m_contrast = 0.7;
    qreal m_bgContrast = 0.9;
    qreal m_shadowGain = 1.0;
};

} // namespace OxygenLim
