#include "gui/MainWindow.h"

#include "core/Installer.h"
#include "core/Paths.h"
#include "core/Records.h"
#include "core/Updater.h"
#include "gui/Dialogs.h"
#include "gui/InstallController.h"
#include "gui/InstallPage.h"
#include "gui/RecordsPage.h"
#include "gui/SettingsPage.h"
#include "gui/UpdateController.h"
#include "gui/Widgets.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KMessageWidget>
#include <KNotification>
#include <KPageWidget>
#include <KSharedConfig>
#include <KWindowConfig>

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace tardrop::gui {
namespace {

constexpr auto kWindowStateGroup = "MainWindow";

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_installs(new InstallController(this))
    , m_updates(new UpdateController(this))
{
    setWindowTitle(i18nc("@title:window", "TarDrop"));
    setAcceptDrops(true);
    buildUi();
    connectControllers();

    if (const Result<UpdateSettings> settings = database::loadSettings(); settings) {
        m_settings = *settings;
    }
    m_settingsPage->setSettings(m_settings);
    reloadRecords();
    syncActivity();

    resize(880, 600);
    setMinimumSize(680, 440);
    // Restore the size the user last chose, the way other KDE applications do.
    KConfigGroup group(KSharedConfig::openStateConfig(), QLatin1String(kWindowStateGroup));
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), group);
    resize(windowHandle()->size());

    QTimer::singleShot(0, this, &MainWindow::maybeCheckOnStartup);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Title bar --------------------------------------------------------
    auto *titleBar = new QWidget;
    titleBar->setBackgroundRole(QPalette::Window);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 12, 20, 12);
    titleLayout->setSpacing(12);

    auto *logo = new QLabel;
    logo->setPixmap(QIcon::fromTheme(QStringLiteral("package-x-generic")).pixmap(32, 32));
    titleLayout->addWidget(logo);

    auto *titleText = new QVBoxLayout;
    titleText->setSpacing(0);
    titleText->addWidget(heading(i18n("TarDrop"), 4));
    titleText->addWidget(caption(i18n("Portable application installer"), false));
    titleLayout->addLayout(titleText);
    titleLayout->addStretch();

    auto *assurance = caption(i18n("User-only • No sudo"), false);
    assurance->setToolTip(i18nc("@info:tooltip",
                                "TarDrop never asks for your password and never installs "
                                "system-wide."));
    titleLayout->addWidget(assurance);
    layout->addWidget(titleBar);

    // --- Inline feedback --------------------------------------------------
    m_message = new KMessageWidget;
    m_message->setCloseButtonVisible(true);
    m_message->setWordWrap(true);
    m_message->hide();
    auto *messageWrapper = new QWidget;
    auto *messageLayout = new QVBoxLayout(messageWrapper);
    messageLayout->setContentsMargins(20, 0, 20, 8);
    messageLayout->addWidget(m_message);
    layout->addWidget(messageWrapper);

    // --- Pages ------------------------------------------------------------
    // A KPageWidget list gives the same persistent left-hand navigation Plasma's System Settings
    // uses, so the current page stays reachable while a background install or update runs.
    m_pages = new KPageWidget;
    m_pages->setFaceType(KPageView::List);

    m_installPage = new InstallPage;
    m_installItem = m_pages->addPage(m_installPage, i18nc("@title", "Install"));
    m_installItem->setIcon(QIcon::fromTheme(QStringLiteral("document-import")));
    m_installItem->setHeader(i18n("Drop a portable archive to install it safely"));

    m_installedPage = new RecordsPage(false);
    m_installedItem = m_pages->addPage(m_installedPage, i18nc("@title", "Installed Applications"));
    m_installedItem->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details"),
                                              QIcon::fromTheme(QStringLiteral("applications-all"))));
    m_installedItem->setHeader(i18n("Applications TarDrop manages for you"));

    m_updatesPage = new RecordsPage(true);
    m_updatesItem = m_pages->addPage(m_updatesPage, i18nc("@title", "Updates"));
    m_updatesItem->setIcon(QIcon::fromTheme(QStringLiteral("system-software-update")));
    m_updatesItem->setHeader(i18n("Check configured sources and replace an application safely"));

    m_settingsPage = new SettingsPage;
    m_settingsItem = m_pages->addPage(m_settingsPage, i18nc("@title", "Settings"));
    m_settingsItem->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    m_settingsItem->setHeader(i18n("Update preferences"));

    layout->addWidget(m_pages, 1);
    setCentralWidget(central);
}

