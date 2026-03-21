#!/usr/bin/env bash
# RTK selective hook — only rewrites: git log, find, tree, ls
# All other commands pass through unchanged.

set -euo pipefail

INPUT=$(cat)
COMMAND=$(echo "$INPUT" | jq -r '.command // empty' 2>/dev/null) || exit 0

if [[ -z "$COMMAND" ]]; then
    exit 0
fi

rewrite=""
case "$COMMAND" in
    git\ log*)  rewrite="rtk $COMMAND" ;;
    find\ *)    rewrite="rtk $COMMAND" ;;
    tree*)      rewrite="rtk $COMMAND" ;;
    ls\ *|ls)   rewrite="rtk $COMMAND" ;;
esac

if [[ -n "$rewrite" ]]; then
    echo "$INPUT" | jq --arg cmd "$rewrite" '.command = $cmd'
else
    exit 0
fi
