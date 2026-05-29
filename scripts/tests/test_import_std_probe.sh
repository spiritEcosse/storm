#!/bin/bash
# Tests for the import-std probe target (issue #326, phase 2).
#
# Builds the probe target with ENABLE_IMPORT_STD_PROBE=ON in an
# isolated build dir, runs it, and asserts the expected stdout.
# Skipped silently when LIBCXX_ROOT is unset (CI matrix safety).

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROBE_TARGET="storm_import_std_probe"
EXPECTED="import std: ok"

PASS=0
FAIL=0
FAILED_TESTS=()
CURRENT_TAG=""

fail() {
    local message="$1"
    echo "  FAIL: $message"
    FAIL=$((FAIL+1))
    FAILED_TESTS+=("$CURRENT_TAG")
    return 0
}

pass() {
    local message="$1"
    echo "  PASS: $message"
    PASS=$((PASS+1))
    return 0
}

resolve_libcxx_root() {
    if [[ -n "${LIBCXX_ROOT:-}" ]]; then
        echo "$LIBCXX_ROOT"
        return
    fi
    # Fall back to the project's conventional location (../clang-p2996).
    local candidate
    candidate="$(cd "$REPO_ROOT/.." && pwd)/clang-p2996"
    if [[ -d "$candidate/build/modules/c++/v1" ]]; then
        echo "$candidate"
    fi
}

configure_and_build() {
    local workdir="$1" libcxx_root="$2"
    mkdir -p "$workdir"
    cmake -S "$REPO_ROOT" -B "$workdir" \
        -G Ninja \
        -DCMAKE_C_COMPILER="$libcxx_root/build/bin/clang" \
        -DCMAKE_CXX_COMPILER="$libcxx_root/build/bin/clang++" \
        -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$libcxx_root/build/bin/clang-scan-deps" \
        -DLIBCXX_ROOT="$libcxx_root" \
        -DENABLE_IMPORT_STD_PROBE=ON \
        -DENABLE_TESTS=OFF \
        -DENABLE_BENCH=OFF \
        -DENABLE_COVERAGE=OFF \
        -DENABLE_TOOLS=OFF \
        > "$workdir/cmake.log" 2>&1 || return 1
    cmake --build "$workdir" --target "$PROBE_TARGET" \
        > "$workdir/build.log" 2>&1 || return 2
}

test_probe_builds_and_runs() {
    CURRENT_TAG="probe_builds_and_runs"
    echo "TEST: $CURRENT_TAG"

    local libcxx_root
    libcxx_root="$(resolve_libcxx_root)"
    if [[ -z "$libcxx_root" ]]; then
        echo "  SKIP: LIBCXX_ROOT not set and ../clang-p2996 not present"
        return
    fi

    local workdir
    workdir="$(mktemp -d)"
    trap "rm -rf $workdir" RETURN

    local rc=0
    configure_and_build "$workdir" "$libcxx_root" || rc=$?
    if [[ $rc -ne 0 ]]; then
        fail "configure/build failed (rc=$rc); see $workdir/{cmake,build}.log"
        cp -r "$workdir" /tmp/import_std_probe_failure || true
        echo "  (logs copied to /tmp/import_std_probe_failure)"
        return
    fi

    local bin
    bin="$(find "$workdir" -name "$PROBE_TARGET" -type f -executable | head -1)"
    if [[ -z "$bin" ]]; then
        fail "probe binary not found under $workdir"
        return
    fi

    local out
    out="$("$bin")" || { fail "probe exited non-zero: $out"; return; }
    if [[ "$out" != "$EXPECTED" ]]; then
        fail "probe stdout mismatch: got '$out', expected '$EXPECTED'"
        return
    fi

    pass "probe configured, built, and printed expected output"
}

test_probe_builds_and_runs

echo
echo "Results: $PASS passed, $FAIL failed"
if [[ $FAIL -gt 0 ]]; then
    echo "Failed tests: ${FAILED_TESTS[*]}"
    exit 1
fi
exit 0
