#!/usr/bin/env bash
set -euo pipefail

VCPKG_ROOT="${VCPKG_ROOT:-/home/nicollas/vcpkg}"

if [[ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    echo "ERROR: vcpkg not found at $VCPKG_ROOT" >&2
    echo "Set VCPKG_ROOT env var or edit this script." >&2
    exit 1
fi

BASELINE=$(grep -o '"builtin-baseline": *"[^"]*"' vcpkg.json | grep -o '[0-9a-f]\{40\}')

if [[ -z "$BASELINE" ]]; then
    echo "ERROR: could not read builtin-baseline from vcpkg.json" >&2
    exit 1
fi

echo "==> Fetching vcpkg..."
git -C "$VCPKG_ROOT" fetch origin

echo "==> Checking out baseline: $BASELINE"
git -C "$VCPKG_ROOT" checkout "$BASELINE"

PRESET="${1:-linux-release}"
echo "==> Configuring cmake (preset: $PRESET)..."
VCPKG_ROOT="$VCPKG_ROOT" cmake --preset "$PRESET"

echo "==> Done. Build with:"
echo "    VCPKG_ROOT=$VCPKG_ROOT cmake --build --preset $PRESET"