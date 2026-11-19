#!/usr/bin/env bash
# Shared argument parsing for the per-platform packaging scripts.
#
# The sourcing script must define usage() first, then source this file. On
# success VERSION, ARCH, BUILD_DIR, DIST_DIR, DO_BUILD and ROOT are set and the
# working directory is the repository root.
#
# shellcheck shell=bash

VERSION=""
ARCH=""
BUILD_DIR="build"
DIST_DIR="dist"
DO_BUILD=1

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
        --version) require_value "$1" "${2:-}"; VERSION="$2"; shift 2 ;;
        --arch) require_value "$1" "${2:-}"; ARCH="$2"; shift 2 ;;
        --build-dir) require_value "$1" "${2:-}"; BUILD_DIR="$2"; shift 2 ;;
        --dist-dir) require_value "$1" "${2:-}"; DIST_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "$VERSION" ] || [ -z "$ARCH" ]; then
    echo "error: --version and --arch are required" >&2
    usage >&2
    exit 2
fi

# Keep the version safe to interpolate into the nfpm config and into file
# names: semver characters only.
case "$VERSION" in
    *[!0-9A-Za-z.+-]*)
        echo "error: invalid version '$VERSION' (use semver characters only)" >&2
        exit 2
        ;;
esac

case "$ARCH" in
    amd64|arm64) ;;
    *) echo "error: unsupported arch '$ARCH' (expected amd64 or arm64)" >&2; exit 2 ;;
esac

# The packagers build natively: Qt, the runtime DLLs and the toolchains come
# from the host. Reject a mismatch so an arm64 host cannot produce an asset
# named for amd64 (or the reverse), which would mislead the self-updater.
if [ -n "${MSYSTEM:-}" ]; then
    case "$MSYSTEM" in
        CLANGARM64) HOST_ARCH=arm64 ;;
        UCRT64|MINGW64) HOST_ARCH=amd64 ;;
        *) HOST_ARCH="" ;;
    esac
else
    case "$(uname -m)" in
        x86_64|amd64) HOST_ARCH=amd64 ;;
        arm64|aarch64) HOST_ARCH=arm64 ;;
        *) HOST_ARCH="" ;;
    esac
fi
if [ -z "$HOST_ARCH" ]; then
    echo "error: cannot determine the host architecture" >&2
    exit 2
fi
if [ "$ARCH" != "$HOST_ARCH" ]; then
    echo "error: --arch $ARCH does not match the host architecture ($HOST_ARCH); cross-builds are not supported" >&2
    exit 2
fi

# BUILD_DIR is written into the YAML template and used as a glob there, so
# reject the characters that would change its meaning.
case "$BUILD_DIR" in
    *'*'*|*'?'*|*'['*)
        echo "error: --build-dir must not contain glob characters (*, ?, [)" >&2
        exit 2
        ;;
esac

# A --no-build run can reuse a build directory configured for another version,
# which would publish assets whose name disagrees with the binary the updater
# installs. Fail when the packaged CLI reports something else.
assert_reports_version() {
    local binary="$1" reported
    reported="$("$binary" --version)"
    # Native Windows binaries write CRLF; command substitution strips the LF
    # but leaves the CR, which would defeat the end-anchored pattern below.
    reported="${reported%$'\r'}"
    case "$reported" in
        *" $VERSION") ;;
        *)
            echo "error: $binary reports '$reported', expected version $VERSION" >&2
            exit 1
            ;;
    esac
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[1]}")/../.." && pwd)"
cd "$ROOT"
