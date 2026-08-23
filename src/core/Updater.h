#pragma once

/// Installed-application bookkeeping and the reversible update transaction.
///
/// This module does not participate in extraction. It records completed installer transactions
/// and, when a user explicitly requests it, coordinates a reversible replacement transaction.

#include "core/Result.h"
#include "core/Types.h"

#include <optional>

namespace tardrop::updates {

/// Records an installer success. Unknown sources deliberately default to Manual, never guessed.
Status recordInstall(const InstalledApp &app, const QString &archive);

/// Removes the database record after the installer has removed its owned files.
Status recordRemoval(const QString &installPath);

/// Checks a record through its configured provider and persists the resulting timestamp/version.
/// The returned release is present only when it is actually newer than the installed version.
Result<std::optional<ReleaseInfo>> checkForUpdate(InstalledRecord &record);

/// Replaces an application only after a newly downloaded archive has passed the existing installer.
/// The prior directory and launcher bytes remain in a private backup until the new install succeeds.
Result<InstalledRecord> update(const InstalledRecord &record, const Logger &log);

/// Compares dot-separated numeric releases conservatively; non-numeric tags are only different.
bool isNewer(const QString &installed, const QString &latest);

/// Applies the chosen update interval to one record's persisted last-check timestamp.
bool startupCheckDue(const InstalledRecord &record, UpdateInterval interval);

} // namespace tardrop::updates
