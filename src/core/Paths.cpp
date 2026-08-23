#include "core/Paths.h"

#include <KLocalizedString>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace tardrop::paths {
namespace {

/// Creates a directory tree and reports the reason when the user's home is unusable.
Result<QString> ensureDir(const QString &path)
{
    if (path.isEmpty()) {
        return failure(i18n("could not determine the directory location"));
    }
    if (!QDir().mkpath(path)) {
        return failure(i18n("could not create %1", path));
    }
    return path;
}

} // namespace

Result<QString> applicationsDir()
{
    const QString home = QDir::homePath();
    if (home.isEmpty()) {
        return failure(i18n("could not determine home directory"));
    }
    return ensureDir(join(home, QStringLiteral("Applications")));
}

Result<QString> applicationsDatabaseDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        return failure(i18n("could not determine XDG data directory"));
    }
    return ensureDir(join(base, QStringLiteral("applications")));
}

Result<QString> iconsRootDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        return failure(i18n("could not determine XDG data directory"));
    }
    return ensureDir(join(base, QStringLiteral("icons")));
}

Result<QString> dataDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        return failure(i18n("could not determine XDG data directory"));
    }
    return ensureDir(join(base, QStringLiteral("tardrop")));
}

QString archiveStem(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (name.isEmpty()) {
        name = QStringLiteral("Application");
    }
    static const QStringList suffixes = {
        QStringLiteral(".tar.gz"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.bz2"),
        QStringLiteral(".tgz"),    QStringLiteral(".tar"),    QStringLiteral(".zip"),
    };
    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix, Qt::CaseInsensitive)) {
            name.chop(suffix.size());
            break;
        }
    }
    return sanitizeName(name);
}

QString sanitizeName(const QString &name)
{
    QString cleaned;
    cleaned.reserve(name.size());
    for (const QChar character : name) {
        const bool keep = character.isLetterOrNumber() || character == u' ' || character == u'-'
            || character == u'_' || character == u'.';
        cleaned.append(keep ? character : u'_');
    }

    qsizetype first = 0;
    qsizetype last = cleaned.size();
    while (first < last && (cleaned.at(first) == u' ' || cleaned.at(first) == u'.')) {
        ++first;
    }
    while (last > first && (cleaned.at(last - 1) == u' ' || cleaned.at(last - 1) == u'.')) {
        --last;
    }

    const QString trimmed = cleaned.mid(first, last - first);
    return trimmed.isEmpty() ? QStringLiteral("Application") : trimmed.left(80);
}

QString prettyName(const QString &raw)
{
    QStringList words;
    QString current;
    const auto flush = [&] {
        if (current.isEmpty()) {
            return;
        }
        // Only the first character is raised; the rest is normalised so machine names such as
        // "APP_launcher" do not shout in the launcher menu.
        words.append(current.at(0).toUpper() + current.mid(1).toLower());
        current.clear();
    };
    for (const QChar character : raw) {
        if (character == u'-' || character == u'_' || character.isSpace()) {
            flush();
        } else {
            current.append(character);
        }
    }
    flush();
    return words.join(u' ').left(80);
}

Result<QString> directChild(const QString &root, const QString &name)
{
    // A child must contribute exactly one path component, so separators and traversal are refused
    // before the path is ever built.
    if (name.isEmpty() || name.contains(u'/') || name == QStringLiteral(".")
        || name == QStringLiteral("..")) {
        return failure(i18n("invalid installation name"));
    }
    const QString candidate = join(root, name);
    if (QFileInfo(candidate).absolutePath() != QFileInfo(root).absoluteFilePath()) {
        return failure(i18n("invalid installation name"));
    }
    return candidate;
}

QString desktopId(const QString &name)
{
    return QStringLiteral("org.tardrop.") + sanitizeName(name).replace(u' ', u'-');
}

QString clean(const QString &path)
{
    return QDir::cleanPath(path);
}

bool isWithin(const QString &root, const QString &path)
{
    const QString cleanRoot = clean(root);
    const QString cleanPath = clean(path);
    if (cleanPath == cleanRoot) {
        return true;
    }
    const QString prefix = cleanRoot.endsWith(u'/') ? cleanRoot : cleanRoot + u'/';
    return cleanPath.startsWith(prefix);
}

QStringList components(const QString &relativePath)
{
    QStringList parts;
    for (const QString &piece : clean(relativePath).split(u'/', Qt::SkipEmptyParts)) {
        if (piece != QStringLiteral(".")) {
            parts.append(piece);
        }
    }
    return parts;
}

QString relativeTo(const QString &root, const QString &path)
{
    if (!isWithin(root, path)) {
        return {};
    }
    const QString cleanRoot = clean(root);
    const QString cleanPath = clean(path);
    if (cleanPath == cleanRoot) {
        return {};
    }
    return cleanPath.mid(cleanRoot.endsWith(u'/') ? cleanRoot.size() : cleanRoot.size() + 1);
}

QString join(const QString &parent, const QString &child)
{
    if (parent.isEmpty()) {
        return child;
    }
    if (parent.endsWith(u'/')) {
        return parent + child;
    }
    return parent + u'/' + child;
}

} // namespace tardrop::paths
