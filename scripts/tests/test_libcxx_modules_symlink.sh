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

make_fake_libcxx_root() {
    local root="$1"
    mkdir -p "$root/build/modules/c++/v1" \
             "$root/build/include/c++/v1" \
             "$root/build/include/x86_64-unknown-linux-gnu/c++/v1" \
             "$root/build/lib/x86_64-unknown-linux-gnu"
    : > "$root/build/modules/c++/v1/std.cppm"
    : > "$root/build/modules/c++/v1/std.compat.cppm"
    return 0
}

run_harness() {
    local workdir="$1" libcxx_root="$2"
    mkdir -p "$workdir"
    cat > "$workdir/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.30)
project(libcxx_symlink_harness NONE)
set(LIBCXX_ROOT "$libcxx_root")
include("$LIBCXX_CMAKE")
EOF
    local rc=0
    (cd "$workdir" && cmake -B build . > cmake.log 2>&1) || rc=$?
    return "$rc"
}

# Dispatcher: prepares tmpdir + fake root, calls hook_pre (optional setup
# the scenario wants before cmake runs), runs cmake, then calls the scenario
# function for assertions.
run_scenario() {
    local tag="$1"
    CURRENT_TAG="$tag"
    echo "TEST: $tag"

    TMP="$(mktemp -d)"
    LIBCXX="$TMP/libcxx"
    LINK_PATH="$LIBCXX/build/share/libc++/v1"
    LINK_TARGET="$LIBCXX/build/modules/c++/v1"
    make_fake_libcxx_root "$LIBCXX"

    if declare -F "pre_$tag" > /dev/null; then
        "pre_$tag"
    fi

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

# ---- run ------------------------------------------------------------------

for tag in \
    creates_symlink_when_missing \
    idempotent_when_symlink_exists \
    refuses_when_share_is_real_dir
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
