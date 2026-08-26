#!/bin/bash
# Tests for cmake/libcxx.cmake symlink behavior (issue #326, phase 1).
#
# Strategy: build a fake LIBCXX_ROOT layout in a temp dir, invoke
# cmake/libcxx.cmake from a minimal harness CMakeLists, and assert the
# symlink (or its absence) per scenario.
#
# Each scenario is a function named scenario_<tag>. The dispatcher loop
# below handles all per-test boilerplate (tmpdir, fake root, cmake invoke,
# cleanup) so scenarios only encode their setup tweaks and assertions.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LIBCXX_CMAKE="$REPO_ROOT/cmake/libcxx.cmake"

PASS=0
FAIL=0
FAILED_TESTS=()

# Per-scenario state, populated by the dispatcher before calling the scenario.
TMP=""
LIBCXX=""
LINK_PATH=""
LINK_TARGET=""
HARNESS_RC=0
CURRENT_TAG=""

fail() {
    local msg="$1"
    echo "  FAIL: $msg"
    FAIL=$((FAIL+1))
    FAILED_TESTS+=("$CURRENT_TAG")
    return 0
}

pass() {
    local msg="$1"
    echo "  PASS: $msg"
    PASS=$((PASS+1))
    return 0
}

mtime_of() {
    local path="$1"
    local mtime
    mtime="$(stat -c '%Y' "$path" 2>/dev/null || stat -f '%m' "$path")"
    echo "$mtime"
    return 0
}

# layout: "both" (default; a superset safe for the symlink-behavior
# scenarios, which don't care which host-branch resolves) | "triple" (ONLY
# the Linux/amd64 per-triple layout — no flat build/lib/libc++.dylib-style
# dir) | "flat" (ONLY the macOS-style flat layout — no triple subdirectory).
# The narrower layouts let a host-branch scenario prove resolution picked
# the RIGHT one: if the logic resolved the other layout instead, the
# now-validating cmake/clang_p2996_host.cmake would FATAL_ERROR on a path
# that was never created here, rather than the test passing by accident on
# an overlapping fixture.
make_fake_libcxx_root() {
    local root="$1" layout="${2:-both}"
    mkdir -p "$root/build/modules/c++/v1" "$root/build/include/c++/v1"
    : > "$root/build/modules/c++/v1/std.cppm"
    : > "$root/build/modules/c++/v1/std.compat.cppm"
    if [[ "$layout" == "triple" || "$layout" == "both" ]]; then
        mkdir -p "$root/build/include/x86_64-unknown-linux-gnu/c++/v1" \
                 "$root/build/lib/x86_64-unknown-linux-gnu"
    fi
    if [[ "$layout" == "flat" ]]; then
        # Flat layout only — no triple subdirectory at all.
        mkdir -p "$root/build/lib"
    fi
    return 0
}

# Scenarios may set these (in their pre_<tag> hook, which runs BEFORE the
# fake root is built) to force a host other than this machine's real one —
# so both the Linux/amd64 and Darwin/arm64 branches, and the error path, are
# exercisable regardless of which host actually runs this script — and to
# pick a narrower fake-root layout than the "both" default.
EXTRA_CMAKE_SETUP=""
FAKE_ROOT_LAYOUT="both"

run_harness() {
    local workdir="$1" libcxx_root="$2"
    mkdir -p "$workdir"
    cat > "$workdir/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.30)
project(libcxx_symlink_harness NONE)
set(LIBCXX_ROOT "$libcxx_root")
$EXTRA_CMAKE_SETUP
# Includes the SAME production file CMakeLists.txt and cmake/libcxx.cmake
# use — not a re-implementation — so these scenarios exercise the real
# host-layout resolution logic, including its EXISTS validation.
include("$LIBCXX_CMAKE")
message(STATUS "STORM_TEST_LIB_DIR=\${_storm_libcxx_lib_dir}")
message(STATUS "STORM_TEST_BUILD_INCLUDE_DIR=\${LIBCXX_BUILD_INCLUDE_DIR}")
message(STATUS "STORM_TEST_MODULES_JSON=\${_storm_libcxx_modules_json}")
EOF
    local rc=0
    (cd "$workdir" && cmake -B build . > cmake.log 2>&1) || rc=$?
    return "$rc"
}

