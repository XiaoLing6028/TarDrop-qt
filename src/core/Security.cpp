#include "core/Security.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QFile>
#include <QStringList>

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

RiskAssessment assessRisk(const QList<SecurityConcern> &concerns)
{
    RiskAssessment risk;
    int escapingSymlinks = 0;
    int escapingHardLinks = 0;
    for (const SecurityConcern &concern : concerns) {
        switch (concern.kind) {
        case ConcernKind::SymbolicLink:
            ++risk.symlinks;
            escapingSymlinks += concern.escapesPackage ? 1 : 0;
            break;
        case ConcernKind::HardLink:
            ++risk.hardLinks;
            escapingHardLinks += concern.escapesPackage ? 1 : 0;
            break;
        case ConcernKind::InstallScript:
            ++risk.installScripts;
            break;
        case ConcernKind::SpecialFile:
            ++risk.specialFiles;
            break;
        case ConcernKind::UnsafePath:
            break;
        }
    }
    risk.escapingLinks = escapingSymlinks + escapingHardLinks;

    const int links = risk.symlinks + risk.hardLinks;
    if (risk.installScripts > 0 || links >= 4) {
        risk.level = RiskLevel::High;
    } else if (links >= 2 || risk.escapingLinks > 0) {
        risk.level = RiskLevel::Medium;
    } else {
        risk.level = RiskLevel::Low;
    }

    QStringList reasons;
    if (risk.installScripts > 0) {
        reasons.append(i18n("install.sh script found (%1)", risk.installScripts));
    }
    if (escapingSymlinks > 0) {
        reasons.append(i18np("%1 symlink escapes the package", "%1 symlinks escape the package",
                             escapingSymlinks));
    }
    if (const int inside = risk.symlinks - escapingSymlinks; inside > 0) {
        reasons.append(i18np("%1 symlink inside the package", "%1 symlinks inside the package",
                             inside));
    }
    if (escapingHardLinks > 0) {
        reasons.append(i18np("%1 hard link escapes the package",
                             "%1 hard links escape the package", escapingHardLinks));
    }
    if (const int inside = risk.hardLinks - escapingHardLinks; inside > 0) {
        reasons.append(i18np("%1 hard link inside the package", "%1 hard links inside the package",
                             inside));
    }
    if (risk.specialFiles > 0) {
        reasons.append(i18np("%1 special file", "%1 special files", risk.specialFiles));
    }
    if (reasons.isEmpty()) {
        reasons.append(i18n("no links or install scripts"));
    }

    QString level;
    switch (risk.level) {
    case RiskLevel::Low:
        level = i18nc("@label risk level", "Low");
        break;
    case RiskLevel::Medium:
        level = i18nc("@label risk level", "Medium");
        break;
    case RiskLevel::High:
        level = i18nc("@label risk level", "High");
        break;
    }
    risk.summary = i18nc("@info risk level, then the reasons for it", "%1 — %2", level,
                         reasons.join(QStringLiteral(", ")));
    return risk;
}

} // namespace tardrop::security
