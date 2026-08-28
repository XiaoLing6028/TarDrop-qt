#include "gui/Dialogs.h"

#include "core/Providers.h"
#include "core/Records.h"
#include "gui/Widgets.h"

#include <KLocalizedString>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace tardrop::gui {
namespace {

/// A readable label for a provider configuration key; the key itself stays the storage name.
QString metadataLabel(const QString &key)
{
    if (key == QStringLiteral("static_download_url")) {
        return i18nc("@label:textbox", "Download URL:");
    }
    if (key == QStringLiteral("static_version")) {
        return i18nc("@label:textbox", "Published version:");
    }
    if (key == QStringLiteral("release_notes")) {
        return i18nc("@label:textbox", "Release notes:");
    }
    if (key == QStringLiteral("website_version_url")) {
        return i18nc("@label:textbox", "Version endpoint URL:");
    }
    if (key == QStringLiteral("website_download_url")) {
        return i18nc("@label:textbox", "Download URL:");
    }
    if (key == QStringLiteral("website_version_prefix")) {
        return i18nc("@label:textbox", "Line prefix (optional):");
    }
    if (key == QStringLiteral("website_notes_url")) {
        return i18nc("@label:textbox", "Release notes URL (optional):");
    }
    return key;
}

} // namespace


ExistingInstallDialog::ExistingInstallDialog(const QString &name, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Existing Installation"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto *message = new QLabel(
        i18n("An application named <b>%1</b> is already installed.", name.toHtmlEscaped()));
    message->setTextFormat(Qt::RichText);
    message->setWordWrap(true);
    layout->addWidget(message);
    layout->addWidget(caption(i18n("Choose whether to replace it or retain both installations.")));

    auto *buttons = new QDialogButtonBox;
    auto *replace = buttons->addButton(i18nc("@action:button", "Replace Existing"),
                                       QDialogButtonBox::AcceptRole);
    replace->setToolTip(i18nc("@info:tooltip",
                              "Remove the current installation and its launcher, then install this "
                              "archive in its place."));
    auto *keepBoth =
        buttons->addButton(i18nc("@action:button", "Keep Both"), QDialogButtonBox::AcceptRole);
    keepBoth->setToolTip(i18nc("@info:tooltip",
                               "Install this archive alongside the existing one, under a separate "
                               "numbered folder."));
    buttons->addButton(QDialogButtonBox::Cancel);

    connect(replace, &QPushButton::clicked, this, [this] {
        m_choice = ExistingChoice::Replace;
        accept();
    });
    connect(keepBoth, &QPushButton::clicked, this, [this] {
        m_choice = ExistingChoice::KeepBoth;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

LauncherChoiceDialog::LauncherChoiceDialog(const QList<LauncherCandidate> &candidates,
                                           QWidget *parent)
    : QDialog(parent)
    , m_candidates(candidates)
{
    setWindowTitle(i18nc("@title:window", "Choose Application Launcher"));
    setModal(true);
    resize(560, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto *message = new QLabel(i18n(
        "Several launchers look equally suitable. Select the application's main entry point:"));
    message->setWordWrap(true);
    layout->addWidget(message);
    layout->addWidget(caption(
        i18n("A higher score means TarDrop is more confident that file is the right one to launch.")));

    m_list = new QListWidget;
    for (const LauncherCandidate &candidate : m_candidates) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2")
                .arg(candidate.relativePath,
                     i18n("Score %1 · %2", candidate.score, candidate.reason)));
        item->setIcon(QIcon::fromTheme(QStringLiteral("application-x-executable")));
        m_list->addItem(item);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    layout->addWidget(m_list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(i18nc("@action:button", "Use This Launcher"));
    buttons->button(QDialogButtonBox::Cancel)
        ->setText(i18nc("@action:button", "Cancel Installation"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(this, &QDialog::accepted, this, [this] {
        const int row = m_list->currentRow();
        if (row >= 0 && row < m_candidates.size()) {
            m_selected = m_candidates.at(row).relativePath;
        }
    });
}

SecurityWarningDialog::SecurityWarningDialog(const QString &archiveName,
                                             const QList<SecurityConcern> &concerns,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Security Check Failed"));
    setModal(true);
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto *headline = new QLabel(i18n(
        "<b>%1</b> was rejected because it contains content TarDrop considers unsafe.",
        archiveName.toHtmlEscaped()));
    headline->setTextFormat(Qt::RichText);
    headline->setWordWrap(true);
    layout->addWidget(headline);

    layout->addWidget(caption(i18np("One archive member failed a security check:",
                                    "%1 archive members failed a security check:",
                                    concerns.size())));

    // The offending members are listed in full: the decision is only meaningful if the user can see
    // what they are agreeing to.
    auto *list = new QListWidget;
    for (const SecurityConcern &concern : concerns) {
        auto *item = new QListWidgetItem(
            i18nc("@item:inlistbox archive member and why it was refused", "%1 — %2",
                  concern.path, concern.reason));
        item->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
        list->addItem(item);
    }
    // The list is a statement of fact, not a control: nothing in it is selectable or focusable.
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setFocusPolicy(Qt::NoFocus);
    // Short lists keep the dialog compact; longer ones scroll rather than filling the screen.
    list->setMaximumHeight(180);
    layout->addWidget(list, 1);

    layout->addWidget(caption(
        i18n("Such content usually means the archive was not built for this kind of installation, "
             "but it can also be used to place files where you did not intend. Only continue if "
             "you trust where this archive came from.")));
    layout->addWidget(caption(
        i18n("If you continue, the listed members are left out of the installation — they are "
             "never extracted — so the application may be incomplete or fail to start.")));

    auto *buttons = new QDialogButtonBox;
    auto *proceed = buttons->addButton(i18nc("@action:button", "Install Anyway"),
                                       QDialogButtonBox::DestructiveRole);
    proceed->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    proceed->setToolTip(i18nc("@info:tooltip",
                              "Install the rest of this archive, omitting the members listed "
                              "above."));
    auto *cancel = buttons->addButton(i18nc("@action:button", "Keep Rejected"),
                                      QDialogButtonBox::RejectRole);
    cancel->setToolTip(i18nc("@info:tooltip", "Do not install this archive."));
    // Cancelling is the safe answer, so it is what Return and Escape both do.
    cancel->setDefault(true);
    cancel->setFocus();

    connect(proceed, &QPushButton::clicked, this, [this] {
        m_accepted = true;
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);
}

UpdateSourceDialog::UpdateSourceDialog(const InstalledRecord &record, QWidget *parent)
    : QDialog(parent)
    , m_record(record)
{
    setWindowTitle(i18nc("@title:window", "Update Source"));
    setModal(true);
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->addWidget(caption(
        i18n("TarDrop only contacts an update source you configure here, and only downloads a new "
             "archive after you press Update.")));

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_provider = new QComboBox;
    m_provider->addItem(i18n("Manual (no update source)"),
                        QVariant::fromValue(static_cast<int>(ProviderKind::Manual)));
    m_provider->addItem(i18n("GitHub Releases"),
                        QVariant::fromValue(static_cast<int>(ProviderKind::GitHubReleases)));
    m_provider->addItem(i18n("Static URL"),
                        QVariant::fromValue(static_cast<int>(ProviderKind::StaticUrl)));
    m_provider->addItem(i18n("Website endpoint"),
                        QVariant::fromValue(static_cast<int>(ProviderKind::WebsiteScraper)));
    form->addRow(i18nc("@label:listbox", "Source:"), m_provider);

    m_sourceUrl = new QLineEdit(m_record.sourceUrl);
    m_sourceUrl->setPlaceholderText(QStringLiteral("https://github.com/owner/repository"));
    m_sourceUrlRow = form->rowCount();
    form->addRow(i18nc("@label:textbox", "Repository URL:"), m_sourceUrl);
    m_form = form;

    layout->addLayout(form);

    m_metadataForm = new QFormLayout;
    m_metadataForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->addLayout(m_metadataForm);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const int index =
        m_provider->findData(QVariant::fromValue(static_cast<int>(m_record.updateProvider)));
    m_provider->setCurrentIndex(std::max(0, index));
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        showFieldsFor(static_cast<ProviderKind>(m_provider->currentData().toInt()));
    });
    showFieldsFor(m_record.updateProvider);
}

void UpdateSourceDialog::showFieldsFor(ProviderKind kind)
{
    // Hiding the whole row keeps the label and field together.
    m_form->setRowVisible(m_sourceUrlRow, kind == ProviderKind::GitHubReleases);

    while (m_metadataForm->rowCount() > 0) {
        m_metadataForm->removeRow(0);
    }
    m_metadataFields.clear();

    // Each provider declares the metadata keys it reads, so the form always matches the code.
    for (const QString &key : updates::requiredMetadataKeys(kind)) {
        auto *field = new QLineEdit(m_record.customMetadata.value(key));
        field->setToolTip(i18nc("@info:tooltip", "Stored as \"%1\".", key));
        m_metadataForm->addRow(metadataLabel(key), field);
        m_metadataFields.append({key, field});
    }
    adjustSize();
}

InstalledRecord UpdateSourceDialog::record() const
{
    InstalledRecord updated = m_record;
    updated.updateProvider = static_cast<ProviderKind>(m_provider->currentData().toInt());
    updated.sourceUrl = updated.updateProvider == ProviderKind::GitHubReleases
        ? m_sourceUrl->text().trimmed()
        : m_record.sourceUrl;
    for (const auto &[key, field] : m_metadataFields) {
        const QString value = field->text().trimmed();
        if (value.isEmpty()) {
            updated.customMetadata.remove(key);
        } else {
            updated.customMetadata.insert(key, value);
        }
    }
    return updated;
}

} // namespace tardrop::gui
