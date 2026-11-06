/*
 * Copyright (C) 2025 Guido Iodice <guido[dot]iodice[at]gmail[dot]com>
 * Copyright (C) 2020 Chris Holland <zrenfire@gmail.com>
 * Copyright (C) 2018 Vlad Zagorodniy <vladzzag@gmail.com>
 * Copyright (C) 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 * Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>
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

#include <atomic>
#include <optional>

// own
#include "Decoration.h"
#include "DrawHelper.h"
#include "OxygenHelper.h"
#include "TilingPopup.h"
#include "TilingController.h"
#include "LongPressAction.h"
#include "AppMenuButtonGroup.h"
#include "BoxShadowHelper.h"
#include "BuildConfig.h"
#include "Button.h"
#include "TextButton.h"
#include "InternalSettings.h"
#include "SettingsProvider.h"
#include "OxygenLim.h"
#include "PixelSnapper.h"

// KDecoration
#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/DecorationButton>
#include <KDecoration3/DecorationButtonGroup>
#include <KDecoration3/DecorationSettings>
#include <KDecoration3/DecorationShadow>
#include <KDecoration3/ScaleHelpers>

// KF
#include <KWindowSystem>
#include <KLocalizedString>

// Qt
#include <KColorUtils>
#include <QApplication>
#include <QEasingCurve>
#include <QHash>
#include <QDebug>
#include <QtMath>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QPainterPath>
#include <QRegion>
#include <QSharedPointer>
#include <QWheelEvent>
#include <QTimer>
#include <QToolTip>
#include <QCursor>
#include <QMetaMethod>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace OxygenLim
{

namespace
{

template <typename ButtonType, typename Func>
void forEachButton(KDecoration3::DecorationButtonGroup *buttonGroup, Func f)
{
    if (!buttonGroup) {
        return;
    }
    for (auto *decoButton : buttonGroup->buttons()) {
        if (auto *button = qobject_cast<ButtonType *>(decoButton)) {
            f(button);
        }
    }
}

} // anonymous namespace

static std::atomic<int> s_decoCount(0);

// Shadows are shared between decorations: [hasBorder][active]
static std::shared_ptr<KDecoration3::DecorationShadow> s_cachedShadows[2][2];
// cross-faded shadows used during the active/inactive transition: key = (index << 1) | hasBorder
static QHash<int, std::shared_ptr<KDecoration3::DecorationShadow>> s_animatedShadows;

Decoration::Decoration(QObject *parent, const QVariantList &args)
    : KDecoration3::Decoration(parent, args)
    , m_internalSettings(nullptr)
{
    ++s_decoCount;
}

Decoration::~Decoration()
{
    int count = --s_decoCount;
    if (count <= 0) {
        Q_ASSERT_X(count >= 0, "Decoration::~Decoration()", "s_decoCount became negative, indicating a logic error!");
        if (count < 0) {
            s_decoCount.store(0); // defensive reset
        }
        for (auto &row : s_cachedShadows) {
            for (auto &shadow : row) {
                shadow.reset();
            }
        }
        s_animatedShadows.clear();
    }
}

bool Decoration::isTilingPopupVisible() const
{
    return m_tilingPopupOpening || (m_tilingPopup && m_tilingPopup->isVisible());
}

bool Decoration::showTilingPopup(const QRectF &anchor, bool fromHover)
{
    if (!canTileWindow(this)) return false;
    if (isTilingPopupVisible()) return true;
    m_tilingPopupHoverMonitor.stop();
    m_tilingPopupOutsideTime.invalidate();
    if (fromHover) {
        m_tilingPopupHoverMonitor.disconnect(this);
        m_tilingPopupHoverMonitor.setInterval(50);
        // Popup grabs can clear the button hover state. Check the actual cursor
        // position so crossing from the button to the menu does not close it.
        connect(&m_tilingPopupHoverMonitor, &QTimer::timeout, this, [this, anchor] {
            const auto cursor = QCursor::pos();
            const auto origin = parent()->property("frameGeometry").toRectF().topLeft();
            const bool inside = anchor.translated(origin).contains(cursor) ||
                (m_tilingPopup && m_tilingPopup->isVisible() && m_tilingPopup->frameGeometry().contains(cursor));
            if (!m_internalSettings->tilingPopupHoverEnabled()) {
                closeTilingPopup();
            } else if (inside || QApplication::mouseButtons() != Qt::NoButton) {
                m_tilingPopupOutsideTime.invalidate();
            } else if (!m_tilingPopupOutsideTime.isValid()) {
                m_tilingPopupOutsideTime.start();
            } else if (m_tilingPopupOutsideTime.elapsed() >= 500) {
                closeTilingPopup();
            }
        });
        m_tilingPopupHoverMonitor.start();
    }
    // Pin the button before the asynchronous availability check and before
    // forceUnpress() can temporarily clear the physical hover state.
    m_tilingPopupOpening = true;
    const auto request = ++m_tilingPopupRequest;
    Q_EMIT tilingPopupVisibilityChanged(true);
    const auto showPopup = [this, anchor, request](bool available, const CustomTilingLayout &layout) {
        if (request != m_tilingPopupRequest) return;
        m_tilingPopupOpening = false;
        if (!available) {
            m_tilingPopupHoverMonitor.stop();
            const QString message = i18nd("oxygendecorationlim", "Enable the Oxygen LIM Tiling Popup script in System Settings > Window Management > KWin Scripts.");
            Q_EMIT tilingPopupUnavailable(message);
            Q_EMIT tilingPopupVisibilityChanged(false);
            QToolTip::showText(QCursor::pos(), message);
            return;
        }
        if (!m_tilingPopup) {
            m_tilingPopup = std::make_unique<TilingPopup>();
            connect(m_tilingPopup.get(), &QMenu::aboutToShow, this, [this] { Q_EMIT tilingPopupVisibilityChanged(true); });
            connect(m_tilingPopup.get(), &QMenu::aboutToHide, this, [this] {
                m_tilingPopupHoverMonitor.stop();
                Q_EMIT tilingPopupVisibilityChanged(false);
            });
            connect(m_tilingPopup.get(), &TilingPopup::keyboardNavigationStarted, this, [this] {
                m_tilingPopupHoverMonitor.stop();
            });
            connect(m_tilingPopup.get(), &TilingPopup::customZoneSelected, this, [this](const QString &id) {
                QTimer::singleShot(0, this, [this, id] {
                    tileCustomWindow(this, id, [](bool success) {
                        if (!success) QToolTip::showText(QCursor::pos(), i18nd("oxygendecorationlim", "The custom tile is unavailable. Open the popup again."));
                    });
                });
            });
            connect(m_tilingPopup.get(), &TilingPopup::zoneSelected, this, [this](Qt::Edges edges) {
                // Defer the geometry change until the menu has released its grab.
                QTimer::singleShot(0, this, [this, edges] { tileWindow(this, edges); });
            });
        }
        m_tilingPopup->setCustomLayout(layout);
        m_tilingPopup->setZoneCornerRadius(Metrics::CornerRadius);
        m_tilingPopup->setSquareCornersTiled(false);
        m_tilingPopup->setAnimationSettings(animationsEnabled(), animationsDuration());
        KDecoration3::Positioner positioner;
        positioner.setAnchorRect(anchor);
        popup(positioner, m_tilingPopup.get());
    };
    checkLongPressAction(this, QStringLiteral("OxygenLimTilingPopupDispatch"), QStringLiteral("oxygen-lim-tiling"),
                         [this, showPopup, request](bool loaded) {
        if (request != m_tilingPopupRequest) return;
        if (loaded) fetchTilingLayout(this, showPopup);
        else showPopup(false, {});
    });
    return true;
}

void Decoration::closeTilingPopup()
{
    m_tilingPopupHoverMonitor.stop();
    if (!isTilingPopupVisible()) return;
    ++m_tilingPopupRequest;
    m_tilingPopupOpening = false;
    if (m_tilingPopup && m_tilingPopup->isVisible()) {
        m_tilingPopup->hide();
    } else {
        Q_EMIT tilingPopupVisibilityChanged(false);
    }
}

void Decoration::setupMenu()
{
    auto repaintTitleBar = [this] {
        update(titleBar());
    };

    m_menuButtons = new AppMenuButtonGroup(this);
    connect(m_menuButtons, &AppMenuButtonGroup::menuUpdated,
            this, &Decoration::updateButtonsGeometry);
    connect(m_menuButtons, &AppMenuButtonGroup::opacityChanged,
            this, repaintTitleBar);
    connect(m_menuButtons, &AppMenuButtonGroup::alwaysShowChanged,
            this, repaintTitleBar);
    m_menuButtons->updateAppMenuModel();
    m_menuButtons->setHamburgerMenu(m_internalSettings->hamburgerMenu());
}

QRectF Decoration::titleBarRect() const
{
    return QRectF(0, 0, size().width(), titleBarHeight());
}

QRectF Decoration::centerRect() const
{
    if (!m_leftButtons || !m_rightButtons) {
        return titleBarRect();
    }

    const qreal margin = sideMargin();
    qreal left = leftOffset() + margin;
    qreal right = size().width() - rightOffset() - margin;
    if (!m_leftButtons->buttons().isEmpty()) {
        left = m_leftButtons->geometry().right() + margin;
    }
    if (!m_rightButtons->buttons().isEmpty()) {
        right = m_rightButtons->geometry().left() - margin;
    }

    return QRectF(left, 0, qMax<qreal>(0, right - left), titleBarHeight());
}

void Decoration::paint(QPainter *painter, const QRectF &repaintRegion)
{
    if (isShadowOnly()) {
        return;
    }

    paintWindowBackground(painter, repaintRegion);
    paintButtons(painter, repaintRegion);
    paintCaption(painter, repaintRegion);
}

bool Decoration::init()
{    
    setInternalSettings(SettingsProvider::self()->internalSettings(this));
    connect(SettingsProvider::self(), &SettingsProvider::configChanged, this, &Decoration::reconfigure);

    // "visualParent" is an internal KWin preview detail, not a public API.
    // If KWin removes or renames it, the runtime lookup safely returns an invalid
    // QVariant. Preview artifacts may return, but the missing property itself
    // cannot cause a compilation failure or crash.
    if (property("visualParent").isValid()) {
        // KWin's preview applies the initial state but can leave later border
        // changes pending. Real windows must keep KWin's state synchronization.
        auto *previewStateTimer = new QTimer(this);
        previewStateTimer->setSingleShot(true);
        connect(this, &KDecoration3::Decoration::nextStateChanged,
                previewStateTimer, qOverload<>(&QTimer::start));
        connect(previewStateTimer, &QTimer::timeout, this, [this] {
            apply(nextState()->clone());
        });
    }

    const auto *decoratedClient = window();

    // KWin can start an interactive move while the button is still held
    // stationary. Cancel button actions on an actual step, without depending
    // on KWin's ABI.
    if (auto *owner = parent()) {
        const auto dragSlot = metaObject()->method(metaObject()->indexOfSlot("cancelButtonActionsForDrag()"));
        const auto *ownerMeta = owner->metaObject();
        for (int i = 0; i < ownerMeta->methodCount(); ++i) {
            const auto signal = ownerMeta->method(i);
            if (signal.methodType() == QMetaMethod::Signal && signal.name() == "interactiveMoveResizeStepped") {
                connect(owner, signal, this, dragSlot);
                break;
            }
        }
    }

    m_leftButtons = new KDecoration3::DecorationButtonGroup(
        KDecoration3::DecorationButtonGroup::Position::Left,
        this,
        &Button::create);

    m_rightButtons = new KDecoration3::DecorationButtonGroup(
        KDecoration3::DecorationButtonGroup::Position::Right,
        this,
        &Button::create);

    setupMenu();

    connect(decoratedClient, &KDecoration3::DecoratedWindow::sizeChanged,
            this, &Decoration::onSizeChanged);
    connect(decoratedClient, &KDecoration3::DecoratedWindow::nextScaleChanged,
            this, &Decoration::onNextScaleChanged);
    
    connect(decoratedClient, &KDecoration3::DecoratedWindow::widthChanged,
        this, &Decoration::onWidthChanged);
    
    connect(decoratedClient, &KDecoration3::DecoratedWindow::maximizedChanged, 
        this, &Decoration::onMaximizedChanged);

    // Oxygen windows are fully opaque when maximized
    connect(decoratedClient, &KDecoration3::DecoratedWindow::maximizedChanged,
        this, [this](bool maximized) { setOpaque(maximized); });
    setOpaque(decoratedClient->isMaximized());

    // colours follow the window palette
    connect(decoratedClient, &KDecoration3::DecoratedWindow::paletteChanged,
        this, [this] { update(); });
    
    connect(decoratedClient, &KDecoration3::DecoratedWindow::maximizedHorizontallyChanged,
            this, &Decoration::updateBordersCornersBlurShadow);
    connect(decoratedClient, &KDecoration3::DecoratedWindow::maximizedVerticallyChanged,
            this, &Decoration::updateBordersCornersBlurShadow);
    connect(decoratedClient, &KDecoration3::DecoratedWindow::shadedChanged,
            this, &Decoration::onShadedChanged);

    connect(decoratedClient, &KDecoration3::DecoratedWindow::captionChanged,
            this, [this] {
                auto newSettings = SettingsProvider::self()->internalSettings(this);
                if (newSettings != m_internalSettings) {
                    setInternalSettings(newSettings);
                    applySettings();
                } else {
                    invalidateCaptionCache();
                    update(titleBar());
                }
            });
    
    connect(decoratedClient, &KDecoration3::DecoratedWindow::activeChanged,
        this, &Decoration::onActiveChanged);

    // The active shadow glow follows the accent colour of the application
    // palette. Rebuild the shadows once a palette change has been applied
    // (deferred, so the new palette is certainly in place).
    new PaletteChangeWatcher([this] {
        QTimer::singleShot(0, this, [this] { invalidateShadows(); });
    }, this);

    // fade between the active and inactive look
    m_activeOpacity = decoratedClient->isActive() ? 1.0 : 0.0;
    m_activeAnimation = new QVariantAnimation(this);
    m_activeAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_activeAnimation, &QVariantAnimation::valueChanged,
        this, [this](const QVariant &value) { setActiveOpacity(value.toReal()); });
    connect(m_activeAnimation, &QVariantAnimation::finished,
        this, [this] { updateShadow(); });

    connect(decoratedClient, &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged,
            this, &Decoration::onAdjacentScreenEdgesChanged);
    
    connect(this, &KDecoration3::Decoration::bordersChanged, 
            this, &Decoration::updateTitleBar);
    
    connect(this, &KDecoration3::Decoration::bordersChanged, 
            this, &Decoration::updateButtonsGeometryDelayed);
     
    updateBordersCornersBlurShadow();
    updateResizeBorders();
    updateTitleBar();
    updateButtonsGeometryDelayed(); // avoid wrong geometry (for example Spectacle)

    connect(this, &KDecoration3::Decoration::sectionUnderMouseChanged,
            this, &Decoration::onSectionUnderMouseChanged);
    updateTitleBarHoverState();


#if HAVE_WAYLAND
    if (KWindowSystem::isPlatformWayland()) {
        auto dbus = QDBusConnection::sessionBus();
        dbus.connect(QStringLiteral("org.kde.KWin"),
                     QStringLiteral("/org/kde/KWin"),
                     QStringLiteral("org.kde.KWin.TabletModeManager"),
                     QStringLiteral("tabletModeChanged"),
                     QStringLiteral("b"),
                     this,
                     SLOT(onTabletModeChanged(bool)));
        
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                      QStringLiteral("/org/kde/KWin"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
        message.setArguments({QStringLiteral("org.kde.KWin.TabletModeManager"), QStringLiteral("tabletMode")});
        auto *call = new QDBusPendingCallWatcher(dbus.asyncCall(message), this);
        connect(call, &QDBusPendingCallWatcher::finished, this, [this, call]() {
            QDBusPendingReply<QDBusVariant> reply = *call;
            if (!reply.isError()) {
                onTabletModeChanged(reply.value().variant().toBool());
            }
            call->deleteLater();
        });
    }
#endif

    // Window Decoration KCM
    // The reconfigure signal will update active windows, but we need to hook
    // individual signals for the preview in the KCM.
    connect(settings().get(), &KDecoration3::DecorationSettings::reconfigured,
        SettingsProvider::self(), &SettingsProvider::reconfigure, Qt::UniqueConnection);
    connect(settings().get(), &KDecoration3::DecorationSettings::alphaChannelSupportedChanged,
        this, &Decoration::reconfigure);
    connect(settings().get(), &KDecoration3::DecorationSettings::borderSizeChanged, 
        this, &Decoration::updateBordersCornersBlurShadow);
    connect(settings().get(), &KDecoration3::DecorationSettings::fontChanged,
        this, &Decoration::updateBordersCornersBlurShadow);
    connect(settings().get(), &KDecoration3::DecorationSettings::spacingChanged,
        this, &Decoration::onSpacingChanged);
    connect(settings().get(), &KDecoration3::DecorationSettings::decorationButtonsLeftChanged,
        this, &Decoration::updateButtonsGeometryDelayed);
    connect(settings().get(), &KDecoration3::DecorationSettings::decorationButtonsRightChanged,
        this, &Decoration::updateButtonsGeometryDelayed);
    
    applySettings();

    return true;
}

bool Decoration::hideTitleBar() const
{
    if (isShadowOnly()) {
        return true;
    }

    if (window()->isShaded()) {
        return false;
    }

    return m_internalSettings ? m_internalSettings->hideTitleBar() : false;
}

bool Decoration::isShadowOnly() const
{
#if HAVE_KDEC_STYLES
    return style() == KDecoration3::Style::Shadow;
#else
    return false;
#endif
}

void Decoration::setInternalSettings(const QSharedPointer<InternalSettings> &settings)
{
    if (m_internalSettings == settings) {
        return;
    }
    m_internalSettings = settings;
    if (m_internalSettings) {
        connect(m_internalSettings.data(), &InternalSettings::configChanged, this, &Decoration::reconfigure, Qt::UniqueConnection);
    }
}

void Decoration::applySettings()
{
    if (!m_internalSettings) {
        return;
    }
    if (m_tilingPopup) {
        m_tilingPopup->setZoneCornerRadius(Metrics::CornerRadius);
        m_tilingPopup->setSquareCornersTiled(false);
        m_tilingPopup->setAnimationSettings(animationsEnabled(), animationsDuration());
    }

    if (m_menuButtons) {
        m_menuButtons->setHamburgerMenu(m_internalSettings->hamburgerMenu());
        m_menuButtons->updateAppMenuModel();
        m_menuButtons->setAlwaysShow(menuAlwaysShow());
    }

    invalidateCaptionCache();
    updateButtonAnimation();
    updateBordersCornersBlurShadow();
    updateTitleBar();
    updateButtonsGeometryDelayed();
    updateTitleBarHoverState();
    update();
}

void Decoration::reconfigure()
{
    // Oxygen colour/shadow settings may have changed: reload them and drop the
    // shadows generated from the old ones, so updateShadow() rebuilds them.
    OxygenHelper::self()->reconfigure();
    for (auto &row : s_cachedShadows) {
        for (auto &shadow : row) {
            shadow.reset();
        }
    }
    s_animatedShadows.clear();

    resetDragMove();
    setInternalSettings(SettingsProvider::self()->internalSettings(this));
    applySettings();
}

void Decoration::mousePressEvent(QMouseEvent *event)
{
    if (m_tilingPopupHoverMonitor.isActive()) closeTilingPopup();
    KDecoration3::Decoration::mousePressEvent(event);

    const QPoint pos = event->position().toPoint();

    // Determine if the click occurred on any of the button groups.
    // Menu buttons always allow dragging, while 
    // left and right groups depend on the configuration.
    const bool onMenuButtons = m_menuButtons && m_menuButtons->geometry().contains(pos);
    const bool onStandardButtons = dragFromButtonsEnabled() && (
        (m_leftButtons && m_leftButtons->geometry().contains(pos)) ||
        (m_rightButtons && m_rightButtons->geometry().contains(pos))
    );

    if (onMenuButtons || onStandardButtons) {
        const Qt::MouseButton button = event->button();

        if (button == Qt::LeftButton) {
            // Initialize window drag move and reject event so KWin handles the drag
            initDragMove(pos);
            event->setAccepted(false);
        } else if (button == Qt::MiddleButton || button == Qt::RightButton) {
            // Accept the event for Middle and Right buttons to prevent KWin 
            // from showing the window's context menu.
            event->accept();
        }
    }
}


void Decoration::hoverEnterEvent(QHoverEvent *event)
{
    KDecoration3::Decoration::hoverEnterEvent(event);
    m_lastHoverPos = event->position();
}


void Decoration::hoverMoveEvent(QHoverEvent *event)
{
    KDecoration3::Decoration::hoverMoveEvent(event);
    m_lastHoverPos = event->position();
    updateTitleBarHoverState();

    const bool dragStarted = dragMoveTick(event->position().toPoint());
    if (dragStarted) {
        cancelButtonActionsForDrag();
        return;
    }

    // The platform check has been removed, and the logic is now unified.
    // The event is forwarded from AppMenuButtonGroup::eventFilter on X11.
    if (m_menuButtons && m_menuButtons->geometry().contains(event->position())) {
        m_menuButtons->handleHoverMove(event->position());
    }
}


void Decoration::mouseReleaseEvent(QMouseEvent *event)
{
    KDecoration3::Decoration::mouseReleaseEvent(event);

    resetDragMove();
}

void Decoration::hoverLeaveEvent(QHoverEvent *event)
{
    KDecoration3::Decoration::hoverLeaveEvent(event);

    m_lastHoverPos = QPointF(-1, -1);
    if (m_menuButtons) {
        m_menuButtons->setHovered(false);
        m_menuButtons->updateShowing();
    }
    resetDragMove();
}


void Decoration::wheelEvent(QWheelEvent *event)
{
    if (!m_menuButtons || !m_menuButtons->geometry().contains(event->position())) {
        KDecoration3::Decoration::wheelEvent(event);
    }
}

void Decoration::onSectionUnderMouseChanged(const Qt::WindowFrameSection)
{
    updateTitleBarHoverState();
}

void Decoration::updateBlur()
{
    // Oxygen is opaque: nothing to blur behind the decoration.
    setBlurRegion(QRegion());
}

void Decoration::updateBordersCornersBlurShadow()
{
    qreal top = bottomOffset();
    if (!hideTitleBar()) {
        top = titleBarHeight();
    }
    setBorders(QMarginsF(leftOffset(), top, rightOffset(), bottomOffset()));
    updateResizeBorders();
    updateBorderRadius();
    updateBlur();
    updateShadow();
}

void Decoration::updateResizeBorders()
{
    // extended resize area, as in Oxygen
    const qreal extSize = settings()->largeSpacing();
    qreal extSides = 0;
    qreal extBottom = 0;
    if (isShadowOnly() || hasNoBorders()) {
        if (!isMaximizedHorizontally()) {
            extSides = extSize;
        }
        if (!isMaximizedVertically()) {
            extBottom = extSize;
        }
    } else if (hasNoSideBorders() && !isMaximizedHorizontally()) {
        extSides = extSize;
    }

    setResizeOnlyBorders(QMarginsF(extSides, 0, extSides, extBottom));
}

void Decoration::updateTitleBar()
{
    if (isShadowOnly()) {
        setTitleBar(QRectF(0, 0, 0, 0));
        invalidateCaptionCache();
        return;
    }

    const auto *s = settings().get();
    const bool maximized = isMaximized();
    const qreal width = maximized ? window()->width() : window()->width() - 2 * s->largeSpacing() * Metrics::TitleBar_SideMargin;
    const qreal height = maximized ? borderTop() : borderTop() - s->smallSpacing() * Metrics::TitleBar_TopMargin;
    const qreal x = maximized ? 0 : s->largeSpacing() * Metrics::TitleBar_SideMargin;
    const qreal y = maximized ? 0 : s->smallSpacing() * Metrics::TitleBar_TopMargin;
    setTitleBar(QRectF(x, y, width, height));
    invalidateCaptionCache();
}

void Decoration::updateBorderRadius()
{
    if (isShadowOnly() && !isMaximized()) {
        const qreal r = Metrics::CornerRadius;
        setBorderRadius(KDecoration3::BorderRadius(r, r, r, r));
    } else {
        setBorderRadius(KDecoration3::BorderRadius());
    }
}

void Decoration::updateTitleBarHoverState()
{
    if (hideTitleBar()) {
        if (m_titleBarHoverActive) {
            m_titleBarHoverActive = false;
            if (m_menuButtons) {
                m_menuButtons->setHovered(false);
            }
        }
        return;
    }

    bool isHovered = titleBarIsHovered();

    if (showCaptionOnHover() && m_captionLimited) {
        if (!m_captionRect.contains(m_lastHoverPos)) {
            isHovered = false;
        }
    }

    if (m_titleBarHoverActive == isHovered) {
        return;
    }

    m_titleBarHoverActive = isHovered;
    if (m_menuButtons) {
        m_menuButtons->setHovered(m_titleBarHoverActive);
    }
}

void Decoration::setButtonGroupHeight(KDecoration3::DecorationButtonGroup *buttonGroup, qreal buttonHeight)
{
    forEachButton<Button>(buttonGroup, [buttonHeight](Button *button) {
        button->setHeight(buttonHeight);
    });
}

void Decoration::setButtonGroupHorzPadding(KDecoration3::DecorationButtonGroup *buttonGroup, qreal value)
{
    // Only apply horizontal padding to TextButton
    forEachButton<TextButton>(buttonGroup, [value](TextButton *button) {
        button->setHorzPadding(value);
    });
}

void Decoration::updateButtonHeight()
{
    // Window buttons of maximized windows extend to the top edge of the screen
    const qreal windowButtonsHeight = captionHeight() + (isMaximized() ? rowTop() : 0);
    setButtonGroupHeight(m_leftButtons, windowButtonsHeight);
    setButtonGroupHeight(m_rightButtons, windowButtonsHeight);
    setButtonGroupHeight(m_menuButtons, captionHeight() + menuExtension());
}

void Decoration::invalidateCaptionCache() const
{
    m_captionCache.availableWidth = -1.0;
    m_captionCache.textWidth = -1.0;
}

void Decoration::updateButtonsGeometry()
{
    invalidateCaptionCache();
    if (m_menuButtons) {
        m_menuButtons->updateShowing();
        setButtonGroupHorzPadding(m_menuButtons, m_internalSettings->menuButtonHorzPadding());
    }

    updateButtonHeight();

    const qreal hPadding = sideMargin();
    // maximized windows let the buttons reach the screen edge
    const qreal windowButtonsTop = isMaximized() ? 0 : rowTop();

    // Left
    if (m_leftButtons) {
        m_leftButtons->setSpacing(buttonSpacing());
        m_leftButtons->setPos(QPointF(leftOffset() + hPadding, windowButtonsTop));
    }

    // Right
    if (m_rightButtons) {
        m_rightButtons->setSpacing(buttonSpacing());
        m_rightButtons->setPos(QPointF(size().width() - rightOffset() - hPadding - m_rightButtons->geometry().width(), windowButtonsTop));
    }

    // Menu
    if (m_menuButtons && !m_menuButtons->buttons().isEmpty()) {
        const qreal captionOffset = captionMinWidth() + settings()->smallSpacing();
        QRectF availableRect = centerRect();
        if (isMenuOnRight()) {
            availableRect.setLeft(availableRect.left() + captionOffset);
        } else {
            availableRect.setRight(availableRect.right() - captionOffset);
        }
        availableRect.translate(0, menuTop());

        m_menuButtons->updateOverflow(availableRect);

        if (isMenuOnRight()) {
            const QPointF topRight = availableRect.topRight();
            m_menuButtons->setPos(QPointF(topRight.x() - m_menuButtons->visibleWidth(), topRight.y()));
        } else {
            m_menuButtons->setPos(availableRect.topLeft());
        }

        m_menuButtons->setSpacing(0);
    }

    updateBlur();
    update();
}

void Decoration::updateButtonsGeometryDelayed()
{
    QTimer::singleShot(0, this, &Decoration::updateButtonsGeometry);
}

void Decoration::setButtonGroupAnimation(KDecoration3::DecorationButtonGroup *buttonGroup, bool enabled, int duration)
{
    forEachButton<Button>(buttonGroup, [enabled, duration](Button *button) {
        button->setAnimationEnabled(enabled);
        button->setAnimationDuration(duration);
    });
}

void Decoration::updateButtonAnimation()
{
    const bool enabled = animationsEnabled();
    const int duration = animationsDuration();
    setButtonGroupAnimation(m_leftButtons, enabled, duration);
    setButtonGroupAnimation(m_rightButtons, enabled, duration);
    setButtonGroupAnimation(m_menuButtons, enabled, duration);

    // Hover Animation
    if (m_menuButtons) {
        m_menuButtons->setAnimationEnabled(enabled);
        m_menuButtons->setAnimationDuration(duration);
    }
}

void Decoration::invalidateShadows()
{
    for (auto &row : s_cachedShadows) {
        for (auto &shadow : row) {
            shadow.reset();
        }
    }
    s_animatedShadows.clear();
    updateShadow();
}

void Decoration::updateShadow()
{
    if (!m_internalSettings || m_internalSettings->hideShadow()) {
        setShadow(nullptr);
        return;
    }

    const bool active = window()->isActive();
    const bool hasBorder = isShadowOnly() || !hasNoBorders() || window()->isShaded();
    constexpr int overlap = 4;

    const auto makeShadow = [](const QImage &image) {
        const int shadowSize = image.width() / 2;
        auto shadow = std::make_shared<KDecoration3::DecorationShadow>();
        shadow->setPadding(QMargins(shadowSize - overlap, shadowSize - overlap, shadowSize - overlap, shadowSize - overlap));
        shadow->setInnerShadowRect(QRect(shadowSize, shadowSize, 1, 1));
        shadow->setShadow(image);
        return shadow;
    };

    // during the transition, show a cross-fade of the two shadows
    if (m_activeAnimation && m_activeAnimation->state() == QAbstractAnimation::Running) {
        const int maxIndex = qMax(1, qMin(256, (120 * animationsDuration()) / 1000));
        const int index = qBound(0, qRound(m_activeOpacity * maxIndex), maxIndex);
        const int key = (index << 1) | (hasBorder ? 1 : 0);
        auto iter = s_animatedShadows.find(key);
        if (iter == s_animatedShadows.end()) {
            const QImage image = OxygenHelper::self()->animatedShadowImage(hasBorder, qreal(index) / maxIndex);
            if (!image.isNull()) {
                iter = s_animatedShadows.insert(key, makeShadow(image));
            }
        }
        if (iter != s_animatedShadows.end()) {
            setShadow(iter.value());
            return;
        }
    }

    auto &cached = s_cachedShadows[hasBorder ? 1 : 0][active ? 1 : 0];
    if (!cached) {
        const QImage shadowImage = OxygenHelper::self()->shadowImage(active, hasBorder);
        if (shadowImage.isNull()) {
            setShadow(nullptr);
            return;
        }
        cached = makeShadow(shadowImage);
    }
    setShadow(cached);
}

bool Decoration::menuAlwaysShow() const
{
    if (!m_internalSettings) {
        return true;
    }
    if (m_internalSettings->hideApplicationMenu()) {
        return false;
    }
    return m_internalSettings->menuAlwaysShow();
}

bool Decoration::useSystemMenuFont() const
{
    return m_internalSettings ? m_internalSettings->useSystemMenuFont() : false;
}

bool Decoration::hamburgerMenu() const
{
    return m_internalSettings ? m_internalSettings->hamburgerMenu() : false;
}

bool Decoration::searchEnabled() const
{
    return m_internalSettings ? m_internalSettings->searchEnabled() : true;
}

bool Decoration::showDisabledActions() const
{
    return m_internalSettings ? m_internalSettings->showDisabledActions() : false;
}

bool Decoration::searchIgnoreTopLevel() const
{
    return m_internalSettings ? m_internalSettings->searchIgnoreTopLevel() : true;
}

bool Decoration::searchIgnoreSubMenus() const
{
    return m_internalSettings ? m_internalSettings->searchIgnoreSubMenus() : false;
}

bool Decoration::searchFuzzyMatching() const
{
    return m_internalSettings ? m_internalSettings->searchFuzzyMatching() : false;
}

bool Decoration::animationsEnabled() const
{
    return m_internalSettings ? m_internalSettings->animationsEnabled() : true;
}

int Decoration::animationsDuration() const
{
    return m_internalSettings ? m_internalSettings->animationsDuration() : 250;
}

bool Decoration::dragFromButtonsEnabled() const
{
    return m_internalSettings ? m_internalSettings->dragFromButtonsEnabled() : true;
}

bool Decoration::hideCaptionWhenLimitedSpace() const
{
    return m_internalSettings ? m_internalSettings->hideCaptionWhenLimitedSpace() : true;
}

bool Decoration::showCaptionOnHover() const
{
    return m_internalSettings ? m_internalSettings->showCaptionOnHover() : false;
}

int Decoration::buttonHeight() const
{
    // oxygen icons were always slightly larger
    const int baseSize = (m_tabletMode ? settings()->gridUnit() * 2 : settings()->gridUnit()) + 2;
    return baseSize * 2;
}

qreal Decoration::captionHeight() const
{
    if (hideTitleBar()) {
        return 0;
    }
    const QFontMetrics fontMetrics(settings()->font());
    return qMax(fontMetrics.height(), buttonHeight());
}

qreal Decoration::titleBarHeight() const
{
    if (hideTitleBar()) {
        return 0;
    }
    return captionHeight() + settings()->smallSpacing() * (Metrics::TitleBar_BottomMargin + Metrics::TitleBar_TopMargin);
}

qreal Decoration::rowTop() const
{
    return settings()->smallSpacing() * Metrics::TitleBar_TopMargin;
}

qreal Decoration::menuExtension() const
{
    return isMaximized() ? rowTop() : 0;
}

qreal Decoration::menuTop() const
{
    return rowTop() - menuExtension();
}

qreal Decoration::buttonSpacing() const
{
    return settings()->smallSpacing() * Metrics::TitleBar_ButtonSpacing;
}

qreal Decoration::sideMargin() const
{
    return settings()->smallSpacing() * Metrics::TitleBar_SideMargin;
}

qreal Decoration::appMenuCaptionSpacing() const
{
    return settings()->largeSpacing() * 3;
}

qreal Decoration::captionMinWidth() const
{
    return settings()->largeSpacing() * 6;
}

qreal Decoration::borderSize(bool bottom) const
{
    const int baseSize = settings()->smallSpacing();
    switch (settings()->borderSize()) {
    case KDecoration3::BorderSize::None:
        return 0;
    case KDecoration3::BorderSize::NoSides:
        return bottom ? qMax(4, baseSize) : 0;
    default:
    case KDecoration3::BorderSize::Tiny:
        return bottom ? qMax(4, baseSize) : baseSize;
    case KDecoration3::BorderSize::Normal:
        return baseSize * 2;
    case KDecoration3::BorderSize::Large:
        return baseSize * 3;
    case KDecoration3::BorderSize::VeryLarge:
        return baseSize * 4;
    case KDecoration3::BorderSize::Huge:
        return baseSize * 5;
    case KDecoration3::BorderSize::VeryHuge:
        return baseSize * 6;
    case KDecoration3::BorderSize::Oversized:
        return baseSize * 10;
    }
}

bool Decoration::hasNoBorders() const
{
    return settings()->borderSize() == KDecoration3::BorderSize::None;
}

bool Decoration::hasNoSideBorders() const
{
    return settings()->borderSize() == KDecoration3::BorderSize::NoSides;
}

bool Decoration::isMaximized() const
{
    return window()->isMaximized();
}

bool Decoration::isMaximizedHorizontally() const
{
    return window()->isMaximizedHorizontally();
}

bool Decoration::isMaximizedVertically() const
{
    return window()->isMaximizedVertically();
}

qreal Decoration::leftOffset() const
{
    if (isShadowOnly() || isMaximizedHorizontally() || window()->adjacentScreenEdges().testFlag(Qt::LeftEdge)) {
        return 0;
    }
    return borderSize();
}

qreal Decoration::rightOffset() const
{
    if (isShadowOnly() || isMaximizedHorizontally() || window()->adjacentScreenEdges().testFlag(Qt::RightEdge)) {
        return 0;
    }
    return borderSize();
}

qreal Decoration::bottomOffset() const
{
    if (isShadowOnly() || isMaximizedVertically() || window()->isShaded() || window()->adjacentScreenEdges().testFlag(Qt::BottomEdge)) {
        return 0;
    }
    return borderSize(true);
}

bool Decoration::titleBarIsHovered() const
{
    return sectionUnderMouse() == Qt::TitleBarArea;
}

QFont Decoration::menuFont() const
{
    return useSystemMenuFont() ? QApplication::font("QMenu") : settings()->font();
}

qreal Decoration::getMenuTextWidth(const QString &text, bool showMnemonic) const
{
    const QFontMetricsF fontMetrics(menuFont());
    const int flags = showMnemonic ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
    // Use an unconstrained bounding rect to get the ideal width.
    const QRectF boundingRect = fontMetrics.boundingRect(QRectF(), flags, text);
    const qreal scale = window()->nextScale();
    return qCeil(boundingRect.width() * scale) / scale;
}

bool Decoration::isMenuOnRight() const
{
    const auto buttonsRight = settings()->decorationButtonsRight();
    return buttonsRight.contains(KDecoration3::DecorationButtonType::ApplicationMenu);
}

QPoint Decoration::windowPos() const
{
    if (KWindowSystem::isPlatformX11()) {
        if (const auto *p = parent()) {
            return p->property("clientGeometry").toRect().topLeft();
        }
    }    
    
    return QPoint(0, 0);
}


void Decoration::initDragMove(const QPoint pos)
{
    m_pressedPoint = pos;
}


void Decoration::resetDragMove()
{
    m_pressedPoint = QPoint();
}

bool Decoration::dragMoveTick(const QPoint pos)
{
    if (m_pressedPoint.isNull()) {
        return false;
    }
    // A popup can consume the release before the decoration receives it.
    if (!QApplication::mouseButtons().testFlag(Qt::LeftButton)) {
        resetDragMove();
        return false;
    }

    QPoint diff = pos - m_pressedPoint;
    if (diff.manhattanLength() >= QApplication::startDragDistance()) {
        resetDragMove();
        return true;
    }
    return false;
}

void Decoration::unPressAllButtons()
{
    auto forceUnpress = [](Button *b) {
        b->cancelLongPress();
        b->forceUnpress();
    };
    forEachButton<Button>(m_leftButtons, forceUnpress);
    forEachButton<Button>(m_rightButtons, forceUnpress);
    forEachButton<Button>(m_menuButtons, forceUnpress);
}

void Decoration::cancelButtonActionsForDrag()
{
    resetDragMove();
    closeTilingPopup();
    unPressAllButtons();
}

QColor Decoration::titleBarBackgroundColor() const
{
    const QPalette palette = window()->palette();
    const QColor active = palette.color(QPalette::Active, QPalette::Window);
    const QColor inactive = palette.color(QPalette::Inactive, QPalette::Window);
    return m_activeOpacity >= 1.0 ? active : (m_activeOpacity <= 0.0 ? inactive : KColorUtils::mix(inactive, active, m_activeOpacity));
}

QColor Decoration::titleBarForegroundColor() const
{
    const QPalette palette = window()->palette();
    const QColor active = palette.color(QPalette::Active, QPalette::WindowText);
    const QColor inactive = palette.color(QPalette::Disabled, QPalette::WindowText);
    return m_activeOpacity >= 1.0 ? active : (m_activeOpacity <= 0.0 ? inactive : KColorUtils::mix(inactive, active, m_activeOpacity));
}

void Decoration::setActiveOpacity(qreal value)
{
    m_activeOpacity = qBound(0.0, value, 1.0);
    if (m_activeAnimation && m_activeAnimation->state() == QAbstractAnimation::Running) {
        updateShadow();
    }
    update();
}

void Decoration::paintWindowBackground(QPainter *painter, const QRectF &repaintRegion) const
{
    const auto *c = window();
    const QRect frame(0, 0, qRound(size().width()), c->isShaded() ? qRound(titleBarHeight()) : qRound(size().height()));
    if (!frame.isValid() || !frame.intersects(repaintRegion.toAlignedRect())) {
        return;
    }

    const QColor color = titleBarBackgroundColor();
    auto *helper = OxygenHelper::self();
    const bool rounded = !isMaximized() && settings()->isAlphaChannelSupported();

    // size of the window minus the outline used for the rounded corners
    QRect inner = frame;
    if (rounded) {
        inner.adjust(1, 1, -1, -1);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (rounded) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(inner), Metrics::CornerRadius - 1, Metrics::CornerRadius - 1);
        painter->setClipPath(clip, Qt::IntersectClip);
    }
    helper->renderWindowBackground(painter, repaintRegion.toAlignedRect(), inner, color, qRound(titleBarHeight()) - 24);
    painter->restore();

    if (rounded) {
        // outline
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QLinearGradient lg(0, -0.5, 0, frame.height() + 0.5);
        lg.setColorAt(0.0, helper->calcLightColor(helper->backgroundTopColor(color)));
        lg.setColorAt(0.51, helper->backgroundBottomColor(color));
        lg.setColorAt(1.0, helper->backgroundBottomColor(color));
        painter->setPen(QPen(lg, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(frame).adjusted(0.5, 0.5, -0.5, -0.5), Metrics::CornerRadius, Metrics::CornerRadius);
        painter->restore();
    }
}

void Decoration::paintCaption(QPainter *painter, const QRectF &repaintRegion) const
{
    // 1. Pre-checks and data gathering
    const auto *decoratedClient = window();
    const bool appMenuVisible = !m_menuButtons->buttons().isEmpty();
    const bool isWaitingForMenu = m_menuButtons->isWaitingForMenu();
    const bool menuLoadedOnce = m_menuButtons->menuLoadedOnce();
    const bool hasAppMenu = decoratedClient->hasApplicationMenu();
    const bool isTitleHidden = m_internalSettings->titleAlignment() == InternalSettings::TitleHidden;

    if (hideTitleBar() || isTitleHidden || (!menuLoadedOnce && (isWaitingForMenu || hasAppMenu))) {
        m_captionLimited = false;
        m_captionRect = QRectF();
        return;
    }

    const QFont font = settings()->font();
    const QString fullCaption = decoratedClient->caption();
    const qreal rowY = rowTop();
    const qreal rowHeight = captionHeight();

    // 2. Baseline constrained geometry (used to determine if space is limited)
    QRectF constrainedRect = centerRect();
    if (appMenuVisible && m_menuButtons->alwaysShow()) {
        const qreal menuWidth = m_menuButtons->visibleWidth() + appMenuCaptionSpacing();
        if (isMenuOnRight()) {
            constrainedRect.setRight(constrainedRect.right() - menuWidth);
        } else {
            constrainedRect.setLeft(constrainedRect.left() + menuWidth);
        }
    }

    // 3. Cache state for interaction
    if (m_captionCache.textWidth < 0 || m_captionCache.fullCaption != fullCaption || m_captionCache.font != font) {
        m_captionCache.fullCaption = fullCaption;
        m_captionCache.font = font;
        m_captionCache.textWidth = QFontMetricsF(font).boundingRect(fullCaption).width();
        m_captionCache.availableWidth = -1.0;
    }
    const qreal textWidth = m_captionCache.textWidth;
    const bool spaceLimited = appMenuVisible && hideCaptionWhenLimitedSpace() && constrainedRect.width() < m_internalSettings->minWidthForCaption();
    const bool textElided = textWidth > constrainedRect.width();

    m_captionLimited = spaceLimited || textElided;
    m_captionRect = QRectF(constrainedRect.left(), rowY, constrainedRect.width(), rowHeight);

    // 4. Reveal logic (show caption on hover if limited)
    const bool isHovered = m_menuButtons->hovered();
    const bool revealingOnHover = showCaptionOnHover() && m_captionLimited && isHovered;

    if (spaceLimited && !revealingOnHover) {
        return;
    }

    // 5. Final drawing geometry and alignment
    QRectF availableRect = (revealingOnHover ? centerRect() : constrainedRect);
    const qreal padding = m_internalSettings->menuButtonHorzPadding();
    availableRect.adjust(padding, 0, -padding, 0);

    QRectF drawingRect;
    Qt::Alignment alignment;
    const QRectF idealRect((size().width() - textWidth) / 2.0, 0, textWidth, rowHeight);

    switch (m_internalSettings->titleAlignment()) {
    case InternalSettings::AlignLeft:
        drawingRect = availableRect;
        alignment = Qt::AlignLeft;
        break;
    case InternalSettings::AlignRight:
        drawingRect = availableRect;
        alignment = Qt::AlignRight;
        break;
    case InternalSettings::AlignCenter:
        drawingRect = availableRect;
        alignment = Qt::AlignCenter;
        break;
    case InternalSettings::AlignCenterFullWidth:
    default:
        if (idealRect.left() < availableRect.left()) {
            drawingRect = availableRect;
            alignment = Qt::AlignLeft;
        } else if (availableRect.right() < idealRect.right()) {
            drawingRect = availableRect;
            alignment = Qt::AlignRight;
        } else {
            drawingRect = QRectF(0, 0, size().width(), rowHeight);
            alignment = Qt::AlignCenter;
        }
        break;
    }

    if (drawingRect.width() <= 0) {
        return;
    }

    drawingRect.setTop(rowY);
    drawingRect.setHeight(rowHeight);

    PixelSnapper snapper(painter);
    drawingRect = snapper.snap(drawingRect);

    if (!drawingRect.intersects(repaintRegion)) {
        return;
    }

    // 6. Painter setup and drawing
    painter->save();
    painter->setFont(font);

    // Opacity logic
    if (appMenuVisible) {
        if (!m_menuButtons->alwaysShow() || revealingOnHover) {
            painter->setOpacity(1.0 - m_menuButtons->opacity());
        }
    }

    if (m_captionCache.availableWidth != drawingRect.width() || m_captionCache.alignment != alignment) {
        m_captionCache.availableWidth = drawingRect.width();
        m_captionCache.alignment = alignment;
        m_captionCache.elidedCaption = QFontMetricsF(font).elidedText(fullCaption, Qt::ElideMiddle, drawingRect.width());
    }

    const int textFlags = alignment | Qt::TextSingleLine | Qt::AlignVCenter;

    // Oxygen draws a light "contrast" copy of the title one pixel below it
    const QColor contrast = OxygenHelper::self()->calcLightColor(titleBarBackgroundColor());
    if (contrast.isValid()) {
        painter->setPen(contrast);
        painter->drawText(drawingRect.translated(0, 1), textFlags, m_captionCache.elidedCaption);
    }

    painter->setPen(titleBarForegroundColor());
    painter->drawText(drawingRect, textFlags, m_captionCache.elidedCaption);
    painter->restore();
}

void Decoration::paintButtons(QPainter *painter, const QRectF &repaintRegion) const
{
    if (hideTitleBar()) {
        return;
    }
    if (m_leftButtons) {
        m_leftButtons->paint(painter, repaintRegion);
    }
    if (m_rightButtons) {
        m_rightButtons->paint(painter, repaintRegion);
    }
    if (m_menuButtons) {
        m_menuButtons->paint(painter, repaintRegion);
    }
}

void Decoration::onSizeChanged()
{
    updateBlur();
    invalidateCaptionCache();
}

void Decoration::onNextScaleChanged()
{
    updateBordersCornersBlurShadow();
    updateTitleBar();
    updateButtonsGeometry();
}

void Decoration::onWidthChanged()
{
    updateTitleBar();
    updateButtonsGeometry();
}

void Decoration::onMaximizedChanged()
{
    updateBordersCornersBlurShadow();
    updateButtonsGeometry();
    updateTitleBar();
}

void Decoration::onShadedChanged()
{
    updateBordersCornersBlurShadow();
    updateButtonsGeometry();
}

void Decoration::onActiveChanged()
{
    const bool active = window()->isActive();
    const qreal target = active ? 1.0 : 0.0;

    m_activeAnimation->stop();
    if (!animationsEnabled() || animationsDuration() <= 0) {
        setActiveOpacity(target);
        updateShadow();
        return;
    }

    m_activeAnimation->setStartValue(m_activeOpacity);
    m_activeAnimation->setEndValue(target);
    m_activeAnimation->setDuration(animationsDuration());
    m_activeAnimation->start();
    updateShadow();
}

void Decoration::onAdjacentScreenEdgesChanged()
{
    updateBordersCornersBlurShadow();
    updateTitleBar();
    updateButtonsGeometry();
}

void Decoration::onSpacingChanged()
{
    updateBordersCornersBlurShadow();
    updateButtonsGeometryDelayed();
}

void Decoration::onTabletModeChanged(bool mode)
{
#if HAVE_WAYLAND
    if (KWindowSystem::isPlatformWayland()) {
        if (m_tabletMode == mode) {
            return;
        }
        m_tabletMode = mode;
        updateBordersCornersBlurShadow();
        updateResizeBorders();
        updateTitleBar();
        updateButtonsGeometryDelayed();
        update();
        return;
    }
#endif
    Q_UNUSED(mode);
}


} // namespace OxygenLim
