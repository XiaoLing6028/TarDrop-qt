#pragma once

/// The TarDrop window.
///
/// This class owns presentation and wiring only. Archive handling stays in `tardrop::installer`,
/// which keeps the window free to change without weakening the security boundary.

#include "core/Types.h"

#include <QList>
#include <QMainWindow>

class KMessageWidget;
class KPageWidget;
class KPageWidgetItem;

namespace tardrop::gui {

class InstallController;
class InstallPage;
class RecordsPage;
class SettingsPage;
class UpdateController;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Queues archives passed on the command line or through a second invocation.
    void openArchives(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void connectControllers();
    void reloadRecords();
    void showInfo(const QString &text);
    void showError(const QString &text);
    void syncActivity();
    void maybeCheckOnStartup();

    void uninstallApp(const InstalledApp &app);
    void uninstallRecord(const InstalledRecord &record);
    void configureSource(const InstalledRecord &record);

    KMessageWidget *m_message = nullptr;
    KPageWidget *m_pages = nullptr;
    KPageWidgetItem *m_installItem = nullptr;
    KPageWidgetItem *m_installedItem = nullptr;
    KPageWidgetItem *m_updatesItem = nullptr;
    KPageWidgetItem *m_settingsItem = nullptr;

    InstallPage *m_installPage = nullptr;
    RecordsPage *m_installedPage = nullptr;
    RecordsPage *m_updatesPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;

    InstallController *m_installs = nullptr;
    UpdateController *m_updates = nullptr;

    QList<InstalledRecord> m_records;
    UpdateSettings m_settings;
};

} // namespace tardrop::gui
