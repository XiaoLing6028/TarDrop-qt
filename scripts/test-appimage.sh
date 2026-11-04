#!/usr/bin/env bash
# Verifies a TarDrop AppImage: file format, contents, library resolution on this machine, relocation,
# startup, and real installs, including the security-sensitive refusals. The functional scenarios
# can also run against a native build, so the two can be compared line by line.
#
#   scripts/test-appimage.sh dist/TarDrop-<version>-x86_64.AppImage
#   scripts/test-appimage.sh --functional-only --exe build/tardrop --results native.txt
#
# Options:
#   --functional-only   skip the format/content checks (needed for a native binary)
#   --exe <path>        program to run in the functional scenarios (default: the AppImage)
#   --results <file>    also write the normalised scenario results to <file>
#   --install-test-ca   add a throwaway test CA to the system trust store (root only; it is removed
#                       again on exit) so the trusted half of the HTTPS check can run. Qt reads only
#                       the system store, not SSL_CERT_FILE. Meant for disposable containers and CI.
#
# Needs bash, coreutils, python3 (fixtures and a local HTTPS server), and openssl (its test CA);
# readelf/ldd for the library checks. Without FUSE the AppImage is run with
# APPIMAGE_EXTRACT_AND_RUN=1. Every scenario uses a throwaway HOME; nothing in the real home is
# touched. Exit status is non-zero if any check fails.
set -uo pipefail

appimage=""
exe=""
functional_only=0
results_file=""
install_test_ca=0
while (($#)); do
    case $1 in
        --functional-only) functional_only=1 ;;
        --exe) exe=$2; shift ;;
        --results) results_file=$2; shift ;;
        --install-test-ca) install_test_ca=1 ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) appimage=$1 ;;
    esac
    shift
done
if ((!functional_only)) && [[ -z $appimage ]]; then
    echo "usage: $0 <AppImage> | --functional-only --exe <binary>" >&2
    exit 2
fi
[[ -n $appimage ]] && appimage=$(readlink -f "$appimage")
[[ -z $exe ]] && exe=$appimage
exe=$(readlink -f "$exe")

work=$(mktemp -d "${TMPDIR:-/tmp}/tardrop-test.XXXXXX")
pids=()
ca_installed=""
cleanup() {
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null; done
    wait 2>/dev/null
    if [[ -n $ca_installed ]]; then
        rm -f "$ca_installed"
        { update-ca-certificates || update-ca-trust extract || trust extract-compat; } >/dev/null 2>&1
    fi
    rm -rf "$work"
}
trap cleanup EXIT

failures=0
pass() { printf '  \033[32mPASS\033[0m %s\n' "$*"; }
fail() { printf '  \033[31mFAIL\033[0m %s\n' "$*"; failures=$((failures + 1)); }
check() { local what=$1; shift; if "$@" >/dev/null 2>&1; then pass "$what"; else fail "$what"; fi; }
section() { printf '\n\033[1m%s\033[0m\n' "$*"; }
results=()
record() { results+=("$*"); printf '  result: %s\n' "$*"; }

# Without FUSE (most containers), the runtime extracts itself to a temporary directory instead.
# (The runtime falls back to that by itself, but prints an error on stdout first.)
if [[ -n $appimage && -z ${APPIMAGE_EXTRACT_AND_RUN:-} ]] \
   && [[ $(QT_QPA_PLATFORM=offscreen timeout 60 "$appimage" --version 2>&1) == *fusermount* ]]; then
    export APPIMAGE_EXTRACT_AND_RUN=1
    echo "note: the AppImage cannot mount itself here (no FUSE); using APPIMAGE_EXTRACT_AND_RUN=1"
fi

