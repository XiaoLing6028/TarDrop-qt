#include "gui/InstallPage.h"

#include "gui/DropZone.h"
#include "gui/Widgets.h"

#include <KCollapsibleGroupBox>
#include <KLocalizedString>

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tardrop::gui {

InstallPage::InstallPage(QWidget *parent)
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
    layout->setSpacing(16);
    scroll->setWidget(content);

    m_dropZone = new DropZone;
    connect(m_dropZone, &DropZone::archivesChosen, this, &InstallPage::archivesChosen);
    layout->addWidget(m_dropZone);

    // --- Activity ---------------------------------------------------------
    auto *activity = new Card;
    auto *activityHeader = new QHBoxLayout;
    auto *activityTitle = new QLabel(i18n("Installation activity"));
    QFont boldFont = activityTitle->font();
    boldFont.setBold(true);
    activityTitle->setFont(boldFont);
    activityHeader->addWidget(activityTitle);
    activityHeader->addStretch();
    m_activityState = new QLabel;
    activityHeader->addWidget(m_activityState);
    activity->body()->addLayout(activityHeader);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 0); // Indeterminate: extraction gives no meaningful percentage.
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);
    activity->body()->addWidget(m_progress);

    m_activityDetail = caption(QString());
    activity->body()->addWidget(m_activityDetail);
    layout->addWidget(activity);

    // --- Installed this session -------------------------------------------
    layout->addWidget(heading(i18n("Installed this session"), 2));
    auto *appsContainer = new QWidget;
    m_appsLayout = new QVBoxLayout(appsContainer);
    m_appsLayout->setContentsMargins(0, 0, 0, 0);
    m_appsLayout->setSpacing(8);
    layout->addWidget(appsContainer);

    // --- Technical log ----------------------------------------------------
    auto *logBox = new KCollapsibleGroupBox;
    logBox->setTitle(i18n("Technical installation log"));
    auto *logLayout = new QVBoxLayout;
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(180);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logLayout->addWidget(m_log);
    logBox->setLayout(logLayout);
    layout->addWidget(logBox);

    layout->addStretch();

    setActivity(false, QString(), 0);
    rebuildInstalledApps();
}

void InstallPage::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void InstallPage::setActivity(bool busy, const QString &currentArchive, int queued)
{
    QPalette statePalette = m_activityState->palette();
    statePalette.setColor(QPalette::WindowText, busy ? accentColor() : positiveColor());
    m_activityState->setPalette(statePalette);
    m_activityState->setText(busy ? i18nc("@info:status", "Working")
                                  : i18nc("@info:status", "Idle"));

    m_progress->setVisible(busy);
    if (busy && !currentArchive.isEmpty()) {
        m_progress->setFormat(i18n("Installing %1", QFileInfo(currentArchive).fileName()));
    }

    if (queued > 0) {
        m_activityDetail->setText(
            i18np("%1 archive waiting in the queue", "%1 archives waiting in the queue", queued));
    } else if (!busy) {
        m_activityDetail->setText(i18n("No installation is currently running."));
    } else {
        m_activityDetail->clear();
    }
}

void InstallPage::addInstalledApp(const InstalledApp &app)
{
    m_apps.append(app);
    rebuildInstalledApps();
}

void InstallPage::removeInstalledApp(const QString &directory)
{
    m_apps.removeIf([&directory](const InstalledApp &app) { return app.directory == directory; });
    rebuildInstalledApps();
}

void InstallPage::rebuildInstalledApps()
{
    while (QLayoutItem *item = m_appsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m_apps.isEmpty()) {
        m_appsLayout->addWidget(
            emptyState(i18n("No applications installed in this session"),
                       i18n("Installed applications will appear here with quick actions.")));
        return;
    }

    for (const InstalledApp &app : std::as_const(m_apps)) {
        auto *card = new Card;

        auto *header = new QHBoxLayout;
        auto *icon = new QLabel;
        icon->setPixmap(applicationIcon(app.icon).pixmap(32, 32));
        icon->setFixedSize(32, 32);
        header->addWidget(icon, 0, Qt::AlignTop);

        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        auto *name = new QLabel(app.name);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        text->addWidget(name);
        text->addWidget(caption(app.directory));
        text->addWidget(caption(i18n("Archive SHA-256: %1", app.sha256)));
        header->addLayout(text, 1);
        card->body()->addLayout(header);

        auto *actions = new QHBoxLayout;
        auto *launch = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")),
                                       i18nc("@action:button", "Launch"));
        launch->setToolTip(i18nc("@info:tooltip", "Start this application now."));
        connect(launch, &QPushButton::clicked, this, [this, app] { Q_EMIT launchRequested(app); });

        auto *folder = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")),
                                       i18nc("@action:button", "Open Folder"));
        folder->setToolTip(
            i18nc("@info:tooltip", "Show the installed files in your file manager."));
        connect(folder, &QPushButton::clicked, this,
                [this, app] { Q_EMIT openFolderRequested(app); });

        auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                       i18nc("@action:button", "Uninstall"));
        remove->setToolTip(i18nc("@info:tooltip",
                                 "Remove this application and its launcher. This cannot be undone."));
        connect(remove, &QPushButton::clicked, this,
                [this, app] { Q_EMIT uninstallRequested(app); });

        actions->addWidget(launch);
        actions->addWidget(folder);
        actions->addWidget(remove);
        actions->addStretch();
        card->body()->addLayout(actions);

        m_appsLayout->addWidget(card);
    }
}

} // namespace tardrop::gui
