/*
 * Copyright (C) 2025 Guido Iodice <guido[dot]iodice[at]gmail[dot]com>
 * Copyright (C) 2020 Chris Holland <zrenfire@gmail.com>
 * Copyright (C) 2018 Vlad Zagorodniy <vladzzag@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// own
#include "BuildConfig.h"
#include "AppMenuButtonGroup.h"
#include "InternalSettings.h"

// KDecoration
#include <KDecoration3/Decoration>

// Qt
#include <QHoverEvent>
#include <QMouseEvent>
#include <QRectF>
#include <QSharedPointer>
#include <QWheelEvent>
#include <memory>
#include <QVariant>
#include <QPainterPath>
#include <KDecoration3/DecorationShadow>
#include <QTimer>
#include <QElapsedTimer>

namespace KDecoration3
{
class DecorationButtonGroup;
}

namespace OxygenLim
{

class Button;
class TextButton;
class TilingPopup;
class MenuOverflowButton;

class Decoration : public KDecoration3::Decoration
{
    Q_OBJECT

public:
    Decoration(QObject *parent = nullptr, const QVariantList &args = QVariantList());
    ~Decoration() override;

    void paint(QPainter *painter, const QRectF &repaintRegion) override;
    bool showTilingPopup(const QRectF &anchor, bool fromHover = false);
    bool isTilingPopupVisible() const;

signals:
    void tilingPopupVisibilityChanged(bool visible);
    void tilingPopupUnavailable(const QString &message);

public slots:
    bool init() override;
    void reconfigure();

private slots:
    void onTabletModeChanged(bool mode);
    void closeTilingPopup();
    void cancelButtonActionsForDrag();

protected:
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    std::unique_ptr<TilingPopup> m_tilingPopup;
    bool m_tilingPopupOpening = false;
    quint64 m_tilingPopupRequest = 0;
    QTimer m_tilingPopupHoverMonitor;
    QElapsedTimer m_tilingPopupOutsideTime;

    void onSectionUnderMouseChanged(const Qt::WindowFrameSection value);
    void onSizeChanged();
    void onNextScaleChanged();
    void onWidthChanged();
    void onMaximizedChanged();
    void onShadedChanged();
    void onActiveChanged();
    void onAdjacentScreenEdgesChanged();
    void onSpacingChanged();

private:
    QRectF titleBarRect() const;
    QRectF centerRect() const;

    QPoint windowPos() const;

    // --- Oxygen metrics -------------------------------------------------
    // Size of the glossy round window buttons (Oxygen "buttonHeight").
    int buttonHeight() const;
    // Height of the caption/menu row (Oxygen "captionHeight").
    qreal captionHeight() const;
    // Vertical offset of the caption/button row from the top of the window.
    qreal rowTop() const;
    qreal buttonSpacing() const;
    qreal sideMargin() const;
    qreal borderSize(bool bottom = false) const;
    bool isMaximized() const;
    bool isMaximizedHorizontally() const;
    bool isMaximizedVertically() const;
    bool isShadowOnly() const;
    bool hasNoBorders() const;
    bool hasNoSideBorders() const;

    qreal leftOffset() const;
    qreal rightOffset() const;
    qreal bottomOffset() const;

    void setupMenu();
    void updateBlur();
    void updateBordersCornersBlurShadow();
    void updateResizeBorders();
    void updateTitleBar();
    void updateTitleBarHoverState();

    void setButtonGroupHeight(KDecoration3::DecorationButtonGroup *buttonGroup, qreal buttonHeight);
    void setButtonGroupHorzPadding(KDecoration3::DecorationButtonGroup *buttonGroup, qreal value);

    void updateButtonHeight();
    void updateButtonsGeometry();
    void updateButtonsGeometryDelayed();
    void invalidateCaptionCache() const;
    void setButtonGroupAnimation(KDecoration3::DecorationButtonGroup *buttonGroup, bool enabled, int duration);
    void updateButtonAnimation();
    void updateShadow();
    void updateBorderRadius();

    bool hideTitleBar() const;
    void setInternalSettings(const QSharedPointer<InternalSettings> &settings);
    void applySettings();

    bool menuAlwaysShow() const;
    bool useSystemMenuFont() const;
    bool hamburgerMenu() const;
    bool searchEnabled() const;
    bool showDisabledActions() const;
    bool searchIgnoreTopLevel() const;
    bool searchIgnoreSubMenus() const;
    bool searchFuzzyMatching() const;
    bool animationsEnabled() const;
    int animationsDuration() const;
    bool dragFromButtonsEnabled() const;
    bool hideCaptionWhenLimitedSpace() const;
    bool showCaptionOnHover() const;
    // Total height of the title bar, i.e. the top border (Oxygen "borderTop").
    qreal titleBarHeight() const;
    qreal appMenuCaptionSpacing() const;
    qreal captionMinWidth() const;

    bool titleBarIsHovered() const;

    QFont menuFont() const;
    qreal getMenuTextWidth(const QString &text, bool showMnemonic = false) const;
    bool isMenuOnRight() const;

    void initDragMove(const QPoint pos);
    void resetDragMove();
    bool dragMoveTick(const QPoint pos);
    void unPressAllButtons();

    // Colours follow the window palette, like the Oxygen style does.
    QColor titleBarBackgroundColor() const;
    QColor titleBarOpaqueBackgroundColor() const { return titleBarBackgroundColor(); }
    QColor titleBarForegroundColor() const;

    void paintWindowBackground(QPainter *painter, const QRectF &repaintRegion) const;
    void paintCaption(QPainter *painter, const QRectF &repaintRegion) const;
    void paintButtons(QPainter *painter, const QRectF &repaintRegion) const;

    KDecoration3::DecorationButtonGroup *m_leftButtons = nullptr;
    KDecoration3::DecorationButtonGroup *m_rightButtons = nullptr;
    AppMenuButtonGroup *m_menuButtons = nullptr;

    QSharedPointer<InternalSettings> m_internalSettings;

    QPoint m_pressedPoint;

    mutable QRectF m_captionRect;
    mutable bool m_captionLimited = false;
    mutable struct {
        QString fullCaption;
        qreal textWidth = -1.0;
        qreal availableWidth = -1.0;
        Qt::Alignment alignment = Qt::AlignCenter;
        QString elidedCaption;
        QFont font;
    } m_captionCache;
    
    QPointF m_lastHoverPos;
    bool m_titleBarHoverActive = false;
    bool m_tabletMode = false;

    friend class AppMenuButtonGroup;
    friend class NavigableMenu;
    friend class Button;
    friend class AppIconButton;
    friend class AppMenuButton;
    friend class TextButton;
};

} // namespace OxygenLim
