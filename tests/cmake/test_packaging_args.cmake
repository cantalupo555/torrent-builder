# Exercises the argument parsers of the packaging scripts through their public
# entry points. Cases exit before building or calling nfpm, so the test only
# needs bash.

if(NOT DEFINED TEST_BASH OR NOT DEFINED TEST_PACKAGING_SCRIPT)
    message(FATAL_ERROR "TEST_BASH and TEST_PACKAGING_SCRIPT are required")
endif()

set(workdir "${CMAKE_CURRENT_LIST_DIR}/../..")

function(expect_exit_for script expected description)
    execute_process(
        COMMAND "${TEST_BASH}" "${script}" ${ARGN}
        WORKING_DIRECTORY "${workdir}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )
    if(NOT rc EQUAL expected)
        message(FATAL_ERROR
            "FAIL: ${description}: expected exit ${expected}, got ${rc}\n${out}\n${err}")
    endif()
    message(STATUS "PASS: ${description}")
endfunction()

# Like expect_exit_for, but also pins the failure to a specific message so a
# different error that happens to exit with the same code cannot pass.
function(expect_exit_message script expected pattern description)
    execute_process(
        COMMAND "${TEST_BASH}" "${script}" ${ARGN}
        WORKING_DIRECTORY "${workdir}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )
    if(NOT rc EQUAL expected)
        message(FATAL_ERROR
            "FAIL: ${description}: expected exit ${expected}, got ${rc}\n${out}\n${err}")
    endif()
    if(NOT err MATCHES "${pattern}")
        message(FATAL_ERROR
            "FAIL: ${description}: stderr does not match '${pattern}'\n${out}\n${err}")
    endif()
    message(STATUS "PASS: ${description}")
endfunction()

function(expect_exit expected description)
    expect_exit_for("${TEST_PACKAGING_SCRIPT}" ${expected} "${description}" ${ARGN})
endfunction()

# Derive the host architecture the same way packaging/common.sh does, since
# the packagers reject a cross-arch request.
set(host_arch "")
if(DEFINED ENV{MSYSTEM})
    if("$ENV{MSYSTEM}" STREQUAL "CLANGARM64")
        set(host_arch arm64)
    elseif("$ENV{MSYSTEM}" STREQUAL "UCRT64" OR "$ENV{MSYSTEM}" STREQUAL "MINGW64")
        set(host_arch amd64)
    else()
        message(FATAL_ERROR
            "unsupported MSYSTEM for the packaging test: $ENV{MSYSTEM}")
    endif()
else()
    execute_process(
        COMMAND "${TEST_BASH}" -c "uname -m"
        OUTPUT_VARIABLE host_machine
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(TOLOWER "${host_machine}" host_machine)
    if(host_machine MATCHES "^(x86_64|amd64)$")
        set(host_arch amd64)
    elseif(host_machine MATCHES "^(aarch64|arm64)$")
        set(host_arch arm64)
    endif()
endif()
if(NOT host_arch)
    message(FATAL_ERROR "unsupported host architecture: ${host_machine}")
endif()
if(host_arch STREQUAL "amd64")
    set(other_arch arm64)
else()
    set(other_arch amd64)
endif()

# Every failure case runs with --no-build and a nonexistent build dir, so a
# parser regression fails the test instead of triggering a full build of the
# developer's build/ tree.
set(SAFE_ARGS --no-build --build-dir /nonexistent-tb-build)

expect_exit(0 "help exits 0" --help)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "are required"
    "missing --arch" ${SAFE_ARGS} --version 1.0.0)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "are required"
    "missing --version" ${SAFE_ARGS} --arch ${host_arch})
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "requires a value"
    "missing value for --version" ${SAFE_ARGS} --version)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "requires a value"
    "missing value for --arch" ${SAFE_ARGS} --version 1.0.0 --arch)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "requires a value"
    "missing value for --dist-dir"
    ${SAFE_ARGS} --version 1.0.0 --arch ${host_arch} --dist-dir)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "requires a value"
    "option used as a value for --dist-dir"
    ${SAFE_ARGS} --version 1.0.0 --arch ${host_arch} --dist-dir --no-build)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "unsupported arch"
    "unsupported arch" ${SAFE_ARGS} --version 1.0.0 --arch x86_64)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "invalid version"
    "invalid version characters" ${SAFE_ARGS} --version "1.0*x" --arch ${host_arch})
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "unknown argument"
    "unknown argument" ${SAFE_ARGS} --version 1.0.0 --arch ${host_arch} --bogus)
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "does not match the host architecture"
    "rejects a cross-arch package"
    ${SAFE_ARGS} --version 1.0.0 --arch ${other_arch})
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 2 "must not contain glob characters"
    "rejects glob characters in --build-dir"
    ${SAFE_ARGS} --version 1.0.0 --arch ${host_arch} --build-dir "out[1]")
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 1 "expected binary not found"
    "accepts valid options and checks the binaries before nfpm"
    --version 1.0.0 --arch ${host_arch} --no-build --build-dir /nonexistent-tb-build)

