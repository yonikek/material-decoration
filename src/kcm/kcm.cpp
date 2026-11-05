#include "kcm.h"

#include <KPluginMetaData>
#include <QObject>

#include "../InternalSettings.h"

#include <ui_config.h>

#include <KLocalizedString>
#include <KPluginFactory>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QIcon>
#include <QLabel>
#include <QTabBar>


K_PLUGIN_CLASS_WITH_JSON(OxygenDecorationLimKCM, "oxygendecorationlim_kcm.json")

OxygenDecorationLimKCM::OxygenDecorationLimKCM(QObject *parent, const KPluginMetaData &data)
    : KCModule(qobject_cast<QWidget*>(parent), data)
    , m_ui(std::make_unique<Ui::Config>())
    , m_settings(std::make_unique<OxygenLim::InternalSettings>())
{
    m_ui->setupUi(widget());

    if (auto *bar = m_ui->tabWidget->findChild<QTabBar *>()) {
        bar->setExpanding(true);
    }

    // Populate combo boxes
    m_ui->kcfg_TitleAlignment->addItems({i18n("Left"),
                                         i18n("Center"),
                                         i18n("Center (Full Width)"),
                                         i18n("Right"),
                                         i18n("Hidden")});


    

    m_ui->longPressInfoButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));

    setupConnections();
}

OxygenDecorationLimKCM::~OxygenDecorationLimKCM() = default;

void OxygenDecorationLimKCM::setupConnections()
{
    connect(m_ui->kcfg_TitleAlignment, &QComboBox::currentIndexChanged, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_MenuAlwaysShow, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_MenuAlwaysShow, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            m_ui->kcfg_ShowCaptionOnHover->setChecked(false);
            m_ui->kcfg_ShowCaptionOnHover->setEnabled(false);
        } else {
            m_ui->kcfg_ShowCaptionOnHover->setEnabled(true);
        }
    });
    connect(m_ui->kcfg_SearchEnabled, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_SearchEnabled, &QCheckBox::toggled, m_ui->kcfg_ShowDisabledActions, &QWidget::setEnabled);
    connect(m_ui->kcfg_SearchEnabled, &QCheckBox::toggled, m_ui->kcfg_SearchIgnoreTopLevel, &QWidget::setEnabled);
    connect(m_ui->kcfg_SearchEnabled, &QCheckBox::toggled, m_ui->kcfg_SearchIgnoreSubMenus, &QWidget::setEnabled);
    connect(m_ui->kcfg_SearchEnabled, &QCheckBox::toggled, m_ui->kcfg_SearchFuzzyMatching, &QWidget::setEnabled);

    connect(m_ui->kcfg_SearchIgnoreSubMenus, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_ui->kcfg_SearchIgnoreTopLevel->setChecked(true);
        }
        updateChanged();
    });
    connect(m_ui->kcfg_SearchIgnoreTopLevel, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            m_ui->kcfg_SearchIgnoreSubMenus->setChecked(false);
        }
        updateChanged();
    });


    connect(m_ui->kcfg_HamburgerMenu, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_ShowDisabledActions, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_SearchFuzzyMatching, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_MenuButtonHorzPadding, &QSpinBox::valueChanged, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_UseSystemMenuFont, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_AnimationsEnabled, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_AnimationsDuration, &QSpinBox::valueChanged, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_HideCaptionWhenLimitedSpace, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_HideCaptionWhenLimitedSpace, &QCheckBox::toggled, m_ui->kcfg_MinWidthForCaption, &QWidget::setEnabled);
    connect(m_ui->kcfg_HideCaptionWhenLimitedSpace, &QCheckBox::toggled, m_ui->label_minWidthForCaption, &QWidget::setEnabled);
    connect(m_ui->kcfg_MinWidthForCaption, qOverload<int>(&QSpinBox::valueChanged), this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_ShowCaptionOnHover, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);

    connect(m_ui->kcfg_TilingPopupHoverEnabled, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_TilingPopupHoverEnabled, &QCheckBox::toggled, m_ui->kcfg_TilingPopupHoverDelay, &QWidget::setEnabled);
    connect(m_ui->kcfg_TilingPopupHoverDelay, qOverload<int>(&QSpinBox::valueChanged), this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_LongPressEnabled, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);
    connect(m_ui->kcfg_LongPressEnabled, &QCheckBox::toggled, m_ui->kcfg_LongPressDuration, &QWidget::setEnabled);
    connect(m_ui->kcfg_LongPressDuration, qOverload<int>(&QSpinBox::valueChanged), this, &OxygenDecorationLimKCM::updateChanged);

    connect(m_ui->kcfg_DragFromButtonsEnabled, &QCheckBox::toggled, this, &OxygenDecorationLimKCM::updateChanged);

    connect(m_ui->exceptionsWidget, &OxygenLim::ExceptionListWidget::changed, this, &OxygenDecorationLimKCM::updateChanged);
}

