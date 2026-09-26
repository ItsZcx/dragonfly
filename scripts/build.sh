#!/usr/bin/env bash
# Configure and build. Usage: ./scripts/build.sh [Debug|RelWithDebInfo|Release]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-RelWithDebInfo}"
VCPKG_ROOT="${VCPKG_ROOT:-$REPO_ROOT/.vcpkg}"

if [[ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    echo "error: vcpkg not found at $VCPKG_ROOT — run scripts/bootstrap.sh first" >&2
    exit 1
fi

cmake -B "$REPO_ROOT/build" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build "$REPO_ROOT/build"
