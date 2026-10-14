#pragma once

/// Small checks shared by the installer before a launcher is made public.

#include "core/Result.h"
#include "core/Types.h"

#include <QList>

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

/// Grades the members a security check refused, so the user sees how worrying they are before
/// deciding whether to install the rest of the package.
///
/// * High — any `install.sh`, or four or more symlinks/hard links.
/// * Medium — otherwise two or three links, or any link whose target leaves the package.
/// * Low — otherwise: at most one link, pointing inside the package.
///
/// An `install.sh` ranks highest because TarDrop never runs it: a package that expects it to run
/// is likely to be broken, or to do something at install time the user cannot see. A link that
/// escapes the package is what a link-based attack needs, so it alone outweighs the count rule.
/// Unsafe paths are not graded; they fail the install before anything is asked.
RiskAssessment assessRisk(const QList<SecurityConcern> &concerns);

} // namespace tardrop::security