void MainWindow::connectControllers()
{
    connect(m_installPage, &InstallPage::archivesChosen, m_installs, &InstallController::enqueue);
    connect(m_installPage, &InstallPage::launchRequested, this, [this](const InstalledApp &app) {
        launchDesktopFile(app.desktopFile, this);
    });
    connect(m_installPage, &InstallPage::openFolderRequested, this,
            [this](const InstalledApp &app) { openInFileManager(app.directory, this); });
    connect(m_installPage, &InstallPage::uninstallRequested, this, &MainWindow::uninstallApp);

    for (RecordsPage *page : {m_installedPage, m_updatesPage}) {
        connect(page, &RecordsPage::launchRequested, this, [this](const InstalledRecord &record) {
            launchDesktopFile(record.desktopFilePath, this);
        });
        connect(page, &RecordsPage::openFolderRequested, this,
                [this](const InstalledRecord &record) {
                    openInFileManager(record.installPath, this);
                });
        connect(page, &RecordsPage::uninstallRequested, this, &MainWindow::uninstallRecord);
        connect(page, &RecordsPage::checkRequested, m_updates, &UpdateController::check);
        connect(page, &RecordsPage::updateRequested, m_updates, &UpdateController::update);
        connect(page, &RecordsPage::configureSourceRequested, this, &MainWindow::configureSource);
    }

    connect(m_settingsPage, &SettingsPage::settingsChanged, this,
            [this](const UpdateSettings &settings) {
                m_settings = settings;
                if (const Status status = database::saveSettings(settings); !status) {
                    showError(i18n("Could not save settings: %1", status.error()));
                }
            });

    connect(m_installs, &InstallController::logLine, m_installPage, &InstallPage::appendLog);
    connect(m_installs, &InstallController::stateChanged, this, &MainWindow::syncActivity);
    connect(m_installs, &InstallController::failed, this, &MainWindow::showError);
    connect(m_installs, &InstallController::installed, this, [this](const InstalledApp &app) {
        m_installPage->addInstalledApp(app);
        reloadRecords();
        showInfo(i18n("%1 is ready to use.", app.name));
        // A Plasma notification means the result is not missed while the window is in the
        // background, which is common when several archives are queued.
        auto *notification = new KNotification(QStringLiteral("installed"));
        notification->setTitle(i18n("Application installed"));
        notification->setText(i18n("%1 is now available in your application launcher.", app.name));
        notification->setIconName(app.icon.isEmpty() ? QStringLiteral("package-x-generic")
                                                     : app.icon);
        notification->sendEvent();
    });
    connect(m_installs, &InstallController::existingInstallPrompt, this,
            [this](const QString &, const QString &name) {
                ExistingInstallDialog dialog(name, this);
                dialog.exec();
                m_installs->answerExisting(dialog.choice());
            });
    connect(m_installs, &InstallController::launcherPrompt, this,
            [this](const QString &, const QList<LauncherCandidate> &candidates) {
                LauncherChoiceDialog dialog(candidates, this);
                dialog.exec();
                m_installs->answerLauncher(dialog.selected());
            });

    connect(m_updates, &UpdateController::logLine, m_installPage, &InstallPage::appendLog);
    connect(m_updates, &UpdateController::busyChanged, this, &MainWindow::syncActivity);
    connect(m_updates, &UpdateController::failed, this, &MainWindow::showError);
    connect(m_updates, &UpdateController::checked, this,
            [this](const InstalledRecord &record, bool newer, const ReleaseInfo &release) {
                reloadRecords();
                if (!newer) {
                    showInfo(i18n("%1 is up to date.", record.name));
                    return;
                }
                QString text = i18n("%1 %2 is available.", record.name, release.version);
                if (!release.downloadUrl.isEmpty()) {
                    text += u' ' + i18n("Press Update to download and install it.");
                }
                showInfo(text);
            });
    connect(m_updates, &UpdateController::updated, this, [this](const InstalledRecord &record) {
        reloadRecords();
        showInfo(i18n("%1 was updated successfully.", record.name));
    });
}

