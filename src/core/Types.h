#pragma once

/// Value types shared between the installer, the update subsystem and the GUI.

#include "core/Result.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>

#include <functional>
#include <variant>

namespace tardrop {

/// Receives human-readable progress lines while a long operation runs.
using Logger = std::function<void(const QString &)>;

/// A completed installation, retained by the UI for launch/open/uninstall actions.
struct InstalledApp {
    QString name;
    QString directory;
    QString executable;
    QString desktopFile;
    QString icon; ///< empty when the package had no usable icon
    QString sha256;
};

/// Determines what to do when an owned installation with the same name already exists.
enum class ExistingChoice { Replace, KeepBoth, Cancel };

/// A launcher candidate exposed to the UI when automatic selection would be uncertain.
struct LauncherCandidate {
    QString relativePath;
    int score = 0;
    QString reason;
};

/// The installer either completes, or asks the user to pick between close candidates.
struct NeedsLauncherChoice {
    QList<LauncherCandidate> candidates;
};

using InstallResult = std::variant<InstalledApp, NeedsLauncherChoice>;

/// Update source selection, persisted in a readable, forward-compatible form.
enum class ProviderKind { GitHubReleases, StaticUrl, WebsiteScraper, Manual };

/// How often an automatic check is allowed to run.
enum class UpdateInterval { Daily, Weekly, Monthly, Never };

/// One installed portable application, stored independently from the application archive.
struct InstalledRecord {
    QString id;
    QString name;
    QString version;          ///< empty means "unknown"
    QString installPath;
    QString desktopFilePath;
    QString iconPath;         ///< empty means "none"
    QString sourceUrl;        ///< empty means "not configured"
    QString archiveFilename;
    quint64 installDate = 0;
    qint64 lastUpdateCheck = -1;  ///< negative means "never checked"
    QString latestVersion;        ///< empty means "not checked"
    ProviderKind updateProvider = ProviderKind::Manual;
    QMap<QString, QString> customMetadata;
};

/// Global update preferences stored beside the installed-app database.
struct UpdateSettings {
    bool checkAutomatically = false;
    bool notifyBetaReleases = false;
    bool checkOnStartup = true;
    UpdateInterval interval = UpdateInterval::Weekly;
};

/// Release metadata returned by a provider, free of provider-specific network detail.
struct ReleaseInfo {
    QString version;
    QString downloadUrl; ///< empty when the provider offers no direct download
    QString notes;
};

} // namespace tardrop

Q_DECLARE_METATYPE(tardrop::InstalledApp)
Q_DECLARE_METATYPE(tardrop::InstalledRecord)
Q_DECLARE_METATYPE(tardrop::LauncherCandidate)
Q_DECLARE_METATYPE(tardrop::ReleaseInfo)
