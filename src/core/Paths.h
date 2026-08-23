#pragma once

/// Filesystem naming helpers. Keeping them here makes installer paths predictable.

#include "core/Result.h"

#include <QString>

namespace tardrop::paths {

/// Returns TarDrop's user-only root, creating it with normal user permissions when needed.
Result<QString> applicationsDir();

/// Returns the standard XDG directory used by Plasma and other launchers.
Result<QString> applicationsDatabaseDir();

/// Returns `~/.local/share/icons`, the XDG root TarDrop stores launcher icons under.
Result<QString> iconsRootDir();

/// Returns `~/.local/share/tardrop`, holding the installed-app database and settings.
Result<QString> dataDir();

/// Reduces an archive name to a friendly, safe application name.
QString archiveStem(const QString &path);

/// Keeps install directory names simple and prevents surprising hidden or traversal directories.
QString sanitizeName(const QString &name);

/// Converts a machine-oriented executable or archive name into a readable display name.
/// Only separator characters are rewritten, so the raw name remains available as a fallback.
QString prettyName(const QString &raw);

/// Ensures a calculated child stays immediately below the owned root.
Result<QString> directChild(const QString &root, const QString &name);

/// Turns a display name into a stable, XDG-friendly desktop file identifier.
QString desktopId(const QString &name);

/// Lexically cleans a path (resolving `.` and `..`) without touching the filesystem.
QString clean(const QString &path);

/// True when `path` is `root` itself or lies below it, compared on whole components.
bool isWithin(const QString &root, const QString &path);

/// Splits a cleaned relative path into its non-empty components.
QStringList components(const QString &relativePath);

/// Returns the path of `path` relative to `root`, or an empty string when it is not below it.
QString relativeTo(const QString &root, const QString &path);

/// Joins a parent directory and a child name with a single separator.
QString join(const QString &parent, const QString &child);

} // namespace tardrop::paths
