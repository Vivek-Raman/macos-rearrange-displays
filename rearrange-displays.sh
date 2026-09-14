#!/usr/bin/env bash
# Shortcut entry point. Compiles the native display tool when its source changes.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/Sources/rearrange_displays.c"
BINARY="$SCRIPT_DIR/.build/rearrange-displays"
CONFIG_DIR="$SCRIPT_DIR/config"

if [[ ! -x "$BINARY" || "$SOURCE" -nt "$BINARY" ]]; then
  mkdir -p "${BINARY%/*}"
  xcrun clang -O2 -framework ApplicationServices "$SOURCE" -o "$BINARY"
fi

mkdir -p "$CONFIG_DIR"
exec "$BINARY" --config-dir "$CONFIG_DIR" "$@"
