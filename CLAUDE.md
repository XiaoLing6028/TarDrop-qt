# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

TarDrop is a KDE Plasma, user-local installer for portable application archives (`.tar`, `.tar.gz`,
`.tgz`, `.tar.xz`, `.tar.bz2`, `.zip`). It extracts an archive under `~/Applications`, finds a native
ELF launcher inside it, and writes a per-user `.desktop` launcher to `~/.local/share/applications`.

It is **Linux only** — C++23, Qt 6 Widgets, KDE Frameworks 6, CMake + Ninja. Do not add
Windows/macOS support or a cross-platform abstraction layer; `CMakeLists.txt` refuses to configure
on other systems on purpose.

## Commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build                                  # build (or: ninja -C build)
./build/tardrop                                      # run
sudo cmake --install build --prefix /usr             # install
```

Use `-DCMAKE_BUILD_TYPE=Debug` while developing. `CMAKE_EXPORT_COMPILE_COMMANDS` is on, so
`build/compile_commands.json` drives clangd.

There are no automated tests in this repository. Verify behavior manually by running the binary and
exercising the drag-and-drop install/uninstall/update flows. When testing, point `HOME` and
`XDG_DATA_HOME` at a scratch directory so your real `~/Applications` and launcher directory are not
touched.

## Safety model (the core design constraint)

This is the single most important thing to preserve when touching anything in `src/core/`:
**TarDrop never shells out and never trusts archive content.**

- No `tar`/`unzip`/shell/`sudo` is invoked; extraction goes through libarchive directly
  (`ArchiveReader.cpp`), and only the format and filter matching the detected extension are enabled,
  so a file cannot claim one format and be decoded as another.
- Archives are extracted into a private staging directory first (`QTemporaryDir` inside
  `~/Applications`, mode 0700), never directly into a public location. Publishing is a
  same-filesystem `rename`.
- Every archive member path is validated: absolute paths, `..`, symlinks, hard links, and
  device/special files are rejected outright (`safeDestination`, plus `archive_entry_filetype` and
  `archive_entry_hardlink` checks). Only regular files and directories are accepted, writes use
  `O_EXCL` to refuse silently overwriting an existing path, and modes are masked to `0777` so
  setuid/setgid cannot survive extraction.
- Archive scripts (e.g. `install.sh`) are never treated as launch candidates and nothing from an
  archive is ever executed automatically — not even to detect a version (`Updater.cpp`'s
  `detectVersion` deliberately avoids running `--version`).
- A launcher candidate must be an ELF binary (`security::isElf`, checked by magic bytes, not
  filename/extension) or a clearly-named launcher script, and it is never auto-launched after
  install.
- File-type and permission checks use `lstat` (`security::isExecutable`, `isRegularFile`) so a
  symbolic link is judged as a link, never as its target.
- Existing installations are handled explicitly via `ExistingChoice` (Replace / KeepBoth / Cancel);
  `installer::uninstall` and `install`'s destination logic double-check that any path being deleted
  or replaced is a direct, owned child of `~/Applications` (or the XDG applications dir for desktop
  files, or `~/.local/share/icons/TarDrop` for icons) before touching it — never widen these checks
  to arbitrary paths.
- `.desktop` files are generated from a fixed template (`desktop::write`); values are validated
  (`security::safeDesktopValue`, `desktop::validList`) to prevent desktop-entry injection, and
  `Exec=` targets read from bundled desktop files are validated the same way
  (`desktopExecTarget` rejects shell metacharacters, field codes, and anything resolving outside the
  extracted root) — that function only ever reads `Exec=` to *identify* a launcher, never to run it.

When adding a new archive format or provider, follow the same pattern used by the existing ones
rather than introducing a shortcut that bypasses this validation.

## Architecture

The build produces two targets, and the split is the security boundary:

- **`tardrop-core`** (static library, `src/core/`) — every security-sensitive decision. It links
  only QtCore, QtNetwork, and libarchive; it must never gain a widget dependency.
- **`tardrop`** (executable, `src/gui/`) — Qt Widgets + KDE Frameworks presentation only.

Core modules:

- **`Result.h`** — `Result<T>` / `Status` are `std::expected<…, QString>`. Core code reports failure
  by value, never by exception, so every failure path is visible at the call site.
- **`Paths`** — bounded user-directory helpers, name sanitising/prettifying, `directChild`, and the
  lexical `clean`/`isWithin`/`relativeTo`/`components` helpers the containment checks rely on.
- **`Security`** — archive SHA-256, ELF magic-byte detection, safe desktop-entry value validation,
  and the `lstat`-based file-type checks.
- **`ArchiveReader`** — format detection (by filename extension only) and streaming, path-validated
  extraction. All libarchive C-API use is confined here, behind RAII wrappers.
- **`Installer`** — the core transaction: extract to a private staging dir → score and select a
  launcher candidate → read desktop metadata that specifically targets the selected launcher →
  publish (atomic `rename` into `~/Applications`) → write the desktop file → record the install.
  Also owns `uninstall`. Read this file first to understand the end-to-end flow.
  - Launcher selection is a scored heuristic (`executableCandidates`/`addCandidate`/
    `candidatePenalty`): root `AppRun` scores highest, then named launcher scripts, then
    desktop-entry `Exec=` targets, then root-level/name-matching executables, with heavy penalties
    for `lib/`, `share/`, `doc/`, `node_modules/`, etc. When the top two candidates score within 10
    points of each other, installation pauses and returns `NeedsLauncherChoice` so the UI can ask
    the user instead of guessing.
- **`IconFinder`** — picks a likely PNG/SVG/XPM/ICO icon without following symlinks (entries are
  sorted first so the choice is reproducible), and copies the chosen icon into XDG icon storage
  under a TarDrop-owned path.
- **`DesktopFile`** — reads/validates existing `.desktop` metadata from an archive (to reuse
  Name/Comment/Categories/etc.) and writes the new per-user launcher entry via `QSaveFile`; also
  asks the desktop database to refresh (best-effort; `update-desktop-database` and `kbuildsycoca6`
  are optional since Plasma also discovers the launcher directory directly).
- **`Records`** — persists installed-app records as human-readable JSON at
  `~/.local/share/tardrop/installed-apps.json` and preferences at `settings.json` in the same
  directory, both written atomically via `QSaveFile`. **The JSON key names and enum spellings are
  wire-compatible with the previous Rust implementation** (`git_hub_releases`, `static_url`,
  `website_scraper`, `manual`); do not rename them.
- **`Providers`** — the `UpdateProvider` interface (`checkLatest` / `downloadLatest`) with built-in
  providers: **GitHub Releases**, **Static URL**, **Website endpoint** (plain-text version scraping,
  no JS execution), and **Manual** (the default — TarDrop never invents a network source for a
  locally-installed archive). This is the only part of the codebase that touches the network. It
  uses `QNetworkAccessManager` with a nested `QEventLoop`, which is safe because providers only ever
  run on a `QtConcurrent` worker thread.
- **`Updater`** — install bookkeeping plus a reversible update transaction: move the current install
  to a private rollback dir, run the normal `installer::install` path on the downloaded archive, and
  restore from the rollback dir on any failure (wrong app identity, needs-launcher-choice, or
  install error) while preserving the established launcher/icon/permissions on success.

GUI modules (`src/gui/`) are deliberately thin — security-sensitive logic lives in core, not here.
`InstallController` and `UpdateController` own all off-thread work via `QtConcurrent::run` +
`QFutureWatcher`; progress lines produced on the worker thread are marshalled back with
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. `InstallController` enforces that the queue
advances only after the previous task and any modal decision it raised have resolved.

Adding a new archive format: extend `archives::Format` and `detect`/`configureReader` in
`ArchiveReader.cpp`. Adding a new update source: add a `ProviderKind` value, implement
`UpdateProvider` in `Providers.cpp`, list its configuration keys in `requiredMetadataKeys` (the
Update Source dialog builds its form from that), and add the enum's JSON spelling in `Records.cpp`
— no installer changes needed, per the interface's design goal.

## Conventions

- C++23; prefer `std::expected` over exceptions, and RAII over manual cleanup.
- `QT_NO_CAST_FROM_ASCII` is enabled: use `QStringLiteral` / `QLatin1String` for literals.
- All user-visible strings go through `i18n()` / `i18nc()` / `i18np()`.
- Colours come from `KColorScheme`, never hard-coded hex values, so custom themes work.
- Prefer KDE APIs over subprocesses: `KIO::ApplicationLauncherJob` and `KIO::OpenUrlJob` instead of
  `xdg-open`, `KNotification` instead of ad-hoc dialogs.
