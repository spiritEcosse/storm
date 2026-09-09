#!/bin/bash
# SessionStart hook. Two independent jobs, and the ORDER between them matters:
#
# 1. Wire git's pre-commit hook (issue #651) — UNCONDITIONAL, before every
#    early exit below. `core.hooksPath` is local config that a clone never
#    carries, and the only other place that sets it is CMakeLists.txt, as a
#    side effect of a successful cmake configure — exactly what a session with
#    no ../clang-p2996 cannot do. Such a session therefore committed with
#    commit.sh (format, clang-tidy, tests, the 100% coverage gate, and the two
#    self-tests it runs even for an otherwise no-op commit — #543, #550) never
#    firing, and with nothing in the output to say so. Moving any exit below
#    above this reintroduces that silent gap, which is why each exit path has
#    its own scenario in scripts/tests/test_session_start_hook.sh.
#
# 2. Provision scripts/dev-container.sh's Docker build environment (issue
#    #628): Claude Code's remote/sandboxed sessions start with no local
#    ../clang-p2996 toolchain, so no CMake preset can configure, build, run
#    tests, or run clang-format/clang-tidy without it. Provisioned in the
#    background, so it's ready by the time a session wants to build.
#
#    No-op when:
#      - not a remote session (only remote sessions lack the toolchain locally)
#      - ../clang-p2996 is already present (nothing to provision)
#      - docker isn't installed (nothing this hook can do)
#
# See scripts/dev-container.sh for the up/exec/status/down commands this
# provisions, and CLAUDE.md's Prerequisites section for how a session is
# expected to use it once ready.

set -euo pipefail

# CLAUDE_PROJECT_DIR is set by Claude Code for hooks; fall back to this
# script's own location so manual invocation (e.g. this skill's own
# validation step) doesn't fail on an unbound variable.
project_dir="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"

# --- 1. Pre-commit hook wiring (#651) ---
#
# Quiet on success: stdout is Claude Code's protocol channel (the async JSON
# below) and session context, not a log. A FAILURE is loud, because a missing
# gate is otherwise indistinguishable from a passing one — but it does not fail
# session start, since nothing else here depends on it.
if ! git -C "$project_dir" config core.hooksPath .githooks 2>/dev/null; then
    echo "session-start: could not set core.hooksPath in $project_dir —" \
         "commits will NOT run commit.sh (see .githooks/pre-commit)" >&2
fi

# --- 2. Docker dev-container provisioning (#628) ---

if [[ "${CLAUDE_CODE_REMOTE:-}" != "true" ]]; then
    exit 0
fi

if [[ -e "$project_dir/../clang-p2996" ]]; then
    exit 0
fi

if ! command -v docker >/dev/null 2>&1; then
    exit 0
fi

# The image build (Manjaro base + pacman -Syu + package install) can take
# several minutes on a cold cache — async so session start isn't blocked on
# it. dev-container.sh's `up` serializes on a flock, so a build/test command
# run before this finishes just waits on the same provisioning via its own
# `exec` call rather than racing it.
echo '{"async": true, "asyncTimeout": 600000}'

"$project_dir/scripts/dev-container.sh" up >>/tmp/storm-dev-container-setup.log 2>&1 || true
