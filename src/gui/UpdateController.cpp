#include "gui/UpdateController.h"

#include "core/Updater.h"

#include <KLocalizedString>

#include <QPointer>
#include <QtConcurrentRun>

namespace tardrop::gui {

UpdateController::UpdateController(QObject *parent)
    : QObject(parent)
    , m_checkWatcher(new QFutureWatcher<CheckOutcome>(this))
    , m_updateWatcher(new QFutureWatcher<UpdateOutcome>(this))
{
    connect(m_checkWatcher, &QFutureWatcher<CheckOutcome>::finished, this,
            &UpdateController::finishCheck);
    connect(m_updateWatcher, &QFutureWatcher<UpdateOutcome>::finished, this,
            &UpdateController::finishUpdate);
}

Logger UpdateController::makeLogger()
{
    QPointer<UpdateController> self(this);
    return [self](const QString &line) {
        QMetaObject::invokeMethod(
            self ? self.data() : nullptr,
            [self, line] {
                if (self) {
                    Q_EMIT self->logLine(line);
                }
            },
            Qt::QueuedConnection);
    };
}

void UpdateController::check(const InstalledRecord &record)
{
    if (busy()) {
        return;
    }
    m_busyName = record.name;
    Q_EMIT busyChanged();

    m_checkWatcher->setFuture(QtConcurrent::run([record] {
        CheckOutcome outcome;
        outcome.record = record;
        outcome.release = updates::checkForUpdate(outcome.record);
        return outcome;
    }));
}

void UpdateController::update(const InstalledRecord &record)
{
    if (busy()) {
        return;
    }
    m_busyName = record.name;
    Q_EMIT busyChanged();

    const Logger log = makeLogger();
    m_updateWatcher->setFuture(
        QtConcurrent::run([record, log] { return updates::update(record, log); }));
}

void UpdateController::finishCheck()
{
    const CheckOutcome outcome = m_checkWatcher->result();
    m_busyName.clear();
    Q_EMIT busyChanged();

    if (!outcome.release) {
        Q_EMIT failed(i18n("Update check failed: %1", outcome.release.error()));
        return;
    }
    const std::optional<ReleaseInfo> release = *outcome.release;
    Q_EMIT checked(outcome.record, release.has_value(), release.value_or(ReleaseInfo{}));
}

void UpdateController::finishUpdate()
{
    const UpdateOutcome outcome = m_updateWatcher->result();
    m_busyName.clear();
    Q_EMIT busyChanged();

    if (!outcome) {
        Q_EMIT failed(i18n("Update failed: %1", outcome.error()));
        return;
    }
    Q_EMIT updated(*outcome);
}

} // namespace tardrop::gui
