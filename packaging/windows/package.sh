#!/usr/bin/env bash
# Build the CLI and the Qt6 GUI in Release mode and package them as a portable
# Windows .zip (executables + Qt runtime + MinGW runtime + libtorrent/OpenSSL).
#
# Run from an MSYS2 MinGW shell (the release workflow uses UCRT64/CLANGARM64).
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: packaging/windows/package.sh --version <x.y.z> --arch <amd64|arm64>
                                    [--build-dir <dir>] [--dist-dir <dir>] [--no-build]

Writes to the dist dir:
  torrent_builder_<version>_windows_<arch>.exe    raw CLI executable
  torrent_builder_<version>_windows_<arch>.zip    CLI + GUI bundle
EOF
}

VERSION=""
ARCH=""
BUILD_DIR="build"
DIST_DIR="dist"
DO_BUILD=1

# shellcheck source=../common.sh
source "$(dirname "${BASH_SOURCE[0]}")/../common.sh"

if [ -z "${MINGW_PREFIX:-}" ]; then
    case "${MSYSTEM:-}" in
        UCRT64) MINGW_PREFIX=/ucrt64 ;;
        CLANGARM64) MINGW_PREFIX=/clangarm64 ;;
        MINGW64) MINGW_PREFIX=/mingw64 ;;
        *) echo "error: MINGW_PREFIX is not set; run this from an MSYS2 MinGW shell" >&2; exit 1 ;;
    esac
fi

export PATH="$MINGW_PREFIX/bin:$PATH"

if ! command -v unzip >/dev/null 2>&1; then
    echo "error: unzip is required to validate the generated archive" >&2
    exit 1
fi

if [ "$DO_BUILD" -eq 1 ]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -G "MSYS Makefiles" \
        -DTORRENT_BUILDER_VERSION="$VERSION" \
        -DTB_ENABLE_GUI=ON \
        -DTB_REQUIRE_GUI=ON \
        -DCMAKE_PREFIX_PATH="$(cygpath -m "$MINGW_PREFIX")"
    cmake --build "$BUILD_DIR" --parallel --target torrent_builder torrent_builder_gui
fi

for exe in "$BUILD_DIR/torrent_builder.exe" "$BUILD_DIR/torrent_builder_gui.exe"; do
    if [ ! -f "$exe" ]; then
        echo "error: expected executable not found: $exe" >&2
        exit 1
    fi
done

if command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$BUILD_DIR/torrent_builder.exe" "$BUILD_DIR/torrent_builder_gui.exe"
fi

WINDEPLOYQT=""
for candidate in \
    "$MINGW_PREFIX/bin/windeployqt6.exe" \
    "$MINGW_PREFIX/bin/windeployqt.exe"
do
    if [ -x "$candidate" ]; then
        WINDEPLOYQT="$candidate"
        break
    fi
done
if [ -z "$WINDEPLOYQT" ]; then
    echo "error: windeployqt not found under $MINGW_PREFIX/bin" >&2
    exit 1
fi

mkdir -p "$DIST_DIR"
# Stage outside --dist-dir: a user-supplied output path must never lose an
# existing directory, and the staging tree must not be published as an asset.
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/torrent-builder-win.XXXXXX")"
ZIP_CHECK=""
cleanup() {
    rm -rf "$STAGE"
    if [ -n "$ZIP_CHECK" ]; then
        rm -rf "$ZIP_CHECK"
    fi
}
trap cleanup EXIT

cp "$BUILD_DIR/torrent_builder.exe" "$BUILD_DIR/torrent_builder_gui.exe" "$STAGE/"
cp README.md LICENSE "$STAGE/"

echo "==> Deploying Qt runtime with $WINDEPLOYQT"
"$WINDEPLOYQT" --release --no-translations --no-opengl-sw --no-system-d3d-compiler \
    "$STAGE/torrent_builder_gui.exe"

