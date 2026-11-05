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

// own
#include "BuildConfig.h"
#include "Button.h"
#include "DrawHelper.h"
#include "OxygenHelper.h"
#include "LongPressAction.h"
#include "TilingController.h"
#include "OxygenLim.h"
#include "Decoration.h"

#include "AppIconButton.h"
#include "ApplicationMenuButton.h"
#include "OnAllDesktopsButton.h"
#include "ContextHelpButton.h"
#include "ShadeButton.h"
#include "KeepAboveButton.h"
#include "KeepBelowButton.h"
#include "CloseButton.h"
#include "MaximizeButton.h"
#include "MinimizeButton.h"

#if HAVE_EXCLUDE_FROM_CAPTURE
#include "ExcludeFromCaptureButton.h"
#endif

#include "TextButton.h"
#include "AppMenuButton.h"
#include "AppMenuButtonGroup.h"

// KDecoration
#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationButton>
#include <KDecoration3/ScaleHelpers>

// KF
#include <KColorUtils>
#include <KLocalizedString>

// Qt
#include <QCoreApplication>
#include <QDebug>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QVariantAnimation>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QCursor>
#include <QToolTip>
#include <QScopedValueRollback>
#include <QApplication>
#include <cmath>

#define UPDATE_GEOM() update(geometry().adjusted(-1, -1, 1, 1))


