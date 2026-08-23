#pragma once

/// Pluggable update sources.
///
/// A provider is the only part of TarDrop that talks to the network, and it never decides on its
/// own to do so: `Updater` creates one solely from persisted, user-supplied configuration.

#include "core/Result.h"
#include "core/Types.h"

#include <QString>

#include <memory>

namespace tardrop::updates {

/// A provider can inspect remote metadata and, when available, fetch the selected release archive.
class UpdateProvider
{
public:
    virtual ~UpdateProvider() = default;

    /// Retrieves current release metadata without changing the installed application.
    [[nodiscard]] virtual Result<ReleaseInfo> checkLatest() = 0;

    /// Downloads the provider's latest release to a private temporary archive.
    [[nodiscard]] virtual Result<ReleaseInfo> downloadLatest(const QString &destination) = 0;
};

/// Creates an appropriate provider from persisted data; missing source information stays manual.
Result<std::unique_ptr<UpdateProvider>> providerFor(const InstalledRecord &record);

/// The configuration keys each provider reads out of `InstalledRecord::customMetadata`.
QStringList requiredMetadataKeys(ProviderKind kind);

} // namespace tardrop::updates
