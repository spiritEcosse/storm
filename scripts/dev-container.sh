#!/bin/bash
# Docker-based build environment for sessions with no local ../clang-p2996
# toolchain (Claude Code remote/sandboxed sessions, or any contributor
# without a self-built clang-p2996). Wraps docker/ci/Dockerfile(.sandbox)
# into a long-lived build container plus a PostgreSQL sidecar; run build
# commands via `exec` instead of natively. See issue #628 and CLAUDE.md's
# Prerequisites section.
#
# Usage:
#   scripts/dev-container.sh up             # start/reuse the containers
#   scripts/dev-container.sh exec <cmd...>  # run a command in the build container
#   scripts/dev-container.sh status         # show container state
#   scripts/dev-container.sh down           # stop and remove the containers
#
# Example:
#   scripts/dev-container.sh exec cmake --preset ninja-debug
#   scripts/dev-container.sh exec cmake --build --preset ninja-debug
#   scripts/dev-container.sh exec ./build/debug/tests/storm_tests
#   scripts/dev-container.sh exec bash -lc 'cmake --preset ninja-debug && cmake --build --preset ninja-debug'
#
# The repo is bind-mounted at the SAME absolute path inside the container as
# on the host, so anything that records that path (CMakeCache.txt, git's
# core.hooksPath) stays valid whether a later step runs on the host or via
# `exec` — see issue #628's step-6 gotcha, which is what this sidesteps.
#
# The build container talks to Postgres at 127.0.0.1 because the sidecar
# shares its network namespace (`--network container:...`).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_CONTAINER="storm-dev-build"
PG_CONTAINER="storm-dev-pg"
PG_IMAGE="postgres:17"
CA_BUNDLE="/root/.ccr/ca-bundle.crt"

export STORM_PG_CONNSTR="host=127.0.0.1 port=5432 dbname=storm_db user=storm_db password=storm_db"

native_toolchain_present() {
    [[ -e "$REPO_ROOT/../clang-p2996" ]]
}

ensure_dockerd() {
    if docker info >/dev/null 2>&1; then
        return
    fi
    echo "dev-container: starting dockerd..."
    nohup dockerd >/tmp/storm-dev-dockerd.log 2>&1 &
    disown
    for _ in $(seq 1 30); do
        docker info >/dev/null 2>&1 && return
        sleep 1
    done
    echo "dev-container: dockerd did not become ready — see /tmp/storm-dev-dockerd.log" >&2
    return 1
}

image_tag() {
    if [[ -r "$CA_BUNDLE" ]]; then
        echo "storm-ci:sandbox"
    else
        echo "storm-ci:dev"
    fi
}

build_image() {
    local tag
    tag="$(image_tag)"
    if docker image inspect "$tag" >/dev/null 2>&1; then
        return
    fi
    echo "dev-container: building $tag (first run only, this can take a few minutes)..."
    if [[ -r "$CA_BUNDLE" ]]; then
        docker build --build-context cacerts="$(dirname "$CA_BUNDLE")" \
            -t "$tag" -f "$REPO_ROOT/docker/ci/Dockerfile.sandbox" "$REPO_ROOT"
    else
        docker build -t "$tag" -f "$REPO_ROOT/docker/ci/Dockerfile" "$REPO_ROOT"
    fi
}

start_build_container() {
    if docker container inspect "$BUILD_CONTAINER" >/dev/null 2>&1; then
        docker start "$BUILD_CONTAINER" >/dev/null
        return
    fi
    docker run -d --name "$BUILD_CONTAINER" \
        -v "$REPO_ROOT:$REPO_ROOT" -w "$REPO_ROOT" \
        "$(image_tag)" sleep infinity >/dev/null
}

start_pg_container() {
    if docker container inspect "$PG_CONTAINER" >/dev/null 2>&1; then
        docker start "$PG_CONTAINER" >/dev/null
    else
        docker run -d --name "$PG_CONTAINER" \
            --network "container:$BUILD_CONTAINER" \
            -e POSTGRES_DB=storm_db -e POSTGRES_USER=storm_db -e POSTGRES_PASSWORD=storm_db \
            "$PG_IMAGE" >/dev/null
    fi
    for _ in $(seq 1 30); do
        docker exec "$PG_CONTAINER" pg_isready -U storm_db >/dev/null 2>&1 && return
        sleep 1
    done
    echo "dev-container: postgres did not become ready in time" >&2
}

cmd_up() {
    if native_toolchain_present; then
        echo "dev-container: ../clang-p2996 already present natively, nothing to do"
        return
    fi
    ensure_dockerd
    build_image
    start_build_container
    start_pg_container
    echo "dev-container: ready — STORM_PG_CONNSTR=\"$STORM_PG_CONNSTR\""
}

cmd_exec() {
    [[ $# -gt 0 ]] || { echo "Usage: $0 exec <cmd...>" >&2; exit 1; }
    if native_toolchain_present; then
        echo "dev-container: ../clang-p2996 present natively — run commands directly instead of via exec" >&2
        exit 1
    fi
    cmd_up
    docker exec -w "$REPO_ROOT" -e STORM_PG_CONNSTR="$STORM_PG_CONNSTR" \
        "$BUILD_CONTAINER" "$@"
}

cmd_status() {
    docker ps -a --filter "name=$BUILD_CONTAINER" --filter "name=$PG_CONTAINER"
}

cmd_down() {
    docker rm -f "$PG_CONTAINER" "$BUILD_CONTAINER" >/dev/null 2>&1 || true
}

action="${1:-}"
[[ $# -gt 0 ]] && shift
case "$action" in
    up) cmd_up ;;
    exec) cmd_exec "$@" ;;
    status) cmd_status ;;
    down) cmd_down ;;
    *)
        echo "Usage: $0 {up|exec <cmd...>|status|down}" >&2
        exit 1
        ;;
esac
