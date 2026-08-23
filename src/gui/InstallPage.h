#pragma once

/// The Install page: drop surface, current activity, session results, and the technical log.

#include "core/Types.h"

#include <QList>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QVBoxLayout;

namespace tardrop::gui {

class DropZone;

class InstallPage : public QWidget
{
    Q_OBJECT
public:
    explicit InstallPage(QWidget *parent = nullptr);

    void appendLog(const QString &line);
    void setActivity(bool busy, const QString &currentArchive, int queued);
    void addInstalledApp(const InstalledApp &app);
    void removeInstalledApp(const QString &directory);

Q_SIGNALS:
    void archivesChosen(const QStringList &paths);
    void launchRequested(const tardrop::InstalledApp &app);
    void openFolderRequested(const tardrop::InstalledApp &app);
    void uninstallRequested(const tardrop::InstalledApp &app);

private:
    void rebuildInstalledApps();

    DropZone *m_dropZone = nullptr;
    QLabel *m_activityState = nullptr;
    QLabel *m_activityDetail = nullptr;
    QProgressBar *m_progress = nullptr;
    QVBoxLayout *m_appsLayout = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QList<InstalledApp> m_apps;
};

} // namespace tardrop::gui
