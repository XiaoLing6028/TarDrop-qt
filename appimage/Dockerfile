# Builder image for the TarDrop AppImage. scripts/build-appimage.sh builds and uses it; it is not
# meant to be published.
#
# Why Ubuntu 24.04 + KDE neon: an AppImage runs on any distribution whose glibc and libstdc++ are at
# least as new as the build machine's, so the base decides compatibility. Noble (glibc 2.39,
# libstdc++ from GCC 14) is the oldest base that has KDE Frameworks 6 and Qt >= 6.6 available, which
# KDE neon provides. Debian 13 would be fully snapshot-pinnable but raises the floor to glibc 2.41,
# which drops Ubuntu 24.04 LTS.
#
# Reproducibility: the base image is pinned by digest and the neon signing key is vendored next to
# this file. neon is a rolling repository with no snapshot service, so package versions are not
# pinnable; the build records every installed package version in the AppImage's build manifest.

# Same manifest-list digest on Docker Hub and its mirrors (e.g. mirror.gcr.io/library/ubuntu).
ARG BASE_IMAGE=docker.io/library/ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8

# KDE neon archive key, fingerprint 444D ABCF 3667 D028 3F89 4EDD E6D4 7362 5575 1E5D.
COPY neon-archive-key.asc /etc/apt/keyrings/neon-archive-key.asc

# The CA bundle is passed as a build secret (the host's trust store) so apt can use HTTPS before the
# image has ca-certificates, including behind a TLS-intercepting proxy. It is never stored in a layer.
RUN --mount=type=secret,id=cacert,required=true,mode=0444 \
    set -eux; \
    printf 'Acquire::https::CAInfo "/run/secrets/cacert";\nAcquire::Retries "5";\n' > /etc/apt/apt.conf.d/99build-ca; \
    sed -i 's#http://#https://#g' /etc/apt/sources.list.d/ubuntu.sources; \
    echo 'deb [signed-by=/etc/apt/keyrings/neon-archive-key.asc] https://archive.neon.kde.org/user noble main' \
        > /etc/apt/sources.list.d/neon.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        ca-certificates curl file binutils patchelf desktop-file-utils appstream librsvg2-bin \
        cmake ninja-build pkgconf g++-14 \
        libarchive-dev \
        qt6-base-dev qt6-base-dev-tools qt6-wayland qt6-svg \
        kf6-kcoreaddons-dev kf6-ki18n-dev kf6-kwidgetsaddons-dev kf6-kconfig-dev \
        kf6-kconfigwidgets-dev kf6-kcolorscheme-dev kf6-kiconthemes-dev kf6-knotifications-dev \
        kf6-kcrash-dev kf6-kio-dev kf6-kservice-dev \
        kf6-breeze-icon-theme plasma-integration kde-style-breeze \
        python3 openssl procps; \
    rm -f /etc/apt/apt.conf.d/99build-ca; \
    rm -rf /var/lib/apt/lists/*; \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100; \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100; \
    update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100; \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 100; \
    dpkg-query -W -f='${Package}\t${Version}\n' > /etc/tardrop-builder-packages.txt

ENV CC=gcc-14 CXX=g++-14
# python3, openssl, and procps are only for scripts/test-appimage.sh, which CI also runs against a
# native build in this image to compare it with the AppImage.
