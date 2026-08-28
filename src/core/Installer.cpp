#include "core/Installer.h"

#include "core/ArchiveReader.h"
#include "core/DesktopFile.h"
#include "core/IconFinder.h"
#include "core/Paths.h"
#include "core/Security.h"
#include "core/Updater.h"

#include <KLocalizedString>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace tardrop::installer {
namespace {

/// Renames a path with the filesystem primitive, which works for directories too.
bool renamePath(const QString &from, const QString &to)
{
    return std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}

/// Lists every regular file below `root`, in a stable order, without following symbolic links.
QStringList walkFiles(const QString &root)
{
    QStringList files;
    QDirIterator iterator(root,
                          QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (security::isRegularFile(path)) {
            files.append(path);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// Parses the first desktop `Exec` argument, supporting the quoted launcher paths common in bundles.
std::optional<QString> firstExecWord(const QString &exec)
{
    QString result;
    bool quoted = false;
    bool escaped = false;
    for (const QChar character : exec) {
        if (escaped) {
            result.append(character);
            escaped = false;
            continue;
        }
        if (character == u'\\') {
            escaped = true;
            continue;
        }
        if (character == u'"') {
            quoted = !quoted;
            continue;
        }
        if (character.isSpace() && !quoted) {
            break;
        }
        result.append(character);
    }
    if (quoted || escaped || result.isEmpty()) {
        return std::nullopt;
    }
    return result;
}

/// Reads a desktop entry's command without executing it and resolves it relative to that file.
///
/// This file only ever reads `Exec=` to *identify* a launcher, never to run it, so anything
/// ambiguous — shell operators, field codes, a target outside the tree — is discarded.
std::optional<QString> desktopExecTarget(const QString &desktopFile, const QString &root)
{
    QFile file(desktopFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    const QString contents = QString::fromUtf8(file.readAll());

    QString exec;
    bool found = false;
    for (const QString &line : contents.split(u'\n')) {
        if (line.startsWith(QStringLiteral("Exec="))) {
            exec = line.mid(5);
            // Split on '\n' leaves a trailing '\r' on CRLF files; it is not part of the command.
            if (exec.endsWith(u'\r')) {
                exec.chop(1);
            }
            found = true;
            break;
        }
    }
    if (!found || exec.isEmpty()) {
        return std::nullopt;
    }
    static const QString forbidden = QStringLiteral("\n\r`$;|&<>");
    for (const QChar character : forbidden) {
        if (exec.contains(character)) {
            return std::nullopt;
        }
    }

    const std::optional<QString> command = firstExecWord(exec);
    if (!command || command->contains(u'%')) {
        return std::nullopt;
    }

    const QString target = paths::clean(command->startsWith(u'/')
                                            ? *command
                                            : paths::join(QFileInfo(desktopFile).absolutePath(),
                                                          *command));
    if (!paths::isWithin(root, target) || !security::isExecutable(target)) {
        return std::nullopt;
    }
    return target;
}

/// Penalizes trees conventionally used for dependencies, documentation, or development artifacts.
int candidatePenalty(const QString &relative)
{
    const QStringList parts = paths::components(relative);
    static const QStringList unsafeTree = {
        QStringLiteral("lib"),  QStringLiteral("lib64"),        QStringLiteral("share"),
        QStringLiteral("doc"),  QStringLiteral("docs"),         QStringLiteral("include"),
        QStringLiteral("node_modules"), QStringLiteral(".git"),
    };
    for (const QString &part : parts) {
        if (unsafeTree.contains(part.toLower())) {
            return -100;
        }
    }
    // Tor transport binaries are helpers, not the browser's user-facing launcher.
    static const QStringList torHelpers = {QStringLiteral("browser"), QStringLiteral("torbrowser"),
                                           QStringLiteral("tor"),
                                           QStringLiteral("pluggabletransports")};
    for (qsizetype index = 0; index + torHelpers.size() <= parts.size(); ++index) {
        bool match = true;
        for (qsizetype offset = 0; offset < torHelpers.size(); ++offset) {
            if (parts.at(index + offset).toLower() != torHelpers.at(offset)) {
                match = false;
                break;
            }
        }
        if (match) {
            return -100;
        }
    }
    return 0;
}

/// Records only the best reason for a path, then applies directory penalties to every heuristic.
void addCandidate(QMap<QString, QPair<int, QString>> &scores,
                  const QString &root,
                  const QString &path,
                  int score,
                  const QString &reason)
{
    const QString relative = paths::relativeTo(root, path);
    if (relative.isEmpty()) {
        return;
    }
    const int finalScore = score + candidatePenalty(relative);
    const auto existing = scores.constFind(relative);
    if (existing == scores.constEnd() || finalScore > existing->first) {
        scores.insert(relative, {finalScore, reason});
    }
}

/// Collapses an archive's one enclosing directory, but preserves archives with several top-level files.
QString packageRoot(const QString &staging)
{
    const QFileInfoList children =
        QDir(staging).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    if (children.size() == 1 && children.first().isDir()) {
        return children.first().absoluteFilePath();
    }
    return staging;
}

/// Chooses the nearest desktop entry whose safe `Exec` target equals the selected launcher.
/// This prevents unrelated bundled tools from donating misleading browser/game metadata.
std::optional<desktop::Metadata> bestDesktopMetadata(const QString &root, const QString &executable)
{
    std::optional<desktop::Metadata> best;
    qsizetype bestDepth = 0;
    const QString wanted = paths::clean(executable);

    for (const QString &path : walkFiles(root)) {
        if (QFileInfo(path).suffix().toLower() != QStringLiteral("desktop")) {
            continue;
        }
        const std::optional<QString> target = desktopExecTarget(path, root);
        if (!target || *target != wanted) {
            continue;
        }
        std::optional<desktop::Metadata> metadata = desktop::readMetadata(path);
        if (!metadata) {
            continue;
        }
        // Prefer package-level metadata over desktop files buried in a component directory.
        const qsizetype depth = paths::components(paths::relativeTo(root, path)).size();
        if (!best || depth < bestDepth) {
            best = std::move(metadata);
            bestDepth = depth;
        }
    }
    return best;
}

/// Supplies useful launcher sections only when the package did not declare its own categories.
QString inferCategories(const QString &name)
{
    const QString lower = name.toLower();
    const auto matches = [&lower](std::initializer_list<const char *> needles) {
        return std::ranges::any_of(needles, [&lower](const char *needle) {
            return lower.contains(QString::fromLatin1(needle));
        });
    };
    if (matches({"browser", "firefox", "tor browser", "chromium", "librewolf"})) {
        return QStringLiteral("Network;WebBrowser;");
    }
    if (matches({"editor", "notepad", "kate", "gedit"})) {
        return QStringLiteral("Utility;TextEditor;");
    }
    if (matches({"ide", "code", "studio", "clion", "idea"})) {
        return QStringLiteral("Development;IDE;");
    }
    if (lower.contains(QStringLiteral("game"))) {
        return QStringLiteral("Game;");
    }
    if (matches({"player", "vlc", "music", "video"})) {
        return QStringLiteral("AudioVideo;Player;");
    }
    if (matches({"image", "photo", "draw", "graphics"})) {
        return QStringLiteral("Graphics;");
    }
    if (matches({"terminal", "console", "terminator"})) {
        return QStringLiteral("System;TerminalEmulator;");
    }
    return QStringLiteral("Utility;");
}

/// Calculates a non-arbitrary final directory, optionally using a numbered sibling for "Keep both".
Result<QString> destinationFor(const QString &root, const QString &name, ExistingChoice choice)
{
    const Result<QString> first = paths::directChild(root, name);
    if (!first) {
        return failure(first.error());
    }
    if (!QFileInfo::exists(*first) || choice != ExistingChoice::KeepBoth) {
        return *first;
    }
    for (int number = 2; number < 10'000; ++number) {
        const Result<QString> candidate =
            paths::directChild(root, QStringLiteral("%1 (%2)").arg(name).arg(number));
        if (!candidate) {
            return failure(candidate.error());
        }
        if (!QFileInfo::exists(*candidate)) {
            return *candidate;
        }
    }
    return failure(i18n("could not find a free installation name"));
}

} // namespace

Result<QList<LauncherCandidate>> executableCandidates(const QString &root,
                                                      const QString &archiveName)
{
    const QString folderName = QFileInfo(root).fileName().toLower();
    const QString wantedName = archiveName.toLower();
    QMap<QString, QPair<int, QString>> scores;
    const QStringList files = walkFiles(root);

    // Desktop files express the package author's intended launcher, so they rank just below AppRun.
    for (const QString &path : files) {
        if (QFileInfo(path).suffix().toLower() != QStringLiteral("desktop")) {
            continue;
        }
        const std::optional<QString> target = desktopExecTarget(path, root);
        if (!target) {
            continue;
        }
        // A top-level desktop file is more likely to describe the package than one in a subcomponent.
        const qsizetype depth = paths::components(paths::relativeTo(root, path)).size();
        const int proximity = static_cast<int>(std::min<qsizetype>(depth > 0 ? depth - 1 : 0, 10));
        addCandidate(scores, root, *target, 95 - proximity,
                     i18n("desktop entry Exec target"));
    }

    for (const QString &path : files) {
        if (!security::isExecutable(path)) {
            continue;
        }
        const QString filename = QFileInfo(path).fileName().toLower();
        const bool isNativeBinary = security::isElf(path);
        // Scripts are candidates only when their names clearly identify them as launchers.
        const bool isLauncherScript = filename.startsWith(QStringLiteral("start-"))
            || filename.startsWith(QStringLiteral("launch-"))
            || filename.startsWith(QStringLiteral("run-"))
            || filename.endsWith(QStringLiteral(".sh"));
        if (!isNativeBinary && !isLauncherScript) {
            continue;
        }

        const QString relative = paths::relativeTo(root, path);
        const qsizetype depth = paths::components(relative).size();
        if (filename == QStringLiteral("apprun") && depth == 1) {
            addCandidate(scores, root, path, 100, i18n("root AppRun"));
            continue;
        }
        if (isLauncherScript) {
            addCandidate(scores, root, path, 90, i18n("named launcher script"));
        }
        if (depth == 1) {
            addCandidate(scores, root, path, 80, i18n("executable at extraction root"));
        }
        if (filename == wantedName) {
            addCandidate(scores, root, path, 70, i18n("filename matches archive"));
        }
        if (filename == folderName) {
            addCandidate(scores, root, path, 70, i18n("filename matches extraction folder"));
        }
        if (isNativeBinary) {
            addCandidate(scores, root, path, 20, i18n("nested executable"));
        }
    }

    QList<LauncherCandidate> candidates;
    candidates.reserve(scores.size());
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        candidates.append(LauncherCandidate{it.key(), it->first, it->second});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const LauncherCandidate &left, const LauncherCandidate &right) {
                  if (left.score != right.score) {
                      return left.score > right.score;
                  }
                  return left.relativePath < right.relativePath;
              });
    return candidates;
}

Result<InstallResult> install(const QString &source,
                              ExistingChoice choice,
                              const QString &selectedLauncher,
                              const Logger &log,
                              bool allowUnsafeContent)
{
    const auto report = [&log](const QString &line) {
        if (log) {
            log(line);
        }
    };

    const Result<archives::Format> format = archives::detect(source);
    if (!format) {
        return failure(format.error());
    }
    const Result<QString> hash = security::archiveSha256(source);
    if (!hash) {
        return failure(hash.error());
    }
    const QString baseName = paths::archiveStem(source);
    report(i18n("Validated archive SHA-256: %1", *hash));

    const Result<QString> applications = paths::applicationsDir();
    if (!applications) {
        return failure(applications.error());
    }

    // Staging lives inside ~/Applications so that publishing is a same-filesystem rename, and it is
    // private (0700) so nothing half-extracted is ever visible to another process.
    QTemporaryDir staging(paths::join(*applications, QStringLiteral(".tardrop-XXXXXX")));
    if (!staging.isValid()) {
        return failure(i18n("could not make private staging directory"));
    }
    report(i18n("Extracting archive into private staging directory…"));
    const archives::MemberPolicy policy = allowUnsafeContent ? archives::MemberPolicy::SkipUnsafe
                                                             : archives::MemberPolicy::Strict;
    QList<SecurityConcern> skipped;
    if (const Status extracted =
            archives::extract(source, *format, staging.path(), policy, &skipped);
        !extracted) {
        if (allowUnsafeContent) {
            // The user already accepted the refused members; anything still failing is either a
            // containment breach or an ordinary extraction error, and neither is negotiable.
            return failure(extracted.error());
        }
        // Describe every problem the package has, so the warning asks the user once rather than
        // once per offending member.
        const Result<QList<SecurityConcern>> concerns = archives::inspect(source, *format);
        if (!concerns || concerns->isEmpty()) {
            return failure(extracted.error());
        }
        const bool containmentBreach = std::ranges::any_of(
            *concerns, [](const SecurityConcern &concern) { return !concern.overridable; });
        if (containmentBreach) {
            return failure(extracted.error());
        }
        report(i18n("Rejected by a security check: %1", extracted.error()));
        return InstallResult{NeedsSecurityConfirmation{*concerns}};
    }
    for (const SecurityConcern &concern : std::as_const(skipped)) {
        report(i18n("Omitted %1 (%2), as you confirmed.", concern.path, concern.reason));
    }

    const QString extractedRoot = packageRoot(staging.path());
    report(i18n("Scoring safe launcher candidates…"));
    const Result<QList<LauncherCandidate>> candidates =
        executableCandidates(extractedRoot, baseName);
    if (!candidates) {
        return failure(candidates.error());
    }

    QString executable;
    if (!selectedLauncher.isEmpty()) {
        const auto match = std::ranges::find_if(*candidates,
                                                [&selectedLauncher](const LauncherCandidate &c) {
                                                    return c.relativePath == selectedLauncher;
                                                });
        if (match == candidates->cend()) {
            return failure(i18n("selected launcher is no longer a safe candidate"));
        }
        executable = paths::join(extractedRoot, match->relativePath);
    } else if (candidates->isEmpty()) {
        return failure(i18n("No safe application launcher was found."));
    } else if (candidates->size() > 1
               && candidates->at(0).score - candidates->at(1).score <= 10) {
        report(i18n("Top launcher candidates are too close to choose safely; asking you to decide."));
        return InstallResult{NeedsLauncherChoice{*candidates}};
    } else {
        executable = paths::join(extractedRoot, candidates->at(0).relativePath);
    }
    executable = paths::clean(executable);
    report(i18n("Selected launcher: %1", executable));

    // Metadata is accepted only from a desktop entry that names the selected, validated launcher.
    const std::optional<desktop::Metadata> metadata = bestDesktopMetadata(extractedRoot, executable);

    // A valid package Name is authoritative. Otherwise build a readable label from the actual
    // launcher (rather than leaking filenames such as `tor-browser` into Plasma's UI).
    const QString rawExecutableName = QFileInfo(executable).completeBaseName();
    const QString generatedName = paths::prettyName(rawExecutableName);
    const QString fallbackName =
        generatedName.isEmpty() ? paths::prettyName(baseName) : generatedName;
    const QString rawFallback = rawExecutableName.isEmpty() ? baseName : rawExecutableName;

    QString name;
    if (metadata && metadata->name && !metadata->name->trimmed().isEmpty()) {
        name = *metadata->name;
    } else {
        name = fallbackName.isEmpty() ? rawFallback : fallbackName;
    }

    const Result<QString> directory = destinationFor(*applications, name, choice);
    if (!directory) {
        return failure(directory.error());
    }
    if (choice == ExistingChoice::Cancel) {
        return failure(i18n("Installation cancelled"));
    }
    if (QFileInfo::exists(*directory) && choice == ExistingChoice::Replace) {
        // The target was calculated as a direct child of our owned Applications root.
        report(i18n("Replacing existing TarDrop installation: %1", *directory));
        if (!QDir(*directory).removeRecursively()) {
            return failure(i18n("could not remove existing managed installation"));
        }
    }

    const QString relativeExecutable = paths::relativeTo(extractedRoot, executable);
    if (relativeExecutable.isEmpty()) {
        return failure(i18n("internal executable path error"));
    }
    const QString iconSource =
        icons::findIcon(extractedRoot,
                        name,
                        metadata && metadata->icon ? *metadata->icon : QString(),
                        metadata ? metadata->source : QString());

    report(i18n("Publishing installation…"));
    if (!renamePath(extractedRoot, *directory)) {
        return failure(i18n("could not publish installation"));
    }
    if (extractedRoot == paths::clean(staging.path())) {
        // The staging directory itself became the installation; it must not be cleaned up.
        staging.setAutoRemove(false);
    }

    const QString finalExecutable = paths::join(*directory, relativeExecutable);
    const QString id = paths::desktopId(name);

    // Copy icon assets into XDG data, rather than referring into a replaceable archive directory.
    QString icon;
    if (!iconSource.isEmpty()) {
        const QString relativeIcon = paths::relativeTo(extractedRoot, iconSource);
        const QString finalSource =
            relativeIcon.isEmpty() ? iconSource : paths::join(*directory, relativeIcon);
        const Result<QString> copied = icons::copyToTarDrop(finalSource, id);
        if (!copied) {
            return failure(withContext(i18n("could not copy application icon into XDG data"),
                                       copied.error()));
        }
        icon = *copied;
    }

    const QString categories = (metadata && metadata->categories) ? *metadata->categories
                                                                  : inferCategories(name);
    report(i18n("Generating desktop launcher…"));
    desktop::Entry entry;
    entry.name = name;
    entry.comment = (metadata && metadata->comment) ? *metadata->comment : QString();
    entry.executable = finalExecutable;
    entry.icon = icon;
    entry.categories = categories;
    entry.terminal = metadata && metadata->terminal ? *metadata->terminal : false;
    entry.startupNotify = metadata && metadata->startupNotify ? *metadata->startupNotify : false;
    entry.startupWmClass = (metadata && metadata->startupWmClass) ? *metadata->startupWmClass
                                                                  : QString();
    entry.mimeType = (metadata && metadata->mimeType) ? *metadata->mimeType : QString();
    entry.id = id;

    const Result<QString> desktopFile = desktop::write(entry);
    if (!desktopFile) {
        return failure(desktopFile.error());
    }
    desktop::refreshIntegrations();
    report(i18n("Done. KDE's application launcher should now find the application."));

    const InstalledApp installed{name, *directory, finalExecutable, *desktopFile, icon, *hash};
    // Persistence remains owned by the update module; the installer reports its completed result.
    if (const Status recorded = updates::recordInstall(installed, source); !recorded) {
        return failure(withContext(i18n("could not record installed application"),
                                   recorded.error()));
    }
    return InstallResult{installed};
}

Status uninstall(const InstalledApp &app)
{
    const Result<QString> root = paths::applicationsDir();
    if (!root) {
        return failure(root.error());
    }
    const Result<QString> database = paths::applicationsDatabaseDir();
    if (!database) {
        return failure(database.error());
    }

    // Removal is only ever allowed for a direct child of the two directories TarDrop owns.
    if (QFileInfo(app.directory).absolutePath() != paths::clean(*root)) {
        return failure(i18n("refusing to remove directory outside ~/Applications"));
    }
    if (QFileInfo(app.desktopFile).absolutePath() != paths::clean(*database)) {
        return failure(i18n("refusing to remove desktop entry outside XDG applications"));
    }

    if (QFileInfo::exists(app.directory) && !QDir(app.directory).removeRecursively()) {
        return failure(i18n("could not remove %1", app.directory));
    }
    if (QFileInfo::exists(app.desktopFile) && !QFile::remove(app.desktopFile)) {
        return failure(i18n("could not remove %1", app.desktopFile));
    }
    if (!app.icon.isEmpty()) {
        const Result<QString> iconRoot = icons::storageDir();
        if (iconRoot && QFileInfo(app.icon).absolutePath() == paths::clean(*iconRoot)
            && QFileInfo::exists(app.icon)) {
            (void)QFile::remove(app.icon);
        }
    }

    desktop::refreshIntegrations();
    if (const Status removed = updates::recordRemoval(app.directory); !removed) {
        return failure(withContext(
            i18n("application files were removed, but database cleanup failed"), removed.error()));
    }
    return {};
}

} // namespace tardrop::installer