# Dispatcher: calls pre_<tag> (optional setup the scenario wants — may set
# EXTRA_CMAKE_SETUP / FAKE_ROOT_LAYOUT — BEFORE the fake root and cmake run),
# builds the fake root, runs cmake, then calls the scenario for assertions.
run_scenario() {
    local tag="$1"
    CURRENT_TAG="$tag"
    echo "TEST: $tag"

    TMP="$(mktemp -d)"
    LIBCXX="$TMP/libcxx"
    LINK_PATH="$LIBCXX/build/share/libc++/v1"
    LINK_TARGET="$LIBCXX/build/modules/c++/v1"

    EXTRA_CMAKE_SETUP=""
    FAKE_ROOT_LAYOUT="both"
    if declare -F "pre_$tag" > /dev/null; then
        "pre_$tag"
    fi
    make_fake_libcxx_root "$LIBCXX" "$FAKE_ROOT_LAYOUT"

    HARNESS_RC=0
    run_harness "$TMP/harness" "$LIBCXX" || HARNESS_RC=$?

    "scenario_$tag"

    rm -rf "$TMP"
    return 0
}

# ---- scenarios ------------------------------------------------------------

scenario_creates_symlink_when_missing() {
    if [[ $HARNESS_RC -ne 0 ]]; then
        fail "cmake configure failed; see $TMP/harness/cmake.log"; return
    fi
    if [[ ! -L "$LINK_PATH" ]]; then
        fail "expected symlink at $LINK_PATH, but it does not exist"; return
    fi
    local target
    target="$(readlink "$LINK_PATH")"
    if [[ "$target" != "$LINK_TARGET" ]]; then
        fail "symlink points to '$target', expected '$LINK_TARGET'"; return
    fi
    if [[ ! -f "$LINK_PATH/std.cppm" ]]; then
        fail "symlink does not resolve std.cppm"; return
    fi
    pass "symlink created and resolves correctly"
}

pre_idempotent_when_symlink_exists() {
    mkdir -p "$LIBCXX/build/share/libc++"
    ln -sfn "$LINK_TARGET" "$LINK_PATH"
    SAVED_MTIME="$(mtime_of "$LINK_PATH")"
    return 0
}

scenario_idempotent_when_symlink_exists() {
    if [[ $HARNESS_RC -ne 0 ]]; then
        fail "cmake configure failed; see $TMP/harness/cmake.log"; return
    fi
    local after
    after="$(mtime_of "$LINK_PATH")"
    if [[ "$SAVED_MTIME" != "$after" ]]; then
        fail "existing correct symlink was modified (mtime changed)"; return
    fi
    pass "existing correct symlink was left untouched"
}

pre_refuses_when_share_is_real_dir() {
    mkdir -p "$LINK_PATH"
    : > "$LINK_PATH/keep.txt"
    return 0
}

scenario_refuses_when_share_is_real_dir() {
    # cmake may or may not FATAL_ERROR here, but it MUST NOT clobber the dir.
    if [[ -L "$LINK_PATH" ]]; then
        fail "real directory was replaced with a symlink"; return
    fi
    if [[ ! -f "$LINK_PATH/keep.txt" ]]; then
        fail "existing content under share/libc++/v1 was destroyed"; return
    fi
    pass "real directory at share/libc++/v1 was preserved"
}

# Host branch scenarios below force CMAKE_HOST_SYSTEM_NAME/_storm_host_arch
# via EXTRA_CMAKE_SETUP rather than relying on the runner's real host, so
# both branches (and the error path) are exercised regardless of whether
# this script runs on macOS/arm64 or Linux/amd64.

grep_test_var() {
    local varname="$1"
    grep -o "STORM_TEST_${varname}=.*" "$TMP/harness/cmake.log" \
        | tail -1 | sed "s/^STORM_TEST_${varname}=//"
}

