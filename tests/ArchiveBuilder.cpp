#include "ArchiveBuilder.h"

#include <QFile>

#include <archive.h>
#include <archive_entry.h>

#include <memory>

namespace tardrop::test {

Member file(const QString &path, const QByteArray &content, int permissions)
{
    return Member{Member::Type::File, path, content, QString(), permissions};
}

Member directory(const QString &path)
{
    return Member{Member::Type::Directory, path, QByteArray(), QString(), 0755};
}

Member symlink(const QString &path, const QString &target)
{
    return Member{Member::Type::Symlink, path, QByteArray(), target, 0777};
}

Member hardLink(const QString &path, const QString &target)
{
    return Member{Member::Type::HardLink, path, QByteArray(), target, 0644};
}

bool writeTarGz(const QString &output, const QList<Member> &members)
{
    const auto freeWriter = [](struct archive *handle) { archive_write_free(handle); };
    std::unique_ptr<struct archive, decltype(freeWriter)> writer(archive_write_new(), freeWriter);
    if (!writer || archive_write_set_format_pax_restricted(writer.get()) != ARCHIVE_OK
        || archive_write_add_filter_gzip(writer.get()) != ARCHIVE_OK
        || archive_write_open_filename(writer.get(), QFile::encodeName(output).constData())
            != ARCHIVE_OK) {
        return false;
    }

    for (const Member &member : members) {
        const auto freeEntry = [](struct archive_entry *entry) { archive_entry_free(entry); };
        std::unique_ptr<struct archive_entry, decltype(freeEntry)> entry(archive_entry_new(),
                                                                         freeEntry);
        archive_entry_set_pathname_utf8(entry.get(), member.path.toUtf8().constData());
        archive_entry_set_perm(entry.get(), static_cast<mode_t>(member.permissions));
        switch (member.type) {
        case Member::Type::File:
            archive_entry_set_filetype(entry.get(), AE_IFREG);
            archive_entry_set_size(entry.get(), member.content.size());
            break;
        case Member::Type::Directory:
            archive_entry_set_filetype(entry.get(), AE_IFDIR);
            break;
        case Member::Type::Symlink:
            archive_entry_set_filetype(entry.get(), AE_IFLNK);
            archive_entry_set_symlink_utf8(entry.get(), member.target.toUtf8().constData());
            break;
        case Member::Type::HardLink:
            archive_entry_set_filetype(entry.get(), AE_IFREG);
            archive_entry_set_hardlink_utf8(entry.get(), member.target.toUtf8().constData());
            break;
        }
        if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK) {
            return false;
        }
        if (member.type == Member::Type::File && !member.content.isEmpty()
            && archive_write_data(writer.get(), member.content.constData(),
                                  static_cast<size_t>(member.content.size()))
                != member.content.size()) {
            return false;
        }
    }
    return archive_write_close(writer.get()) == ARCHIVE_OK;
}

} // namespace tardrop::test
