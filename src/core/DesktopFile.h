#pragma once

/// Freedesktop desktop-entry metadata, writing, and desktop-cache refreshes.

#include "core/Result.h"

#include <QString>

#include <optional>

namespace tardrop::desktop {

/// Safe, optional metadata read from a package's desktop entry.
/// Every field is optional because an archive's entry is untrusted input, not a contract.
struct Metadata {
    QString source;
    std::optional<QString> name;
    std::optional<QString> icon;
    std::optional<QString> categories;
    std::optional<QString> comment;
    std::optional<bool> terminal;
    std::optional<QString> startupWmClass;
    std::optional<bool> startupNotify;
    std::optional<QString> mimeType;
};

/// Values used for TarDrop's generated launcher. The executable itself is always TarDrop-validated.
struct Entry {
    QString name;
    QString comment;
    QString executable;
    QString icon;
    QString categories;
    bool terminal = false;
    bool startupNotify = false;
    QString startupWmClass;
    QString mimeType;
    QString id;
};

/// Parses fields that can enrich a generated entry. Invalid values are discarded, not propagated.
std::optional<Metadata> readMetadata(const QString &path);

/// Writes a conservative, specification-shaped launcher after validating every copied value.
Result<QString> write(const Entry &entry);

/// Only accepts semicolon-separated desktop lists, preventing arbitrary newline-style injection.
bool validList(const QString &value);

/// Refreshes caches when the associated desktop tools are present; all failures are non-fatal.
void refreshIntegrations();

} // namespace tardrop::desktop
