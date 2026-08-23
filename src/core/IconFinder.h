#pragma once

/// Icon selection and copying into TarDrop's private XDG icon directory.

#include "core/Result.h"

#include <QString>

namespace tardrop::icons {

/// Finds an archive icon using the desktop `Icon` name when available, then quality hints.
/// Returns an empty string when the package ships nothing usable.
QString findIcon(const QString &root,
                 const QString &appName,
                 const QString &preferredName,
                 const QString &desktopFile);

/// Copies a verified image into one owned location so the launcher survives archive replacement.
Result<QString> copyToTarDrop(const QString &source, const QString &id);

/// The directory TarDrop owns for launcher icons; nothing outside it is ever deleted.
Result<QString> storageDir();

} // namespace tardrop::icons
