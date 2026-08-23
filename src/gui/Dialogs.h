#pragma once

/// The modal decisions that intentionally pause the installation queue, plus update-source setup.

#include "core/Types.h"

#include <QDialog>
#include <QList>

class QLineEdit;
class QListWidget;
class QComboBox;
class QFormLayout;

namespace tardrop::gui {

/// Asks whether an existing installation should be replaced or kept alongside the new one.
class ExistingInstallDialog : public QDialog
{
    Q_OBJECT
public:
    ExistingInstallDialog(const QString &name, QWidget *parent = nullptr);

    /// The user's decision; `Cancel` when the dialog was dismissed.
    [[nodiscard]] ExistingChoice choice() const { return m_choice; }

private:
    ExistingChoice m_choice = ExistingChoice::Cancel;
};

/// Asks the user to pick between launcher candidates TarDrop scored too closely to decide alone.
class LauncherChoiceDialog : public QDialog
{
    Q_OBJECT
public:
    LauncherChoiceDialog(const QList<LauncherCandidate> &candidates, QWidget *parent = nullptr);

    /// The chosen path relative to the extracted root, or empty when the install was cancelled.
    [[nodiscard]] QString selected() const { return m_selected; }

private:
    QListWidget *m_list = nullptr;
    QList<LauncherCandidate> m_candidates;
    QString m_selected;
};

/// Configures where TarDrop may look for a newer version of one installed application.
class UpdateSourceDialog : public QDialog
{
    Q_OBJECT
public:
    UpdateSourceDialog(const InstalledRecord &record, QWidget *parent = nullptr);

    /// The record with the user's provider selection applied.
    [[nodiscard]] InstalledRecord record() const;

private:
    void showFieldsFor(ProviderKind kind);

    InstalledRecord m_record;
    QComboBox *m_provider = nullptr;
    QLineEdit *m_sourceUrl = nullptr;
    QFormLayout *m_form = nullptr;
    int m_sourceUrlRow = 0;
    QFormLayout *m_metadataForm = nullptr;
    QList<QPair<QString, QLineEdit *>> m_metadataFields;
};

} // namespace tardrop::gui
