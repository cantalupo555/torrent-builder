#!/usr/bin/env bash
# Build the CLI and the Qt6 GUI in Release mode and package them as .deb/.rpm.
#
# Used by the release workflow; also runnable locally on Debian/Ubuntu hosts
# with cmake, Qt6 dev packages, nfpm and the usual build dependencies.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: packaging/linux/package.sh --version <x.y.z> --arch <amd64|arm64>
                                  [--build-dir <dir>] [--dist-dir <dir>] [--no-build]

Builds torrent_builder and torrent_builder_gui, then writes to the dist dir:
  torrent_builder_<version>_linux_<arch>        raw CLI binary
  torrent_builder_<version>_linux_<arch>.deb    CLI + GUI package
  torrent_builder_<version>_linux_<arch>.rpm    CLI + GUI package
EOF
}

VERSION=""
ARCH=""
BUILD_DIR="build"
DIST_DIR="dist"
DO_BUILD=1

# shellcheck source=../common.sh
source "$(dirname "${BASH_SOURCE[0]}")/../common.sh"

if [ "$DO_BUILD" -eq 1 ]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DTORRENT_BUILDER_VERSION="$VERSION" \
        -DTB_ENABLE_GUI=ON \
        -DTB_REQUIRE_GUI=ON
    cmake --build "$BUILD_DIR" --parallel --target torrent_builder torrent_builder_gui
fi

for bin in "$BUILD_DIR/torrent_builder" "$BUILD_DIR/torrent_builder_gui"; do
    if [ ! -x "$bin" ]; then
        echo "error: expected binary not found: $bin" >&2
        exit 1
    fi
done

assert_reports_version "$BUILD_DIR/torrent_builder"

if ! command -v nfpm >/dev/null 2>&1; then
    echo "error: nfpm not found in PATH" >&2
    exit 1
fi

if command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$BUILD_DIR/torrent_builder" "$BUILD_DIR/torrent_builder_gui"
fi

mkdir -p "$DIST_DIR"

# shellcheck disable=SC2016 # '@VERSION@', '@ARCH@' and '@BUILD_DIR@' are literal placeholders.
# Escape the user-supplied build dir for the sed replacement (backslash,
# ampersand and the # delimiter) and for the YAML single-quoted scalar
# (apostrophes are doubled). Using sed keeps the result identical under bash
# 3.2 (macOS /bin/bash) and 5.x, whose ${var//pattern/replacement} handling of
# backslashes differs.
BUILD_DIR_SED="$(printf '%s' "$BUILD_DIR" | sed -e 's/[\\&#]/\\&/g' -e "s/'/''/g")"
sed -e "s#@VERSION@#${VERSION}#g" -e "s#@ARCH@#${ARCH}#g" -e "s#@BUILD_DIR@#${BUILD_DIR_SED}#g" \
    packaging/linux/nfpm.yaml.in > "$BUILD_DIR/nfpm.yaml"

TMP_OUT="$(mktemp -d)"
trap 'rm -rf "$TMP_OUT"' EXIT

nfpm package -p deb -f "$BUILD_DIR/nfpm.yaml" -t "$TMP_OUT"
nfpm package -p rpm -f "$BUILD_DIR/nfpm.yaml" -t "$TMP_OUT"

DEB_SRC="$(find "$TMP_OUT" -maxdepth 1 -name '*.deb' -print -quit)"
RPM_SRC="$(find "$TMP_OUT" -maxdepth 1 -name '*.rpm' -print -quit)"
if [ -z "$DEB_SRC" ] || [ -z "$RPM_SRC" ]; then
    echo "error: nfpm did not produce both packages" >&2
    ls -la "$TMP_OUT" >&2
    exit 1
fi

DEB="$DIST_DIR/torrent_builder_${VERSION}_linux_${ARCH}.deb"
RPM="$DIST_DIR/torrent_builder_${VERSION}_linux_${ARCH}.rpm"
mv "$DEB_SRC" "$DEB"
mv "$RPM_SRC" "$RPM"

# Keep publishing the standalone CLI binary: torrent_builder update looks for
# exactly this asset name.
cp "$BUILD_DIR/torrent_builder" "$DIST_DIR/torrent_builder_${VERSION}_linux_${ARCH}"

ls -la "$DEB" "$RPM" "$DIST_DIR/torrent_builder_${VERSION}_linux_${ARCH}"
