#!/usr/bin/env bash
# Builds TarDrop with flatpak-builder and exports a single-file bundle, dist/TarDrop.flatpak.
#
# Requirements: flatpak and flatpak-builder. The KDE runtime and SDK are not on any TarDrop server;
# they are fetched from Flathub's runtime repository, which is where KDE publishes them. Nothing is
# uploaded anywhere.
#
# Environment overrides:
#   OUTPUT     bundle path                    (default: dist/TarDrop.flatpak)
#   BUILD_DIR  flatpak-builder build directory (default: .flatpak-builder/build)
#   REPO_DIR   local OSTree repository         (default: .flatpak-builder/repo)
#   FLATPAK_BUILDER_ARGS  extra flatpak-builder arguments, e.g. --disable-rofiles-fuse
set -euo pipefail

APP_ID=org.tardrop.TarDrop
BRANCH=stable
RUNTIME_REPO=https://dl.flathub.org/repo/flathub.flatpakrepo

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

manifest=flatpak/$APP_ID.yml
output=${OUTPUT:-dist/TarDrop.flatpak}
build_dir=${BUILD_DIR:-.flatpak-builder/build}
repo_dir=${REPO_DIR:-.flatpak-builder/repo}

for tool in flatpak flatpak-builder; do
    if ! command -v "$tool" >/dev/null; then
        echo "error: $tool is not installed" >&2
        exit 1
    fi
done

# Only used to download org.kde.Platform/org.kde.Sdk; TarDrop itself is never published there.
flatpak remote-add --user --if-not-exists flathub "$RUNTIME_REPO"

# shellcheck disable=SC2086 # FLATPAK_BUILDER_ARGS is a list of separate arguments.
flatpak-builder --user --install-deps-from=flathub --force-clean --ccache \
    ${FLATPAK_BUILDER_ARGS:-} \
    --default-branch="$BRANCH" --repo="$repo_dir" "$build_dir" "$manifest"

mkdir -p "$(dirname "$output")"
# --runtime-repo lets `flatpak install` offer to fetch the KDE runtime when it is missing.
flatpak build-bundle --runtime-repo="$RUNTIME_REPO" "$repo_dir" "$output" "$APP_ID" "$BRANCH"

(cd "$(dirname "$output")" && sha256sum "$(basename "$output")" > "$(basename "$output").sha256")
echo "Bundle: $output"
cat "$output.sha256"