# ==================================================================================================
# Static checks
# ==================================================================================================
if ((!functional_only)); then
    section "Artifact"
    bytes() { od -An -tx1 -j "$2" -N "$3" "$1" | tr -d ' \n'; }
    check "ELF magic" test "$(bytes "$appimage" 0 4)" = 7f454c46
    check "64-bit ELF" test "$(bytes "$appimage" 4 1)" = 02
    check "x86-64 machine type" test "$(bytes "$appimage" 18 2)" = 3e00
    check "AppImage type 2 magic (AI\\x02)" test "$(bytes "$appimage" 8 3)" = 414902
    check "executable permission" test -x "$appimage"
    check "file name carries version and architecture" \
        bash -c '[[ $(basename "$1") =~ ^TarDrop-[0-9]+\.[0-9]+\.[0-9]+.*-x86_64\.AppImage$ ]]' _ "$appimage"
    if [[ -f $appimage.sha256 ]]; then
        check "checksum file matches" bash -c 'cd "$(dirname "$1")" && sha256sum -c --status "$(basename "$1").sha256"' _ "$appimage"
    else
        fail "checksum file $appimage.sha256 missing"
    fi
    printf '  size: %s bytes, sha256: %s\n' "$(stat -c %s "$appimage")" "$(sha256sum "$appimage" | cut -d' ' -f1)"

    section "Contents"
    (cd "$work" && "$appimage" --appimage-extract >/dev/null 2>&1)
    root=$work/squashfs-root
    check "extracts with --appimage-extract" test -d "$root/usr"
    check "AppRun is executable" test -x "$root/AppRun"
    check "AppRun strips the runtime variables" grep -q '^unset APPDIR APPIMAGE ARGV0 OWD' "$root/AppRun"
    check "AppRun sets no library or plugin path" \
        bash -c '! grep -E "^[^#]*(LD_LIBRARY_PATH|QT_PLUGIN_PATH|XDG_DATA_DIRS|QT_QPA_PLATFORMTHEME)" "$1"' _ "$root/AppRun"
    desktop=$root/org.tardrop.TarDrop.desktop
    check "desktop entry at the root" test -f "$desktop"
    check "desktop entry Exec=tardrop %F" grep -qx 'Exec=tardrop %F' "$desktop"
    check "desktop entry Icon=org.tardrop.TarDrop" grep -qx 'Icon=org.tardrop.TarDrop' "$desktop"
    check "desktop entry keeps the archive MIME types" grep -q '^MimeType=.*application/zip' "$desktop"
    if command -v desktop-file-validate >/dev/null; then
        check "desktop-file-validate" desktop-file-validate "$desktop"
    fi
    check "root icon (SVG)" bash -c 'head -c 512 "$1" | grep -q "<svg"' _ "$root/org.tardrop.TarDrop.svg"
    check ".DirIcon is a 256x256 PNG" bash -c '
        f=$(readlink -f "$1"); [[ $(od -An -tx1 -N8 "$f" | tr -d " \n") == 89504e470d0a1a0a ]] &&
        [[ $(od -An -tx1 -j16 -N8 "$f" | tr -d " \n") == 0000010000000100 ]]' _ "$root/.DirIcon"
    check "AppStream metainfo" test -f "$root/usr/share/metainfo/org.tardrop.TarDrop.metainfo.xml"
    check "build manifest" test -f "$root/usr/share/tardrop/appimage-build-manifest.txt"
    check "qt.conf next to the binary" grep -qx 'Plugins = plugins' "$root/usr/bin/qt.conf"
    check "kioworker and its qt.conf" test -x "$root/usr/lib/libexec/kf6/kioworker" -a -f "$root/usr/lib/libexec/kf6/qt.conf"
    for plugin in platforms/libqxcb.so platforms/libqwayland.so platforms/libqoffscreen.so \
                  wayland-shell-integration/libxdg-shell.so wayland-decoration-client/libadwaita.so \
                  platformthemes/KDEPlasmaPlatformTheme6.so styles/breeze6.so \
                  imageformats/libqsvg.so imageformats/libqico.so iconengines/libqsvgicon.so \
                  tls/libqopensslbackend.so platforminputcontexts/libcomposeplatforminputcontextplugin.so \
                  kf6/kio/kio_file.so kf6/kwindowsystem/KF6WindowSystemX11Plugin.so; do
        check "plugin $plugin" test -f "$root/usr/plugins/$plugin"
    done
    check "Breeze icons bundled (libKF6BreezeIcons)" test -f "$root/usr/lib/libKF6BreezeIcons.so.6"
    check "libarchive bundled" test -f "$root/usr/lib/libarchive.so.13"
    check "OpenSSL 3 bundled as a matched pair" test -f "$root/usr/lib/libssl.so.3" -a -f "$root/usr/lib/libcrypto.so.3"
    check "glibc / libstdc++ left to the host" bash -c '! ls "$1"/usr/lib/libc.so.6 "$1"/usr/lib/libstdc++.so.6 2>/dev/null' _ "$root"

    section "Relocatability and libraries"
    mapfile -t elves < <(find "$root" -type f -exec sh -c 'head -c4 "$1" | grep -q "^.ELF"' _ {} \; -print)
    printf '  %d ELF files\n' "${#elves[@]}"
    if command -v readelf >/dev/null; then
        bad_rpath=()
        for f in "${elves[@]}"; do
            rp=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*(R\(UN\)\{0,1\}PATH).*\[\(.*\)\]/\2/p')
            # Every search path must be relative to the file itself; never a build-machine path.
            for entry in ${rp//:/ }; do
                [[ $entry == '$ORIGIN'* ]] || bad_rpath+=("${f#"$root"/}: $entry")
            done
        done
        if ((${#bad_rpath[@]} == 0)); then pass "all RPATH/RUNPATH entries are \$ORIGIN-relative"
        else fail "absolute RPATH entries: ${bad_rpath[*]}"; fi
    fi
    if grep -rIl -e '/tmp/AppDir' -e '/tmp/build' "$root" >/dev/null 2>&1; then
        fail "build-machine paths in text files: $(grep -rIl -e /tmp/AppDir -e /tmp/build "$root" | tr '\n' ' ')"
    else
        pass "no build-machine paths in text files"
    fi
    if command -v ldd >/dev/null; then
        missing=()
        for f in "${elves[@]}"; do
            while read -r line; do
                [[ $line == *"not found"* ]] && missing+=("${f#"$root"/}: ${line%% *}")
            done < <(env -u LD_LIBRARY_PATH ldd "$f" 2>/dev/null)
        done
        if ((${#missing[@]} == 0)); then pass "every dependency resolves on this host"
        else fail "unresolved: $(printf '%s\n' "${missing[@]}" | sort -u | tr '\n' ' ')"; fi
        # What the host must provide: every DT_NEEDED entry that is not shipped in usr/lib.
        if command -v readelf >/dev/null; then
            host_libs=$(for f in "${elves[@]}"; do
                    readelf -d "$f" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
                done | sort -u | while read -r lib; do
                    [[ -e $root/usr/lib/$lib ]] || echo "$lib"
                done | tr '\n' ' ')
            printf '  host-provided: %s\n' "$host_libs"
        fi
        glibc_max=$(for f in "${elves[@]}"; do objdump -T "$f" 2>/dev/null; done \
            | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
        [[ -n $glibc_max ]] && printf '  newest glibc symbol required: %s\n' "$glibc_max"
        cxx_max=$(for f in "${elves[@]}"; do objdump -T "$f" 2>/dev/null; done \
            | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1)
        [[ -n $cxx_max ]] && printf '  newest libstdc++ symbol required: %s\n' "$cxx_max"
    fi

    section "Startup"
    # QApplication needs a display even for --version; offscreen stands in for one. Qt logs to the
    # journal when stderr is not a terminal, so force stderr to see why anything fails.
    export QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1
    version_out=$("$appimage" --version 2>/dev/null)
    check "--version prints the version ($version_out)" bash -c '[[ $1 =~ ^tardrop\ [0-9]+\.[0-9]+ ]]' _ "$version_out"
    check "--help lists the archive argument" bash -c '"$1" --help 2>/dev/null | grep -q "archive"' _ "$appimage"
    reloc_dir="$work/moved dir/ü"
    mkdir -p "$reloc_dir"
    cp "$appimage" "$reloc_dir/TarDrop.AppImage"
    check "runs after moving to a path with spaces and non-ASCII" \
        bash -c 'cd / && "$1" --version | grep -q "^tardrop"' _ "$reloc_dir/TarDrop.AppImage"
    mv "$root" "$work/elsewhere"
    check "extracted AppDir runs from another location" \
        bash -c 'env -u APPDIR "$1/AppRun" --version | grep -q "^tardrop"' _ "$work/elsewhere"
    rm -rf "$work/elsewhere" "$work/moved dir"
fi

# ==================================================================================================
# Functional scenarios: real installs into a throwaway home, driven through the command line.
# ==================================================================================================
section "Functional scenarios ($( [[ $exe == "$appimage" ]] && echo AppImage || echo "$exe" ))"
command -v python3 >/dev/null || { fail "python3 is needed for the fixtures"; exit 1; }

fixtures=$work/fixtures
mkdir -p "$fixtures"
elf=$(readlink -f "$(type -P true)")
python3 - "$fixtures" "$elf" <<'PY'
import io, os, sys, tarfile, zipfile
out, elf = sys.argv[1], open(sys.argv[2], "rb").read()

def add(tar, name, data=b"", mode=0o644, kind=tarfile.REGTYPE, link=""):
    info = tarfile.TarInfo(name)
    info.mode, info.type, info.linkname, info.mtime = mode, kind, link, 1700000000
    if kind == tarfile.REGTYPE:
        info.size = len(data)
        tar.addfile(info, io.BytesIO(data))
    else:
        tar.addfile(info)

def make(name, members, mode="w:gz"):
    with tarfile.open(os.path.join(out, name), mode) as tar:
        for member in members:
            add(tar, *member)

def app(root):
    binary = root.split("-")[0]
    return [(f"{root}/", b"", 0o755, tarfile.DIRTYPE), (f"{root}/{binary}", elf, 0o755),
            (f"{root}/README.txt", b"hello\n")]
# The setuid helper sits in a dependency directory, so it is not a competing launcher candidate.
make("hello-1.2.3.tar.gz", app("hello-1.2.3") + [("hello-1.2.3/lib/suid-helper", elf, 0o4755)])
make("xzapp-2.0.tar.xz", app("xzapp-2.0"), "w:xz")
make("traversal-1.0.tar.gz", app("traversal-1.0") + [("traversal-1.0/../../escaped.txt", b"x")])
make("absolute-1.0.tar.gz", app("absolute-1.0") + [(f"{out}/absolute-escape.txt", b"x")])
make("links-1.0.tar.gz", app("links-1.0") + [("links-1.0/outside", b"", 0o777, tarfile.SYMTYPE, "/etc/passwd")])
make("script-1.0.tar.gz", app("script-1.0") + [
    ("script-1.0/install.sh", f"#!/bin/sh\ntouch '{out}/install-sh-ran'\n".encode(), 0o755)])
with zipfile.ZipFile(os.path.join(out, "zipapp-3.1.zip"), "w") as z:
    info = zipfile.ZipInfo("zipapp-3.1/zipapp", (2023, 11, 14, 0, 0, 0))
    info.external_attr = (0o104755 << 16)  # regular file, setuid
    z.writestr(info, elf)
    z.writestr(zipfile.ZipInfo("zipapp-3.1/README.txt", (2023, 11, 14, 0, 0, 0)), "zip\n")
PY

# A stand-in for update-desktop-database that records the environment TarDrop gives to the programs
# it starts; installed applications and the file manager inherit exactly the same environment.
spy=$work/spy-bin
mkdir -p "$spy"
cat > "$spy/update-desktop-database" <<EOF
#!/bin/sh
env > "$work/child-env.\$\$"
EOF
chmod +x "$spy/update-desktop-database"

# Runs TarDrop with a fresh home on one archive (or none), until `until` succeeds or `seconds` pass.
# Leaves the process running when `keep` is set (its PID in $last_pid).
new_home() {
    home=$work/home-$1
    rm -rf "$home"
    mkdir -p "$home/.config" "$home/.local/share" "$home/.cache" "$home/runtime"
    chmod 700 "$home/runtime"
}
start_tardrop() {
    env -i HOME="$home" PATH="$spy:/usr/bin:/bin" LANG=C.UTF-8 \
        XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" \
        XDG_CACHE_HOME="$home/.cache" XDG_RUNTIME_DIR="$home/runtime" \
        QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ${APPIMAGE_EXTRACT_AND_RUN:+APPIMAGE_EXTRACT_AND_RUN=1} \
        TMPDIR="$work" "$exe" "$@" >"$home/stdout.log" 2>"$home/stderr.log" &
    last_pid=$!
    pids+=("$last_pid")
}
wait_for() {
    local seconds=$1; shift
    for ((i = 0; i < seconds * 4; i++)); do
        "$@" >/dev/null 2>&1 && return 0
        sleep 0.25
    done
    return 1
}
stop_tardrop() {
    # The AppImage runtime stays the parent of TarDrop; stopping TarDrop ends both.
    local child
    for child in $(pgrep -P "$last_pid" 2>/dev/null) "$last_pid"; do kill "$child" 2>/dev/null; done
    wait "$last_pid" 2>/dev/null
}
alive() { kill -0 "$last_pid" 2>/dev/null; }
records() { echo "$home/.local/share/tardrop/installed-apps.json"; }
has_records() { [[ $(record_count) == "$1" ]]; }
record_count() { python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$(records)" 2>/dev/null || echo 0; }
# What a scenario left in the home, normalised so a native and an AppImage run compare equal.
tree_of() {
    (cd "$home" && find Applications .local/share/applications .local/share/icons/TarDrop -mindepth 1 \
        \( -type f -o -type l -o -type d \) -printf '%p %m %y\n' 2>/dev/null | grep -v mimeinfo.cache | sort | tr '\n' ';')
}

# --- successful installs --------------------------------------------------------------------------
for archive in hello-1.2.3.tar.gz xzapp-2.0.tar.xz zipapp-3.1.zip; do
    name=${archive%%-*}
    new_home "$name"
    start_tardrop "$fixtures/$archive"
    if wait_for 60 has_records 1; then
        pass "$archive installed"
    else
        fail "$archive was not installed (stderr: $(tail -3 "$home/stderr.log" | tr '\n' ' '))"
    fi
    stop_tardrop
    dir=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[0]["install_path"])' "$(records)" 2>/dev/null)
    launcher=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[0]["desktop_file_path"])' "$(records)" 2>/dev/null)
    check "$archive: installed below ~/Applications" bash -c '[[ $1 == "$2"/Applications/* && -d $1 ]]' _ "$dir" "$home"
    check "$archive: launcher in XDG_DATA_HOME/applications" bash -c '[[ $1 == "$2"/.local/share/applications/*.desktop && -f $1 ]]' _ "$launcher" "$home"
    check "$archive: launcher Exec points into the install" grep -q "^Exec=.*$dir" "$launcher"
    check "$archive: no setuid/setgid bits survive" bash -c '[[ -z $(find "$1" -perm /6000) ]]' _ "$dir"
    record "$archive installed version=$(python3 -c 'import json,sys; r=json.load(open(sys.argv[1]))[0]; print(r["version"], r["update_provider"], r["archive_filename"])' "$(records)" 2>/dev/null) tree=$(tree_of)"
done

# --- refusals that can never be overridden ----------------------------------------------------------
for archive in traversal-1.0.tar.gz absolute-1.0.tar.gz; do
    new_home "${archive%%-*}"
    start_tardrop "$fixtures/$archive"
    sleep 8
    check "$archive: still running after the refusal" alive
    check "$archive: nothing recorded" test "$(record_count)" = 0
    check "$archive: nothing written outside the staging area" \
        bash -c '! ls "$1"/escaped.txt "$1"/../escaped.txt "$2"/absolute-escape.txt 2>/dev/null' _ "$home/Applications" "$fixtures"
    stop_tardrop
    record "$archive refused records=$(record_count) tree=$(tree_of)"
done

# --- refusals that ask first (links, install.sh): the security prompt blocks the queue ----------------
for archive in links-1.0.tar.gz script-1.0.tar.gz; do
    new_home "${archive%%-*}"
    start_tardrop "$fixtures/$archive"
    sleep 8
    check "$archive: waiting on the security prompt (still running)" alive
    check "$archive: nothing installed before the user answers" test "$(record_count)" = 0
    check "$archive: install.sh never executed" test ! -e "$fixtures/install-sh-ran"
    check "$archive: no symlink created" bash -c '[[ -z $(find "$1" -type l 2>/dev/null) ]]' _ "$home/Applications"
    stop_tardrop
    record "$archive prompted records=$(record_count) tree=$(tree_of)"
done

# --- environment of programs TarDrop starts ---------------------------------------------------------
section "Child-process environment"
mapfile -t envs < <(ls "$work"/child-env.* 2>/dev/null)
if ((${#envs[@]} == 0)); then
    fail "TarDrop started no desktop-integration helper"
else
    pass "TarDrop started update-desktop-database ${#envs[@]} times"
    leaked=$(cat "${envs[@]}" | grep -E '^(APPIMAGE|APPDIR|ARGV0|OWD|LD_LIBRARY_PATH|QT_PLUGIN_PATH|QT_QPA_PLATFORMTHEME|XDG_DATA_DIRS|LD_PRELOAD)=' | sort -u)
    if [[ -z $leaked ]]; then pass "no AppImage or library variables reach child processes"
    else fail "leaked into child processes: $(echo "$leaked" | tr '\n' ' ')"; fi
    check "child PATH unchanged" bash -c 'p=$1; shift; ! grep -h "^PATH=" "$@" | grep -v -x "PATH=$p:/usr/bin:/bin"' _ "$spy" "${envs[@]}"
    record "child-env leaked=[${leaked//$'\n'/,}]"
fi

# --- HTTPS update check through the host's OpenSSL ----------------------------------------------------
section "HTTPS update check"
if command -v openssl >/dev/null; then
    tls=$work/tls
    mkdir -p "$tls"
    openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=TarDrop test CA" \
        -addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign \
        -keyout "$tls/ca.key" -out "$tls/ca.crt" >/dev/null 2>&1
    openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" -keyout "$tls/srv.key" \
        -out "$tls/srv.csr" >/dev/null 2>&1
    printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > "$tls/ext"
    openssl x509 -req -in "$tls/srv.csr" -CA "$tls/ca.crt" -CAkey "$tls/ca.key" -CAcreateserial \
        -days 2 -extfile "$tls/ext" -out "$tls/srv.crt" >/dev/null 2>&1
    printf 'version 9.9.9\n' > "$tls/latest.txt"
    port_file=$tls/port
    python3 - "$tls" "$port_file" <<'PY' &
import http.server, ssl, sys, os, functools
d, port_file = sys.argv[1], sys.argv[2]
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=d)
server = http.server.HTTPServer(("127.0.0.1", 0), handler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(d, "srv.crt"), os.path.join(d, "srv.key"))
server.socket = ctx.wrap_socket(server.socket, server_side=True)
open(port_file, "w").write(str(server.server_address[1]))
server.serve_forever()
PY
    pids+=($!)
    wait_for 10 test -s "$port_file"
    port=$(cat "$port_file")

    new_home https
    start_tardrop "$fixtures/hello-1.2.3.tar.gz"
    wait_for 60 has_records 1
    stop_tardrop
    check "HTTPS scenario: application installed first" test "$(record_count)" = 1
    # The user-entered provider configuration, as Updates -> Update Source... would store it.
    python3 - "$(records)" "$home/.local/share/tardrop/settings.json" "$port" <<'PY'
import json, sys
records, settings, port = sys.argv[1], sys.argv[2], sys.argv[3]
data = json.load(open(records))
data[0]["update_provider"] = "website_scraper"
data[0]["custom_metadata"] = {
    "website_version_url": f"https://localhost:{port}/latest.txt",
    "website_download_url": f"https://localhost:{port}/hello-9.9.9.tar.gz",
    "website_version_prefix": "version",
}
json.dump(data, open(records, "w"))
json.dump({"check_automatically": True, "check_on_startup": True,
           "notify_beta_releases": False, "interval": "daily"}, open(settings, "w"))
PY
    # Untrusted first: the check must fail closed against a certificate the host does not trust.
    start_tardrop
    sleep 8
    latest=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[0]["latest_version"] or "")' "$(records)" 2>/dev/null)
    check "untrusted certificate is rejected (latest_version stays empty)" \
        bash -c '[[ -z $1 && $2 == 1 ]]' _ "$latest" "$(record_count)"
    stop_tardrop
    record "https untrusted latest=${latest:-<none>}"
    # Trusted: the CA goes into the system store, which is the only one Qt consults.
    if ((install_test_ca)); then
        if [[ -d /usr/local/share/ca-certificates ]] && command -v update-ca-certificates >/dev/null; then
            ca_installed=/usr/local/share/ca-certificates/tardrop-test-ca.crt
            cp "$tls/ca.crt" "$ca_installed" && update-ca-certificates >/dev/null 2>&1
        elif [[ -d /etc/pki/ca-trust/source/anchors ]] && command -v update-ca-trust >/dev/null; then
            ca_installed=/etc/pki/ca-trust/source/anchors/tardrop-test-ca.crt
            cp "$tls/ca.crt" "$ca_installed" && update-ca-trust extract >/dev/null 2>&1
        elif [[ -d /etc/ca-certificates/trust-source/anchors ]] && command -v trust >/dev/null; then
            ca_installed=/etc/ca-certificates/trust-source/anchors/tardrop-test-ca.crt
            cp "$tls/ca.crt" "$ca_installed" && trust extract-compat >/dev/null 2>&1
        fi
        if [[ -n $ca_installed ]]; then
            start_tardrop
            if wait_for 30 bash -c '[[ $(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))[0][\"latest_version\"])" "$1") == 9.9.9 ]]' _ "$(records)"; then
                pass "trusted HTTPS check found version 9.9.9 (host CA store)"
                if [[ $exe == "$appimage" ]]; then
                    # The TLS libraries in use must be the bundled pair, never a host copy.
                    tpid=$(pgrep -n -x tardrop)
                    ssl_maps=$(grep -E 'lib(ssl|crypto)\.so' "/proc/$tpid/maps" 2>/dev/null | awk '{print $6}' | sort -u)
                    printf '  mapped: %s\n' "$(echo "$ssl_maps" | tr '\n' ' ')"
                    check "libssl/libcrypto mapped from the AppImage only" \
                        bash -c '[[ -n $1 ]] && ! grep -v -e /.mount_ -e /appimage_extracted_ <<<"$1"' _ "$ssl_maps"
                fi
            else
                fail "trusted HTTPS update check did not complete (stderr: $(tail -3 "$home/stderr.log" | tr '\n' ' '))"
            fi
            stop_tardrop
            record "https trusted latest=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[0]["latest_version"])' "$(records)" 2>/dev/null)"
        else
            fail "--install-test-ca: no known system trust store tooling"
        fi
    else
        echo "  skipped trusted half: pass --install-test-ca in a disposable environment"
    fi
else
    echo "  skipped: openssl not installed"
fi

if [[ -n $results_file ]]; then
    printf '%s\n' "${results[@]}" | sed "s#$work#<work>#g" > "$results_file"
fi
section "Summary"
if ((failures)); then
    echo "  $failures check(s) failed"
    exit 1
fi
echo "  all checks passed"
