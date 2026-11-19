# Packaging

Scripts used by `.github/workflows/release.yml` to turn a tagged commit into the
release assets. They can also be run locally to reproduce a package.

| Script | Platform | Output |
| --- | --- | --- |
| `linux/package.sh` + `linux/verify.sh` | Linux | `.deb` and `.rpm` with CLI + GUI |
| `windows/package.sh` | Windows (MSYS2 shell) | portable `.zip` with CLI + GUI + DLLs |
| `macos/package.sh` | macOS | `.dmg` with the `.app` bundle and the CLI |

Every script also emits the standalone CLI asset (`_linux_<arch>`,
`_windows_<arch>.exe`, `_macos_<arch>.zip`) because `torrent_builder update`
selects the asset by that exact suffix. Do not rename those files.

The three `package.sh` scripts share argument parsing and validation through
`common.sh`; each one keeps only its platform-specific build and packaging
steps. They build natively: `--arch` must match the host, so an arm64 machine
cannot label its output as amd64 (or the reverse) and mislead the self-updater.

## Linux

Requirements: CMake, a C++23 compiler, Qt6 development files
(`qt6-base-dev`), libtorrent-rasterbar, Boost, OpenSSL, pkg-config, Docker
(for `verify.sh`) and [nfpm](https://nfpm.goreleaser.com) v2.49.0.

```bash
./packaging/linux/package.sh --version 0.7.0 --arch amd64
./packaging/linux/verify.sh --deb dist/*.deb --rpm dist/*.rpm --version 0.7.0
```

`package.sh` configures `build/` in Release mode with `TB_REQUIRE_GUI=ON`, so a
missing Qt fails the build instead of silently producing a CLI-only package.
`verify.sh` inspects the package metadata and then installs it in clean
`ubuntu:24.04` and `fedora:latest` containers and runs the GUI smoke test. Use
`--skip-containers` when Docker is unavailable.

Qt6 is a runtime dependency, not bundled. The `.deb` depends on
`libqt6widgets6t64 | libqt6widgets6` (and friends) to cover the `t64`
transition on Ubuntu 24.04/Debian 13 while staying installable on older naming.
The `.rpm` depends on Qt/libtorrent/OpenSSL SONAMEs because nfpm writes the
RPM header directly and rpmbuild never runs its automatic dependency
generator. Both formats also require glibc >= 2.39 and libstdc++ >= 13, the
Ubuntu 24.04 build baseline, so older distributions refuse the package instead
of installing a binary that cannot start.

## Windows

Run from an MSYS2 MinGW shell (`UCRT64` or `CLANGARM64`) with the packages the
release workflow installs: `git`, `base-devel`, `<prefix>-toolchain`,
`<prefix>-cmake`, `<prefix>-libtorrent-rasterbar`, `<prefix>-pkg-config`,
`<prefix>-openssl`, `<prefix>-qt6-base` and `unzip` (used to validate the
generated archive):

```bash
./packaging/windows/package.sh --version 0.7.0 --arch amd64
```

`windeployqt` only deploys Qt, so the script also resolves every non-system DLL
with `ldd` and copies it from the MinGW prefix. The packaged bundle is
smoke-tested with `QT_QPA_PLATFORM=offscreen` before it is zipped. There is no
installer: the archive is portable.

## macOS

Requirements: `brew install cmake boost libtorrent-rasterbar openssl@3
pkg-config qt dylibbundler`.

```bash
./packaging/macos/package.sh --version 0.7.0 --arch arm64
```

`macdeployqt` deploys the Qt frameworks; `dylibbundler` then embeds the Homebrew
dylibs (`libtorrent-rasterbar`, `libssl`, `libcrypto`) that `macdeployqt` does
not handle. The script fails if any binary still references `/opt/homebrew` or
`/usr/local`, signs everything ad-hoc (`codesign --force --deep --sign -`), and
runs the packaged smoke test from the mounted image.

The `.app` and the `CLI` folder are self-contained: the CLI links to the
bundled `CLI/lib`. Do not run `torrent_builder update` on that binary — the
updater installs the standalone CLI asset, which expects Homebrew's libtorrent
and OpenSSL. Update from a new `.dmg` instead.

The disk image is **not notarized**: without an Apple Developer ID, Gatekeeper
requires right-click → Open (or System Settings → Privacy & Security → Open
Anyway) on the first launch.

## Validating a release

`release.yml` also runs on `workflow_dispatch`, which builds and uploads every
artifact without publishing a release:

```bash
gh workflow run release.yml --ref <branch> -f version=0.0.0-dryrun
```

Run it before tagging: the dry run executes the same matrix — including the
Windows and macOS packaging paths — without publishing, so a broken packager
surfaces before the tag exists.

## Known limitations

- Linux packages are built on Ubuntu 24.04 and need glibc 2.39 or newer
  (Ubuntu 24.04+, Debian 13+, Fedora 40+). Older distributions need a different
  build baseline.
- RPM packages are validated on Fedora only. The SONAME dependencies should
  also resolve on RHEL and openSUSE, but those are not covered by CI.
- macOS builds are ad-hoc signed, not notarized.
