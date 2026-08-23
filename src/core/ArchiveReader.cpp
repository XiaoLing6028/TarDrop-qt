#include "core/ArchiveReader.h"

#include "core/Paths.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <archive.h>
#include <archive_entry.h>

#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace tardrop::archives {
namespace {

constexpr std::size_t kBlockSize = 64 * 1024;

/// RAII wrapper so no libarchive handle survives an early return.
struct ArchiveReadDeleter {
    void operator()(struct archive *handle) const noexcept
    {
        if (handle) {
            archive_read_free(handle);
        }
    }
};
using ArchiveReadPtr = std::unique_ptr<struct archive, ArchiveReadDeleter>;

/// RAII wrapper for the `open(2)` descriptor used to create extracted files.
class Descriptor
{
public:
    explicit Descriptor(int fd) noexcept : m_fd(fd) {}
    ~Descriptor() { reset(); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_fd >= 0; }
    [[nodiscard]] int get() const noexcept { return m_fd; }
    void reset() noexcept
    {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

private:
    int m_fd;
};

/// Reports the libarchive error text, falling back to a generic message.
QString readerError(struct archive *handle, const QString &fallback)
{
    const char *message = archive_error_string(handle);
    return (message && *message) ? QString::fromLocal8Bit(message) : fallback;
}

/// Ensures an archive path is a relative normal path and turns it into a destination path.
///
/// Absolute paths, traversal, and empty names are refused outright instead of being sanitised,
/// so a hostile archive cannot silently place a file somewhere unexpected.
Result<QString> safeDestination(const QString &root, const QString &archivePath)
{
    if (archivePath.isEmpty()) {
        return failure(i18n("archive contains an empty path"));
    }
    const auto unsafePath = [&] {
        return failure(i18n("archive contains unsafe path: %1", archivePath));
    };
    if (archivePath.startsWith(u'/')) {
        return unsafePath();
    }

    QString result = root;
    bool any = false;
    for (const QString &piece : archivePath.split(u'/')) {
        if (piece.isEmpty()) {
            continue; // Repeated or trailing separators carry no component.
        }
        if (piece == QStringLiteral(".") || piece == QStringLiteral("..")) {
            return unsafePath();
        }
        result = paths::join(result, piece);
        any = true;
    }
    if (!any) {
        return unsafePath();
    }
    // Defence in depth: the component walk above already guarantees this.
    if (!paths::isWithin(root, result)) {
        return unsafePath();
    }
    return result;
}

/// Configures a reader for exactly one format and one filter.
///
/// Enabling only the detected combination keeps the extension-based decision authoritative: an
/// archive cannot claim to be a `.zip` and then be decoded as something else entirely.
void configureReader(struct archive *handle, Format format)
{
    switch (format) {
    case Format::Tar:
        archive_read_support_format_tar(handle);
        archive_read_support_filter_none(handle);
        break;
    case Format::TarGz:
        archive_read_support_format_tar(handle);
        archive_read_support_filter_gzip(handle);
        break;
    case Format::TarXz:
        archive_read_support_format_tar(handle);
        archive_read_support_filter_xz(handle);
        break;
    case Format::TarBz2:
        archive_read_support_format_tar(handle);
        archive_read_support_filter_bzip2(handle);
        break;
    case Format::Zip:
        archive_read_support_format_zip(handle);
        archive_read_support_filter_none(handle);
        break;
    }
}

/// Retains only ordinary read/write/execute bits. This prevents setuid/setgid archives.
void setSafeMode(int fd, mode_t mode)
{
    ::fchmod(fd, mode & 0777);
}

/// Streams one regular member into a freshly created file.
///
/// `O_EXCL` refuses to overwrite an existing path, so an archive that lists the same name twice
/// fails loudly instead of quietly replacing already-extracted content.
Status writeRegularFile(struct archive *handle, const QString &output, mode_t mode)
{
    const QByteArray encoded = QFile::encodeName(output);
    Descriptor descriptor(::open(encoded.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (!descriptor.valid()) {
        return failure(i18n("refusing to overwrite %1", output));
    }

    const void *buffer = nullptr;
    std::size_t size = 0;
    la_int64_t offset = 0;
    while (true) {
        const int status = archive_read_data_block(handle, &buffer, &size, &offset);
        if (status == ARCHIVE_EOF) {
            break;
        }
        if (status < ARCHIVE_WARN) {
            return failure(readerError(handle, i18n("could not read archive member")));
        }
        // Sparse members report gaps as jumps in `offset`; seeking keeps the file's holes intact.
        if (::lseek(descriptor.get(), static_cast<off_t>(offset), SEEK_SET) < 0) {
            return failure(i18n("could not write %1", output));
        }
        const char *cursor = static_cast<const char *>(buffer);
        std::size_t remaining = size;
        while (remaining > 0) {
            const ssize_t written = ::write(descriptor.get(), cursor, remaining);
            if (written <= 0) {
                if (written < 0 && errno == EINTR) {
                    continue;
                }
                return failure(i18n("could not write %1", output));
            }
            cursor += written;
            remaining -= static_cast<std::size_t>(written);
        }
    }

    setSafeMode(descriptor.get(), mode);
    return {};
}

} // namespace

Result<Format> detect(const QString &path)
{
    const QString lower = QFileInfo(path).fileName().toLower();
    if (lower.endsWith(QStringLiteral(".tar.gz")) || lower.endsWith(QStringLiteral(".tgz"))) {
        return Format::TarGz;
    }
    if (lower.endsWith(QStringLiteral(".tar.xz"))) {
        return Format::TarXz;
    }
    if (lower.endsWith(QStringLiteral(".tar.bz2"))) {
        return Format::TarBz2;
    }
    if (lower.endsWith(QStringLiteral(".tar"))) {
        return Format::Tar;
    }
    if (lower.endsWith(QStringLiteral(".zip"))) {
        return Format::Zip;
    }
    return failure(i18n("Unsupported archive type. Use tar, tar.gz, tgz, tar.xz, tar.bz2, or zip."));
}

Status extract(const QString &source, Format format, const QString &destination)
{
    if (!QDir().mkpath(destination)) {
        return failure(i18n("could not create extraction directory"));
    }
    const QString root = paths::clean(destination);

    ArchiveReadPtr reader(archive_read_new());
    if (!reader) {
        return failure(i18n("could not create an archive reader"));
    }
    configureReader(reader.get(), format);

    if (archive_read_open_filename(reader.get(), QFile::encodeName(source).constData(), kBlockSize)
        != ARCHIVE_OK) {
        return failure(readerError(reader.get(), i18n("could not open archive")));
    }

    while (true) {
        struct archive_entry *entry = nullptr;
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) {
            break;
        }
        if (status < ARCHIVE_WARN) {
            return failure(readerError(reader.get(), i18n("could not read archive member")));
        }

        const char *rawPath = archive_entry_pathname_utf8(entry);
        const QString relative = rawPath ? QString::fromUtf8(rawPath)
                                         : QFile::decodeName(archive_entry_pathname(entry));
        const Result<QString> output = safeDestination(root, relative);
        if (!output) {
            return failure(output.error());
        }

        // A hard link points at an already-extracted file and would let one member alias another.
        if (archive_entry_hardlink(entry) != nullptr
            || archive_entry_hardlink_utf8(entry) != nullptr) {
            return failure(i18n("archive contains a hard link: %1", relative));
        }

        const mode_t filetype = archive_entry_filetype(entry);
        if (filetype == AE_IFDIR) {
            if (!QDir().mkpath(*output)) {
                return failure(i18n("could not create %1", *output));
            }
            continue;
        }
        if (filetype == AE_IFLNK) {
            return failure(i18n("archive contains a symbolic link: %1", relative));
        }
        if (filetype != AE_IFREG) {
            return failure(
                i18n("archive contains unsupported link or special file: %1", relative));
        }

        const QString parent = QFileInfo(*output).absolutePath();
        if (!QDir().mkpath(parent)) {
            return failure(i18n("could not create %1", parent));
        }
        // Some ZIP writers omit permissions entirely; fall back to a plain, non-executable file.
        mode_t mode = static_cast<mode_t>(archive_entry_perm(entry));
        if (mode == 0) {
            mode = (format == Format::Zip) ? 0644 : 0;
        }
        const Status written = writeRegularFile(reader.get(), *output, mode);
        if (!written) {
            return failure(written.error());
        }
    }

    if (archive_read_close(reader.get()) != ARCHIVE_OK) {
        return failure(readerError(reader.get(), i18n("could not finish reading the archive")));
    }
    return {};
}

QString nameFilter()
{
    return i18n("Portable archives (*.tar *.tar.gz *.tgz *.tar.xz *.tar.bz2 *.zip)");
}

} // namespace tardrop::archives
