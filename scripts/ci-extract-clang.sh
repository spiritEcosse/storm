#!/bin/bash
# Extract clang-p2996 from container image (CI only).
# Requires: skopeo, umoci, CLANG_IMAGE env var.

set -euo pipefail

IMAGE="${CLANG_IMAGE:?CLANG_IMAGE env var required}"

# Extract scratch image without Docker daemon (not available inside container)
skopeo copy "docker://${IMAGE}" oci:clang-oci:latest
umoci unpack --image clang-oci:latest clang-bundle
mv clang-bundle/rootfs/opt/clang-p2996 /opt/clang-p2996

# Create clang/clang++ symlinks
ln -s clang-21 /opt/clang-p2996/build/bin/clang
ln -s clang-21 /opt/clang-p2996/build/bin/clang++

# Symlink so ${sourceDir}/../clang-p2996 resolves to /opt/clang-p2996
parent=$(dirname "${GITHUB_WORKSPACE:-.}")
mkdir -p "$parent"
ln -s /opt/clang-p2996 "$parent/clang-p2996"

# Cleanup
rm -rf clang-oci clang-bundle