# windeployqt only handles Qt. Copy every non-system DLL the loaders report,
# resolving them from the MinGW prefix so system DLLs are never shipped.
echo "==> Copying MinGW/libtorrent/OpenSSL runtime DLLs"
collect_runtime_dlls() {
    local exe="$1" dll base
    while IFS= read -r dll; do
        base="$(basename "$dll")"
        if [ -f "$MINGW_PREFIX/bin/$base" ]; then
            cp -f "$MINGW_PREFIX/bin/$base" "$STAGE/"
        fi
    done < <(ldd "$exe" | awk '{ for (i = 1; i <= NF; i++) if (tolower($i) ~ /\.dll$/) print $i }')
}
collect_runtime_dlls "$STAGE/torrent_builder_gui.exe"
collect_runtime_dlls "$STAGE/torrent_builder.exe"

# The offscreen plugin is what makes --smoke-test usable on a headless runner.
if [ ! -f "$STAGE/platforms/qoffscreen.dll" ]; then
    mkdir -p "$STAGE/platforms"
    cp "$MINGW_PREFIX/share/qt6/plugins/platforms/qoffscreen.dll" "$STAGE/platforms/"
fi
if [ ! -f "$STAGE/platforms/qwindows.dll" ]; then
    echo "error: platforms/qwindows.dll missing from the bundle" >&2
    exit 1
fi

echo "==> Running packaged smoke tests"
# Windows resolves DLLs from PATH after the executable's own directory, so the
# MinGW prefix on PATH would mask a missing runtime DLL. Restrict PATH to the
# system directories to prove the bundle is self-contained.
SMOKE_PATH="/c/Windows/system32:/c/Windows"
( cd "$STAGE" && PATH="$SMOKE_PATH" QT_QPA_PLATFORM=offscreen ./torrent_builder_gui.exe --smoke-test )
( PATH="$SMOKE_PATH"; assert_reports_version "$STAGE/torrent_builder.exe" )

ZIP="$DIST_DIR/torrent_builder_${VERSION}_windows_${ARCH}.zip"
rm -f "$ZIP"
echo "==> Creating $(basename "$ZIP")"
STAGE_WIN="$(cygpath -w "$STAGE")"
ZIP_WIN="$(cygpath -w "$(cd "$DIST_DIR" && pwd)/$(basename "$ZIP")")"
# PowerShell single-quoted strings cannot contain a quote, so a checkout path
# such as C:\Users\O'Brien\... would terminate the command. Double any quote
# before interpolating the paths.
PS_STAGE="${STAGE_WIN//\'/\'\'}"
PS_ZIP="${ZIP_WIN//\'/\'\'}"
powershell.exe -NoProfile -NonInteractive -Command \
    "\$ErrorActionPreference = 'Stop'; Compress-Archive -Force -Path '${PS_STAGE}\\*' -DestinationPath '${PS_ZIP}'"

echo "==> Validating $(basename "$ZIP")"
unzip -t "$ZIP" >/dev/null
for entry in torrent_builder.exe torrent_builder_gui.exe; do
    if ! unzip -l "$ZIP" "$entry" >/dev/null 2>&1; then
        echo "error: $ZIP is missing $entry" >&2
        exit 1
    fi
done

# Run the smoke tests from the extracted archive: checking the staging tree is
# not enough, because a zip missing a DLL or the platform plugin would still
# pass unzip -t.
echo "==> Smoke-testing the extracted archive"
ZIP_CHECK="$(mktemp -d "${TMPDIR:-/tmp}/torrent-builder-zip.XXXXXX")"
unzip -q "$ZIP" -d "$ZIP_CHECK"
( cd "$ZIP_CHECK" && PATH="$SMOKE_PATH" QT_QPA_PLATFORM=offscreen ./torrent_builder_gui.exe --smoke-test )
( PATH="$SMOKE_PATH"; assert_reports_version "$ZIP_CHECK/torrent_builder.exe" )

# Keep publishing the standalone CLI executable: torrent_builder update looks
# for exactly this asset name.
cp "$BUILD_DIR/torrent_builder.exe" "$DIST_DIR/torrent_builder_${VERSION}_windows_${ARCH}.exe"

ls -la "$ZIP" "$DIST_DIR/torrent_builder_${VERSION}_windows_${ARCH}.exe"