namespace OxygenLim
{

namespace
{

KDecoration3::DecorationButtonType buttonTypeFromArgs(const QVariantList &args)
{
    return args.value(0).value<KDecoration3::DecorationButtonType>();
}

Decoration *decorationFromArgs(const QVariantList &args)
{
    return args.value(1).value<Decoration *>();
}

}   
    
Button::Button(KDecoration3::DecorationButtonType type, Decoration *decoration, QObject *parent)
    : DecorationButton(type, decoration, parent)
    , m_animationEnabled(true)
    , m_animation(new QVariantAnimation(this))
    , m_opacity(1)
    , m_transitionValue(0)
    , m_padding()
    , m_isGtkButton(false)
    , m_holdTimer(new QTimer(this))
    , m_penScale(1.0)
{
    m_holdTimer->setSingleShot(true);
    connect(m_holdTimer, &QTimer::timeout, this, &Button::handleHoldTimeout);
    
    if (!decoration) {
        qWarning() << "Button created without a valid decoration";
        return;
    }


    if (QCoreApplication::applicationName() == QStringLiteral("kded6")) {
        // See: https://github.com/Zren/oxygen-decoration-lim/issues/22
        // kde-gtk-config has a kded module which renders the buttons to svgs for gtk.
        m_isGtkButton = true;
    }

    // Animation based on SierraBreezeEnhanced
    // https://github.com/kupiqu/SierraBreezeEnhanced/blob/master/breezebutton.cpp#L45
    // The GTK bridge needs animations disabled to render hover states. See Issue #50.
    // https://invent.kde.org/plasma/kde-gtk-config/-/blob/master/kded/kwin_bridge/dummydecorationbridge.cpp#L35
    m_animationEnabled = !m_isGtkButton && decoration->animationsEnabled();
    m_animation->setDuration(decoration->animationsDuration());
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setEasingCurve(QEasingCurve::InOutCubic);
    
    
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setTransitionValue(value.toReal());
    });
    
   
    if (type == KDecoration3::DecorationButtonType::Maximize) {
        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setSingleShot(true);
        connect(m_hoverTimer, &QTimer::timeout, this, [this, decoration] {
            if (isHovered() && isEnabled() && isVisible() &&
                QApplication::mouseButtons() == Qt::NoButton &&
                decoration->m_internalSettings->tilingPopupHoverEnabled()) {
                decoration->showTilingPopup(geometry(), true);
            }
        });
        connect(this, &Button::hoveredChanged, this, [this, decoration](bool hovered) {
            m_hoverTimer->stop();
            if (hovered && decoration->m_internalSettings->tilingPopupHoverEnabled()) {
                // The base button has already requested its tooltip on this signal.
                decoration->requestHideToolTip();
                if (!isPressed()) m_hoverTimer->start(decoration->m_internalSettings->tilingPopupHoverDelay());
            }
        });
        connect(this, &Button::pressedChanged, this, [this] {
            if (isPressed()) m_hoverTimer->stop();
        });
        connect(decoration, &Decoration::tilingPopupUnavailable, this, [this](const QString &message) {
            if (m_longPressToolTipSuspended) m_longPressWarning = message;
        });
        connect(decoration, &Decoration::tilingPopupVisibilityChanged, this, &Button::setTilingPopupOpen);
        setTilingPopupOpen(decoration->isTilingPopupVisible());
    }

    if (type == KDecoration3::DecorationButtonType::Minimize ||
        type == KDecoration3::DecorationButtonType::Maximize ||
        type == KDecoration3::DecorationButtonType::Close) {
        m_toolTipTimer = new QTimer(this);
        m_toolTipTimer->setSingleShot(true);
        connect(m_toolTipTimer, &QTimer::timeout, this, &Button::refreshLongPressToolTip);
        connect(this, &Button::hoveredChanged, this, [this](bool hovered) {
            if (hovered && !m_restoringHover) {
                m_longPressToolTipSuspended = false;
                m_longPressWarning.clear();
            }
            m_toolTipText.clear();
            ++m_toolTipGeneration;
            // Another button's leave event can hide the tooltip later in the
            // same dispatch. Refresh after the group's hover events finish.
            if (hovered) m_toolTipTimer->start(0);
            else refreshLongPressToolTip();
        });
        connect(this, &Button::pressedChanged, this, &Button::refreshLongPressToolTip);
        connect(this, &Button::checkedChanged, this, &Button::refreshLongPressToolTip);
        connect(this, &Button::enabledChanged, this, &Button::refreshLongPressToolTip);
        connect(this, &Button::visibilityChanged, this, &Button::refreshLongPressToolTip);
    }

    connect(this, &Button::hoveredChanged, this, &Button::updateAnimationState);
    connect(this, &Button::hoveredChanged, this, [this](bool hovered) {
        if (!hovered && this->type() == KDecoration3::DecorationButtonType::Maximize) {
            m_holdTimer->stop();
        }
    });

    
    connect(this, &Button::transitionValueChanged, this, [this]() {
        UPDATE_GEOM();
    });
    
    connect(this, &Button::opacityChanged, this, [this]() {
        UPDATE_GEOM();
    });
    
    connect(this, &Button::checkedChanged, this, [this]() {
        UPDATE_GEOM();
    }); 
    
    
    connect(this, &Button::pressedChanged, this, [this]() {
        const auto *deco = qobject_cast<Decoration *>(this->decoration());
        if (deco && deco->m_internalSettings->longPressEnabled() &&
            (this->type() == KDecoration3::DecorationButtonType::Close ||
            this->type() == KDecoration3::DecorationButtonType::Maximize ||
            this->type() == KDecoration3::DecorationButtonType::Minimize)) {
            if (isPressed()) {
                m_longPressTriggered = false;
                m_holdTimer->start(deco->m_internalSettings->longPressDuration());
            } else {
                m_holdTimer->stop();
            }
        }
        UPDATE_GEOM();
    }); 
    
    connect(this, &Button::enabledChanged, this, [this]() {
        UPDATE_GEOM();
    }); 
    
    connect(this, &Button::geometryChanged, this, [this]() {
        UPDATE_GEOM();     
        if (auto *deco = qobject_cast<Decoration *>(this->decoration())) {
            QTimer::singleShot(0, this, [deco]() { deco->updateButtonsGeometry(); });
        }
    });
    
    
    
    

    setHeight(decoration->captionHeight());

    auto *decoratedClient = decoration->window();

    switch (type) {
    case KDecoration3::DecorationButtonType::Menu:
        AppIconButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::ApplicationMenu:
        ApplicationMenuButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::OnAllDesktops:
        OnAllDesktopsButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::ContextHelp:
        ContextHelpButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::Shade:
        ShadeButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::KeepAbove:
        KeepAboveButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::KeepBelow:
        KeepBelowButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::Close:
        CloseButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::Maximize:
        MaximizeButton::init(this, decoratedClient);
        break;

    case KDecoration3::DecorationButtonType::Minimize:
        MinimizeButton::init(this, decoratedClient);
        break;
        
    case KDecoration3::DecorationButtonType::Spacer:
        break;    

#if HAVE_EXCLUDE_FROM_CAPTURE
    case KDecoration3::DecorationButtonType::ExcludeFromCapture:
        ExcludeFromCaptureButton::init(this, decoratedClient);
        break;
#endif

    default:
        break;
    }
}

Button::~Button()
{
}

