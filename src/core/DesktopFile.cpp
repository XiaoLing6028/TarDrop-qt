#include "core/DesktopFile.h"

#include "core/Paths.h"
#include "core/Security.h"

#include <KLocalizedString>

#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace tardrop::desktop {
namespace {

/// Recognises only the two spellings the specification and common packagers actually emit.
std::optional<bool> parseBoolean(const QString &value)
{
    if (value == QStringLiteral("true") || value == QStringLiteral("True")) {
        return true;
    }
    if (value == QStringLiteral("false") || value == QStringLiteral("False")) {
        return false;
    }
    return std::nullopt;
}

/// Quotes an absolute path because archive names can legitimately contain spaces.
QString quoteArgument(const QString &path)
{
    QString escaped = path;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return u'"' + escaped + u'"';
}

/// Runs an optional desktop-integration helper, ignoring absence and failure alike.
void runIfPresent(const QString &program, const QStringList &arguments)
{
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return;
    }
    QProcess process;
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(program, arguments);
    process.waitForFinished(15'000);
}

} // namespace

std::optional<Metadata> readMetadata(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    Metadata metadata;
    metadata.source = path;
    bool inEntry = false;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line == QStringLiteral("[Desktop Entry]")) {
            inEntry = true;
            continue;
        }
        if (line.startsWith(u'[')) {
            inEntry = false;
        }
        if (!inEntry || line.startsWith(u'#')) {
            continue;
        }
        const qsizetype separator = line.indexOf(u'=');
        if (separator < 0) {
            continue;
        }
        // The key keeps any locale suffix, so `Name[de]` never overrides the plain `Name`.
        const QString key = line.left(separator);
        const QString value = line.mid(separator + 1);
        if (!security::safeDesktopValue(value)) {
            continue;
        }

        if (key == QStringLiteral("Name")) {
            metadata.name = value;
        } else if (key == QStringLiteral("Icon")) {
            metadata.icon = value;
        } else if (key == QStringLiteral("Categories") && validList(value)) {
            metadata.categories = value;
        } else if (key == QStringLiteral("Comment")) {
            metadata.comment = value;
        } else if (key == QStringLiteral("Terminal")) {
            metadata.terminal = parseBoolean(value);
        } else if (key == QStringLiteral("StartupNotify")) {
            metadata.startupNotify = parseBoolean(value);
        } else if (key == QStringLiteral("StartupWMClass")) {
            metadata.startupWmClass = value;
        } else if (key == QStringLiteral("MimeType") && validList(value)) {
            metadata.mimeType = value;
        }
    }
    return metadata;
}

Result<QString> write(const Entry &entry)
{
    for (const QString &value : {entry.name, entry.categories, entry.id}) {
        if (const Status status = security::safeDesktopValue(value); !status) {
            return failure(status.error());
        }
    }
    for (const QString &value : {entry.comment, entry.startupWmClass, entry.mimeType}) {
        if (value.isEmpty()) {
            continue;
        }
        if (const Status status = security::safeDesktopValue(value); !status) {
            return failure(status.error());
        }
    }

    const Result<QString> directory = paths::applicationsDatabaseDir();
    if (!directory) {
        return failure(directory.error());
    }
    const QString target = paths::join(*directory, entry.id + QStringLiteral(".desktop"));

    QString content = QStringLiteral("[Desktop Entry]\nVersion=1.5\nType=Application\nName=%1\n")
                          .arg(entry.name);
    if (!entry.comment.isEmpty()) {
        content += QStringLiteral("Comment=%1\n").arg(entry.comment);
    }
    content += QStringLiteral("Exec=%1\n").arg(quoteArgument(entry.executable));
    if (!entry.icon.isEmpty()) {
        content += QStringLiteral("Icon=%1\n").arg(QString(entry.icon).remove(u'\n'));
    }
    // StartupNotify is true only when the packaged entry explicitly declares support. Directly
    // launching a portable process normally cannot send the completion signal, which makes Plasma
    // show a misleading bouncing cursor until it times out.
    content += QStringLiteral("Terminal=%1\nCategories=%2\nStartupNotify=%3\n")
                   .arg(entry.terminal ? QStringLiteral("true") : QStringLiteral("false"),
                        entry.categories,
                        entry.startupNotify ? QStringLiteral("true") : QStringLiteral("false"));
    if (!entry.startupWmClass.isEmpty()) {
        content += QStringLiteral("StartupWMClass=%1\n").arg(entry.startupWmClass);
    }
    if (!entry.mimeType.isEmpty()) {
        content += QStringLiteral("MimeType=%1\n").arg(entry.mimeType);
    }

    // A save file keeps a half-written launcher from ever being visible to Plasma.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failure(i18n("could not write %1", target));
    }
    const QByteArray encoded = content.toUtf8();
    if (file.write(encoded) != encoded.size() || !file.commit()) {
        return failure(i18n("could not write %1", target));
    }
    return target;
}

bool validList(const QString &value)
{
    if (value.isEmpty() || !value.endsWith(u';')) {
        return false;
    }
    for (const QString &part : value.split(u';')) {
        for (const QChar character : part) {
            const bool allowed = (character.unicode() < 128 && character.isLetterOrNumber())
                || character == u'-' || character == u'_' || character == u'/' || character == u'.'
                || character == u'+';
            if (!allowed) {
                return false;
            }
        }
    }
    return true;
}

void refreshIntegrations()
{
    const Result<QString> applications = paths::applicationsDatabaseDir();
    const Result<QString> icons = paths::iconsRootDir();
    if (applications) {
        runIfPresent(QStringLiteral("update-desktop-database"), {*applications});
    }
    if (icons) {
        runIfPresent(QStringLiteral("gtk-update-icon-cache"),
                     {QStringLiteral("-f"), QStringLiteral("-t"), *icons});
    }
    // Plasma's service cache; harmless and quick when the application list is unchanged.
    runIfPresent(QStringLiteral("kbuildsycoca6"), {});
}

} // namespace tardrop::desktop
