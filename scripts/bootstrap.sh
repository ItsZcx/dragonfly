#!/usr/bin/env bash
# Bootstrap vcpkg for manifest-mode dependency resolution.
#
# Idempotent: safe to run repeatedly.
#
# vcpkg always lives at <repo>/.vcpkg. The path is not configurable, so that a
# build always uses the same vcpkg the bootstrap created. A second vcpkg
# installed elsewhere on the machine (Homebrew, for example) is never used, and
# cannot be picked up by accident.
#
# A full clone is required, not a shallow one: vcpkg.json pins a
# builtin-baseline commit, and a shallow clone has no history to reach it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="$REPO_ROOT/.vcpkg"

if [[ ! -d "$VCPKG_ROOT/.git" ]]; then
    echo "==> cloning vcpkg into $VCPKG_ROOT"
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    echo "==> bootstrapping vcpkg"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

echo "==> vcpkg ready at $VCPKG_ROOT"
echo "    configure a build with ./scripts/build.sh <preset>"
