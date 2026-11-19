#!/usr/bin/env bash
# Build the CLI and the Qt6 GUI in Release mode and package them for macOS:
# a .dmg with the .app bundle (Qt deployed) plus the CLI, and the standalone CLI
# .zip kept for the self-update.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: packaging/macos/package.sh --version <x.y.z> --arch <amd64|arm64>
                                  [--build-dir <dir>] [--dist-dir <dir>] [--no-build]

Writes to the dist dir:
  torrent_builder_<version>_macos_<arch>.zip    CLI only (self-update asset)
  torrent_builder_<version>_macos_<arch>.dmg    GUI .app plus CLI
EOF
}

VERSION=""
ARCH=""
BUILD_DIR="build"
DIST_DIR="dist"
DO_BUILD=1

# shellcheck source=../common.sh
source "$(dirname "${BASH_SOURCE[0]}")/../common.sh"

QT_PREFIX="$(brew --prefix qt)"
OPENSSL_PREFIX="$(brew --prefix openssl@3)"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
if [ ! -x "$MACDEPLOYQT" ]; then
    echo "error: macdeployqt not found at $MACDEPLOYQT" >&2
    exit 1
fi
DYLIBBUNDLER="$(command -v dylibbundler || true)"
if [ -z "$DYLIBBUNDLER" ]; then
    echo "error: dylibbundler not found in PATH (brew install dylibbundler)" >&2
    exit 1
fi

if [ "$DO_BUILD" -eq 1 ]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DTORRENT_BUILDER_VERSION="$VERSION" \
        -DTB_ENABLE_GUI=ON \
        -DTB_REQUIRE_GUI=ON \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX"
    cmake --build "$BUILD_DIR" --parallel --target torrent_builder torrent_builder_gui
fi

APP="$BUILD_DIR/torrent_builder_gui.app"
if [ ! -d "$APP" ]; then
    echo "error: expected app bundle not found: $APP" >&2
    exit 1
fi
if [ ! -x "$BUILD_DIR/torrent_builder" ]; then
    echo "error: expected CLI binary not found: $BUILD_DIR/torrent_builder" >&2
    exit 1
fi

if command -v strip >/dev/null 2>&1; then
    strip -x "$BUILD_DIR/torrent_builder"
    strip -x "$APP/Contents/MacOS/torrent_builder_gui"
    # Stripping invalidates the ad-hoc signature the linker applied, and Apple
    # Silicon refuses to run unsigned code. The standalone CLI is shipped in
    # the zip exactly as built here, so re-sign both binaries now.
    codesign --force --sign - "$BUILD_DIR/torrent_builder" "$APP/Contents/MacOS/torrent_builder_gui"
fi

echo "==> Deploying Qt frameworks with $MACDEPLOYQT"
"$MACDEPLOYQT" "$APP" -always-overwrite -no-translations

# The shipped app runs on the cocoa platform plugin; the offscreen copy below
# exists only for the headless smoke test. Fail if macdeployqt stops deploying
# cocoa, since the offscreen run would not notice.
COCOA_PLUGIN="$APP/Contents/PlugIns/platforms/libqcocoa.dylib"
if [ ! -f "$COCOA_PLUGIN" ]; then
    echo "error: macdeployqt did not deploy the cocoa platform plugin: $COCOA_PLUGIN" >&2
    exit 1
fi

echo "==> Bundling Homebrew dylibs (libtorrent, OpenSSL)"
"$DYLIBBUNDLER" -of -cd -b \
    -x "$APP/Contents/MacOS/torrent_builder_gui" \
    -d "$APP/Contents/Frameworks" \
    -p "@executable_path/../Frameworks/"

# Any dependency still pointing at a Homebrew prefix means the bundle will not
# start on a machine without Homebrew installed.
check_no_homebrew_refs() {
    local target="$1"
    if otool -L "$target" | grep -Eq '(/opt/homebrew|/usr/local)/'; then
        echo "error: $target still references Homebrew paths:" >&2
        otool -L "$target" | grep -E '(/opt/homebrew|/usr/local)/' >&2
        exit 1
    fi
}

check_no_homebrew_refs "$APP/Contents/MacOS/torrent_builder_gui"
# qt.conf makes the bundle the only plugin source, so every Mach-O inside the
# .app (Qt frameworks, plugins, extra dylibs) must be free of Homebrew paths.
while IFS= read -r binary; do
    if file -b "$binary" | grep -q "Mach-O"; then
        check_no_homebrew_refs "$binary"
    fi
done < <(find "$APP" -type f)

# The packaged smoke test runs with QT_QPA_PLATFORM=offscreen, but macdeployqt
# intentionally deploys only the cocoa platform plugin. Copy the offscreen
# plugin and repoint its Qt references at the frameworks already in the bundle,
# so the test does not depend on the build machine's Homebrew install.
OFFSCREEN_PLUGIN="$APP/Contents/PlugIns/platforms/libqoffscreen.dylib"
OFFSCREEN_SOURCE="$QT_PREFIX/share/qt/plugins/platforms/libqoffscreen.dylib"
if [ ! -f "$OFFSCREEN_SOURCE" ]; then
    echo "error: offscreen platform plugin not found: $OFFSCREEN_SOURCE" >&2
    exit 1
