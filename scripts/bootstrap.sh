#!/usr/bin/env bash
# Bootstrap vcpkg for manifest-mode dependency resolution.
# Idempotent: safe to run repeatedly. Reads VCPKG_ROOT (default: ./.vcpkg).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-$REPO_ROOT/.vcpkg}"

if [[ ! -d "$VCPKG_ROOT/.git" ]]; then
    echo "==> cloning vcpkg into $VCPKG_ROOT"
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    echo "==> bootstrapping vcpkg"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

echo "==> vcpkg ready at $VCPKG_ROOT"
echo "    export VCPKG_ROOT=\"$VCPKG_ROOT\""
