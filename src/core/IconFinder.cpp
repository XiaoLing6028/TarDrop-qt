#include "core/IconFinder.h"

#include "core/Paths.h"
#include "core/Security.h"

#include <KLocalizedString>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>

namespace tardrop::icons {
namespace {

/// Accepts only regular image files. The extractor has already rejected links inside archives,
/// and links are skipped again here so a pre-existing tree cannot redirect the copy.
bool validIcon(const QString &path)
{
    if (!security::isRegularFile(path)) {
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("svg") || suffix == QStringLiteral("png")
        || suffix == QStringLiteral("xpm") || suffix == QStringLiteral("ico");
}

/// Removes the separators that distinguish "my-app" from "my_app" from "My App".
QString collapse(const QString &value)
{
    QString result = value.toLower();
    result.remove(u' ');
    result.remove(u'-');
    result.remove(u'_');
    return result;
}

} // namespace

QString findIcon(const QString &root,
                 const QString &appName,
                 const QString &preferredName,
                 const QString &desktopFile)
{
    const QString wanted = collapse(preferredName.isEmpty() ? appName : preferredName);

    // A relative path in a desktop file is occasionally used by portable bundles; resolve it
    // against that file's directory and require the result to stay inside the extracted tree.
    if (!preferredName.isEmpty() && preferredName.contains(u'/')) {
        QString candidate = preferredName;
        if (!candidate.startsWith(u'/')) {
            if (desktopFile.isEmpty()) {
                candidate.clear();
            } else {
                candidate = paths::join(QFileInfo(desktopFile).absolutePath(), candidate);
            }
        }
        candidate = paths::clean(candidate);
        if (!candidate.isEmpty() && paths::isWithin(root, candidate) && validIcon(candidate)) {
            return candidate;
        }
    }

    // Collecting and sorting first keeps the choice reproducible for identical trees, which
    // readdir order alone would not guarantee.
    QStringList files;
    QDirIterator iterator(root,
                          QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.append(iterator.next());
    }
    std::sort(files.begin(), files.end());

    QString best;
    qint64 bestScore = 0;
    for (const QString &path : std::as_const(files)) {
        if (!validIcon(path)) {
            continue;
        }
        const QFileInfo info(path);
        const QString stem = collapse(info.completeBaseName());

        qint64 score = 0;
        if (stem == wanted) {
            score = 10'000;
        } else if (!wanted.isEmpty() && (stem.contains(wanted) || wanted.contains(stem))) {
            score = 4'000;
        } else if (stem.contains(QStringLiteral("icon"))) {
            score = 1'000;
        }

        // Vector icons are resolution-independent; common launcher raster sizes follow them.
        const QString suffix = info.suffix().toLower();
        if (suffix == QStringLiteral("svg")) {
            score += 3'000;
        } else if (stem.contains(QStringLiteral("256x256"))) {
            score += 2'560;
        } else if (stem.contains(QStringLiteral("128x128"))) {
            score += 1'280;
        } else if (stem.contains(QStringLiteral("64x64"))) {
            score += 640;
        } else if (stem.contains(QStringLiteral("48x48"))) {
            score += 480;
        }
        score += std::min<qint64>(info.size() / 1024, 500);

        if (best.isEmpty() || score > bestScore) {
            best = path;
            bestScore = score;
        }
    }
    return best;
}

Result<QString> storageDir()
{
    const Result<QString> icons = paths::iconsRootDir();
    if (!icons) {
        return failure(icons.error());
    }
    const QString root = paths::join(*icons, QStringLiteral("TarDrop"));
    if (!QDir().mkpath(root)) {
        return failure(i18n("could not create %1", root));
    }
    return root;
}

Result<QString> copyToTarDrop(const QString &source, const QString &id)
{
    const Result<QString> root = storageDir();
    if (!root) {
        return failure(root.error());
    }
    QString extension = QFileInfo(source).suffix().toLower();
    if (extension != QStringLiteral("svg") && extension != QStringLiteral("png")
        && extension != QStringLiteral("xpm") && extension != QStringLiteral("ico")) {
        extension = QStringLiteral("png");
    }
    const QString fileName = paths::sanitizeName(id).replace(u' ', u'-') + u'.' + extension;
    const QString target = paths::join(*root, fileName);

    // Replacing an icon from an earlier install of the same application is expected.
    if (QFile::exists(target) && !QFile::remove(target)) {
        return failure(i18n("could not replace %1", target));
    }
    if (!QFile::copy(source, target)) {
        return failure(i18n("could not copy %1", source));
    }
    return target;
}

} // namespace tardrop::icons