KDecoration3::DecorationButton* Button::create(KDecoration3::DecorationButtonType type, KDecoration3::Decoration *decoration, QObject *parent)
{
    auto deco = qobject_cast<Decoration*>(decoration);
    if (!deco) {
        return nullptr;
    }

    switch (type) {
    case KDecoration3::DecorationButtonType::Menu:
    // case KDecoration3::DecorationButtonType::ApplicationMenu:
    case KDecoration3::DecorationButtonType::OnAllDesktops:
    case KDecoration3::DecorationButtonType::ContextHelp:
    case KDecoration3::DecorationButtonType::Shade:
    case KDecoration3::DecorationButtonType::KeepAbove:
    case KDecoration3::DecorationButtonType::KeepBelow:
    case KDecoration3::DecorationButtonType::Close:
    case KDecoration3::DecorationButtonType::Maximize:
    case KDecoration3::DecorationButtonType::Minimize:
    case KDecoration3::DecorationButtonType::Spacer:
#if HAVE_EXCLUDE_FROM_CAPTURE
    case KDecoration3::DecorationButtonType::ExcludeFromCapture:
#endif
        return new Button(type, deco, parent);

    default:
        return nullptr;
    }
}

Button::Button(QObject *parent, const QVariantList &args)
    : Button(buttonTypeFromArgs(args), decorationFromArgs(args), parent)
{
    if (args.size() < 2) {
        qWarning() << "Button constructor received too few arguments:" << args;
    }
}

void Button::mousePressEvent(QMouseEvent *event)
{
    // A popup can consume the release ending the previous long press. A new
    // gesture must start clean even if long-press actions were since disabled.
    m_longPressTriggered = false;
    m_longPressWarning.clear();
    ++m_pressGeneration;
    KDecoration3::DecorationButton::mousePressEvent(event);
    // Preserve KWin's separate middle/right-click maximize actions.
    if (type() == KDecoration3::DecorationButtonType::Maximize && event->button() != Qt::LeftButton) {
        m_holdTimer->stop();
    }
}

void Button::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_longPressTriggered) {
        // A recognized long press consumes the click even if its action is
        // still pending or unavailable, consistently for all three buttons.
        QScopedValueRollback<bool> restoringHover(m_restoringHover, true);
        m_longPressTriggered = false;
        forceUnpress();

        // Refresh hover state, simulate a mouse HoverMove.
        const QPointF decoPos = event->position();
        QHoverEvent hoverEvent(QEvent::HoverMove, decoPos, event->globalPosition(), QPointF(-1, -1), event->modifiers());
        QCoreApplication::sendEvent(decoration(), &hoverEvent);

        // Restoring hover can request the base tooltip. Cancel that request
        // without losing an unavailable-action warning shown during the hold.
        decoration()->requestHideToolTip();
        if (!m_longPressWarning.isEmpty()) QToolTip::showText(QCursor::pos(), m_longPressWarning);
        return;
    }
    KDecoration3::DecorationButton::mouseReleaseEvent(event);
}

void Button::paint(QPainter *painter, const QRectF &repaintRegion)
{
    Q_UNUSED(repaintRegion)

    auto *deco = qobject_cast<Decoration *>(this->decoration());
    if (!deco || type() == KDecoration3::DecorationButtonType::Spacer) {
        return;
    }

    painter->save();
    painter->setOpacity(m_opacity);

    if (qobject_cast<AppMenuButton *>(this)) {
        paintMenuButton(painter, deco);
    } else if (type() == KDecoration3::DecorationButtonType::Menu) {
        // application icon, drawn at the size of the Oxygen buttons
        const qreal size = deco->buttonHeight();
        const QRectF iconRect(geometry().left(), geometry().top() + slabYOffset(deco), size, size);
        deco->window()->icon().paint(painter, iconRect.toRect());
    } else {
        paintOxygenButton(painter, deco);
    }

    painter->restore();
}

qreal Button::slabYOffset(const Decoration *deco) const
{
    // Same as the Oxygen vertical offset: window buttons of maximized windows
    // are taller (to reach the screen edge) and their slab is centered in the
    // caption row.
    return (deco->isMaximized() ? deco->rowTop() : 0) + (deco->captionHeight() - deco->buttonHeight()) / 2.0;
}

bool Button::isToggleButton() const
{
    switch (type()) {
    case KDecoration3::DecorationButtonType::OnAllDesktops:
    case KDecoration3::DecorationButtonType::KeepAbove:
    case KDecoration3::DecorationButtonType::KeepBelow:
#if HAVE_EXCLUDE_FROM_CAPTURE
    case KDecoration3::DecorationButtonType::ExcludeFromCapture:
#endif
        return true;
    default:
        return false;
    }
}

