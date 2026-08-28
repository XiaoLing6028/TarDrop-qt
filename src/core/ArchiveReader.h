#pragma once

/// Archive recognition and extraction.
///
/// Every archive member is checked before it is written. This module never delegates extraction to
/// a shell command, which avoids command injection and inconsistent external tools.

#include "core/Result.h"
#include "core/Types.h"

#include <QList>
#include <QString>

namespace tardrop::archives {

/// Formats which TarDrop can safely read today. Add a value and a reader setup to extend it.
enum class Format { Tar, TarGz, TarXz, TarBz2, Zip };

/// What extraction does with a member a check rejected.
///
/// Neither policy relaxes a check: `Strict` fails the whole archive, and `SkipUnsafe` — used only
/// after the user has been warned and has confirmed — leaves the rejected member out of the
/// extracted tree. A member is never written because the user consented to it.
enum class MemberPolicy { Strict, SkipUnsafe };

/// Identifies a supported type by extension; no archive is extracted based on a guessed command.
Result<Format> detect(const QString &path);

/// Reads only the member headers and reports everything the checks would reject.
///
/// Nothing is written and no member data is decoded, so this is safe to run on a package that has
/// already been refused, in order to describe every problem it has at once.
Result<QList<SecurityConcern>> inspect(const QString &source, Format format);

/// Extracts `source` into the empty, private `destination` directory.
/// Symlinks, hardlinks, device files, and traversal paths are rejected rather than followed.
///
/// Under `MemberPolicy::SkipUnsafe` the rejected members are appended to `skipped` (when given)
/// instead of failing the archive; a member that would escape `destination` still fails outright.
Status extract(const QString &source,
               Format format,
               const QString &destination,
               MemberPolicy policy = MemberPolicy::Strict,
               QList<SecurityConcern> *skipped = nullptr);

/// Human-readable file-dialog name filters for every supported format.
QString nameFilter();

} // namespace tardrop::archives
