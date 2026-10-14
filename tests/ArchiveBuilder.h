#pragma once

/// Writes small tar.gz fixtures with libarchive, so tests can describe hostile archives in code
/// instead of shipping binary files.

#include <QByteArray>
#include <QList>
#include <QString>

namespace tardrop::test {

struct Member {
    enum class Type { File, Directory, Symlink, HardLink };

    Type type = Type::File;
    QString path;
    QByteArray content; ///< files only
    QString target;     ///< links only, stored verbatim
    int permissions = 0644;
};

Member file(const QString &path, const QByteArray &content = {}, int permissions = 0644);
Member directory(const QString &path);
Member symlink(const QString &path, const QString &target);
Member hardLink(const QString &path, const QString &target);

/// Writes `members`, in order, as a gzip-compressed pax archive. Returns false on any error.
bool writeTarGz(const QString &output, const QList<Member> &members);

} // namespace tardrop::test