# Build the stub tree through bash so the paths and permissions match what the
# packager sees. A native CMake on Windows would write /tmp/... to the drive
# root instead of the MSYS mount.
set(stub_setup [=[
dir="$(mktemp -d)"
cat > "$dir/torrent_builder" <<'STUB'
#!/bin/sh
printf 'torrent_builder 9.9.9\r\n'
STUB
cat > "$dir/torrent_builder_gui" <<'STUB'
#!/bin/sh
exit 0
STUB
chmod +x "$dir/torrent_builder" "$dir/torrent_builder_gui"
printf '%s\n' "$dir"
]=])
execute_process(
    COMMAND "${TEST_BASH}" -c "${stub_setup}"
    OUTPUT_VARIABLE stub_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE stub_rc)
if(NOT stub_rc EQUAL 0 OR NOT stub_dir)
    message(FATAL_ERROR "could not create the stub build tree")
endif()
expect_exit_message("${TEST_PACKAGING_SCRIPT}" 1
    "reports 'torrent_builder 9.9.9', expected version 1.0.0"
    "rejects a binary reporting another version"
    --version 1.0.0 --arch ${host_arch} --no-build --build-dir "${stub_dir}")
execute_process(COMMAND "${TEST_BASH}" -c "rm -rf -- '${stub_dir}'")

# Rendering case: a fake nfpm captures the generated config and a fake strip
# keeps the shell stubs intact. The assertion lives in bash so the paths are
# the ones the scripts see (native CMake resolves /tmp differently on
# Windows). The build dir mixes #, &, an apostrophe and a backslash so every
# escape branch in package.sh is exercised.
set(render_setup [=[
set -e
script="$1"; arch="$2"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
build="$tmp/out #1 & a'b\\c"
mkdir -p "$build" "$tmp/bin" "$tmp/dist"
cat > "$build/torrent_builder" <<'STUB'
#!/bin/sh
printf 'torrent_builder 1.0.0\r\n'
STUB
cat > "$build/torrent_builder_gui" <<'STUB'
#!/bin/sh
exit 0
STUB
chmod +x "$build/torrent_builder" "$build/torrent_builder_gui"
cat > "$tmp/bin/strip" <<'STUB'
#!/bin/sh
exit 0
STUB
cat > "$tmp/bin/nfpm" <<'STUB'
#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        -f) cp "$2" "$TB_CAPTURE"; shift 2 ;;
        *) shift ;;
    esac
done
exit 0
STUB
chmod +x "$tmp/bin/strip" "$tmp/bin/nfpm"
PATH="$tmp/bin:$PATH" TB_CAPTURE="$tmp/nfpm.yaml" "$script" \
    --version 1.0.0 --arch "$arch" --no-build --build-dir "$build" --dist-dir "$tmp/dist" || true
if [ ! -f "$tmp/nfpm.yaml" ]; then
    echo "the fake nfpm did not capture a config" >&2
    exit 1
fi
yaml_path="$(printf '%s' "$build" | sed "s/'/''/g")"
for binary in torrent_builder torrent_builder_gui; do
    expected="src: '${yaml_path}/${binary}'"
    if ! grep -Fq -- "$expected" "$tmp/nfpm.yaml"; then
        echo "rendered config does not contain: $expected" >&2
        cat "$tmp/nfpm.yaml" >&2
        exit 1
    fi
done
if ! grep -Fq 'version: 1.0.0' "$tmp/nfpm.yaml"; then
    echo "rendered config does not contain the package version" >&2
    cat "$tmp/nfpm.yaml" >&2
    exit 1
fi
]=])
execute_process(
    COMMAND "${TEST_BASH}" -c "${render_setup}" _ "${TEST_PACKAGING_SCRIPT}" "${host_arch}"
    WORKING_DIRECTORY "${workdir}"
    RESULT_VARIABLE render_rc
    OUTPUT_VARIABLE render_out
    ERROR_VARIABLE render_err)
if(NOT render_rc EQUAL 0)
    message(FATAL_ERROR
        "FAIL: renders the build dir as a literal YAML scalar\n${render_out}\n${render_err}")
endif()
message(STATUS "PASS: renders the build dir as a literal YAML scalar")

if(DEFINED TEST_VERIFY_SCRIPT)
    expect_exit_for("${TEST_VERIFY_SCRIPT}" 0 "verify help exits 0" --help)
    expect_exit_message("${TEST_VERIFY_SCRIPT}" 2 "--deb is required" "verify requires --deb")
    expect_exit_message("${TEST_VERIFY_SCRIPT}" 2 "requires a value"
        "verify rejects --deb without a value" --deb)
    expect_exit_message("${TEST_VERIFY_SCRIPT}" 2 "requires a value"
        "verify rejects --rpm without a value" --deb /tmp/torrent-builder.deb --rpm)
    expect_exit_message("${TEST_VERIFY_SCRIPT}" 2 "requires a value"
        "verify rejects --version without a value" --deb /tmp/torrent-builder.deb --version)
endif()
