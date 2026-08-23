#include "core/Providers.h"

#include <KLocalizedString>

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace tardrop::updates {
namespace {

constexpr int kRequestTimeoutMs = 60'000;

/// One blocking HTTPS GET, usable from a worker thread.
///
/// Providers run off the GUI thread, so a nested event loop is the simplest correct way to use
/// Qt's asynchronous network stack synchronously without blocking the interface.
class HttpClient
{
public:
    /// Fetches a URL and returns its body, refusing anything that is not plain HTTPS/HTTP.
    Result<QByteArray> get(const QString &url) { return fetch(url, nullptr); }

    /// Streams a URL straight into `destination`, so a large archive never sits in memory.
    Status download(const QString &url, const QString &destination)
    {
        QFile file(destination);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return failure(i18n("could not write %1", destination));
        }
        const Result<QByteArray> result = fetch(url, &file);
        file.close();
        if (!result) {
            QFile::remove(destination);
            return failure(result.error());
        }
        return {};
    }

private:
    Result<QByteArray> fetch(const QString &url, QIODevice *sink)
    {
        const QUrl parsed(url);
        if (!parsed.isValid()
            || (parsed.scheme() != QStringLiteral("https")
                && parsed.scheme() != QStringLiteral("http"))) {
            return failure(i18n("update source is not a valid http(s) URL: %1", url));
        }

        QNetworkAccessManager manager;
        QNetworkRequest request(parsed);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("TarDrop"));
        // Vendors routinely redirect release downloads to a CDN; never downgrade to plain HTTP.
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);

        std::unique_ptr<QNetworkReply> reply(manager.get(request));
        QByteArray body;
        QEventLoop loop;
        QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, [&] {
            const QByteArray chunk = reply->readAll();
            if (sink) {
                sink->write(chunk);
            } else {
                body.append(chunk);
            }
        });
        QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);

        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, reply.get(), &QNetworkReply::abort);
        timeout.start(kRequestTimeoutMs);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            return failure(i18n("update request failed: %1", reply->errorString()));
        }
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) {
            return failure(i18n("update source returned HTTP %1", status));
        }
        return body;
    }
};

/// GitHub's public releases API provider, selected explicitly through database metadata.
class GitHubReleasesProvider final : public UpdateProvider
{
public:
    static Result<std::unique_ptr<UpdateProvider>> create(const QString &url)
    {
        const QStringList parts =
            QString(url).remove(QRegularExpression(QStringLiteral("/+$"))).split(u'/');
        if (parts.size() < 2) {
            return failure(i18n("invalid GitHub repository URL"));
        }
        return std::unique_ptr<UpdateProvider>(
            new GitHubReleasesProvider(parts.at(parts.size() - 2), parts.at(parts.size() - 1)));
    }

    Result<ReleaseInfo> checkLatest() override
    {
        const Result<Release> release = latest();
        if (!release) {
            return failure(release.error());
        }
        return release->info;
    }

    Result<ReleaseInfo> downloadLatest(const QString &destination) override
    {
        const Result<Release> release = latest();
        if (!release) {
            return failure(release.error());
        }
        if (release->info.downloadUrl.isEmpty()) {
            return failure(i18n("GitHub release has no downloadable assets"));
        }
        HttpClient client;
        if (const Status status = client.download(release->info.downloadUrl, destination);
            !status) {
            return failure(status.error());
        }
        return release->info;
    }

private:
    struct Release {
        ReleaseInfo info;
    };

    GitHubReleasesProvider(QString owner, QString repository)
        : m_owner(std::move(owner))
        , m_repository(std::move(repository))
    {
    }

    Result<Release> latest()
    {
        HttpClient client;
        const Result<QByteArray> body = client.get(
            QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                .arg(m_owner, m_repository));
        if (!body) {
            return failure(body.error());
        }
        const QJsonDocument document = QJsonDocument::fromJson(*body);
        if (!document.isObject()) {
            return failure(i18n("invalid GitHub release response"));
        }
        const QJsonObject object = document.object();
        const QString tag = object.value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty()) {
            return failure(i18n("invalid GitHub release response"));
        }
        Release release;
        release.info.version = tag.startsWith(u'v') ? tag.mid(1) : tag;
        release.info.notes = object.value(QStringLiteral("body")).toString();
        const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
        if (!assets.isEmpty()) {
            release.info.downloadUrl = assets.first()
                                           .toObject()
                                           .value(QStringLiteral("browser_download_url"))
                                           .toString();
        }
        return release;
    }

    QString m_owner;
    QString m_repository;
};

/// A static provider reads its URL and version from custom metadata, useful for stable vendors.
class StaticUrlProvider final : public UpdateProvider
{
public:
    static Result<std::unique_ptr<UpdateProvider>> create(const InstalledRecord &record)
    {
        const QString url = record.customMetadata.value(QStringLiteral("static_download_url"));
        const QString version = record.customMetadata.value(QStringLiteral("static_version"));
        if (url.isEmpty()) {
            return failure(i18n("static provider needs static_download_url metadata"));
        }
        if (version.isEmpty()) {
            return failure(i18n("static provider needs static_version metadata"));
        }
        return std::unique_ptr<UpdateProvider>(new StaticUrlProvider(
            url, version, record.customMetadata.value(QStringLiteral("release_notes"))));
    }

    Result<ReleaseInfo> checkLatest() override { return ReleaseInfo{m_version, m_url, m_notes}; }

