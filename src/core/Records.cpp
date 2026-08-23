#include "core/Records.h"

#include "core/Paths.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace tardrop::database {
namespace {

Result<QString> databasePath()
{
    const Result<QString> directory = paths::dataDir();
    if (!directory) {
        return failure(directory.error());
    }
    return paths::join(*directory, QStringLiteral("installed-apps.json"));
}

Result<QString> settingsPath()
{
    const Result<QString> directory = paths::dataDir();
    if (!directory) {
        return failure(directory.error());
    }
    return paths::join(*directory, QStringLiteral("settings.json"));
}

/// An absent or JSON-null value maps to the empty string, matching `Option<String>`.
QString optionalString(const QJsonValue &value)
{
    return value.isString() ? value.toString() : QString();
}

QJsonValue toJson(const QString &value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

InstalledRecord recordFromJson(const QJsonObject &object)
{
    InstalledRecord record;
    record.id = optionalString(object.value(QStringLiteral("id")));
    record.name = optionalString(object.value(QStringLiteral("name")));
    record.version = optionalString(object.value(QStringLiteral("version")));
    record.installPath = optionalString(object.value(QStringLiteral("install_path")));
    record.desktopFilePath = optionalString(object.value(QStringLiteral("desktop_file_path")));
    record.iconPath = optionalString(object.value(QStringLiteral("icon_path")));
    record.sourceUrl = optionalString(object.value(QStringLiteral("source_url")));
    record.archiveFilename = optionalString(object.value(QStringLiteral("archive_filename")));
    record.installDate =
        static_cast<quint64>(object.value(QStringLiteral("install_date")).toDouble(0));
    const QJsonValue lastCheck = object.value(QStringLiteral("last_update_check"));
    record.lastUpdateCheck = lastCheck.isDouble() ? static_cast<qint64>(lastCheck.toDouble()) : -1;
    record.latestVersion = optionalString(object.value(QStringLiteral("latest_version")));
    record.updateProvider =
        providerFromString(optionalString(object.value(QStringLiteral("update_provider"))));
    const QJsonObject metadata = object.value(QStringLiteral("custom_metadata")).toObject();
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        record.customMetadata.insert(it.key(), it.value().toString());
    }
    return record;
}

QJsonObject recordToJson(const InstalledRecord &record)
{
    QJsonObject metadata;
    for (auto it = record.customMetadata.constBegin(); it != record.customMetadata.constEnd();
         ++it) {
        metadata.insert(it.key(), it.value());
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), record.id);
    object.insert(QStringLiteral("name"), record.name);
    object.insert(QStringLiteral("version"), toJson(record.version));
    object.insert(QStringLiteral("install_path"), record.installPath);
    object.insert(QStringLiteral("desktop_file_path"), record.desktopFilePath);
    object.insert(QStringLiteral("icon_path"), toJson(record.iconPath));
    object.insert(QStringLiteral("source_url"), toJson(record.sourceUrl));
    object.insert(QStringLiteral("archive_filename"), record.archiveFilename);
    object.insert(QStringLiteral("install_date"), static_cast<double>(record.installDate));
    object.insert(QStringLiteral("last_update_check"),
                  record.lastUpdateCheck < 0 ? QJsonValue(QJsonValue::Null)
                                             : QJsonValue(static_cast<double>(record.lastUpdateCheck)));
    object.insert(QStringLiteral("latest_version"), toJson(record.latestVersion));
    object.insert(QStringLiteral("update_provider"), providerToString(record.updateProvider));
    object.insert(QStringLiteral("custom_metadata"), metadata);
    return object;
}

/// Writes JSON through a save file so a crash mid-write cannot destroy the database.
Status writeJson(const QString &path, const QJsonDocument &document)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failure(i18n("could not write %1", path));
    }
    const QByteArray encoded = document.toJson(QJsonDocument::Indented);
    if (file.write(encoded) != encoded.size() || !file.commit()) {
        return failure(i18n("could not write %1", path));
    }
    return {};
}

} // namespace

Result<QList<InstalledRecord>> load()
{
    const Result<QString> path = databasePath();
    if (!path) {
        return failure(path.error());
    }
    QFile file(*path);
    if (!file.exists()) {
        return QList<InstalledRecord>{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(i18n("could not read installed applications database"));
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return failure(i18n("installed applications database is invalid"));
    }
    QList<InstalledRecord> records;
    const QJsonArray array = document.array();
    records.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            records.append(recordFromJson(value.toObject()));
        }
    }
    return records;
}

