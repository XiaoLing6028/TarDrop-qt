#pragma once

/// Archive recognition and extraction.
///
/// Every archive member is checked before it is written. This module never delegates extraction to
/// a shell command, which avoids command injection and inconsistent external tools.

#include "core/Result.h"

#include <QString>

namespace tardrop::archives {

/// Formats which TarDrop can safely read today. Add a value and a reader setup to extend it.
enum class Format { Tar, TarGz, TarXz, TarBz2, Zip };

/// Identifies a supported type by extension; no archive is extracted based on a guessed command.
Result<Format> detect(const QString &path);

/// Extracts `source` into the empty, private `destination` directory.
/// Symlinks, hardlinks, device files, and traversal paths are rejected rather than followed.
Status extract(const QString &source, Format format, const QString &destination);

/// Human-readable file-dialog name filters for every supported format.
QString nameFilter();

} // namespace tardrop::archives