# Shared assertion for the two host-branch scenarios below: cmake must have
# succeeded, and _storm_libcxx_lib_dir/LIBCXX_BUILD_INCLUDE_DIR/
# _storm_libcxx_modules_json must resolve to exactly the expected triple
# (include dir "" means "unset, no duplicate -I"). Each scenario's fake root
# contains ONLY its own layout (see FAKE_ROOT_LAYOUT in its pre_ hook) — if
# resolution picked the wrong branch, cmake/clang_p2996_host.cmake's own
# EXISTS validation would FATAL_ERROR on a path this fixture never created,
# rather than the test passing by accident on an overlapping fixture.
assert_resolved_paths() {
    local expected_lib_dir="$1" expected_include_dir="$2" expected_json="$3" ok_msg="$4"
    if [[ $HARNESS_RC -ne 0 ]]; then
        fail "cmake configure failed; see $TMP/harness/cmake.log"; return
    fi
    local lib_dir include_dir json
    lib_dir="$(grep_test_var LIB_DIR)"
    include_dir="$(grep_test_var BUILD_INCLUDE_DIR)"
    json="$(grep_test_var MODULES_JSON)"
    if [[ "$lib_dir" != "$expected_lib_dir" ]]; then
        fail "lib dir resolved to '$lib_dir', expected '$expected_lib_dir'"; return
    fi
    if [[ "$include_dir" != "$expected_include_dir" ]]; then
        fail "include dir resolved to '$include_dir', expected '$expected_include_dir'"; return
    fi
    if [[ "$json" != "$expected_json" ]]; then
        fail "modules.json resolved to '$json', expected '$expected_json'"; return
    fi
    pass "$ok_msg"
}

pre_resolves_linux_amd64_triple_paths() {
    EXTRA_CMAKE_SETUP='set(CMAKE_HOST_SYSTEM_NAME "Linux")
set(_storm_host_arch "x86_64")'
    FAKE_ROOT_LAYOUT="triple"
    return 0
}

scenario_resolves_linux_amd64_triple_paths() {
    assert_resolved_paths \
        "$LIBCXX/build/lib/x86_64-unknown-linux-gnu" \
        "$LIBCXX/build/include/x86_64-unknown-linux-gnu/c++/v1" \
        "$LIBCXX/build/lib/x86_64-unknown-linux-gnu/libc++.modules.json" \
        "Linux/amd64 resolves the triple-subdirectory layout"
}

pre_resolves_darwin_arm64_flat_paths() {
    EXTRA_CMAKE_SETUP='set(CMAKE_HOST_SYSTEM_NAME "Darwin")
set(_storm_host_arch "arm64")'
    FAKE_ROOT_LAYOUT="flat"
    return 0
}

scenario_resolves_darwin_arm64_flat_paths() {
    assert_resolved_paths \
        "$LIBCXX/build/lib" \
        "" \
        "$LIBCXX/build/lib/libc++.modules.json" \
        "Darwin/arm64 resolves the flat layout with no duplicate -I"
}

pre_rejects_unsupported_host() {
    EXTRA_CMAKE_SETUP='set(CMAKE_HOST_SYSTEM_NAME "Windows")
set(_storm_host_arch "x86_64")'
    return 0
}

scenario_rejects_unsupported_host() {
    if [[ $HARNESS_RC -eq 0 ]]; then
        fail "cmake configure succeeded for an unsupported host; expected FATAL_ERROR"; return
    fi
    if ! grep -q "Unsupported host for clang-p2996" "$TMP/harness/cmake.log"; then
        fail "expected a clear 'Unsupported host' error; see $TMP/harness/cmake.log"; return
    fi
    pass "unsupported host fails configure with a clear error"
}

# ---- run ------------------------------------------------------------------

for tag in \
    creates_symlink_when_missing \
    idempotent_when_symlink_exists \
    refuses_when_share_is_real_dir \
    resolves_linux_amd64_triple_paths \
    resolves_darwin_arm64_flat_paths \
    rejects_unsupported_host
do
    run_scenario "$tag"
done

echo
echo "Results: $PASS passed, $FAIL failed"
if [[ $FAIL -gt 0 ]]; then
    echo "Failed tests: ${FAILED_TESTS[*]}"
    exit 1
fi
exit 0