void Button::paintMenuButton(QPainter *painter, Decoration *deco)
{
    auto *helper = OxygenHelper::self();
    const auto *menuButton = qobject_cast<AppMenuButton *>(this);
    const auto *group = qobject_cast<AppMenuButtonGroup *>(parent());
    const bool otherMenuOpen = group && menuButton && group->isMenuOpen() && group->currentIndex() != menuButton->buttonIndex();

    // Oxygen-like slab behind hovered, pressed and opened menu entries
    const bool sunken = isChecked() || isPressed();
    const qreal strength = otherMenuOpen ? 0.0 : (sunken ? 1.0 : (isVisuallyHovered() ? m_transitionValue : 0.0));
    if (strength > 0.0) {
        const QColor base = deco->titleBarBackgroundColor();
        const QColor light = helper->calcLightColor(base);
        const QColor dark = helper->calcDarkColor(base);
        const QRectF slab = geometry().adjusted(0.5, 1.5, -0.5, -1.5);

        QLinearGradient gradient(slab.topLeft(), slab.bottomLeft());
        gradient.setColorAt(0.0, OxygenHelper::alphaColor(sunken ? dark : light, strength));
        gradient.setColorAt(1.0, OxygenHelper::alphaColor(sunken ? light : dark, strength));

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(OxygenHelper::alphaColor(dark, strength * 0.7), 1));
        painter->setBrush(gradient);
        painter->drawRoundedRect(slab, 3.5, 3.5);
        painter->restore();
    }

    // Foreground.
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);

    const QRectF contentRect = contentArea();

    PixelSnapper snapper(painter);

    // TextButton is special, so we don't scale the painter
    if (auto textButton = qobject_cast<TextButton*>(this)) {
        textButton->paintIcon(painter, contentRect, snapper);
        return;
    }

    // All further rendering is performed inside a 18x18 square
    const qreal size = qMin(contentRect.width(), contentRect.height()) * 0.6;

    // For a sharper image, we use physical-pixel-aligned positioning and scaling
    const qreal localToPhysicalScale = snapper.localToPhysicalScale();
    const qreal physicalIconSize = (localToPhysicalScale > 0.0) ? qRound(size * localToPhysicalScale) : qRound(size * snapper.dpr());
    const qreal iconLogicalSize = (localToPhysicalScale > 0.0) ? (physicalIconSize / localToPhysicalScale) : (physicalIconSize / snapper.dpr());

    // Translate to a center aligned with physical pixel grid
    painter->translate(snapper.snap(contentRect.center()));

    // Scale by physical-aligned factor
    painter->scale(iconLogicalSize / 18.0, iconLogicalSize / 18.0);

    // default pen width, not snapped
    m_penScale = 1.5;
    setPenWidth(painter, m_penScale, false);

    PixelSnapper iconSnapper(painter);
    paintIcon(painter, QRectF(-9, -9, 18, 18), iconSnapper);
}