Status save(const QList<InstalledRecord> &records)
{
    const Result<QString> path = databasePath();
    if (!path) {
        return failure(path.error());
    }
    QJsonArray array;
    for (const InstalledRecord &record : records) {
        array.append(recordToJson(record));
    }
    return writeJson(*path, QJsonDocument(array));
}

Status upsert(const InstalledRecord &record)
{
    Result<QList<InstalledRecord>> records = load();
    if (!records) {
        return failure(records.error());
    }
    const auto match = std::ranges::find_if(*records, [&record](const InstalledRecord &existing) {
        return existing.desktopFilePath == record.desktopFilePath
            || existing.installPath == record.installPath;
    });
    if (match != records->end()) {
        *match = record;
    } else {
        records->append(record);
    }
    return save(*records);
}

Status remove(const QString &installPath)
{
    Result<QList<InstalledRecord>> records = load();
    if (!records) {
        return failure(records.error());
    }
    records->removeIf([&installPath](const InstalledRecord &record) {
        return record.installPath == installPath;
    });
    return save(*records);
}

Result<UpdateSettings> loadSettings()
{
    const Result<QString> path = settingsPath();
    if (!path) {
        return failure(path.error());
    }
    QFile file(*path);
    if (!file.exists()) {
        return UpdateSettings{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(i18n("could not read update settings"));
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return failure(i18n("update settings file is invalid"));
    }
    const QJsonObject object = document.object();
    UpdateSettings settings;
    settings.checkAutomatically =
        object.value(QStringLiteral("check_automatically")).toBool(settings.checkAutomatically);
    settings.notifyBetaReleases =
        object.value(QStringLiteral("notify_beta_releases")).toBool(settings.notifyBetaReleases);
    settings.checkOnStartup =
        object.value(QStringLiteral("check_on_startup")).toBool(settings.checkOnStartup);
    settings.interval =
        intervalFromString(object.value(QStringLiteral("interval")).toString());
    return settings;
}

Status saveSettings(const UpdateSettings &settings)
{
    const Result<QString> path = settingsPath();
    if (!path) {
        return failure(path.error());
    }
    QJsonObject object;
    object.insert(QStringLiteral("check_automatically"), settings.checkAutomatically);
    object.insert(QStringLiteral("notify_beta_releases"), settings.notifyBetaReleases);
    object.insert(QStringLiteral("check_on_startup"), settings.checkOnStartup);
    object.insert(QStringLiteral("interval"), intervalToString(settings.interval));
    return writeJson(*path, QJsonDocument(object));
}

QString providerToString(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::GitHubReleases:
        return QStringLiteral("git_hub_releases");
    case ProviderKind::StaticUrl:
        return QStringLiteral("static_url");
    case ProviderKind::WebsiteScraper:
        return QStringLiteral("website_scraper");
    case ProviderKind::Manual:
        break;
    }
    return QStringLiteral("manual");
}

ProviderKind providerFromString(const QString &value)
{
    if (value == QStringLiteral("git_hub_releases")) {
        return ProviderKind::GitHubReleases;
    }
    if (value == QStringLiteral("static_url")) {
        return ProviderKind::StaticUrl;
    }
    if (value == QStringLiteral("website_scraper")) {
        return ProviderKind::WebsiteScraper;
    }
    return ProviderKind::Manual;
}

QString intervalToString(UpdateInterval interval)
{
    switch (interval) {
    case UpdateInterval::Daily:
        return QStringLiteral("daily");
    case UpdateInterval::Monthly:
        return QStringLiteral("monthly");
    case UpdateInterval::Never:
        return QStringLiteral("never");
    case UpdateInterval::Weekly:
        break;
    }
    return QStringLiteral("weekly");
}

UpdateInterval intervalFromString(const QString &value)
{
    if (value == QStringLiteral("daily")) {
        return UpdateInterval::Daily;
    }
    if (value == QStringLiteral("monthly")) {
        return UpdateInterval::Monthly;
    }
    if (value == QStringLiteral("never")) {
        return UpdateInterval::Never;
    }
    return UpdateInterval::Weekly;
}

quint64 intervalSeconds(UpdateInterval interval)
{
    switch (interval) {
    case UpdateInterval::Daily:
        return 86'400;
    case UpdateInterval::Weekly:
        return 604'800;
    case UpdateInterval::Monthly:
        return 2'592'000;
    case UpdateInterval::Never:
        break;
    }
    return 0;
}

quint64 now()
{
    return static_cast<quint64>(QDateTime::currentSecsSinceEpoch());
}

} // namespace tardrop::database