fi
mkdir -p "$(dirname "$OFFSCREEN_PLUGIN")"
cp "$OFFSCREEN_SOURCE" "$OFFSCREEN_PLUGIN"
while IFS= read -r dep; do
    # The first entry may be the plugin's own install id, depending on whether
    # it was linked as a bundle; never rewrite it.
    if [ "$(basename "$dep")" = "libqoffscreen.dylib" ]; then
        continue
    fi
    case "$dep" in
        @rpath/*)
            install_name_tool -change "$dep" \
                "@executable_path/../Frameworks/${dep#@rpath/}" "$OFFSCREEN_PLUGIN"
            ;;
        /opt/homebrew/*|/usr/local/*)
            case "$dep" in
                */lib/*.framework/*)
                    install_name_tool -change "$dep" \
                        "@executable_path/../Frameworks/${dep#*/lib/}" "$OFFSCREEN_PLUGIN"
                    ;;
                *)
                    echo "error: cannot map Homebrew dependency '$dep' in $OFFSCREEN_PLUGIN" >&2
                    exit 1
                    ;;
            esac
            ;;
    esac
done < <(otool -L "$OFFSCREEN_PLUGIN" | awk 'NR > 1 { print $1 }')
check_no_homebrew_refs "$OFFSCREEN_PLUGIN"

if ! find "$APP/Contents/Frameworks" -maxdepth 1 -name 'Qt*.framework' -print -quit | grep -q .; then
    echo "error: no Qt frameworks found in the app bundle" >&2
    exit 1
fi

mkdir -p "$DIST_DIR"
# Stage outside --dist-dir: a user-supplied output path must never lose an
# existing dmg/ directory.
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/torrent-builder-dmg.XXXXXX")"
MOUNT=""
ZIP_STAGE=""
cleanup() {
    if [ -n "$MOUNT" ]; then
        hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
        rm -rf "$MOUNT"
    fi
    if [ -n "$ZIP_STAGE" ]; then
        rm -rf "$ZIP_STAGE"
    fi
    rm -rf "$STAGE"
}
trap cleanup EXIT
cp -R "$APP" "$STAGE/Torrent Builder.app"
cp README.md LICENSE "$STAGE/"
ln -s /Applications "$STAGE/Applications"

mkdir -p "$STAGE/CLI/lib"
cp "$BUILD_DIR/torrent_builder" "$STAGE/CLI/torrent_builder"
"$DYLIBBUNDLER" -of -cd -b \
    -x "$STAGE/CLI/torrent_builder" \
    -d "$STAGE/CLI/lib" \
    -p "@executable_path/lib/"
check_no_homebrew_refs "$STAGE/CLI/torrent_builder"
while IFS= read -r dylib; do
    check_no_homebrew_refs "$dylib"
done < <(find "$STAGE/CLI/lib" -type f -name '*.dylib')

if ! find "$STAGE/CLI/lib" -maxdepth 1 -name '*.dylib' -print -quit | grep -q .; then
    echo "error: no dylibs bundled for the CLI" >&2
    exit 1
fi

# Sign after every modification: Apple Silicon refuses to run unsigned or
# invalidly signed code, and macdeployqt/dylibbundler rewrite binaries.
echo "==> Signing (ad-hoc)"
xattr -cr "$STAGE/Torrent Builder.app" || true
codesign --force --deep --sign - "$STAGE/Torrent Builder.app"
codesign --force --sign - "$STAGE/CLI/torrent_builder" "$STAGE"/CLI/lib/*.dylib
codesign --verify --deep --strict "$STAGE/Torrent Builder.app"

DMG="$DIST_DIR/torrent_builder_${VERSION}_macos_${ARCH}.dmg"
rm -f "$DMG"
echo "==> Creating $(basename "$DMG")"
hdiutil create -volname "Torrent Builder ${VERSION}" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
hdiutil verify "$DMG"

echo "==> Running packaged smoke tests"
MOUNT="$(mktemp -d)"
hdiutil attach -nobrowse -readonly -mountpoint "$MOUNT" "$DMG" >/dev/null
QT_QPA_PLATFORM=offscreen "$MOUNT/Torrent Builder.app/Contents/MacOS/torrent_builder_gui" --smoke-test
codesign --verify --deep --strict "$MOUNT/Torrent Builder.app"
assert_reports_version "$MOUNT/CLI/torrent_builder"

# Keep publishing the standalone CLI zip: torrent_builder update looks for
# exactly this asset name on macOS.
codesign --verify --strict "$BUILD_DIR/torrent_builder"
CLI_ZIP="$DIST_DIR/torrent_builder_${VERSION}_macos_${ARCH}.zip"
rm -f "$CLI_ZIP"
ZIP_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/torrent-builder-cli.XXXXXX")"
cp "$BUILD_DIR/torrent_builder" "$ZIP_STAGE/torrent_builder"
echo "==> Running standalone CLI smoke test"
assert_reports_version "$ZIP_STAGE/torrent_builder"
CLI_ZIP_ABS="$(cd "$DIST_DIR" && pwd)/$(basename "$CLI_ZIP")"
( cd "$ZIP_STAGE" && ditto -c -k torrent_builder "$CLI_ZIP_ABS" )
rm -rf "$ZIP_STAGE"
ZIP_STAGE=""

ls -la "$DMG" "$CLI_ZIP"