void Button::paintOxygenButton(QPainter *painter, Decoration *deco)
{
    auto *helper = OxygenHelper::self();
    const auto *client = deco->window();
    const bool active = client->isActive();

    QPalette palette(client->palette());
    palette.setCurrentColorGroup(active ? QPalette::Active : QPalette::Inactive);

    const int size = deco->buttonHeight();
    const QPointF origin(geometry().left(), geometry().top() + slabYOffset(deco));

    // base button color and text color
    const QColor base = palette.color(active ? QPalette::Active : QPalette::Inactive, QPalette::Button);
    QColor color = palette.color(active ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);

    // decide decoration color
    QColor glow;
    const bool hovered = isVisuallyHovered();
    const bool checkedToggle = isToggleButton() && isChecked();
    const bool animated = m_animation->state() == QAbstractAnimation::Running;
    const qreal glowIntensity = m_transitionValue;
    if (animated || hovered || checkedToggle) {
        const QColor toggleColor = helper->focusColor(palette);
        const QColor toggledHoverGlow = palette.color(QPalette::Disabled, QPalette::ButtonText);
        const QColor toggledHoverColor = KColorUtils::mix(toggledHoverGlow, color, 0.6);

        if (type() == KDecoration3::DecorationButtonType::Close) {
            glow = helper->negativeTextColor(palette);
        } else if (hovered && checkedToggle) {
            glow = toggledHoverGlow;
        } else if (checkedToggle) {
            glow = toggleColor;
        } else {
            glow = helper->hoverColor(palette);
        }

        if (animated) {
            if (checkedToggle) {
                color = KColorUtils::mix(toggleColor, toggledHoverColor, glowIntensity);
                glow = KColorUtils::mix(toggleColor, toggledHoverGlow, glowIntensity);
            } else {
                color = KColorUtils::mix(color, glow, glowIntensity);
                glow = OxygenHelper::alphaColor(glow, glowIntensity);
            }
        } else if (hovered != checkedToggle) {
            color = glow; // hovered xor checked: the glow colour is also the symbol colour
        } else if (checkedToggle) {
            color = toggledHoverColor; // checked and hovered
        }
    }

    // draw button shape
    const bool sunken = isPressed() || checkedToggle;
    const qreal dpr = painter->device()->devicePixelRatioF();
    const int physicalSize = qRound(size * dpr);
    QPixmap buttonPixmap = helper->windecoButton(base, glow, sunken, size, physicalSize);
    buttonPixmap.setDevicePixelRatio(dpr);
    painter->drawPixmap(origin, buttonPixmap);

    // Icon
    painter->setRenderHints(QPainter::Antialiasing);
    painter->translate(origin);

    const qreal width = 1.2;

    // contrast
    painter->setBrush(Qt::NoBrush);
    painter->translate(0, 1.5);
    painter->setPen(QPen(helper->calcLightColor(base), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    drawOxygenIcon(painter, size, deco);

    // main
    painter->translate(0, -1.5);
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    drawOxygenIcon(painter, size, deco);
}

void Button::drawOxygenIcon(QPainter *painter, qreal width, Decoration *deco)
{
    painter->save();

    // keep all co-ordinates between 0 and 21
    painter->scale(width / 21, width / 21);

    // make sure pen width is always larger than 1.1 in "real" coordinates
    QPen pen(painter->pen());
    pen.setWidthF(qMax(1.1 * 21 / width, pen.widthF()));
    painter->setPen(pen);

    switch (type()) {
    case KDecoration3::DecorationButtonType::OnAllDesktops:
        painter->drawPoint(QPointF(10.5, 10.5));
        break;

    case KDecoration3::DecorationButtonType::ContextHelp:
        painter->translate(1.5, 1.5);
        painter->drawArc(7, 5, 4, 4, 135 * 16, -180 * 16);
        painter->drawArc(9, 8, 4, 4, 135 * 16, 45 * 16);
        painter->drawPoint(9, 12);
        painter->translate(-1.5, -1.5);
        break;

    case KDecoration3::DecorationButtonType::Minimize:
        painter->drawPolyline(QPolygonF() << QPointF(7.5, 9.5) << QPointF(10.5, 12.5) << QPointF(13.5, 9.5));
        break;

    case KDecoration3::DecorationButtonType::Maximize:
        if (deco->window()->isMaximized()) {
            painter->drawPolygon(QPolygonF() << QPointF(7.5, 10.5) << QPointF(10.5, 7.5) << QPointF(13.5, 10.5) << QPointF(10.5, 13.5));
        } else {
            painter->drawPolyline(QPolygonF() << QPointF(7.5, 11.5) << QPointF(10.5, 8.5) << QPointF(13.5, 11.5));
        }
        break;

    case KDecoration3::DecorationButtonType::Close:
        painter->drawLine(QPointF(7.5, 7.5), QPointF(13.5, 13.5));
        painter->drawLine(QPointF(13.5, 7.5), QPointF(7.5, 13.5));
        break;

    case KDecoration3::DecorationButtonType::KeepAbove:
        painter->drawPolyline(QPolygonF() << QPointF(7.5, 14) << QPointF(10.5, 11) << QPointF(13.5, 14));
        painter->drawPolyline(QPolygonF() << QPointF(7.5, 10) << QPointF(10.5, 7) << QPointF(13.5, 10));
        break;

    case KDecoration3::DecorationButtonType::KeepBelow:
        painter->drawPolyline(QPolygonF() << QPointF(7.5, 11) << QPointF(10.5, 14) << QPointF(13.5, 11));
        painter->drawPolyline(QPolygonF() << QPointF(7.5, 7) << QPointF(10.5, 10) << QPointF(13.5, 7));
        break;

#if HAVE_EXCLUDE_FROM_CAPTURE
    case KDecoration3::DecorationButtonType::ExcludeFromCapture:
        painter->drawPolyline(QPolygonF() << QPointF(7.5, 9) << QPointF(7.5, 7.5) << QPointF(9, 7.5));
        painter->drawPolyline(QPolygonF() << QPointF(12, 13.5) << QPointF(13.5, 13.5) << QPointF(13.5, 12));

        if (!isChecked()) {
            painter->drawPolyline(QPolygonF() << QPointF(12, 7.5) << QPointF(13.5, 7.5) << QPointF(13.5, 9));
            painter->drawPolyline(QPolygonF() << QPointF(7.5, 12) << QPointF(7.5, 13.5) << QPointF(9, 13.5));
        } else {
            QPen redPen(painter->pen());
            redPen.setColor(OxygenHelper::self()->negativeTextColor(deco->window()->palette()));
            redPen.setWidthF(0.8);
            painter->setPen(redPen);
            painter->drawLine(QPointF(7.5, 13.5), QPointF(13.5, 7.5));
        }
        break;
#endif

    case KDecoration3::DecorationButtonType::Shade:
        if (!isChecked()) {
            painter->drawPolyline(QPolygonF() << QPointF(7.5, 7.5) << QPointF(10.5, 10.5) << QPointF(13.5, 7.5));
            painter->drawLine(QPointF(7.5, 13.0), QPointF(13.5, 13.0));
        } else {
            painter->drawPolyline(QPolygonF() << QPointF(7.5, 10.5) << QPointF(10.5, 7.5) << QPointF(13.5, 10.5));
            painter->drawLine(QPointF(7.5, 13.0), QPointF(13.5, 13.0));
        }
        break;

    default:
        break;
    }

    painter->restore();
}

void Button::paintIcon(QPainter *painter, const QRectF &iconRect, const PixelSnapper &snapper)
{
    Q_UNUSED(painter)
    Q_UNUSED(iconRect)
    Q_UNUSED(snapper)
}

void Button::updateSize(qreal contentWidth, qreal contentHeight)
{
    const QSizeF size(
        m_padding.left() + contentWidth + m_padding.right(),
        m_padding.top() + contentHeight + m_padding.bottom()
    );
    setGeometry(QRectF(geometry().topLeft(), size));
}

void Button::setHeight(qreal buttonHeight)
{
    if (!qobject_cast<AppMenuButton *>(this)) {
        // Oxygen window buttons are round slabs of a fixed size
        const auto *deco = qobject_cast<Decoration *>(decoration());
        if (deco) {
            updateSize(deco->buttonHeight(), buttonHeight);
            return;
        }
    }
    // For simplicity, don't count the 1.x:1 scaling in the left/right padding.
    // The left/right padding is mainly for the border offset alignment.
    updateSize(buttonHeight * 1.1, buttonHeight);
}

void Button::setPenWidth(QPainter *painter, const qreal scale, bool snapped)
{
    QPen pen(foregroundColor());
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    
    const qreal nominalLocalWidth = PenWidth::Symbol * scale;
    PixelSnapper snapper(painter);

    if (snapped) {
        pen.setWidthF(snapper.snappedPenWidth(nominalLocalWidth));
    } else {
        pen.setWidthF(nominalLocalWidth);
    }

    painter->setPen(pen);
}

qreal Button::penScale() const
{
    return m_penScale;
}    
    
QColor Button::backgroundColor() const
{
    const auto *deco = qobject_cast<Decoration *>(this->decoration());
    if (!deco) {
        return {};
    }
    const bool active = deco->window()->isActive();
    return deco->window()->palette().color(active ? QPalette::Active : QPalette::Inactive, QPalette::Button);
}

QColor Button::foregroundColor() const
{
    const auto *deco = qobject_cast<Decoration *>(this->decoration());
    if (!deco) {
        return {};
    }
    const bool active = deco->window()->isActive();
    return deco->window()->palette().color(active ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
}


QRectF Button::contentArea() const
{
    return geometry().adjusted(
        m_padding.left(),
        m_padding.top(),
        -m_padding.right(),
        -m_padding.bottom()
    );
}

bool Button::animationEnabled() const
{
    return m_animationEnabled;
}

void Button::setAnimationEnabled(bool value)
{
    if (m_animationEnabled != value) {
        m_animationEnabled = value;
        Q_EMIT animationEnabledChanged();
    }
}

int Button::animationDuration() const
{
    return m_animation->duration();
}

void Button::setAnimationDuration(int value)
{
    if (m_animation->duration() != value) {
        m_animation->setDuration(value);
        Q_EMIT animationDurationChanged();
    }
}

qreal Button::opacity() const
{
    return m_opacity;
}

void Button::setOpacity(qreal value)
{
    if (m_opacity != value) {
        m_opacity = value;
        Q_EMIT opacityChanged();
    }
}

qreal Button::transitionValue() const
{
    return m_transitionValue;
}

void Button::setTransitionValue(qreal value)
{
    if (m_transitionValue != value) {
        m_transitionValue = value;
        Q_EMIT transitionValueChanged(value);
    }
}

QMarginsF &Button::padding()
{
    return m_padding;
}

void Button::setHorzPadding(qreal value)
{
    padding().setLeft(value);
    padding().setRight(value);
}

void Button::setIsLeftmost(bool isLeftmost)
{
    m_isLeftmost = isLeftmost;
}

void Button::setIsRightmost(bool isRightmost)
{
    m_isRightmost = isRightmost;
}

bool Button::isVisuallyHovered() const
{
    return isHovered() || m_tilingPopupOpen;
}

void Button::setTilingPopupOpen(bool open)
{
    if (m_tilingPopupOpen == open) return;
    m_tilingPopupOpen = open;
    refreshLongPressToolTip();
    updateAnimationState(isHovered());
    UPDATE_GEOM();
}

void Button::updateAnimationState(bool hovered)
{
    if (m_tilingPopupOpen) {
        // Popup grabs generate hover changes even while the logical hover is
        // pinned. Restarting a completed Forward animation resets it to zero.
        m_animation->stop();
        setTransitionValue(1);
        UPDATE_GEOM();
        return;
    }
    if (m_animationEnabled) {
        if (type() == KDecoration3::DecorationButtonType::Maximize
            && ((hovered && qFuzzyCompare(m_transitionValue, 1.0)) || (!hovered && qFuzzyIsNull(m_transitionValue)))) {
            m_animation->stop();
            return;
        }
        const QAbstractAnimation::Direction dir = hovered ? QAbstractAnimation::Forward : QAbstractAnimation::Backward;
        if (m_animation->state() == QAbstractAnimation::Running && m_animation->direction() != dir) {
            m_animation->stop();
        }
        m_animation->setDirection(dir);
        if (m_animation->state() != QAbstractAnimation::Running) {
            m_animation->start();
        }
        UPDATE_GEOM();
    } else {
        setTransitionValue(1);
    }
}

void Button::cancelLongPress()
{
    m_holdTimer->stop();
    if (m_hoverTimer) m_hoverTimer->stop();
    // A script availability check may already be pending when movement starts.
    ++m_pressGeneration;
    m_longPressTriggered = false;
    m_longPressWarning.clear();
}

void Button::forceUnpress()
{
    // HACK: There is no public API to set the pressed state of a
    // KDecoration3::DecorationButton. This works around the issue by toggling
    // the enabled state, which has the side effect of resetting the
    // button's internal pressed state.
    const bool wasEnabled = isEnabled();
    setEnabled(!wasEnabled);
    setEnabled(wasEnabled);
}

QString Button::normalToolTip() const
{
    switch (type()) {
    case KDecoration3::DecorationButtonType::Minimize: return i18nd("kdecoration", "Minimize");
    case KDecoration3::DecorationButtonType::Maximize:
        return isChecked() ? i18nd("kdecoration", "Restore") : i18nd("kdecoration", "Maximize");
    case KDecoration3::DecorationButtonType::Close: return i18nd("kdecoration", "Close");
    default: return {};
    }
}

void Button::refreshLongPressToolTip()
{
    if (!m_toolTipTimer) return;
    m_toolTipTimer->stop();
    const auto generation = ++m_toolTipGeneration;
    auto *deco = qobject_cast<Decoration *>(decoration());
    if (!deco || m_isGtkButton || !isHovered() || isPressed() || !isVisible() || !isEnabled() || m_tilingPopupOpen || m_longPressToolTipSuspended) {
        m_toolTipText.clear();
        return;
    }
    if (type() == KDecoration3::DecorationButtonType::Maximize && deco->m_internalSettings->tilingPopupHoverEnabled()) {
        deco->requestHideToolTip();
        m_toolTipText.clear();
        return;
    }
    if (m_toolTipText.isEmpty()) {
        m_toolTipText = normalToolTip();
        deco->requestShowToolTip(m_toolTipText);
    }
    const auto update = [this, generation](bool available) {
        if (generation != m_toolTipGeneration || !isHovered() || isPressed() || !isVisible() || !isEnabled() || m_tilingPopupOpen || m_longPressToolTipSuspended) return;
        auto *deco = qobject_cast<Decoration *>(decoration());
        if (!deco) return;
        if (type() == KDecoration3::DecorationButtonType::Maximize && deco->m_internalSettings->tilingPopupHoverEnabled()) {
            deco->requestHideToolTip();
            m_toolTipText.clear();
            return;
        }
        QString text = normalToolTip();
        if (available && deco->m_internalSettings->longPressEnabled()) {
            switch (type()) {
            case KDecoration3::DecorationButtonType::Minimize:
                text += QLatin1Char('\n') + i18nd("oxygendecorationlim", "Press and hold to minimize other windows");
                break;
            case KDecoration3::DecorationButtonType::Maximize:
                text += QLatin1Char('\n') + i18nd("oxygendecorationlim", "Press and hold to show snapping zones");
                break;
            case KDecoration3::DecorationButtonType::Close:
                text += QLatin1Char('\n') + i18nd("oxygendecorationlim", "Press and hold to minimize to the notification area");
                break;
            default: break;
            }
        }
        if (text != m_toolTipText) {
            m_toolTipText = text;
            deco->requestShowToolTip(text);
        }
        // Refresh only while hovered, without restarting an unchanged tooltip.
        m_toolTipTimer->start(2000);
    };
    if (!deco->m_internalSettings->longPressEnabled()) {
        update(false);
    } else if (type() == KDecoration3::DecorationButtonType::Minimize) {
        checkLongPressAction(this, QStringLiteral("minimizeAllOthersActiveScreen"), QStringLiteral("minimizeall"), update);
    } else if (type() == KDecoration3::DecorationButtonType::Close) {
        checkLongPressAction(this, QStringLiteral("Minimize to tray"), QStringLiteral("com.github.luisbocanegra.minimize2tray"), update);
    } else if (canTileWindow(deco)) {
        checkLongPressAction(this, QStringLiteral("OxygenLimTilingPopupDispatch"), QStringLiteral("oxygen-lim-tiling"), update);
    } else {
        update(false);
    }
}

void Button::handleHoldTimeout()
{
    if (type() == KDecoration3::DecorationButtonType::Maximize && (!isPressed() || !isHovered())) return;
    m_longPressTriggered = true;
    m_longPressToolTipSuspended = true;
    refreshLongPressToolTip();
    switch (type()) {
    case KDecoration3::DecorationButtonType::Close:
        onCloseHold();
        break;
    case KDecoration3::DecorationButtonType::Maximize:
        onMaximizeHold();
        break;
    case KDecoration3::DecorationButtonType::Minimize:
        onMinimizeHold();
        break;
    default:
        break;
    }
}

void Button::invokeLongPressAction(const QString &shortcut, const QString &script, const QString &unavailableMessage)
{
    const auto generation = m_pressGeneration;
    checkLongPressAction(this, shortcut, script, [this, shortcut, unavailableMessage, generation](bool available) {
        if (generation != m_pressGeneration) return;
        if (!available) {
            m_longPressWarning = unavailableMessage;
            QToolTip::showText(QCursor::pos(), unavailableMessage);
            return;
        }
        // These shortcuts act on the active window. Do not affect another
        // window if focus changed while the asynchronous check was pending.
        auto *deco = qobject_cast<Decoration *>(decoration());
        if (!deco || !deco->window() || !deco->window()->isActive()) return;
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                                                    QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("invokeShortcut"));
        message << shortcut;
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 1500), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation, unavailableMessage](QDBusPendingCallWatcher *call) {
            if (generation == m_pressGeneration && call->reply().type() == QDBusMessage::ErrorMessage) {
                m_longPressWarning = unavailableMessage;
                QToolTip::showText(QCursor::pos(), unavailableMessage);
            }
            call->deleteLater();
        });
    });
}

void Button::onCloseHold()
{
    invokeLongPressAction(QStringLiteral("Minimize to tray"), QStringLiteral("com.github.luisbocanegra.minimize2tray"),
                         i18nd("oxygendecorationlim", "Install kwin-minimize2tray and enable Minimize to Tray in System Settings > Window Management > KWin Scripts."));
}

void Button::onMinimizeHold()
{
    invokeLongPressAction(QStringLiteral("minimizeAllOthersActiveScreen"), {},
                         i18nd("oxygendecorationlim", "The action to minimize all other windows is unavailable. It requires Plasma 6.6 or later."));
}

void Button::onMaximizeHold()
{
    auto *deco = qobject_cast<Decoration *>(decoration());
    if (!deco || !deco->showTilingPopup(geometry())) {
        // Preserve the normal click if this host cannot perform native tiling.
        m_longPressTriggered = false;
        m_longPressToolTipSuspended = false;
    } else {
        // The popup takes the mouse grab, so the release may never reach us.
        forceUnpress();
    }
}

} // namespace OxygenLim
