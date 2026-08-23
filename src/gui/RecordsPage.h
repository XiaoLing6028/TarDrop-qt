#pragma once

/// Durable, database-backed application records, shown either for management or for updates.

#include "core/Types.h"

#include <QList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace tardrop::gui {

class RecordsPage : public QWidget
{
    Q_OBJECT
public:
    /// `updatesOnly` switches the wording and surfaces the release-checking controls.
    explicit RecordsPage(bool updatesOnly, QWidget *parent = nullptr);

    void setRecords(const QList<InstalledRecord> &records);
    void setBusy(const QString &busyName);

Q_SIGNALS:
    void launchRequested(const tardrop::InstalledRecord &record);
    void openFolderRequested(const tardrop::InstalledRecord &record);
    void checkRequested(const tardrop::InstalledRecord &record);
    void updateRequested(const tardrop::InstalledRecord &record);
    void uninstallRequested(const tardrop::InstalledRecord &record);
    void configureSourceRequested(const tardrop::InstalledRecord &record);

private:
    void rebuild();

    bool m_updatesOnly;
    QVBoxLayout *m_list = nullptr;
    QList<InstalledRecord> m_records;
    QString m_busyName;
};

} // namespace tardrop::gui
