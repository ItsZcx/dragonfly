#!/usr/bin/env bash
# Configure and build one preset.
#
# Usage: ./scripts/build.sh <preset>
#
# A preset is required. There is no default, because the presets differ in more
# than optimization level: debug and tsan instrument the binary, and building
# the wrong one silently is worse than being asked to choose.
#
# compile_commands.json at the repository root is a symlink to the preset built
# most recently, so clangd and clang-tidy find the flags of the current build.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_DIR="$REPO_ROOT/.vcpkg"
VCPKG_TOOLCHAIN="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake"

usage() {
    cat <<'EOF'
usage: ./scripts/build.sh <preset>

Configure and build one preset.

presets:
  dev      RelWithDebInfo, no sanitizers            -> build/dev
  release  Release, no sanitizers                   -> build/release
  debug    Debug, address+undefined sanitizers      -> build/debug
  tsan     Debug, thread sanitizer                  -> build/tsan
EOF
}

if [[ $# -ne 1 ]]; then
    usage
    exit 1
fi

PRESET="$1"

case "$PRESET" in
    dev|release|debug|tsan) ;;
    *)
        echo "error: unknown preset '$PRESET'" >&2
        echo >&2
        usage >&2
        exit 1
        ;;
esac

if [[ ! -f "$VCPKG_TOOLCHAIN" ]]; then
    echo "error: vcpkg not found at $VCPKG_DIR" >&2
    echo "       run ./scripts/bootstrap.sh first" >&2
    exit 1
fi

cd "$REPO_ROOT"

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"

# compile_commands.json points at a fast, non-instrumented build so editors use
# the flags of a normal build. The sanitizer presets deliberately leave it
# alone: pointing clangd at build/debug would make diagnostics slower and can
# produce spurious include errors from sanitizer runtime headers.
case "$PRESET" in
    dev|release)
        ln -sfn "build/$PRESET/compile_commands.json" compile_commands.json
        echo
        echo "==> built preset '$PRESET' (build/$PRESET)"
        echo "==> compile_commands.json -> build/$PRESET/compile_commands.json"
        ;;
    *)
        echo
        echo "==> built preset '$PRESET' (build/$PRESET)"
        echo "==> compile_commands.json left unchanged (sanitized build)"
        ;;
esac
