#pragma once

/// Sequential install orchestration.
///
/// Archive work runs on a thread-pool task so the window stays interactive; the queue advances
/// only after the previous task, and any modal decision it raised, has resolved.

#include "core/Types.h"

#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

namespace tardrop::gui {

class InstallController : public QObject
{
    Q_OBJECT
public:
    explicit InstallController(QObject *parent = nullptr);

    /// Adds supported files to the sequential queue; unsuitable files receive a friendly error.
    void enqueue(const QStringList &paths);

    [[nodiscard]] bool busy() const { return m_running; }
    [[nodiscard]] int queued() const { return static_cast<int>(m_queue.size()); }
    [[nodiscard]] QString currentArchive() const { return m_current; }

public Q_SLOTS:
    /// Answers `existingInstallPrompt`.
    void answerExisting(tardrop::ExistingChoice choice);
    /// Answers `launcherPrompt`; an empty path cancels this archive.
    void answerLauncher(const QString &relativePath);

Q_SIGNALS:
    void logLine(const QString &line);
    void stateChanged();
    void installed(const tardrop::InstalledApp &app);
    void failed(const QString &message);
    void existingInstallPrompt(const QString &archivePath, const QString &name);
    void launcherPrompt(const QString &archivePath,
                        const QList<tardrop::LauncherCandidate> &candidates);

private:
    using Outcome = Result<InstallResult>;

    void startNext();
    void startWorker(const QString &path, ExistingChoice choice, const QString &selectedLauncher);
    void finish();

    QQueue<QString> m_queue;
    QString m_current;
    ExistingChoice m_currentChoice = ExistingChoice::KeepBoth;
    bool m_running = false;
    bool m_waitingForAnswer = false;
    QString m_pendingArchive;
    QFutureWatcher<Outcome> *m_watcher = nullptr;
};

} // namespace tardrop::gui
