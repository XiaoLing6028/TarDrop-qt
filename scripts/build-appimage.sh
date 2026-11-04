#!/usr/bin/env bash
# Builds the TarDrop AppImage: dist/TarDrop-<version>-x86_64.AppImage plus a .sha256 file and a
# build manifest.
#
# On the host this needs only bash and Docker (or Podman with `DOCKER=podman`). It builds the
# builder image from appimage/Dockerfile and re-runs itself inside it, so the binary is always
# compiled against the same base (Ubuntu 24.04 + KDE neon) regardless of the host distribution.
# Every run uses a fresh build directory inside a throwaway container; the source tree is mounted
# read-only.
#
# Environment overrides (host side):
#   DOCKER       container engine                       (default: docker)
#   OUTPUT_DIR   where the AppImage is written           (default: dist)
#   BASE_IMAGE   the builder's base image, e.g. a registry mirror of the pinned digest
#   BUILDER_IMAGE use this already-built builder image instead of building appimage/Dockerfile
#   SKIP_TESTS=1 do not run the unit tests before packaging
#
# Every downloaded build tool is pinned to a release and verified against the SHA-256 below before
# it is executed. Downloads are cached in .cache/appimage-tools (git-ignored).
set -euo pipefail

readonly BUILDER_MARKER=/etc/tardrop-builder-packages.txt

# name|url|sha256
readonly TOOLS=(
    "linuxdeploy-x86_64.AppImage|https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage|c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
    "linuxdeploy-plugin-qt-x86_64.AppImage|https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage|15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"
    "appimagetool-x86_64.AppImage|https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage|ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
    "runtime-x86_64|https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64|2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
)

log() { printf '\033[1m==> %s\033[0m\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# ------------------------------------------------------------------------------------------------
# Host side: build the builder image and run the packaging step inside it.
# ------------------------------------------------------------------------------------------------
run_on_host() {
    local root docker output cache ca epoch commit
    root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
    docker=${DOCKER:-docker}
    command -v "$docker" >/dev/null || die "$docker is not installed"
    [[ $(uname -m) == x86_64 ]] || die "only x86_64 AppImages are built"

    output=${OUTPUT_DIR:-$root/dist}
    cache=$root/.cache/appimage-tools
    mkdir -p "$output" "$cache"
    output=$(cd "$output" && pwd)

    # apt and curl in the builder trust exactly what the host trusts, which also covers hosts that
    # sit behind a TLS-intercepting proxy. The bundle is a build secret and a read-only run mount.
    ca=${SSL_CERT_FILE:-}
    if [[ -z $ca ]]; then
        for candidate in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt \
                         /etc/ssl/ca-bundle.pem /etc/ssl/cert.pem; do
            [[ -r $candidate ]] && { ca=$candidate; break; }
        done
    fi
    [[ -r $ca ]] || die "no CA bundle found; set SSL_CERT_FILE"

    # Timestamps inside the image come from the last commit, so rebuilding a commit is repeatable.
    epoch=$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)
    commit=$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
    if [[ -n $(git -C "$root" status --porcelain 2>/dev/null) ]]; then
        commit+="-dirty"
    fi

    local proxy_args=()
    for var in HTTPS_PROXY https_proxy NO_PROXY no_proxy; do
        [[ -n ${!var:-} ]] && proxy_args+=(--build-arg "$var=${!var}")
    done
    local base_args=()
    [[ -n ${BASE_IMAGE:-} ]] && base_args+=(--build-arg "BASE_IMAGE=$BASE_IMAGE")

    local image=${BUILDER_IMAGE:-}
    if [[ -z $image ]]; then
        image=tardrop-appimage-builder:local
        log "Building the builder image ($image)"
        DOCKER_BUILDKIT=1 "$docker" build --network host "${proxy_args[@]}" "${base_args[@]}" \
            --secret "id=cacert,src=$ca" -t "$image" "$root/appimage"
    fi

    local env_args=()
    for var in HTTPS_PROXY https_proxy NO_PROXY no_proxy SKIP_TESTS; do
        [[ -n ${!var:-} ]] && env_args+=(-e "$var=${!var}")
    done

    log "Packaging inside the builder"
    "$docker" run --rm --network host \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp/home -e SOURCE_DATE_EPOCH="$epoch" -e TARDROP_COMMIT="$commit" \
        -e SSL_CERT_FILE=/etc/tardrop-ca.crt -e CURL_CA_BUNDLE=/etc/tardrop-ca.crt \
        -e BASE_IMAGE_REF="${BASE_IMAGE:-default}" -e BUILDER_IMAGE_REF="${BUILDER_IMAGE:-}" \
        "${env_args[@]}" \
        -v "$ca:/etc/tardrop-ca.crt:ro" \
        -v "$root:/src:ro" -v "$output:/out" -v "$cache:/cache" \
        "$image" /src/scripts/build-appimage.sh
}

