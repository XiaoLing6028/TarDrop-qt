#pragma once

/// Off-thread release checks and reversible update transactions.
///
/// Only one network operation runs at a time, which keeps the "what is TarDrop doing right now?"
/// answer unambiguous and prevents two updates from racing over the same directory.

#include "core/Types.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <optional>

namespace tardrop::gui {

class UpdateController : public QObject
{
    Q_OBJECT
public:
    explicit UpdateController(QObject *parent = nullptr);

    [[nodiscard]] bool busy() const { return !m_busyName.isEmpty(); }
    [[nodiscard]] QString busyName() const { return m_busyName; }

    /// Starts an explicit release check. Manual providers report their configuration requirement.
    void check(const InstalledRecord &record);
    /// Starts a reversible update transaction through the update subsystem.
    void update(const InstalledRecord &record);

Q_SIGNALS:
    void logLine(const QString &line);
    void busyChanged();
    void checked(const tardrop::InstalledRecord &record,
                 bool newerAvailable,
                 const tardrop::ReleaseInfo &release);
    void updated(const tardrop::InstalledRecord &record);
    void failed(const QString &message);

private:
    struct CheckOutcome {
        Result<std::optional<ReleaseInfo>> release = std::optional<ReleaseInfo>{};
        InstalledRecord record;
    };
    using UpdateOutcome = Result<InstalledRecord>;

    void finishCheck();
    void finishUpdate();
    Logger makeLogger();

    QString m_busyName;
    QFutureWatcher<CheckOutcome> *m_checkWatcher = nullptr;
    QFutureWatcher<UpdateOutcome> *m_updateWatcher = nullptr;
};

} // namespace tardrop::gui
