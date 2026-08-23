# TarDrop

TarDrop is a KDE Plasma application that installs portable application archives into your own user
account. Drop a `.tar`, `.tar.gz`, `.tgz`, `.tar.xz`, `.tar.bz2`, or `.zip` archive onto its window.
It extracts the archive beneath `~/Applications`, finds a native ELF application inside it, and
writes a per-user launcher to `~/.local/share/applications`, so the application shows up in
Application Launcher, Kickoff, and KRunner.

It is written in C++23 with Qt 6 Widgets and the KDE Frameworks, and targets Linux only.

## Safety model

TarDrop does not use `tar`, `unzip`, a shell, or `sudo`. Archives are extracted into a private
staging directory first. Every member path is checked, and absolute paths, `..`, symlinks, hard
links, device files, and overwrite attempts are rejected. Only regular files and directories are
accepted, and setuid/setgid bits are stripped from everything written. It never runs archive
scripts; `install.sh` is not a launch candidate. A launcher is made only for an ELF binary or a
clearly named launcher script, and is never automatically launched.

Existing installations are either replaced, given a separate numbered directory, or cancelled.
Uninstall checks that it is deleting only an immediate child of `~/Applications`, the matching XDG
launcher, and an icon inside TarDrop's own icon directory.

## Requirements

* GNU/Linux with a Wayland or X11 session (KDE Plasma 6 recommended)
* CMake 3.28+, Ninja, and a C++23 compiler (GCC 14+ or Clang 18+)
* Qt 6.6 or newer — Core, Gui, Widgets, Network, Concurrent, DBus
* KDE Frameworks 6 — CoreAddons, I18n, WidgetsAddons, Config, ConfigWidgets, ColorScheme,
  IconThemes, Notifications, Crash, KIO, Service
* libarchive 3.6 or newer

On Arch Linux:

```bash
sudo pacman -S --needed cmake ninja gcc qt6-base kcoreaddons ki18n kwidgetsaddons \
    kconfig kconfigwidgets kcolorscheme kiconthemes knotifications kcrash kio kservice libarchive
```

## Build and run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tardrop
```

To install system-wide:

```bash
sudo cmake --install build --prefix /usr
```

`tardrop` also accepts archives as command-line arguments, and registers the supported archive MIME
types, so "Open With → TarDrop" works from Dolphin.

## Architecture

The project is split into a GUI-free core library and a thin presentation layer. All
security-sensitive decisions live in `tardrop-core`, which links only against QtCore, QtNetwork, and
libarchive, so no widget code can weaken them.

`src/core/`

* `Paths` — bounded user-directory and naming helpers.
* `Security` — archive SHA-256, ELF magic-byte detection, desktop-value validation, link-safe
  file-type checks.
* `ArchiveReader` — format detection by extension and streaming, path-validated extraction.
* `DesktopFile` — reads/validates existing `.desktop` metadata and writes the per-user launcher.
* `IconFinder` — chooses a likely PNG, SVG, XPM, or ICO icon without following links.
* `Installer` — the extract → inspect → publish transaction, and the uninstall rules.
* `Records` — the JSON-backed installed-app database and update preferences.
* `Providers` — the `UpdateProvider` interface and its built-in implementations.
* `Updater` — install bookkeeping, version detection, and the rollback-safe update transaction.

`src/gui/`

* `MainWindow` — the window, its `KPageWidget` navigation, and all wiring.
* `DropZone` — the drag-and-drop target.
* `InstallPage`, `RecordsPage`, `SettingsPage` — the four pages.
* `InstallController`, `UpdateController` — off-thread orchestration via `QtConcurrent`.
* `Dialogs` — existing-installation, launcher-choice, and update-source dialogs.
* `Widgets` — theme-derived colours and shared card/caption helpers.

## KDE integration

* Navigation uses `KPageWidget`, the same list-style page switcher as System Settings.
* Colours come from `KColorScheme`, so Breeze Light, Breeze Dark, and custom schemes all work.
* Applications are started through `KIO::ApplicationLauncherJob` using their own desktop entry, and
  folders open with `KIO::OpenUrlJob` — no `xdg-open` subprocess.
* A successful install raises a `KNotification`, so a queued batch can be left in the background.
* Inline feedback uses `KMessageWidget` rather than blocking message boxes; only the two decisions
  that genuinely gate the install queue are modal.
* `KAboutData`, `KCrash`, and window-state saving behave the way other KDE applications do.

## Updates

TarDrop records each successful install in `~/.local/share/tardrop/installed-apps.json`, including
its install and desktop paths, detected version, archive name, provider configuration, and check
timestamps. The database is human-readable and atomically replaced on changes. Update preferences
live in `settings.json` in the same directory. Both files use the same schema as previous versions.

The built-in providers are GitHub Releases, Static URL, Website endpoint, and Manual. New providers
implement the small `UpdateProvider` interface, without changing installer code. Applications
installed from a local archive default to **Manual** because TarDrop never invents a network source.
Provider configuration is stored in each record's `source_url` and `custom_metadata` and is edited
from **Updates → Update Source…**, so sources are never trusted from archive content.

An update is only downloaded after the user presses **Update**. TarDrop moves the existing
application to a private rollback location, invokes the normal archive validation/install path,
preserves the established launcher, icon, and root permissions, and restores the old application if
validation or installation fails — including when the downloaded archive turns out to be a different
application. For safety, version detection never executes `--version`; it uses desktop metadata,
version files, archive filenames, and provider release metadata.

## Notes

TarDrop intentionally rejects archives that contain symlinks, hardlinks, or special files. This is
stricter than ordinary archive tools because the product's job is safely handling untrusted
downloads. Launcher discovery scores root `AppRun`, safe `Exec=` targets from nearby desktop files,
conventional launcher scripts, root executables, and name matches. Dependency, documentation, and
known helper-binary trees are heavily penalized. When the two top candidates are within 10 points,
TarDrop shows a chooser instead of guessing.
