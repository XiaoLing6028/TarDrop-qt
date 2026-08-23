#pragma once

/// A tiny error-carrying result type shared by every core operation.
///
/// The Rust original used `anyhow::Result`; `std::expected` gives the same
/// "either a value or a human-readable reason" shape without exceptions, which
/// keeps the security-sensitive paths explicit about every failure.

#include <QString>

#include <expected>
#include <utility>

namespace tardrop {

template <class T>
using Result = std::expected<T, QString>;

using Status = std::expected<void, QString>;

/// Builds a failed `Result`/`Status` from a message.
[[nodiscard]] inline std::unexpected<QString> failure(QString message)
{
    return std::unexpected(std::move(message));
}

/// Prefixes a failure with context, mirroring `anyhow::Context`.
[[nodiscard]] inline QString withContext(const QString &context, const QString &reason)
{
    return context + QStringLiteral(": ") + reason;
}

} // namespace tardrop
