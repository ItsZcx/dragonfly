#!/usr/bin/env bash
# Run the test suite. Usage: ./scripts/test.sh [ctest args...]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "error: no build directory at $BUILD_DIR — run scripts/build.sh first" >&2
    exit 1
fi

ctest --test-dir "$BUILD_DIR" --output-on-failure "$@"