void OxygenDecorationLimKCM::load()
{
    m_settings->load();
    m_ui->exceptionsWidget->load();
    updateUI();
    updateChanged();
}

void OxygenDecorationLimKCM::loadUIFromSettings(const OxygenLim::InternalSettings &settings)
{
    m_ui->kcfg_TitleAlignment->setCurrentIndex(settings.titleAlignment());
    m_ui->kcfg_MenuAlwaysShow->setChecked(settings.menuAlwaysShow());
    m_ui->kcfg_ShowCaptionOnHover->setChecked(settings.showCaptionOnHover());
    m_ui->kcfg_ShowCaptionOnHover->setEnabled(settings.menuAlwaysShow());
    m_ui->kcfg_SearchEnabled->setChecked(settings.searchEnabled());
    m_ui->kcfg_ShowDisabledActions->setEnabled(settings.searchEnabled());
    m_ui->kcfg_SearchIgnoreTopLevel->setEnabled(settings.searchEnabled());
    m_ui->kcfg_SearchIgnoreSubMenus->setEnabled(settings.searchEnabled());
    m_ui->kcfg_SearchFuzzyMatching->setEnabled(settings.searchEnabled());
    m_ui->kcfg_HamburgerMenu->setChecked(settings.hamburgerMenu());
    m_ui->kcfg_ShowDisabledActions->setChecked(settings.showDisabledActions());
    m_ui->kcfg_SearchIgnoreTopLevel->setChecked(settings.searchIgnoreTopLevel());
    m_ui->kcfg_SearchIgnoreSubMenus->setChecked(settings.searchIgnoreSubMenus());
    m_ui->kcfg_SearchFuzzyMatching->setChecked(settings.searchFuzzyMatching());
    m_ui->kcfg_MenuButtonHorzPadding->setValue(settings.menuButtonHorzPadding());
    m_ui->kcfg_UseSystemMenuFont->setChecked(settings.useSystemMenuFont());
    m_ui->kcfg_AnimationsEnabled->setChecked(settings.animationsEnabled());
    m_ui->kcfg_AnimationsDuration->setValue(settings.animationsDuration());
    m_ui->kcfg_HideCaptionWhenLimitedSpace->setChecked(settings.hideCaptionWhenLimitedSpace());
    m_ui->kcfg_MinWidthForCaption->setValue(settings.minWidthForCaption());
    m_ui->kcfg_MinWidthForCaption->setEnabled(settings.hideCaptionWhenLimitedSpace());
    m_ui->label_minWidthForCaption->setEnabled(settings.hideCaptionWhenLimitedSpace());

    m_ui->kcfg_TilingPopupHoverEnabled->setChecked(settings.tilingPopupHoverEnabled());
    m_ui->kcfg_TilingPopupHoverDelay->setValue(settings.tilingPopupHoverDelay());
    m_ui->kcfg_TilingPopupHoverDelay->setEnabled(settings.tilingPopupHoverEnabled());
    m_ui->kcfg_LongPressEnabled->setChecked(settings.longPressEnabled());
    m_ui->kcfg_LongPressDuration->setValue(settings.longPressDuration());
    m_ui->kcfg_LongPressDuration->setEnabled(settings.longPressEnabled());

    m_ui->kcfg_DragFromButtonsEnabled->setChecked(settings.dragFromButtonsEnabled());
}

void OxygenDecorationLimKCM::updateUI()
{
    loadUIFromSettings(*m_settings);
}

