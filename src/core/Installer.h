#pragma once

/// The installation transaction: extract privately, inspect, then publish atomically.

#include "core/Result.h"
#include "core/Types.h"

#include <QString>

namespace tardrop::installer {

/// Installs an archive only below `~/Applications`; all public changes happen after inspection.
///
/// `selectedLauncher` is a path relative to the extracted root, supplied only after the user has
/// answered a `NeedsLauncherChoice` result.
///
/// `allowUnsafeContent` may only be set after the user has answered a `NeedsSecurityConfirmation`
/// result. It does not disable any check: the members those checks refused are left out of the
/// installation, and a member that would escape the extraction root still fails the install.
Result<InstallResult> install(const QString &source,
                              ExistingChoice choice,
                              const QString &selectedLauncher,
                              const Logger &log,
                              bool allowUnsafeContent = false);

/// Removes only the exact directory, desktop file, and icon recorded by TarDrop.
Status uninstall(const InstalledApp &app);

/// Scores likely application launchers inside an already-extracted tree.
/// Exposed for the update subsystem and for inspection; it never touches anything public.
Result<QList<LauncherCandidate>> executableCandidates(const QString &root,
                                                      const QString &archiveName);

} // namespace tardrop::installer
