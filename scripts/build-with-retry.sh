#!/bin/bash
# Build with retry: clang-scan-deps may segfault (known C++26 module issue).
# Clears module cache and retries up to 3 times.
# Usage: ./scripts/build-with-retry.sh <preset>

set -uo pipefail

PRESET="${1:?Usage: build-with-retry.sh <preset>}"

for attempt in 1 2 3; do
    echo "=== Build attempt $attempt ==="
    if cmake --build --preset "$PRESET"; then
        echo "Build succeeded on attempt $attempt"
        exit 0
    fi
    echo "Build failed on attempt $attempt, clearing caches..."
    # Clear .ddi scan-deps outputs so ninja re-scans. Leave *.pcm files in
    # build/*/module-cache/ intact — those are the authoritative per-preset
    # module cache (pinned via -fmodules-cache-path in root CMakeLists.txt)
    # and deleting them forces builtin modules to be rebuilt on every retry,
    # which aggravates the very scan-deps flakiness this retry is working
    # around. Only prune stray PCMs outside module-cache (there shouldn't be
    # any in a healthy build, but clean them up defensively).
    find build/ -name "*.pcm" -not -path "*/module-cache/*" -delete 2>/dev/null || true
    find build/ -name "*.ddi" -delete 2>/dev/null || true
done
echo "Build failed after 3 attempts"
exit 1
