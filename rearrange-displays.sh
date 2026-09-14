#!/usr/bin/env bash
# Shortcut entry point for the checked-in native display tool.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/.build/rearrange-displays"
CONFIG_DIR="$SCRIPT_DIR/config"

if [[ ! -x "$BINARY" ]]; then
  echo "Missing bundled display tool: $BINARY" >&2
  exit 1
fi

mkdir -p "$CONFIG_DIR"
exec "$BINARY" --config-dir "$CONFIG_DIR" "$@"
