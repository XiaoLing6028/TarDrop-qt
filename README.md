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

## AppImage

TarDrop is also available as a single-file AppImage, `TarDrop-<version>-x86_64.AppImage`, attached
to [GitHub Releases](https://github.com/XiaoLing6028/TarDrop-qt/releases). It needs no installation,
no root, and no runtime download: Qt 6, the KDE Frameworks, and libarchive are inside the file.

### Run it

```bash
sha256sum -c TarDrop-0.2.0-x86_64.AppImage.sha256   # check it against the .sha256 from the release
chmod +x TarDrop-0.2.0-x86_64.AppImage
./TarDrop-0.2.0-x86_64.AppImage                      # archives can be passed as arguments too
```

To get TarDrop into your application launcher (and "Open With → TarDrop" for archives), integrate the
AppImage with [Gear Lever](https://flathub.org/apps/it.mijorus.gearlever) or
[AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher); the AppImage carries its desktop
entry (`org.tardrop.TarDrop`), MIME types, and icon. To update TarDrop itself, replace the file with
the newer release.

The AppImage runs on the host like the native build, not in a sandbox: launchers, icons, and records
go to the same `~/.local/share` locations, existing records are shared with a native install, and
**Launch** starts installed applications directly.

### Compatibility

* x86_64 only.
* glibc 2.38 or newer and libstdc++ from GCC 13.2 or newer (the newest symbols the bundle uses are
  `GLIBC_2.38` and `GLIBCXX_3.4.32`). It is built on Ubuntu 24.04.
* Tested: Ubuntu 24.04, Debian 13, Fedora 45, and Arch Linux (all functional checks, see
  [Testing the AppImage](#testing-the-appimage)), plus X11 and Wayland sessions under Xvfb and Weston.
  Other distributions that meet the glibc requirement (openSUSE Tumbleweed, Fedora 39+, Ubuntu
  24.10+) are expected to work but have not been tested.
* The host provides only what every desktop installation has and what must match the host's
  drivers and session: glibc, libstdc++/libgcc_s, the GL dispatch libraries (libglvnd: `libEGL`,
  `libGLX`, `libOpenGL`), `libX11`, `libX11-xcb`, `libxcb`, `libSM`, `libICE`, `libwayland-client`,
  fontconfig, FreeType, HarfBuzz, zlib, GMP, `libcom_err`, and `libgpg-error`.
* FUSE: the AppImage mounts itself with `fusermount3`/`fusermount` (package `fuse3`, preinstalled
  on most desktops). Without it, the runtime says so and falls back to extracting itself to a
  temporary directory, which only makes start-up slower. `--appimage-extract-and-run` forces that.

### What is bundled, and why

* **Qt 6.11 and KDE Frameworks 6.31** (from KDE neon), including the Wayland, X11, and offscreen
  platform plugins, the SVG and ICO image plugins, and the Breeze icon theme.
* **The Plasma platform theme and Breeze style.** On Plasma, Qt selects them from your `kdeglobals`,
  so TarDrop gets your colour scheme, fonts, and the KDE file dialog exactly like the native build.
  The host's own copies cannot be loaded: they are built against the host's Qt. KIO's `file` worker
  and URI filters are included for that dialog.
* **libarchive** and its compression libraries, so archive handling is identical everywhere.
* **OpenSSL 3**, used by the update providers. Distributions cannot be relied on for it any more:
  Fedora 45 ships OpenSSL 4 and has OpenSSL 3 only as an optional compat package. Certificates still
  come from the host's trust store.

The AppImage's `AppRun` sets no `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `XDG_DATA_DIRS`, or platform
theme (libraries are found through `$ORIGIN` RPATHs and plugins through `qt.conf`), and it removes the
`APPIMAGE`, `APPDIR`, `ARGV0`, and `OWD` variables the runtime sets. Programs TarDrop starts (installed
applications, the file manager, `update-desktop-database`, `kbuildsycoca6`) therefore see exactly the
environment they would see from the native build; an application started with `APPIMAGE` set would
otherwise believe it *is* TarDrop's AppImage (Electron's auto-updater would try to replace it).

### Known limitations

* Security fixes in the bundled libraries (libarchive, OpenSSL, Qt) arrive with a new TarDrop
  AppImage, not with your distribution's updates. Rebuild or download a newer release.
* Outside Plasma, Qt's own Fusion style and file dialog are used, as with a native build that has
  no platform theme. Host Qt plugins (other styles, platform themes, the fcitx5 input method) are
  not loaded; the IBus and compose input methods are bundled.
* KDE's crash dialog (DrKonqi) is not bundled, so a crash is not reported through it.
* The bundled Breeze icons are used on every desktop. On GNOME Wayland, Qt draws the window's title
  bar itself and takes its buttons from the host's Adwaita icon theme.

### Building the AppImage

```bash
./scripts/build-appimage.sh    # -> dist/TarDrop-<version>-x86_64.AppImage, .sha256, .manifest.txt
```

The host needs only bash, an x86_64 machine, Docker with BuildKit, and network access. The script
builds a builder image from `appimage/Dockerfile` (Ubuntu 24.04, pinned by digest, plus the KDE neon
repository, whose signing key is vendored in `appimage/`), then re-runs itself in a throwaway
container with the source tree mounted read-only. Inside, it configures a fresh build directory, runs
the unit tests, installs into an AppDir, and packages it with pinned releases of linuxdeploy,
linuxdeploy-plugin-qt, appimagetool, and the AppImage type 2 runtime. Each tool's SHA-256 is pinned in
the script and verified before it runs; downloads are cached in `.cache/appimage-tools/`. Nothing is
uploaded anywhere.

| Variable | Effect |
| --- | --- |
| `OUTPUT_DIR` | Where the AppImage is written (default `dist/`). |
| `BASE_IMAGE` | Another source for the pinned base image, e.g. a registry mirror of the same digest. |
| `BUILDER_IMAGE` | Use an already-built builder image instead of building `appimage/Dockerfile`. |
| `SKIP_TESTS=1` | Do not run the unit tests before packaging. |
| `DOCKER` | Container engine (default `docker`; Podman has not been tested). |

Reproducibility: the base image, signing key, and every tool are pinned, and file timestamps come
from the last commit (`SOURCE_DATE_EPOCH`). KDE neon is a rolling repository without snapshots, so
its package versions cannot be pinned; each build records every package version it used in
`dist/TarDrop-<version>-x86_64.manifest.txt` and inside the AppImage at
`usr/share/tardrop/appimage-build-manifest.txt`.

The **AppImage** GitHub Actions workflow runs the same script for pull requests, `main`, `v*` tags, or
on demand, runs the tests below on the runner and on Fedora, Arch, and Debian containers, and keeps
the AppImage as a workflow artifact. It never creates or publishes a release.

### Testing the AppImage

```bash
./scripts/test-appimage.sh dist/TarDrop-0.2.0-x86_64.AppImage
```

This checks the file format and architecture, the desktop entry, icons, bundled plugins and
libraries, that every RPATH is `$ORIGIN`-relative and every dependency resolves on the machine, and
that it runs after being moved to a path with spaces and non-ASCII characters. It then runs real
installs into throwaway home directories through the command line: tar.gz, tar.xz, and zip installs
(setuid bits stripped), traversal and absolute-path archives (refused, nothing written), symlink and
`install.sh` archives (held at the security prompt, the script never run), the environment the
programs TarDrop starts receive, and an HTTPS update check against a local server whose certificate
must be rejected until it is trusted. It needs python3 and openssl, and never touches your home.

* `--install-test-ca` adds the test CA to the system trust store for the trusted half of the HTTPS
  check (root only; removed again on exit). Qt reads only the system store, so this is meant for
  disposable containers and CI.
* `--functional-only --exe build/tardrop --results native.txt` runs the same scenarios against a
  native build; the results of both runs must be identical, which CI checks with `diff`.

### Troubleshooting

* **"No suitable fusermount binary found":** install `fuse3`, or run with
  `--appimage-extract-and-run`; the AppImage also falls back to that by itself.
* **Nothing happens / it exits at once:** start it from a terminal with
  `QT_FORCE_STDERR_LOGGING=1`; Qt otherwise sends its messages to the journal. `QT_DEBUG_PLUGINS=1`
  shows which plugins load.
* **An installed application does not appear in Plasma:** run `kbuildsycoca6`, or log out and back
  in, to rebuild Plasma's application cache.
* **An installed application fails to start:** it runs on the host with the host's libraries, exactly
  as with the native build. Run its launcher's `Exec` path in a terminal to see the error.

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
