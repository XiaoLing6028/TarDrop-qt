#include "core/Security.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QFile>

#include <sys/stat.h>

namespace tardrop::security {
namespace {

/// Reads `lstat` data so a symbolic link is judged as a link, never as its target.
bool linkStat(const QString &path, struct stat &info)
{
    return ::lstat(QFile::encodeName(path).constData(), &info) == 0;
}

} // namespace

Result<QString> archiveSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(i18n("could not open archive"));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // Streaming keeps memory flat regardless of archive size and still surfaces read errors.
    if (!hash.addData(&file)) {
        return failure(i18n("could not read archive"));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool isElf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    char magic[4] = {};
    if (file.read(magic, sizeof(magic)) != sizeof(magic)) {
        return false;
    }
    return magic[0] == '\x7f' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

bool isExecutable(const QString &path)
{
    struct stat info {};
    return linkStat(path, info) && S_ISREG(info.st_mode) && (info.st_mode & S_IXUSR) != 0;
}

bool isRegularFile(const QString &path)
{
    struct stat info {};
    return linkStat(path, info) && S_ISREG(info.st_mode);
}

Status safeDesktopValue(const QString &value)
{
    if (value.isEmpty() || value.contains(u'\n') || value.contains(u'\r')
        || value.contains(QChar(u'\0'))) {
        return failure(i18n("unsafe desktop-entry value"));
    }
    return {};
}

} // namespace tardrop::security