void OxygenDecorationLimKCM::save()
{
    m_ui->exceptionsWidget->save();

    m_settings->setTitleAlignment(m_ui->kcfg_TitleAlignment->currentIndex());
    m_settings->setMenuAlwaysShow(m_ui->kcfg_MenuAlwaysShow->isChecked());
    m_settings->setShowCaptionOnHover(m_ui->kcfg_ShowCaptionOnHover->isChecked());
    m_settings->setSearchEnabled(m_ui->kcfg_SearchEnabled->isChecked());
    m_settings->setHamburgerMenu(m_ui->kcfg_HamburgerMenu->isChecked());
    m_settings->setShowDisabledActions(m_ui->kcfg_ShowDisabledActions->isChecked());
    m_settings->setSearchIgnoreTopLevel(m_ui->kcfg_SearchIgnoreTopLevel->isChecked());
    m_settings->setSearchIgnoreSubMenus(m_ui->kcfg_SearchIgnoreSubMenus->isChecked());
    m_settings->setSearchFuzzyMatching(m_ui->kcfg_SearchFuzzyMatching->isChecked());
    m_settings->setMenuButtonHorzPadding(m_ui->kcfg_MenuButtonHorzPadding->value());
    m_settings->setUseSystemMenuFont(m_ui->kcfg_UseSystemMenuFont->isChecked());
    m_settings->setAnimationsEnabled(m_ui->kcfg_AnimationsEnabled->isChecked());
    m_settings->setAnimationsDuration(m_ui->kcfg_AnimationsDuration->value());
    m_settings->setHideCaptionWhenLimitedSpace(m_ui->kcfg_HideCaptionWhenLimitedSpace->isChecked());
    m_settings->setMinWidthForCaption(m_ui->kcfg_MinWidthForCaption->value());

    m_settings->setTilingPopupHoverEnabled(m_ui->kcfg_TilingPopupHoverEnabled->isChecked());
    m_settings->setTilingPopupHoverDelay(m_ui->kcfg_TilingPopupHoverDelay->value());
    m_settings->setLongPressEnabled(m_ui->kcfg_LongPressEnabled->isChecked());
    m_settings->setLongPressDuration(m_ui->kcfg_LongPressDuration->value());

    m_settings->setDragFromButtonsEnabled(m_ui->kcfg_DragFromButtonsEnabled->isChecked());

    m_settings->save();
    const auto message = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("reconfigure"));
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, [](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<> reply = *finished;
        if (reply.isError()) {
            qWarning() << "oxygen-decoration-lim: failed to notify KWin to reconfigure:"
                       << reply.error().name()
                       << reply.error().message();
        }
        finished->deleteLater();
    });
    updateChanged();
}

void OxygenDecorationLimKCM::defaults()
{
    m_ui->exceptionsWidget->defaults();

    OxygenLim::InternalSettings s;
    s.setDefaults();

    loadUIFromSettings(s);

    updateChanged();
}

bool OxygenDecorationLimKCM::isChanged() const
{
    if (m_ui->kcfg_TitleAlignment->currentIndex() != m_settings->titleAlignment()) return true;
    if (m_ui->kcfg_MenuAlwaysShow->isChecked() != m_settings->menuAlwaysShow()) return true;
    if (m_ui->kcfg_ShowCaptionOnHover->isChecked() != m_settings->showCaptionOnHover()) return true;
    if (m_ui->kcfg_SearchEnabled->isChecked() != m_settings->searchEnabled()) return true;
    if (m_ui->kcfg_HamburgerMenu->isChecked() != m_settings->hamburgerMenu()) return true;
    if (m_ui->kcfg_ShowDisabledActions->isChecked() != m_settings->showDisabledActions()) return true;
    if (m_ui->kcfg_SearchIgnoreTopLevel->isChecked() != m_settings->searchIgnoreTopLevel()) return true;
    if (m_ui->kcfg_SearchIgnoreSubMenus->isChecked() != m_settings->searchIgnoreSubMenus()) return true;
    if (m_ui->kcfg_SearchFuzzyMatching->isChecked() != m_settings->searchFuzzyMatching()) return true;
    if (m_ui->kcfg_MenuButtonHorzPadding->value() != m_settings->menuButtonHorzPadding()) return true;
    if (m_ui->kcfg_UseSystemMenuFont->isChecked() != m_settings->useSystemMenuFont()) return true;
    if (m_ui->kcfg_AnimationsEnabled->isChecked() != m_settings->animationsEnabled()) return true;
    if (m_ui->kcfg_AnimationsDuration->value() != m_settings->animationsDuration()) return true;
    if (m_ui->kcfg_HideCaptionWhenLimitedSpace->isChecked() != m_settings->hideCaptionWhenLimitedSpace()) return true;
    if (m_ui->kcfg_MinWidthForCaption->value() != m_settings->minWidthForCaption()) return true;
    if (m_ui->kcfg_TilingPopupHoverEnabled->isChecked() != m_settings->tilingPopupHoverEnabled()) return true;
    if (m_ui->kcfg_TilingPopupHoverDelay->value() != m_settings->tilingPopupHoverDelay()) return true;
    if (m_ui->kcfg_LongPressEnabled->isChecked() != m_settings->longPressEnabled()) return true;
    if (m_ui->kcfg_LongPressDuration->value() != m_settings->longPressDuration()) return true;
    if (m_ui->kcfg_DragFromButtonsEnabled->isChecked() != m_settings->dragFromButtonsEnabled()) return true;

    if (m_ui->exceptionsWidget->isChanged()) return true;

    return false;
}

void OxygenDecorationLimKCM::updateChanged()
{
    setNeedsSave(isChanged());
}

#include "kcm.moc"
