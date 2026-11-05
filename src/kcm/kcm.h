#pragma once

#include <KCModule>
#include <memory>

class KConfig;
class QWidget;
class QObject;
class KPluginMetaData;

namespace Ui
{
class Config;
}

namespace OxygenLim
{
class InternalSettings;
}

class OxygenDecorationLimKCM : public KCModule
{
    Q_OBJECT
public:
    explicit OxygenDecorationLimKCM(QObject *parent, const KPluginMetaData &data);
    ~OxygenDecorationLimKCM() override;

    void load() override;
    void save() override;
    void defaults() override;

private:
    void updateChanged();

private:
    std::unique_ptr<Ui::Config> m_ui;
    std::unique_ptr<OxygenLim::InternalSettings> m_settings;

    void setupConnections();
    bool isChanged() const;
    void updateUI();
    void loadUIFromSettings(const OxygenLim::InternalSettings &settings);
};
