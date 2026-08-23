#pragma once

/// Update preferences, saved as soon as they change so the next launch sees the selected policy.

#include "core/Types.h"

#include <QWidget>

class QCheckBox;
class QRadioButton;

namespace tardrop::gui {

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void setSettings(const UpdateSettings &settings);
    [[nodiscard]] UpdateSettings settings() const;

Q_SIGNALS:
    void settingsChanged(const tardrop::UpdateSettings &settings);

private:
    void emitChange();

    bool m_loading = false;
    QCheckBox *m_automatic = nullptr;
    QCheckBox *m_beta = nullptr;
    QCheckBox *m_startup = nullptr;
    QRadioButton *m_daily = nullptr;
    QRadioButton *m_weekly = nullptr;
    QRadioButton *m_monthly = nullptr;
    QRadioButton *m_never = nullptr;
};

} // namespace tardrop::gui
