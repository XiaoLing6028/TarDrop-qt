#pragma once

/// Small checks shared by the installer before a launcher is made public.

#include "core/Result.h"

#include <QString>

namespace tardrop::security {

/// Hashes the input while reading it, both providing an audit value and detecting read failures.
Result<QString> archiveSha256(const QString &path);

/// Checks ELF magic rather than trusting a filename. Shell scripts are deliberately not launchers.
bool isElf(const QString &path);

/// Returns whether a regular file has user execute permission; never follows a link while deciding.
bool isExecutable(const QString &path);

/// Returns whether the path is a regular file, without following symbolic links.
bool isRegularFile(const QString &path);

/// Allows names that cannot inject a desktop-entry field or path component.
Status safeDesktopValue(const QString &value);

} // namespace tardrop::security
