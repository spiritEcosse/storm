#!/bin/bash
# Docker-based build environment for sessions with no local ../clang-p2996
# toolchain (Claude Code remote/sandboxed sessions, or any contributor
# without a self-built clang-p2996). Wraps docker/ci/Dockerfile into a
# long-lived build container plus a PostgreSQL sidecar; run build commands
# via `exec` instead of natively. See issue #628 and CLAUDE.md's
# Prerequisites section.
#
# Usage:
#   scripts/dev-container.sh up             # start/reuse the containers
#   scripts/dev-container.sh exec <cmd...>  # run a command in the build container
#   scripts/dev-container.sh status         # show container state
#   scripts/dev-container.sh rebuild        # drop the image + containers, rebuild from scratch
#   scripts/dev-container.sh down           # stop and remove the containers
#
# Example:
#   scripts/dev-container.sh exec cmake --preset ninja-debug
#   scripts/dev-container.sh exec cmake --build --preset ninja-debug
#   scripts/dev-container.sh exec ./build/debug/tests/storm_tests
#   scripts/dev-container.sh exec ./commit.sh
#   scripts/dev-container.sh exec bash -lc 'cmake --preset ninja-debug && cmake --build --preset ninja-debug'
#
# `exec` falls through to running the command natively (no Docker at all)
# when ../clang-p2996 is already present, so callers can prefix every build
# command with it unconditionally and it does the right thing either way.
#
# The repo is bind-mounted at the SAME absolute path inside the container as
# on the host, so anything that records that path (CMakeCache.txt, git's
# core.hooksPath) stays valid whether a later step runs on the host or via
# `exec` — see issue #628's step-6 gotcha, which is what this sidesteps. That
# also means `git commit` works via `exec git commit -m ...` (or `exec
# ./commit.sh` directly) — do NOT run `git commit` on the host in a session
# with no native toolchain; the pre-commit hook needs the compiler.
#
# Postgres is reached over a shared unix-socket volume
# (storm-dev-pgsock:/var/run/postgresql on both containers), matching
# CMakePresets.json's own `host=/var/run/postgresql` testPreset value
# exactly — ctest/coverage presets need no override. A shared volume (rather
# than sharing a network namespace) means the two containers can be stopped,
# restarted, and recreated independently without ever silently breaking
# connectivity between them.
#
# `up` (and therefore `exec`, which calls it) is safe to call concurrently —
# it serializes on a flock, so a second caller waits for the first to finish
# provisioning rather than racing it.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCKERFILE="$REPO_ROOT/docker/ci/Dockerfile"
BUILD_CONTAINER="storm-dev-build"
PG_CONTAINER="storm-dev-pg"
PG_VOLUME="storm-dev-pgsock"
PG_IMAGE="postgres:17"
CA_BUNDLE="/root/.ccr/ca-bundle.crt"
LOCK_FILE="/tmp/storm-dev-container.lock"

export STORM_PG_CONNSTR="host=/var/run/postgresql dbname=storm_db user=storm_db"

native_toolchain_present() {
    [[ -e "$REPO_ROOT/../clang-p2996" ]]
}

retry() {
    local attempts="$1" delay=2 n=1
    shift
    until "$@"; do
        if (( n >= attempts )); then
            return 1
        fi
        echo "dev-container: '$*' failed (attempt $n/$attempts) — retrying in ${delay}s..." >&2
        sleep "$delay"
        delay=$(( delay * 2 ))
        n=$(( n + 1 ))
    done
}

ensure_dockerd() {
    if docker info >/dev/null 2>&1; then
        return
    fi
    echo "dev-container: starting dockerd..." >&2
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
    echo "dev-container: building $tag (first run only, this can take a few minutes)..." >&2
    if [[ -r "$CA_BUNDLE" ]]; then
        retry 3 docker build --build-context cacerts="$(dirname "$CA_BUNDLE")" \
            -t "$tag" -f "$DOCKERFILE" "$REPO_ROOT"
    else
        retry 3 docker build -t "$tag" -f "$DOCKERFILE" "$REPO_ROOT"
    fi
}

start_build_container() {
    if docker container inspect "$BUILD_CONTAINER" >/dev/null 2>&1; then
        docker start "$BUILD_CONTAINER" >/dev/null
        return
    fi
    docker run -d --name "$BUILD_CONTAINER" \
        -v "$REPO_ROOT:$REPO_ROOT" -v "$PG_VOLUME:/var/run/postgresql" -w "$REPO_ROOT" \
        "$(image_tag)" sleep infinity >/dev/null
}

start_pg_container() {
    docker volume create "$PG_VOLUME" >/dev/null
    if docker container inspect "$PG_CONTAINER" >/dev/null 2>&1; then
        docker start "$PG_CONTAINER" >/dev/null
    else
        retry 3 docker pull "$PG_IMAGE"
        docker run -d --name "$PG_CONTAINER" \
            -v "$PG_VOLUME:/var/run/postgresql" \
            -e POSTGRES_DB=storm_db -e POSTGRES_USER=storm_db -e POSTGRES_PASSWORD=storm_db \
            "$PG_IMAGE" >/dev/null
    fi
    for _ in $(seq 1 30); do
        docker exec "$PG_CONTAINER" pg_isready -U storm_db >/dev/null 2>&1 && return
        sleep 1
    done
    echo "dev-container: postgres did not become ready in time" >&2
    return 1
}

cmd_up() {
    if native_toolchain_present; then
        echo "dev-container: ../clang-p2996 already present natively, nothing to do" >&2
        return
    fi
    (
        flock -w 900 9 || { echo "dev-container: timed out waiting on another 'up'" >&2; exit 1; }
        ensure_dockerd
        build_image
        start_build_container
        start_pg_container
    ) 9>"$LOCK_FILE"
    echo "dev-container: ready — STORM_PG_CONNSTR=\"$STORM_PG_CONNSTR\"" >&2
}

cmd_exec() {
    [[ $# -gt 0 ]] || { echo "Usage: $0 exec <cmd...>" >&2; exit 1; }
    if native_toolchain_present; then
        exec "$@"
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

cmd_rebuild() {
    cmd_down
    docker rmi -f "$(image_tag)" >/dev/null 2>&1 || true
    cmd_up
}

action="${1:-}"
[[ $# -gt 0 ]] && shift
case "$action" in
    up) cmd_up ;;
    exec) cmd_exec "$@" ;;
    status) cmd_status ;;
    rebuild) cmd_rebuild ;;
    down) cmd_down ;;
    *)
        echo "Usage: $0 {up|exec <cmd...>|status|rebuild|down}" >&2
        exit 1
        ;;
esac
