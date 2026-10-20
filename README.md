# TarDrop

TarDrop is a KDE Plasma application that installs portable application archives into your own user
account. Drop a `.tar`, `.tar.gz`, `.tgz`, `.tar.xz`, `.tar.bz2`, or `.zip` archive onto its window.
It extracts the archive beneath `~/Applications`, finds a native ELF application inside it, and
writes a per-user launcher to `~/.local/share/applications`, so the application shows up in
Application Launcher, Kickoff, and KRunner.

It is written in C++23 with Qt 6 Widgets and the KDE Frameworks, and targets Linux only.
<img width="1032" height="672" alt="Screenshot_20260824_163405" src="https://github.com/user-attachments/assets/8a02755d-80e9-4fd3-a90d-bc3ce979ae32" />

[Screencast_20260824_163614.webm](https://github.com/user-attachments/assets/f6ca7b26-900c-4f4d-b8b2-795fe918553f)

## Safety model

TarDrop does not use `tar`, `unzip`, a shell, or `sudo`. Archives are extracted into a private
staging directory first. Every member path is checked, and absolute paths, `..`, symlinks, hard
links, device files, and overwrite attempts are rejected. Only regular files and directories are
accepted, and setuid/setgid bits are stripped from everything written. It never runs archive
scripts, and a package that ships an `install.sh` is refused like a link would be, so the script can
never become a launch candidate.

When a package is refused only for content that can be left out (links, special files,
`install.sh`), TarDrop shows a risk assessment and asks before doing anything:

* **High** — any `install.sh`, or 4 or more symlinks/hard links
* **Medium** — 2–3 symlinks/hard links, or any link pointing outside the package directory
* **Low** — at most one link, pointing inside the package

For example: "High — install.sh script found (1), 2 symlinks escape the package". Installing anyway
still never extracts the refused members; it installs the rest of the package without them.
Absolute paths and `..` are never negotiable and fail the install outright. A launcher is made only for an ELF binary or a
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

On Fedora:

```bash
sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config qt6-qtbase-devel \
    kf6-kcoreaddons-devel kf6-ki18n-devel kf6-kwidgetsaddons-devel kf6-kconfig-devel \
    kf6-kconfigwidgets-devel kf6-kcolorscheme-devel kf6-kiconthemes-devel \
    kf6-knotifications-devel kf6-kcrash-devel kf6-kio-devel kf6-kservice-devel libarchive-devel
```

## Build and run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tardrop
```

To run the unit tests (needs QtTest; pass `-DBUILD_TESTING=OFF` to skip building them):

```bash
ctest --test-dir build --output-on-failure
```

To install system-wide:

```bash
sudo cmake --install build --prefix /usr
```

`tardrop` also accepts archives as command-line arguments, and registers the supported archive MIME
types, so "Open With → TarDrop" works from Dolphin.

## Flatpak

TarDrop is also available as a single-file Flatpak bundle, `TarDrop.flatpak`, attached to
[GitHub Releases](https://github.com/XiaoLing6028/TarDrop-qt/releases). It is not published on
Flathub. It installs per user, without `sudo`, and works on KDE Plasma and GNOME: TarDrop itself
and every application it installs show up in Kickoff, KRunner, and the GNOME application overview.

### Setup

You need Flatpak itself. Many distributions, Fedora among them, ship it preinstalled; otherwise
install it with `sudo dnf install flatpak`, `sudo pacman -S flatpak`, or `sudo apt install flatpak`.
That is the only step that needs administrator rights. If you have never used Flatpak before, log out and back in
once after installing it, so your session picks up the directories Flatpak exports launchers to.

The bundle runs on the KDE Platform 6.11 runtime (`org.kde.Platform//6.11`). KDE distributes its
stable runtimes through Flathub's repository, so the runtime is downloaded from there; TarDrop itself
is not. The bundle names that repository, and `flatpak install` offers to add it when the runtime is
missing. To set it up yourself first:

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Platform//6.11
```

### Install, launch, update, uninstall

```bash
flatpak install --user ./TarDrop.flatpak      # install for the current user
flatpak run org.tardrop.TarDrop               # or start "TarDrop" from your application launcher
flatpak install --user ./TarDrop.flatpak      # update: install a newer bundle over the old one
flatpak update --user                         # updates the KDE runtime (bundles carry no update URL)
flatpak uninstall --user org.tardrop.TarDrop  # remove TarDrop
```

Check the bundle against the `TarDrop.flatpak.sha256` file from the same release before installing
it: `sha256sum -c TarDrop.flatpak.sha256`.

Uninstalling the Flatpak removes TarDrop only. The applications it installed, their launchers, and
its records in `~/.local/share/tardrop` are ordinary files in your home and stay, exactly as with a
native install; uninstall them from TarDrop first if you want them gone. `--delete-data` additionally
removes `~/.var/app/org.tardrop.TarDrop`, which only holds TarDrop's window state and caches.

### Permissions

TarDrop's whole job is to put applications and launchers onto the host, so it needs write access
outside its sandbox. It gets exactly the directories it writes, and nothing else in your home:

| Permission | Why |
| --- | --- |
| `--filesystem=~/Applications:create` | The install root; TarDrop only creates immediate children of it. |
| `--filesystem=xdg-data/applications:create` | Per-user launchers that Plasma and GNOME read. |
| `--filesystem=xdg-data/icons/TarDrop:create` | The launcher icons TarDrop copies out of each package. |
| `--filesystem=xdg-data/tardrop:create` | The installed-app database and settings, shared with a native install. |
| `--share=network` | The GitHub Releases, Static URL, and Website update providers. |
| `--socket=wayland`, `--socket=fallback-x11`, `--share=ipc` | Showing the window. |

Inside a Flatpak, `XDG_DATA_HOME` points at the app's private `~/.var/app` directory, which no
desktop launcher scans. TarDrop therefore writes launchers, icons, and records to the host's data
home (`$XDG_DATA_HOME`, else `~/.local/share`) — the same files a native build writes.

Archives are opened through desktop portals instead of broad file access: **Browse…** uses the file
chooser portal, "Open With → TarDrop" in Dolphin or Files forwards the file through the document
portal, and drag and drop uses the file-transfer portal, which Dolphin and GTK 4 file managers such
as GNOME Files offer.

Be aware of what the sandbox does and does not protect. The applications TarDrop installs are not
sandboxed — they run on the host, with the host's libraries, like any other program in your home.
TarDrop can create and replace launchers in `~/.local/share/applications`, and a launcher can run
anything once clicked, so the Flatpak sandbox is not a security boundary around what TarDrop
installs; TarDrop's own archive checks still are. TarDrop is deliberately *not* given
`org.freedesktop.Flatpak` (`flatpak-spawn --host`) or `org.freedesktop.systemd1`, so it cannot start
programs on the host by itself.

### Differences from the native build

* **Launch is unavailable.** From inside the sandbox, an application would start inside TarDrop's
  sandbox, against the KDE runtime's libraries instead of yours, and fail or misbehave. The button
  is disabled; start installed applications from your application launcher instead.
* **Plasma's application menu updates itself.** The native build runs `kbuildsycoca6` after each
  install; inside the sandbox that would only refresh TarDrop's private cache, so Plasma's `kded6`
  notices the new launcher on its own. GNOME watches the launcher directory directly.
* **Archive access goes through portals** (see above), so archives keep a
  `/run/user/<uid>/doc/…` path inside TarDrop. Only the archive file name is recorded.

### Building the bundle

```bash
./scripts/build-flatpak.sh            # writes dist/TarDrop.flatpak and dist/TarDrop.flatpak.sha256
```

This needs `flatpak` and `flatpak-builder`, and downloads `org.kde.Sdk//6.11` (about 1.2 GB) on the
first run. The manifest is `flatpak/org.tardrop.TarDrop.yml`; the unit tests run inside the build
sandbox, which also covers the Flatpak path handling. The **Flatpak bundle** GitHub Actions workflow
runs the same script for `v*` tags or on demand and keeps the bundle as a workflow artifact; it never
creates or publishes a release.

### Troubleshooting

* **TarDrop is missing from the launcher after installing the bundle:** log out and back in once, so
  `~/.local/share/flatpak/exports/share` is on `XDG_DATA_DIRS`, and check that
  `flatpak list --user` shows `org.tardrop.TarDrop`.
* **An installed application does not appear in Plasma:** run `kbuildsycoca6` on the host, or log
  out and back in, to rebuild Plasma's application cache.
* **"Launch" is greyed out:** expected in the Flatpak build; see above.
* **Dropping an archive does nothing, or it "could not be opened":** the file manager did not export
  the drag through the file-transfer portal. Use **Browse…**, or allow TarDrop to read your
  downloads: `flatpak override --user --filesystem=xdg-download:ro org.tardrop.TarDrop`.
* **The runtime is missing:** `flatpak install --user flathub org.kde.Platform//6.11`.
* **Inspect or reset permissions:** `flatpak info --show-permissions org.tardrop.TarDrop` and
  `flatpak override --user --reset org.tardrop.TarDrop`.
* **An installed application fails to start:** it runs on the host, so missing libraries are missing
  on your system, exactly as with the native build. Run its launcher's `Exec` path in a terminal to
  see the error.

## Architecture

The project is split into a GUI-free core library and a thin presentation layer. All
security-sensitive decisions live in `tardrop-core`, which links only against QtCore, QtNetwork, and
libarchive, so no widget code can weaken them.

`src/core/`

* `Paths` — bounded user-directory and naming helpers.
* `Security` — archive SHA-256, ELF magic-byte detection, desktop-value validation, link-safe
  file-type checks, and the risk assessment for refused packages.
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
* `Dialogs` — existing-installation, launcher-choice, security-warning (with its risk assessment),
  and update-source dialogs.
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

TarDrop intentionally rejects archives that contain symlinks, hardlinks, special files, or an
`install.sh`, and only installs the rest after showing a risk assessment and getting consent. This
is stricter than ordinary archive tools because the product's job is safely handling untrusted
downloads. Launcher discovery scores root `AppRun`, safe `Exec=` targets from nearby desktop files,
conventional launcher scripts, root executables, and name matches. Dependency, documentation, and
known helper-binary trees are heavily penalized. When the two top candidates are within 10 points,
TarDrop shows a chooser instead of guessing.
