#include "gui/SettingsPage.h"

#include "gui/Widgets.h"

#include <KLocalizedString>

#include <QButtonGroup>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tardrop::gui {

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    scroll->setWidget(content);

    auto *card = new Card;

    m_automatic = new QCheckBox(i18n("Check for updates automatically"));
    m_automatic->setToolTip(i18nc("@info:tooltip",
                                  "Lets TarDrop check configured sources on its own, on the "
                                  "schedule below."));
    m_beta = new QCheckBox(i18n("Notify about beta releases"));
    m_beta->setToolTip(i18nc("@info:tooltip",
                             "Include pre-release versions when reporting an available update."));
    m_startup = new QCheckBox(i18n("Check on startup"));
    m_startup->setToolTip(i18nc("@info:tooltip",
                                "Run one automatic check shortly after TarDrop opens, if due."));
    for (QCheckBox *box : {m_automatic, m_beta, m_startup}) {
        card->body()->addWidget(box);
        connect(box, &QCheckBox::toggled, this, &SettingsPage::emitChange);
    }

    card->body()->addSpacing(6);
    card->body()->addWidget(heading(i18n("Update interval"), 0));
    card->body()->addWidget(caption(i18n("How often automatic checks are allowed to run.")));

    auto *group = new QButtonGroup(this);
    m_daily = new QRadioButton(i18n("Daily"));
    m_weekly = new QRadioButton(i18n("Weekly"));
    m_monthly = new QRadioButton(i18n("Monthly"));
    m_never = new QRadioButton(i18n("Never"));
    for (QRadioButton *button : {m_daily, m_weekly, m_monthly, m_never}) {
        group->addButton(button);
        card->body()->addWidget(button);
        connect(button, &QRadioButton::toggled, this, &SettingsPage::emitChange);
    }

    layout->addWidget(card);
    layout->addStretch();
}

void SettingsPage::setSettings(const UpdateSettings &settings)
{
    // Guarded so that populating the controls does not write the file back immediately.
    m_loading = true;
    m_automatic->setChecked(settings.checkAutomatically);
    m_beta->setChecked(settings.notifyBetaReleases);
    m_startup->setChecked(settings.checkOnStartup);
    switch (settings.interval) {
    case UpdateInterval::Daily:
        m_daily->setChecked(true);
        break;
    case UpdateInterval::Weekly:
        m_weekly->setChecked(true);
        break;
    case UpdateInterval::Monthly:
        m_monthly->setChecked(true);
        break;
    case UpdateInterval::Never:
        m_never->setChecked(true);
        break;
    }
    m_loading = false;
}

UpdateSettings SettingsPage::settings() const
{
    UpdateSettings settings;
    settings.checkAutomatically = m_automatic->isChecked();
    settings.notifyBetaReleases = m_beta->isChecked();
    settings.checkOnStartup = m_startup->isChecked();
    if (m_daily->isChecked()) {
        settings.interval = UpdateInterval::Daily;
    } else if (m_monthly->isChecked()) {
        settings.interval = UpdateInterval::Monthly;
    } else if (m_never->isChecked()) {
        settings.interval = UpdateInterval::Never;
    } else {
        settings.interval = UpdateInterval::Weekly;
    }
    return settings;
}

void SettingsPage::emitChange()
{
    if (!m_loading) {
        Q_EMIT settingsChanged(settings());
    }
}

} // namespace tardrop::gui
