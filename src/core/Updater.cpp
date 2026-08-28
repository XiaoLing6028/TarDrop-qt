#include "core/Updater.h"

#include "core/ArchiveReader.h"
#include "core/Installer.h"
#include "core/Paths.h"
#include "core/Providers.h"
#include "core/Records.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace tardrop::updates {
namespace {

/// Splits a version into comparable numeric parts; unparsable segments count as zero.
QList<quint64> versionParts(const QString &value)
{
    QString trimmed = value;
    while (trimmed.startsWith(u'v')) {
        trimmed.remove(0, 1);
    }
    QList<quint64> parts;
    for (const QString &piece : trimmed.split(u'.')) {
        parts.append(piece.toULongLong());
    }
    return parts;
}

/// Finds a conventional dotted version segment in release filenames without guessing arbitrary text.
QString versionFromText(const QString &text)
{
    QString current;
    QStringList pieces;
    for (const QChar character : text) {
        const bool keep =
            (character.unicode() < 128 && character.isLetterOrNumber()) || character == u'.';
        if (keep) {
            current.append(character);
        } else {
            pieces.append(current);
            current.clear();
        }
    }
    pieces.append(current);

    for (const QString &piece : std::as_const(pieces)) {
        const auto digits =
            std::ranges::count_if(piece, [](QChar c) { return c.unicode() < 128 && c.isDigit(); });
        if (digits >= 2 && piece.contains(u'.')) {
            QString result = piece;
            while (result.startsWith(u'v')) {
                result.remove(0, 1);
            }
            return result;
        }
    }
    return {};
}

/// Detects versions from non-executing metadata and names. Executing `--version` would violate
/// TarDrop's rule that archives are never run automatically, even after installation.
QString detectVersion(const InstalledApp &app, const QString &archive)
{
    // Vendors commonly add one of these extension keys to their desktop entry. The freedesktop
    // `Version=1.5` format key is intentionally ignored because it is not the app version.
    QFile entry(app.desktopFile);
    if (entry.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(entry.readAll()).split(u'\n');
        for (const QString &key : {QStringLiteral("X-AppVersion="), QStringLiteral("X-Version="),
                                   QStringLiteral("AppVersion=")}) {
            for (const QString &line : lines) {
                if (!line.startsWith(key)) {
                    continue;
                }
                const QString value = line.mid(key.size()).trimmed();
                if (!value.isEmpty() && value.size() < 80) {
                    return value;
                }
            }
        }
    }

    for (const QString &name : {QStringLiteral("VERSION"), QStringLiteral("version"),
                                QStringLiteral("VERSION.txt")}) {
        QFile file(paths::join(app.directory, name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString value = QString::fromUtf8(file.readLine()).trimmed();
        if (!value.isEmpty() && value.size() < 80) {
            return value;
        }
    }
    return versionFromText(QFileInfo(archive).fileName());
}

/// Uses only a basename from the provider URL and falls back to `.tar` for safe installer routing.
QString downloadFilename(const QString &url)
{
    if (!url.isEmpty()) {
        const QString withoutQuery = url.section(u'?', 0, 0);
        const QString name = withoutQuery.section(u'/', -1);
        if (!name.isEmpty() && archives::detect(name)) {
            return name;
        }
    }
    return QStringLiteral("update.tar");
}

bool renamePath(const QString &from, const QString &to)
{
    return std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}

/// Reads a whole file, used to snapshot the launcher and icon a user may have customised.
std::optional<QByteArray> readAll(const QString &path)
{
    if (path.isEmpty()) {
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return file.readAll();
}

Status writeAll(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failure(i18n("could not write %1", path));
    }
    if (file.write(data) != data.size()) {
        return failure(i18n("could not write %1", path));
    }
    return {};
}

/// Restores every moved user-owned file after a failed update attempt.
Status rollback(const InstalledRecord &record,
                const QString &savedDirectory,
                const std::optional<QByteArray> &desktopFile,
                const std::optional<QByteArray> &icon)
{
    if (QFileInfo::exists(record.installPath)
        && !QDir(record.installPath).removeRecursively()) {
        return failure(i18n("could not clear %1 while rolling back", record.installPath));
    }
    if (!renamePath(savedDirectory, record.installPath)) {
        return failure(i18n("could not restore %1 from the rollback snapshot", record.installPath));
    }
    if (desktopFile) {
        if (const Status status = writeAll(record.desktopFilePath, *desktopFile); !status) {
            return failure(status.error());
        }
    }
    if (icon && !record.iconPath.isEmpty()) {
        if (const Status status = writeAll(record.iconPath, *icon); !status) {
            return failure(status.error());
        }
    }
    return {};
}

} // namespace

Status recordInstall(const InstalledApp &app, const QString &archive)
{
    const Result<QList<InstalledRecord>> existingRecords = database::load();
    if (!existingRecords) {
        return failure(existingRecords.error());
    }
    const auto existing =
        std::ranges::find_if(*existingRecords, [&app](const InstalledRecord &record) {
            return record.installPath == app.directory;
        });
    const bool hadRecord = existing != existingRecords->cend();

    InstalledRecord record;
    record.id = paths::desktopId(app.name);
    record.name = app.name;
    record.version = detectVersion(app, archive);
    record.installPath = app.directory;
    record.desktopFilePath = app.desktopFile;
    record.iconPath = app.icon;
    // Provider configuration belongs to the user, so a reinstall must not discard it.
    record.sourceUrl = hadRecord ? existing->sourceUrl : QString();
    record.archiveFilename = QFileInfo(archive).fileName();
    if (record.archiveFilename.isEmpty()) {
        record.archiveFilename = QStringLiteral("archive");
    }
    record.installDate = hadRecord ? existing->installDate : database::now();
    record.lastUpdateCheck = -1;
    record.latestVersion.clear();
    record.updateProvider = ProviderKind::Manual;
    return database::upsert(record);
}

Status recordRemoval(const QString &installPath)
{
    return database::remove(installPath);
}

Result<std::optional<ReleaseInfo>> checkForUpdate(InstalledRecord &record)
{
    Result<std::unique_ptr<UpdateProvider>> provider = providerFor(record);
    if (!provider) {
        return failure(provider.error());
    }
    const Result<ReleaseInfo> release = (*provider)->checkLatest();
    if (!release) {
        return failure(release.error());
    }
    record.lastUpdateCheck = static_cast<qint64>(database::now());
    record.latestVersion = release->version;
    if (const Status stored = database::upsert(record); !stored) {
        return failure(stored.error());
    }
    if (!isNewer(record.version, release->version)) {
        return std::optional<ReleaseInfo>{};
    }
    return std::optional<ReleaseInfo>{*release};
}

Result<InstalledRecord> update(const InstalledRecord &record, const Logger &log)
{
    const auto report = [&log](const QString &line) {
        if (log) {
            log(line);
        }
    };

    Result<std::unique_ptr<UpdateProvider>> provider = providerFor(record);
    if (!provider) {
        return failure(provider.error());
    }

    QTemporaryDir downloads;
    if (!downloads.isValid()) {
        return failure(i18n("could not create update download directory"));
    }
    // Keep the remote filename extension: the installer deliberately detects formats by it.
    const Result<ReleaseInfo> announced = (*provider)->checkLatest();
    if (!announced) {
        return failure(announced.error());
    }
    const QString archive =
        paths::join(downloads.path(), downloadFilename(announced->downloadUrl));

    report(i18n("Downloading update…"));
    const Result<ReleaseInfo> release = (*provider)->downloadLatest(archive);
    if (!release) {
        return failure(release.error());
    }

    const QString parent = QFileInfo(record.installPath).absolutePath();
    if (parent.isEmpty()) {
        return failure(i18n("invalid installation path"));
    }
    QTemporaryDir backup(paths::join(parent, QStringLiteral(".tardrop-update-backup-XXXXXX")));
    if (!backup.isValid()) {
        return failure(i18n("could not create rollback snapshot directory"));
    }
    const QString savedDirectory = paths::join(backup.path(), QStringLiteral("application"));

    const std::optional<QByteArray> oldDesktop = readAll(record.desktopFilePath);
    const std::optional<QByteArray> oldIcon = readAll(record.iconPath);
    struct stat oldPermissions {};
    const bool hasPermissions =
        ::stat(QFile::encodeName(record.installPath).constData(), &oldPermissions) == 0;

    report(i18n("Creating rollback snapshot…"));
    if (!renamePath(record.installPath, savedDirectory)) {
        return failure(i18n("could not move current application into rollback snapshot"));
    }

    const Result<InstallResult> result =
        installer::install(archive, ExistingChoice::Replace, QString(), log);

    /// Restores the snapshot and reports why the update was abandoned.
    const auto abandon = [&](const QString &reason) -> Result<InstalledRecord> {
        if (const Status restored = rollback(record, savedDirectory, oldDesktop, oldIcon);
            !restored) {
            return failure(withContext(reason, restored.error()));
        }
        return failure(reason);
    };

    if (!result) {
        return abandon(withContext(i18n("update failed and was rolled back"), result.error()));
    }
    if (std::holds_alternative<NeedsLauncherChoice>(*result)) {
        return abandon(i18n("update needs a launcher choice; installation was rolled back"));
    }
    if (std::holds_alternative<NeedsSecurityConfirmation>(*result)) {
        // An update runs unattended against a downloaded archive, so a package the checks refuse is
        // never installed here; the user can decide about it by installing the archive themselves.
        return abandon(
            i18n("the downloaded archive was rejected by a security check; the update was rolled "
                 "back"));
    }

    const InstalledApp installed = std::get<InstalledApp>(*result);
    if (installed.directory != record.installPath) {
        // A changed application identity must never overwrite a different managed directory.
        if (QFileInfo::exists(installed.directory)) {
            (void)QDir(installed.directory).removeRecursively();
        }
        (void)database::remove(installed.directory);
        return abandon(
            i18n("downloaded archive identifies as a different application; installation was "
                 "rolled back"));
    }

    // Preserve the established launcher, icon and root permissions: user customisations survive.
    if (oldDesktop) {
        if (const Status status = writeAll(record.desktopFilePath, *oldDesktop); !status) {
            return failure(status.error());
        }
    }
    if (oldIcon && !record.iconPath.isEmpty()) {
        if (const Status status = writeAll(record.iconPath, *oldIcon); !status) {
            return failure(status.error());
        }
    }
    if (hasPermissions) {
        ::chmod(QFile::encodeName(installed.directory).constData(), oldPermissions.st_mode & 07777);
    }

    InstalledRecord updated = record;
    updated.version = release->version;
    updated.archiveFilename = record.archiveFilename;
    updated.lastUpdateCheck = static_cast<qint64>(database::now());
    updated.latestVersion.clear();
    if (const Status stored = database::upsert(updated); !stored) {
        return failure(stored.error());
    }
    report(i18n("Update completed and rollback snapshot discarded."));
    return updated;
}

bool isNewer(const QString &installed, const QString &latest)
{
    if (installed.isEmpty()) {
        return true;
    }
    const QList<quint64> newParts = versionParts(latest);
    const QList<quint64> oldParts = versionParts(installed);
    return std::lexicographical_compare(oldParts.cbegin(), oldParts.cend(), newParts.cbegin(),
                                        newParts.cend());
}

bool startupCheckDue(const InstalledRecord &record, UpdateInterval interval)
{
    const quint64 seconds = database::intervalSeconds(interval);
    if (seconds == 0) {
        return false;
    }
    if (record.lastUpdateCheck < 0) {
        return true;
    }
    const quint64 current = database::now();
    const quint64 last = static_cast<quint64>(record.lastUpdateCheck);
    return current > last && (current - last) >= seconds;
}

} // namespace tardrop::updates
