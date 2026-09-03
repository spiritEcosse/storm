#!/bin/bash
# SessionStart hook (issue #628): Claude Code's remote/sandboxed sessions
# start with no local ../clang-p2996 toolchain, so no CMake preset can
# configure, build, run tests, or run clang-format/clang-tidy without it.
# This hook provisions scripts/dev-container.sh's Docker build environment in
# the background, so it's ready by the time a session wants to build.
#
# No-op when:
#   - not a remote session (only remote sessions lack the toolchain locally)
#   - ../clang-p2996 is already present (nothing to provision)
#   - docker isn't installed (nothing this hook can do)
#
# See scripts/dev-container.sh for the up/exec/status/down commands this
# provisions, and CLAUDE.md's Prerequisites section for how a session is
# expected to use it once ready.

set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

if [ -e "$CLAUDE_PROJECT_DIR/../clang-p2996" ]; then
    exit 0
fi

if ! command -v docker >/dev/null 2>&1; then
    exit 0
fi

# The image build (Manjaro base + pacman -Syu + package install) can take
# several minutes on a cold cache — async so session start isn't blocked on
# it. dev-container.sh is idempotent, so a build/test command run before this
# finishes just waits on the same containers via its own `exec` call.
echo '{"async": true, "asyncTimeout": 600000}'

"$CLAUDE_PROJECT_DIR/scripts/dev-container.sh" up >>/tmp/storm-dev-container-setup.log 2>&1 || true
