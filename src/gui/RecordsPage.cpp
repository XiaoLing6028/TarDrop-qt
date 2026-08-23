#include "gui/RecordsPage.h"

#include "core/Records.h"
#include "gui/Widgets.h"

#include <KLocalizedString>

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tardrop::gui {
namespace {

/// A short, human label for the configured update source.
QString providerLabel(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::GitHubReleases:
        return i18n("GitHub Releases");
    case ProviderKind::StaticUrl:
        return i18n("Static URL");
    case ProviderKind::WebsiteScraper:
        return i18n("Website endpoint");
    case ProviderKind::Manual:
        break;
    }
    return i18n("Manual (no update source)");
}

} // namespace

RecordsPage::RecordsPage(bool updatesOnly, QWidget *parent)
    : QWidget(parent)
    , m_updatesOnly(updatesOnly)
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
    layout->setSpacing(10);
    scroll->setWidget(content);

    layout->addWidget(caption(
        m_updatesOnly
            ? i18n("Check sources manually; TarDrop never downloads updates without your approval.")
            : i18n("Applications installed by TarDrop are stored in your user-only application "
                   "directory.")));

    auto *listContainer = new QWidget;
    m_list = new QVBoxLayout(listContainer);
    m_list->setContentsMargins(0, 0, 0, 0);
    m_list->setSpacing(8);
    layout->addWidget(listContainer);
    layout->addStretch();

    rebuild();
}

void RecordsPage::setRecords(const QList<InstalledRecord> &records)
{
    m_records = records;
    rebuild();
}

void RecordsPage::setBusy(const QString &busyName)
{
    m_busyName = busyName;
    rebuild();
}

void RecordsPage::rebuild()
{
    while (QLayoutItem *item = m_list->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m_records.isEmpty()) {
        m_list->addWidget(
            emptyState(i18n("No managed applications yet"),
                       i18n("Install a portable archive to create an application record.")));
        return;
    }

    for (const InstalledRecord &record : std::as_const(m_records)) {
        auto *card = new Card;

        auto *header = new QHBoxLayout;
        auto *icon = new QLabel;
        icon->setPixmap(applicationIcon(record.iconPath).pixmap(36, 36));
        icon->setFixedSize(36, 36);
        header->addWidget(icon, 0, Qt::AlignTop);

        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        auto *name = new QLabel(record.name);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        nameFont.setPointSize(nameFont.pointSize() + 1);
        name->setFont(nameFont);
        text->addWidget(name);
        text->addWidget(caption(i18n("Installed: %1",
                                     record.version.isEmpty() ? i18n("Unknown version")
                                                              : record.version)));
        if (m_updatesOnly) {
            text->addWidget(caption(i18n("Latest: %1",
                                         record.latestVersion.isEmpty()
                                             ? i18n("Not checked")
                                             : record.latestVersion)));
            text->addWidget(caption(i18n("Update source: %1",
                                         providerLabel(record.updateProvider))));
        }
        text->addWidget(caption(record.installPath));
        header->addLayout(text, 1);
        card->body()->addLayout(header);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(6);

        auto *launch = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")),
                                       i18nc("@action:button", "Launch"));
        launch->setToolTip(i18nc("@info:tooltip", "Start this application now."));
        connect(launch, &QPushButton::clicked, this,
                [this, record] { Q_EMIT launchRequested(record); });
        actions->addWidget(launch);

        auto *folder = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")),
                                       i18nc("@action:button", "Open Folder"));
        folder->setToolTip(i18nc("@info:tooltip",
                                 "Show the installed files in your file manager."));
        connect(folder, &QPushButton::clicked, this,
                [this, record] { Q_EMIT openFolderRequested(record); });
        actions->addWidget(folder);

        if (m_updatesOnly) {
            auto *source = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")),
                                           i18nc("@action:button", "Update Source…"));
            source->setToolTip(i18nc("@info:tooltip",
                                     "Choose where TarDrop should look for newer versions of this "
                                     "application."));
            connect(source, &QPushButton::clicked, this,
                    [this, record] { Q_EMIT configureSourceRequested(record); });
            actions->addWidget(source);

            auto *check = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                          i18nc("@action:button", "Check for Updates"));
            check->setToolTip(i18nc("@info:tooltip",
                                    "Look for a newer version using this application's configured "
                                    "source. TarDrop only checks when you press this."));
            check->setEnabled(m_busyName.isEmpty());
            connect(check, &QPushButton::clicked, this,
                    [this, record] { Q_EMIT checkRequested(record); });
            actions->addWidget(check);

            const bool canUpdate = !record.latestVersion.isEmpty()
                && record.latestVersion != record.version;
            auto *update = new QPushButton(QIcon::fromTheme(QStringLiteral("install")),
                                           i18nc("@action:button", "Update"));
            update->setToolTip(canUpdate
                                   ? i18nc("@info:tooltip",
                                           "Download and install the newer version, keeping a "
                                           "rollback copy in case it fails.")
                                   : i18nc("@info:tooltip",
                                           "Check for updates first; this becomes available once a "
                                           "newer version is found."));
            update->setEnabled(canUpdate && m_busyName.isEmpty());
            connect(update, &QPushButton::clicked, this,
                    [this, record] { Q_EMIT updateRequested(record); });
            actions->addWidget(update);
        }

        auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                       i18nc("@action:button", "Uninstall"));
        remove->setToolTip(i18nc("@info:tooltip",
                                 "Remove this application and its launcher. This cannot be undone."));
        connect(remove, &QPushButton::clicked, this,
                [this, record] { Q_EMIT uninstallRequested(record); });
        actions->addWidget(remove);
        actions->addStretch();
        card->body()->addLayout(actions);

        if (m_busyName == record.name) {
            auto *progress = new QProgressBar;
            progress->setRange(0, 0);
            progress->setFormat(i18n("Working…"));
            card->body()->addWidget(progress);
        }

        m_list->addWidget(card);
    }
}

} // namespace tardrop::gui