    Result<ReleaseInfo> downloadLatest(const QString &destination) override
    {
        HttpClient client;
        if (const Status status = client.download(m_url, destination); !status) {
            return failure(status.error());
        }
        return checkLatest();
    }

private:
    StaticUrlProvider(QString url, QString version, QString notes)
        : m_url(std::move(url))
        , m_version(std::move(version))
        , m_notes(std::move(notes))
    {
    }

    QString m_url;
    QString m_version;
    QString m_notes;
};

/// A deliberately narrow website provider for vendors with a stable plain-text version endpoint.
/// It avoids executing page JavaScript or evaluating arbitrary scraping expressions.
class WebsiteScraperProvider final : public UpdateProvider
{
public:
    static Result<std::unique_ptr<UpdateProvider>> create(const InstalledRecord &record)
    {
        const QString versionUrl =
            record.customMetadata.value(QStringLiteral("website_version_url"));
        const QString downloadUrl =
            record.customMetadata.value(QStringLiteral("website_download_url"));
        if (versionUrl.isEmpty()) {
            return failure(i18n("website provider needs website_version_url metadata"));
        }
        if (downloadUrl.isEmpty()) {
            return failure(i18n("website provider needs website_download_url metadata"));
        }
        return std::unique_ptr<UpdateProvider>(new WebsiteScraperProvider(
            versionUrl, downloadUrl,
            record.customMetadata.value(QStringLiteral("website_version_prefix")),
            record.customMetadata.value(QStringLiteral("website_notes_url"))));
    }

    Result<ReleaseInfo> checkLatest() override
    {
        HttpClient client;
        const Result<QByteArray> body = client.get(m_versionUrl);
        if (!body) {
            return failure(body.error());
        }

        QString candidate;
        bool found = false;
        for (const QString &rawLine : QString::fromUtf8(*body).split(u'\n')) {
            const QString line = rawLine.trimmed();
            if (m_versionPrefix.isEmpty()) {
                if (!line.isEmpty()) {
                    candidate = line;
                    found = true;
                    break;
                }
            } else if (line.startsWith(m_versionPrefix)) {
                candidate = line.mid(m_versionPrefix.size()).trimmed();
                found = true;
                break;
            }
        }
        if (!found) {
            return failure(i18n("website version endpoint did not contain a version"));
        }
        // A version is short and contains at least one digit; anything else is a page, not a version.
        const bool hasDigit = std::ranges::any_of(candidate, [](QChar c) { return c.isDigit(); });
        if (candidate.size() > 80 || !hasDigit) {
            return failure(i18n("website version endpoint returned an invalid version"));
        }

        ReleaseInfo info;
        info.version = candidate.startsWith(u'v') ? candidate.mid(1) : candidate;
        info.downloadUrl = m_downloadUrl;
        if (!m_notesUrl.isEmpty()) {
            if (const Result<QByteArray> notes = client.get(m_notesUrl); notes) {
                info.notes = QString::fromUtf8(*notes);
            }
        }
        return info;
    }

    Result<ReleaseInfo> downloadLatest(const QString &destination) override
    {
        HttpClient client;
        if (const Status status = client.download(m_downloadUrl, destination); !status) {
            return failure(status.error());
        }
        return checkLatest();
    }

private:
    WebsiteScraperProvider(QString versionUrl, QString downloadUrl, QString versionPrefix,
                           QString notesUrl)
        : m_versionUrl(std::move(versionUrl))
        , m_downloadUrl(std::move(downloadUrl))
        , m_versionPrefix(std::move(versionPrefix))
        , m_notesUrl(std::move(notesUrl))
    {
    }

    QString m_versionUrl;
    QString m_downloadUrl;
    QString m_versionPrefix;
    QString m_notesUrl;
};

/// The default: TarDrop never invents a network source for a locally-installed archive.
class ManualProvider final : public UpdateProvider
{
public:
    Result<ReleaseInfo> checkLatest() override
    {
        return failure(i18n("this application has no update source configured"));
    }
    Result<ReleaseInfo> downloadLatest(const QString &) override
    {
        return failure(i18n("this application has no update source configured"));
    }
};

} // namespace

Result<std::unique_ptr<UpdateProvider>> providerFor(const InstalledRecord &record)
{
    switch (record.updateProvider) {
    case ProviderKind::GitHubReleases:
        if (record.sourceUrl.isEmpty()) {
            return failure(i18n("GitHub provider needs a repository URL"));
        }
        return GitHubReleasesProvider::create(record.sourceUrl);
    case ProviderKind::StaticUrl:
        return StaticUrlProvider::create(record);
    case ProviderKind::WebsiteScraper:
        return WebsiteScraperProvider::create(record);
    case ProviderKind::Manual:
        break;
    }
    return std::unique_ptr<UpdateProvider>(new ManualProvider);
}

QStringList requiredMetadataKeys(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::StaticUrl:
        return {QStringLiteral("static_download_url"), QStringLiteral("static_version"),
                QStringLiteral("release_notes")};
    case ProviderKind::WebsiteScraper:
        return {QStringLiteral("website_version_url"), QStringLiteral("website_download_url"),
                QStringLiteral("website_version_prefix"), QStringLiteral("website_notes_url")};
    case ProviderKind::GitHubReleases:
    case ProviderKind::Manual:
        break;
    }
    return {};
}

} // namespace tardrop::updates
