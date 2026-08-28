#include "gui/InstallController.h"

#include "core/ArchiveReader.h"
#include "core/Installer.h"
#include "core/Paths.h"

#include <KLocalizedString>

#include <QFileInfo>
#include <QPointer>
#include <QtConcurrentRun>

namespace tardrop::gui {

InstallController::InstallController(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFutureWatcher<Outcome>(this))
{
    connect(m_watcher, &QFutureWatcher<Outcome>::finished, this, &InstallController::finish);
}

void InstallController::enqueue(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (const Result<archives::Format> format = archives::detect(path); format) {
            m_queue.enqueue(path);
        } else {
            Q_EMIT failed(QStringLiteral("%1: %2").arg(path, format.error()));
        }
    }
    Q_EMIT stateChanged();
    startNext();
}

void InstallController::startNext()
{
    if (m_running || m_waitingForAnswer || m_queue.isEmpty()) {
        return;
    }
    const QString path = m_queue.dequeue();

    // The real name is only known after inspection, so the pre-flight check uses the archive's own
    // name. It exists purely to raise the replace/keep-both question before any work starts.
    const Result<QString> applications = paths::applicationsDir();
    const bool likelyExists =
        applications
        && QFileInfo::exists(paths::join(*applications, paths::archiveStem(path)));

    if (likelyExists) {
        m_waitingForAnswer = true;
        m_pendingArchive = path;
        Q_EMIT stateChanged();
        Q_EMIT existingInstallPrompt(path, paths::archiveStem(path));
        return;
    }
    startWorker(path, ExistingChoice::KeepBoth, QString(), false);
}

void InstallController::startWorker(const QString &path,
                                    ExistingChoice choice,
                                    const QString &selectedLauncher,
                                    bool allowUnsafeContent)
{
    m_current = path;
    m_currentChoice = choice;
    m_currentAllowUnsafe = allowUnsafeContent;
    m_running = true;
    Q_EMIT logLine(i18n("Installing %1…", path));
    Q_EMIT stateChanged();

    // Log lines are produced on the worker thread and marshalled back through the event loop.
    QPointer<InstallController> self(this);
    Logger log = [self](const QString &line) {
        QMetaObject::invokeMethod(
            self ? self.data() : nullptr,
            [self, line] {
                if (self) {
                    Q_EMIT self->logLine(line);
                }
            },
            Qt::QueuedConnection);
    };

    m_watcher->setFuture(
        QtConcurrent::run([path, choice, selectedLauncher, log, allowUnsafeContent] {
            return installer::install(path, choice, selectedLauncher, log, allowUnsafeContent);
        }));
}

void InstallController::finish()
{
    const Outcome outcome = m_watcher->result();
    const QString path = m_current;
    const ExistingChoice choice = m_currentChoice;
    m_current.clear();
    m_running = false;

    /// Parks the queue on this archive until the modal decision it raised has been answered.
    const auto await = [this, path, choice] {
        m_waitingForAnswer = true;
        m_pendingArchive = path;
        m_currentChoice = choice;
        Q_EMIT stateChanged();
    };

    if (!outcome) {
        Q_EMIT failed(i18n("Installation failed: %1", outcome.error()));
    } else if (const auto *app = std::get_if<InstalledApp>(&*outcome)) {
        Q_EMIT installed(*app);
    } else if (const auto *launcher = std::get_if<NeedsLauncherChoice>(&*outcome)) {
        await();
        Q_EMIT launcherPrompt(path, launcher->candidates);
        return;
    } else {
        const auto &refused = std::get<NeedsSecurityConfirmation>(*outcome);
        await();
        Q_EMIT securityPrompt(path, refused.concerns);
        return;
    }

    Q_EMIT stateChanged();
    startNext();
}

void InstallController::answerExisting(ExistingChoice choice)
{
    if (!m_waitingForAnswer) {
        return;
    }
    const QString path = m_pendingArchive;
    m_waitingForAnswer = false;
    m_pendingArchive.clear();

    if (choice == ExistingChoice::Cancel) {
        Q_EMIT logLine(i18n("Installation cancelled."));
        Q_EMIT stateChanged();
        startNext();
        return;
    }
    startWorker(path, choice, QString(), false);
}

void InstallController::answerLauncher(const QString &relativePath)
{
    if (!m_waitingForAnswer) {
        return;
    }
    const QString path = m_pendingArchive;
    const ExistingChoice choice = m_currentChoice;
    // An archive whose content the user already accepted must not be asked about a second time.
    const bool allowUnsafe = m_currentAllowUnsafe;
    m_waitingForAnswer = false;
    m_pendingArchive.clear();

    if (relativePath.isEmpty()) {
        Q_EMIT logLine(i18n("Installation cancelled while choosing a launcher."));
        Q_EMIT stateChanged();
        startNext();
        return;
    }
    startWorker(path, choice, relativePath, allowUnsafe);
}

void InstallController::answerSecurity(bool installAnyway)
{
    if (!m_waitingForAnswer) {
        return;
    }
    const QString path = m_pendingArchive;
    const ExistingChoice choice = m_currentChoice;
    m_waitingForAnswer = false;
    m_pendingArchive.clear();

    if (!installAnyway) {
        Q_EMIT logLine(i18n("Installation rejected: %1 did not pass a security check.", path));
        Q_EMIT stateChanged();
        startNext();
        return;
    }
    Q_EMIT logLine(i18n("Continuing at your request; the refused content stays out of the "
                        "installation."));
    startWorker(path, choice, QString(), true);
}

} // namespace tardrop::gui