# ------------------------------------------------------------------------------------------------
# Builder side.
# ------------------------------------------------------------------------------------------------

# Downloads a pinned tool into the cache (unless an intact copy is there) and verifies it.
fetch_tool() {
    local name=$1 url=$2 sha=$3 dest=/cache/$1
    if [[ -f $dest ]] && echo "$sha  $dest" | sha256sum --check --status; then
        return
    fi
    log "Downloading $name"
    rm -f "$dest.part"
    curl --fail --location --silent --show-error --retry 4 --proto '=https' --tlsv1.2 \
        -o "$dest.part" "$url"
    echo "$sha  $dest.part" | sha256sum --check --status \
        || { rm -f "$dest.part"; die "checksum mismatch for $name ($url)"; }
    mv "$dest.part" "$dest"
}

# Extracts a verified tool AppImage so it runs without FUSE (containers and CI rarely have it).
extract_tool() {
    local name=$1 dir=$2
    mkdir -p "$dir"
    install -m 755 "/cache/$name" "$dir/$name"
    (cd "$dir" && "./$name" --appimage-extract >/dev/null && rm "$name")
    echo "$dir/squashfs-root/AppRun"
}

run_in_builder() {
    local src=/src build=/tmp/build appdir=/tmp/AppDir tools=/tmp/tools
    mkdir -p "$HOME"
    rm -rf "$build" "$appdir" "$tools"

    # ---- Compile, test, and install into the AppDir ------------------------------------------
    log "Configuring"
    cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
    local version
    version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")
    [[ -n $version ]] || die "could not read the project version"

    log "Building TarDrop $version"
    cmake --build "$build"
    if [[ ${SKIP_TESTS:-0} != 1 ]]; then
        log "Running the unit tests"
        ctest --test-dir "$build" --output-on-failure
    fi
    DESTDIR=$appdir cmake --install "$build"

    # ---- Pinned tooling -------------------------------------------------------------------------
    local entry name url sha
    for entry in "${TOOLS[@]}"; do
        IFS='|' read -r name url sha <<<"$entry"
        fetch_tool "$name" "$url" "$sha"
    done
    local linuxdeploy plugin_qt appimagetool
    linuxdeploy=$(extract_tool linuxdeploy-x86_64.AppImage "$tools/linuxdeploy")
    plugin_qt=$(extract_tool linuxdeploy-plugin-qt-x86_64.AppImage "$tools/plugin-qt")
    appimagetool=$(extract_tool appimagetool-x86_64.AppImage "$tools/appimagetool")
    # linuxdeploy looks its plugins up on PATH by name.
    mkdir -p "$tools/bin"
    ln -s "$plugin_qt" "$tools/bin/linuxdeploy-plugin-qt"
    export PATH=$tools/bin:$PATH

    # ---- Desktop entry and icon ---------------------------------------------------------------
    # AppImage integration tools (Gear Lever, AppImageLauncher, appimaged) install the AppImage's
    # icon under the desktop entry's Icon name, so it needs the application ID rather than the
    # shared theme name `package-x-generic` the native build uses. The artwork is the same Breeze
    # icon (LGPL-3.0), taken from the builder's Breeze icon theme.
    local app_id=org.tardrop.TarDrop icons=$tools/icons
    local desktop=$appdir/usr/share/applications/$app_id.desktop
    mkdir -p "$icons"
    cp /usr/share/icons/breeze/mimetypes/64/package-x-generic.svg "$icons/$app_id.svg"
    rsvg-convert -w 256 -h 256 -o "$icons/$app_id.png" "$icons/$app_id.svg"
    desktop-file-edit --set-icon="$app_id" --set-key=X-AppImage-Version --set-value="$version" \
        "$desktop"

    # ---- Libraries and plugins ----------------------------------------------------------------
    local qt_plugins kf_libexec
    qt_plugins=$(qmake6 -query QT_INSTALL_PLUGINS)
    kf_libexec=/usr/lib/x86_64-linux-gnu/libexec/kf6

    # Plugins that linuxdeploy-plugin-qt does not know about, copied so their dependencies are
    # deployed by the same linuxdeploy run:
    #  * the Plasma platform theme and Breeze style: on Plasma, Qt selects them from the user's
    #    kdeglobals, giving the same look, colour scheme, and KDE file dialog as the native build.
    #    The host's own copies cannot be used; they are built against the host's Qt.
    #  * KIO's file worker and URI filters, which the KDE file dialog lists and resolves paths with.
    #  * KWindowSystem's X11/Wayland backends (window activation when launching applications).
    #  * KIconThemes' icon engine and the KF6 framework-integration plugin.
    #  * Qt Wayland's client plugins, which the Qt plugin does not deploy for Qt 6.11: without the
    #    xdg-shell integration no window can be shown on a Wayland session; the decorations draw
    #    title bars where the compositor does not (GNOME, through the Adwaita plugin).
    local plugin_dirs=(
        wayland-shell-integration/libxdg-shell.so
        wayland-decoration-client/libadwaita.so
        wayland-decoration-client/libbradient.so
        wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
        platformthemes/KDEPlasmaPlatformTheme6.so
        styles/breeze6.so
        kf6/kio/kio_file.so
        kf6/urifilters
        kf6/kwindowsystem
        kf6/FrameworkIntegrationPlugin.so
        kiconthemes6
    )
    local item
    for item in "${plugin_dirs[@]}"; do
        mkdir -p "$appdir/usr/plugins/$(dirname "$item")"
        cp -a "$qt_plugins/$item" "$appdir/usr/plugins/$item"
    done
    # KIO starts out-of-process workers with kioworker, which it looks up relative to its own
    # library: <libdir>/libexec/kf6. linuxdeploy places libraries in usr/lib.
    install -Dm755 "$kf_libexec/kioworker" "$appdir/usr/lib/libexec/kf6/kioworker"

    local deploy_deps=()
    while IFS= read -r -d '' item; do
        deploy_deps+=(--deploy-deps-only "$item")
    done < <(find "$appdir/usr/plugins" "$appdir/usr/lib/libexec" -type f -print0)

    log "Deploying libraries"
    # The Qt plugin deploys the platform (X11, Wayland), image-format, icon-engine, TLS, and input
    # plugins plus qt.conf. Wayland's platform plugin is called libqwayland.so since Qt 6.10; the
    # offscreen platform is kept for headless smoke tests (QT_QPA_PLATFORM=offscreen).
    # OpenSSL 3 is bundled as a matched libssl/libcrypto pair. Hosts cannot be relied on for it:
    # Fedora 45 ships OpenSSL 4 (libssl.so.4) and has OpenSSL 3 only as an optional compat package.
    # Qt's TLS plugin dlopens libssl.so.3 through its own $ORIGIN RUNPATH, so it always gets the
    # bundled copy and never mixes it with a host libcrypto. Certificates still come from the
    # host's trust store, which Qt reads itself.
    QMAKE=$(command -v qmake6) \
    EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so" \
    EXTRA_QT_MODULES="svg" \
    DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1 \
    "$linuxdeploy" --appdir "$appdir" \
        --executable "$appdir/usr/bin/tardrop" \
        --desktop-file "$desktop" \
        --icon-file "$icons/$app_id.svg" --icon-file "$icons/$app_id.png" \
        "${deploy_deps[@]}" \
        --plugin qt

    # Qt only dlopens libssl, so make sure the pair is present even if nothing links it directly.
    local ssl_lib
    for ssl_lib in libssl.so.3 libcrypto.so.3; do
        [[ -e $appdir/usr/lib/$ssl_lib ]] || install -m 644 "/usr/lib/x86_64-linux-gnu/$ssl_lib" "$appdir/usr/lib/"
    done

    # Some plugins the Qt plugin deploys (e.g. the xdg-desktop-portal theme) are left without a
    # search path; they would only work while Qt is already loaded. Point every such ELF at usr/lib.
    local elf rel
    while IFS= read -r -d '' elf; do
        head -c4 "$elf" | grep -q $'^\x7fELF' || continue
        [[ -n $(patchelf --print-rpath "$elf") ]] && continue
        rel=$(realpath --relative-to="$(dirname "$elf")" "$appdir/usr/lib")
        patchelf --set-rpath "\$ORIGIN/$rel" "$elf"
    done < <(find "$appdir/usr/plugins" "$appdir/usr/lib" "$appdir/usr/bin" -type f -print0)

    # kioworker is a Qt program of its own and reads qt.conf from its own directory.
    sed 's#^Prefix = .*#Prefix = ../../../#' "$appdir/usr/bin/qt.conf" \
        > "$appdir/usr/lib/libexec/kf6/qt.conf"

    # The Qt plugin's AppRun hooks export variables (e.g. QT_QPA_PLATFORMTHEME on GNOME) that every
    # program TarDrop starts would inherit, so TarDrop uses its own AppRun without hooks.
    rm -rf "$appdir/apprun-hooks"
    rm -f "$appdir/AppRun" "$appdir/AppRun.wrapped"
    install -m 755 "$src/appimage/AppRun" "$appdir/AppRun"
    # Not used by TarDrop (removed if the Qt plugin deployed them): the GTK 3 theme would need GTK 3
    # built for this Qt, and the virtual
    # keyboard input method only adds Qt Quick payload.
    rm -f "$appdir/usr/plugins/platformthemes/libqgtk3.so" \
          "$appdir/usr/plugins/platforminputcontexts/libqtvirtualkeyboardplugin.so" \
          "$appdir"/usr/lib/libQt6VirtualKeyboard.so*
    # Desktop entry and AppStream data are validated here, offline, so the build does not depend on
    # appimagetool's networked URL checks (it also only looks for the older .appdata.xml name).
    desktop-file-validate "$appdir/$app_id.desktop"
    appstreamcli validate-tree --no-net "$appdir"
    ln -sf "usr/share/icons/hicolor/256x256/apps/$app_id.png" "$appdir/.DirIcon"

    # ---- Provenance ---------------------------------------------------------------------------
    local manifest=$appdir/usr/share/tardrop/appimage-build-manifest.txt
    mkdir -p "$(dirname "$manifest")"
    {
        echo "TarDrop $version AppImage (x86_64)"
        echo "source commit: ${TARDROP_COMMIT:-unknown}"
        echo "SOURCE_DATE_EPOCH: ${SOURCE_DATE_EPOCH:-unset}"
        if [[ -n ${BUILDER_IMAGE_REF:-} ]]; then
            echo "builder image: $BUILDER_IMAGE_REF (prebuilt, BUILDER_IMAGE)"
        else
            echo "builder base image: ${BASE_IMAGE_REF:-default} (digest pinned in appimage/Dockerfile)"
        fi
        echo
        echo "== tools (sha256 verified)"
        for entry in "${TOOLS[@]}"; do
            IFS='|' read -r name url sha <<<"$entry"
            echo "$sha  $url"
        done
        echo
        echo "== builder packages"
        cat "$BUILDER_MARKER"
    } > "$manifest"

    # ---- AppImage -----------------------------------------------------------------------------
    local output=/out/TarDrop-$version-x86_64.AppImage
    rm -f "$output" "$output.sha256"
    log "Creating $(basename "$output")"
    # Normalise timestamps so identical inputs give identical squashfs images.
    find "$appdir" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-0}" {} +
    ARCH=x86_64 VERSION=$version "$appimagetool" --no-appstream \
        --runtime-file /cache/runtime-x86_64 --comp zstd \
        "$appdir" "$output"
    chmod 755 "$output"
    (cd /out && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")
    cp "$manifest" "/out/TarDrop-$version-x86_64.manifest.txt"

    log "Done"
    ls -l "$output"
    cat "$output.sha256"
}

if [[ -f $BUILDER_MARKER && -d /src && -d /out ]]; then
    run_in_builder
else
    run_on_host
fi
