#!/bin/bash
# Create the `${sourceDir}/../clang-p2996` symlink Storm's CMakePresets expects.
# Idempotent — safe to run on every container start.

set -euo pipefail

parent=$(dirname "$PWD")
target="$parent/clang-p2996"

if [[ ! -e "$target" ]]; then
    ln -s /opt/clang-p2996 "$target"
fi

exec "$@"
