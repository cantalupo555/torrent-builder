#!/usr/bin/env bash
# Validate the Linux packages produced by packaging/linux/package.sh.
#
# The strongest check is installing each package in a clean container of the
# distro it targets and running the GUI smoke test under Xvfb, which loads the
# xcb platform plugin the declared dependencies exist for. Metadata and
# contents are checked first with dpkg-deb and, when rpm is available on the
# host, with rpm, so --skip-containers still validates both packages.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: packaging/linux/verify.sh --deb <path> [--rpm <path>] [--version <x.y.z>]
                                 [--skip-containers]

Checks package contents and dependencies with dpkg-deb and, when rpm is
available on the host, with rpm. Unless --skip-containers is passed, installs
the packages in ubuntu:24.04 and fedora:latest containers, runs
torrent_builder_gui --smoke-test under Xvfb and, when --version is passed,
fails if the installed CLI reports anything else.
EOF
}

DEB=""
RPM=""
VERSION=""
SKIP_CONTAINERS=0

require_value() {
    case "${2:-}" in
        ''|-*)
            echo "error: $1 requires a value" >&2
            usage >&2
            exit 2
            ;;
    esac
}

while [ $# -gt 0 ]; do
    case "$1" in
        --deb) require_value "$1" "${2:-}"; DEB="$2"; shift 2 ;;
        --rpm) require_value "$1" "${2:-}"; RPM="$2"; shift 2 ;;
        --version) require_value "$1" "${2:-}"; VERSION="$2"; shift 2 ;;
        --skip-containers) SKIP_CONTAINERS=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "$DEB" ]; then
    echo "error: --deb is required" >&2
    usage >&2
    exit 2
fi
if [ ! -f "$DEB" ]; then
    echo "error: deb not found: $DEB" >&2
    exit 1
fi

abs_path() {
    printf '%s/%s\n' "$(cd "$(dirname "$1")" && pwd)" "$(basename "$1")"
}

DEB="$(abs_path "$DEB")"
if [ -n "$RPM" ]; then
    if [ ! -f "$RPM" ]; then
        echo "error: rpm not found: $RPM" >&2
        exit 1
    fi
    RPM="$(abs_path "$RPM")"
fi

echo "==> Checking .deb contents and metadata"
if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "error: dpkg-deb is required to inspect the package" >&2
    exit 1
fi

CONTENTS="$(dpkg-deb -c "$DEB")"
for path in \
    ./usr/bin/torrent_builder \
    ./usr/bin/torrent_builder_gui \
    ./usr/share/applications/torrent-builder.desktop \
    ./usr/share/icons/hicolor/scalable/apps/torrent-builder.svg \
    ./usr/share/icons/hicolor/256x256/apps/torrent-builder.png \
    ./usr/share/doc/torrent-builder/copyright
do
    if ! printf '%s\n' "$CONTENTS" | grep -qE "[[:space:]]${path}\$"; then
        echo "error: $path missing from $DEB" >&2
        exit 1
    fi
done

META="$(dpkg-deb -I "$DEB")"
for dep in \
    'libc6 (>= 2.39)' \
    'libstdc++6 (>= 13)' \
    'libqt6widgets6t64 | libqt6widgets6' \
    'libqt6gui6t64 | libqt6gui6' \
    'libqt6core6t64 | libqt6core6' \
    'libqt6dbus6t64 | libqt6dbus6' \
    'libtorrent-rasterbar2.0t64 | libtorrent-rasterbar2.0' \
    'libssl3t64 | libssl3' \
    'libxcb-cursor0'
do
    if ! printf '%s\n' "$META" | grep -qF "$dep"; then
        echo "error: dependency '$dep' missing from $DEB" >&2
        exit 1
    fi
done