void MainWindow::reloadRecords()
{
    if (const Result<QList<InstalledRecord>> records = database::load(); records) {
        m_records = *records;
    } else {
        m_records.clear();
    }
    m_installedPage->setRecords(m_records);
    m_updatesPage->setRecords(m_records);
}

void MainWindow::syncActivity()
{
    m_installPage->setActivity(m_installs->busy(), m_installs->currentArchive(),
                               m_installs->queued());
    m_installedPage->setBusy(m_updates->busyName());
    m_updatesPage->setBusy(m_updates->busyName());
}

void MainWindow::showInfo(const QString &text)
{
    m_message->setMessageType(KMessageWidget::Positive);
    m_message->setText(text);
    m_message->animatedShow();
}

void MainWindow::showError(const QString &text)
{
    m_message->setMessageType(KMessageWidget::Error);
    m_message->setText(text);
    m_message->animatedShow();
}

void MainWindow::openArchives(const QStringList &paths)
{
    m_pages->setCurrentPage(m_installItem);
    m_installs->enqueue(paths);
}

void MainWindow::maybeCheckOnStartup()
{
    // Startup checks are opt-in and skip manual records, so opening TarDrop never contacts a
    // network service unless the user enabled automatic checking and configured a provider.
    if (!m_settings.checkAutomatically || !m_settings.checkOnStartup) {
        return;
    }
    for (const InstalledRecord &record : std::as_const(m_records)) {
        if (record.updateProvider != ProviderKind::Manual
            && updates::startupCheckDue(record, m_settings.interval)) {
            m_updates->check(record);
            return;
        }
    }
}

void MainWindow::uninstallApp(const InstalledApp &app)
{
    if (KMessageBox::warningContinueCancel(
            this,
            i18n("Remove <b>%1</b> and its application launcher? This cannot be undone.",
                 app.name.toHtmlEscaped()),
            i18nc("@title:window", "Uninstall Application"),
            KGuiItem(i18nc("@action:button", "Uninstall"),
                     QStringLiteral("edit-delete")))
        != KMessageBox::Continue) {
        return;
    }
    if (const Status status = installer::uninstall(app); !status) {
        showError(i18n("Uninstall failed: %1", status.error()));
        return;
    }
    m_installPage->removeInstalledApp(app.directory);
    reloadRecords();
    showInfo(i18n("%1 was uninstalled.", app.name));
}

void MainWindow::uninstallRecord(const InstalledRecord &record)
{
    // A durable record is converted to the installer's narrowly scoped removal operation; the
    // installer re-checks that every path it deletes is one TarDrop owns.
    InstalledApp app;
    app.name = record.name;
    app.directory = record.installPath;
    app.desktopFile = record.desktopFilePath;
    app.icon = record.iconPath;
    uninstallApp(app);
}

void MainWindow::configureSource(const InstalledRecord &record)
{
    UpdateSourceDialog dialog(record, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const InstalledRecord updated = dialog.record();
    if (const Status status = database::upsert(updated); !status) {
        showError(i18n("Could not save the update source: %1", status.error()));
        return;
    }
    reloadRecords();
    showInfo(i18n("Update source saved for %1.", updated.name));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        openArchives(paths);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    KConfigGroup group(KSharedConfig::openStateConfig(), QLatin1String(kWindowStateGroup));
    KWindowConfig::saveWindowSize(windowHandle(), group);
    group.sync();
    QMainWindow::closeEvent(event);
}

} // namespace tardrop::gui
