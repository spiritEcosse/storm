#!/bin/bash
# Install system dependencies for CI.
# Usage: ./scripts/ci-install-deps.sh [--with-lcov]

set -euo pipefail

pacman -Syu --noconfirm

PACKAGES=(cmake ninja gcc sqlite postgresql-libs python python-yaml git skopeo umoci)

if [[ "${1:-}" == "--with-lcov" ]]; then
    PACKAGES+=(lcov)
fi

pacman -S --noconfirm --needed "${PACKAGES[@]}"