if [ -n "$RPM" ]; then
    if command -v rpm >/dev/null 2>&1; then
        echo "==> Checking .rpm metadata and contents"
        RPM_DEPS="$(rpm -qpR "$RPM")"
        for dep in \
            'glibc >= 2.39' \
            'libstdc++ >= 13' \
            'libQt6Widgets.so.6()(64bit)' \
            'libQt6Gui.so.6()(64bit)' \
            'libQt6Core.so.6()(64bit)' \
            'libQt6DBus.so.6()(64bit)' \
            'libtorrent-rasterbar.so.2.0()(64bit)' \
            'libssl.so.3()(64bit)' \
            'libcrypto.so.3()(64bit)' \
            'libxcb-cursor.so.0()(64bit)'
        do
            if ! printf '%s\n' "$RPM_DEPS" | grep -qF "$dep"; then
                echo "error: dependency '$dep' missing from $RPM" >&2
                exit 1
            fi
        done
        RPM_FILES="$(rpm -qlp "$RPM")"
        for path in \
            /usr/bin/torrent_builder \
            /usr/bin/torrent_builder_gui \
            /usr/share/applications/torrent-builder.desktop
        do
            if ! printf '%s\n' "$RPM_FILES" | grep -qx "$path"; then
                echo "error: $path missing from $RPM" >&2
                exit 1
            fi
        done
    elif [ "$SKIP_CONTAINERS" -eq 1 ]; then
        echo "warning: rpm is not installed and --skip-containers was passed; $(basename "$RPM") was not validated" >&2
    fi
fi

if [ "$SKIP_CONTAINERS" -eq 1 ]; then
    echo "==> Skipping container checks (--skip-containers)"
    exit 0
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker is required for the install smoke tests" >&2
    exit 1
fi

echo "==> Installing $DEB in ubuntu:24.04 and running the GUI smoke test"
docker run --rm \
    -v "$DEB:/tmp/torrent-builder.deb:ro" \
    -e DEBIAN_FRONTEND=noninteractive \
    -e TB_EXPECTED_VERSION="${VERSION}" \
    ubuntu:24.04 bash -c '
        set -euo pipefail
        apt-get update -qq
        apt-get install -y -qq /tmp/torrent-builder.deb xvfb xauth
        xvfb-run -a torrent_builder_gui --smoke-test
        reported="$(torrent_builder --version)"
        echo "$reported"
        if [ -n "${TB_EXPECTED_VERSION:-}" ]; then
            reported="${reported%$'\''\r'\''}"
            case "$reported" in
                *" $TB_EXPECTED_VERSION") ;;
                *) echo "error: installed CLI reports '"'"'$reported'"'"', expected $TB_EXPECTED_VERSION" >&2; exit 1 ;;
            esac
        fi
    '

if [ -n "$RPM" ]; then
    echo "==> Installing $(basename "$RPM") in fedora:latest and running the GUI smoke test"
    docker run --rm \
        -v "$RPM:/tmp/torrent-builder.rpm:ro" \
        -e TB_EXPECTED_VERSION="${VERSION}" \
        fedora:latest bash -c '
            set -euo pipefail
            dnf -y -q install /tmp/torrent-builder.rpm xorg-x11-server-Xvfb
            xvfb-run -a torrent_builder_gui --smoke-test
            reported="$(torrent_builder --version)"
            echo "$reported"
            if [ -n "${TB_EXPECTED_VERSION:-}" ]; then
                reported="${reported%$'\''\r'\''}"
                case "$reported" in
                    *" $TB_EXPECTED_VERSION") ;;
                    *) echo "error: installed CLI reports '"'"'$reported'"'"', expected $TB_EXPECTED_VERSION" >&2; exit 1 ;;
                esac
            fi

            rpm -qpR /tmp/torrent-builder.rpm
            rpm -qpR /tmp/torrent-builder.rpm | grep -qF "libQt6Widgets.so.6()(64bit)"
            rpm -qpR /tmp/torrent-builder.rpm | grep -qF "libtorrent-rasterbar.so.2.0()(64bit)"
            rpm -qpR /tmp/torrent-builder.rpm | grep -qF "glibc >= 2.39"
            rpm -qpR /tmp/torrent-builder.rpm | grep -qF "libstdc++ >= 13"

            rpm -qlp /tmp/torrent-builder.rpm | grep -qx "/usr/bin/torrent_builder"
            rpm -qlp /tmp/torrent-builder.rpm | grep -qx "/usr/bin/torrent_builder_gui"
            rpm -qlp /tmp/torrent-builder.rpm | grep -qx "/usr/share/applications/torrent-builder.desktop"
        '
fi

echo "==> Linux package verification passed"
