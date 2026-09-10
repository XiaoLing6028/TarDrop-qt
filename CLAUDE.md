# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure (Debug also fine)
cmake --build build                                  # build
./build/tardrop [archive...]                         # run; archives may be passed as arguments
sudo cmake --install build --prefix /usr             # system-wide install
```

Requires CMake 3.28+, Ninja, a C++23 compiler (GCC 14+/Clang 18+), Qt 6.6+, KDE Frameworks 6, and
libarchive 3.6+. The build hard-fails on non-Linux. There is no test suite, linter config, or CI in
the repo — verify changes by building and exercising the app. `compile_commands.json` is exported
into `build/` for clangd.

## Architecture

Two CMake targets enforce a security boundary that must not be crossed:

* **`tardrop-core`** (static lib, `src/core/`) — every security-sensitive decision. It links only
  QtCore, QtNetwork, libarchive, KCoreAddons, and KI18n. Do not add widget/GUI dependencies here;
  the point is that presentation code cannot weaken the safety rules.
* **`tardrop`** (executable, `src/main.cpp`, `src/gui/`) — Qt Widgets/KDE front end. Owns wiring and
  presentation only.

Core modules: `Paths` (bounded naming/directory helpers), `Security` (SHA-256, ELF magic, desktop
value validation, link-safe file-type checks), `ArchiveReader` (format detection + path-validated
streaming extraction), `DesktopFile`, `IconFinder`, `Installer` (extract → inspect → publish
transaction, plus uninstall rules), `Records` (JSON database + settings), `Providers`
(`UpdateProvider` interface and built-ins), `Updater` (version detection, rollback-safe update).

GUI: `MainWindow` hosts a `KPageWidget` with four pages (`InstallPage`, two `RecordsPage`
instances for Installed/Updates, `SettingsPage`). `InstallController` and `UpdateController` run
core work off-thread via `QtConcurrent` + `QFutureWatcher`; `InstallController` keeps a sequential
queue that only advances after the previous task and any modal decision it raised has resolved.
`Dialogs` holds the three decision dialogs; `Widgets` holds theme-derived colours and card helpers.

### Error handling

No exceptions. Core returns `tardrop::Result<T>` = `std::expected<T, QString>` and `Status` =
`std::expected<void, QString>` (`src/core/Result.h`), with `failure(msg)` and `withContext()`
helpers. The error string is a translated, user-facing reason — it is displayed directly.

### Install flow

`installer::install()` returns `InstallResult`, a `std::variant` of `InstalledApp`,
`NeedsLauncherChoice`, or `NeedsSecurityConfirmation`. The two "needs" arms are questions for the
user, not failures: the controller emits a prompt signal, and the install is re-invoked with the
answer (`selectedLauncher` / `allowUnsafeContent`). `allowUnsafeContent` never relaxes a check — it
omits the refused members, and non-overridable concerns (traversal, absolute paths) still fail.

## Invariants to preserve

These are product requirements, not incidental behaviour. Do not "simplify" them away:

* No `tar`, `unzip`, shell, or `sudo` subprocesses; archive work goes through libarchive.
* Extraction happens in a private staging directory first; publish only after inspection.
* Symlinks, hard links, device files, absolute paths, `..`, and overwrites are rejected; setuid/
  setgid bits are stripped. Only regular files and directories are accepted.
* Archive scripts are never executed — not at install, and not for version detection (no
  `--version` invocation). Version comes from desktop metadata, version files, archive names, or
  provider release metadata.
* Installs are confined to immediate children of `~/Applications`; uninstall deletes only the
  recorded directory, XDG launcher, and TarDrop-owned icon.
* Update sources are never taken from archive content — only from persisted, user-entered config in
  `InstalledRecord::source_url`/`custom_metadata`. Local-archive installs default to `Manual`.
* Networking lives only in `Providers`, and only when `Updater` builds a provider from that config.
* Launcher candidates within 10 points of each other trigger a chooser rather than a guess.

## Conventions

* `QT_NO_CAST_FROM_ASCII`, `QT_NO_URL_CAST_FROM_STRING`, and `QT_USE_QSTRINGBUILDER` are defined:
  wrap every literal in `QStringLiteral` / `QByteArrayLiteral`, and user-facing text in `i18n()`.
* Builds use `-Wall -Wextra -Wpedantic` plus hardening flags; keep new code warning-clean.
* Headers use `#pragma once`, `///` doc comments explaining *why* a rule exists, and includes
  relative to `src/` (`#include "core/Types.h"`).
* Shared value types live in `src/core/Types.h`; anything crossing a queued signal needs
  `Q_DECLARE_METATYPE` there and `qRegisterMetaType` in `main.cpp`.
* KDE integration is deliberate: `KIO::ApplicationLauncherJob`/`OpenUrlJob` instead of `xdg-open`,
  `KColorScheme` instead of hard-coded colours, `KMessageWidget` inline feedback instead of blocking
  message boxes (only the queue-gating decisions are modal), `KNotification` on success.
* On-disk JSON in `~/.local/share/tardrop/` (`installed-apps.json`, `settings.json`) keeps the
  schema of the earlier Rust implementation and is written atomically — do not break compatibility.
