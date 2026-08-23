#pragma once

/// Persistent installed-application records and update preferences.
///
/// The on-disk format is the same human-readable JSON the Rust implementation wrote, so an
/// existing `~/.local/share/tardrop/installed-apps.json` keeps working after the rewrite.

#include "core/Result.h"
#include "core/Types.h"

#include <QList>
#include <QString>

namespace tardrop::database {

/// Loads all records, treating a missing database as an empty first-run state.
Result<QList<InstalledRecord>> load();

/// Atomically saves records so a crash cannot leave a half-written JSON database.
Status save(const QList<InstalledRecord> &records);

/// Inserts or replaces a record by desktop path, which is stable across same-app updates.
Status upsert(const InstalledRecord &record);

/// Removes only a record belonging to the exact managed installation path.
Status remove(const QString &installPath);

/// Reads settings, returning safe defaults before the user has changed anything.
Result<UpdateSettings> loadSettings();

/// Saves settings atomically using the same directory as the installed-app database.
Status saveSettings(const UpdateSettings &settings);

/// Serialisation helpers, shared with the provider configuration UI.
QString providerToString(ProviderKind kind);
ProviderKind providerFromString(const QString &value);
QString intervalToString(UpdateInterval interval);
UpdateInterval intervalFromString(const QString &value);

/// The interval in seconds, or 0 when automatic checking is disabled.
quint64 intervalSeconds(UpdateInterval interval);

/// Current wall-clock time as a UNIX timestamp.
quint64 now();

} // namespace tardrop::database
