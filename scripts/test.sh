#!/usr/bin/env bash
# Run the test suite for one preset.
#
# Usage: ./scripts/test.sh <preset> [ctest args...]
#
# A preset is required. There is no default, because the presets are separate
# build directories and testing the wrong one reports a pass for a binary that
# is not the one under test.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat <<'EOF'
usage: ./scripts/test.sh <preset> [ctest args...]

Run the tests for one preset. A preset is required; there is no default.
The preset must have been built first with ./scripts/build.sh.

presets:
  dev      RelWithDebInfo, no sanitizers            -> build/dev
  release  Release, no sanitizers                   -> build/release
  debug    Debug, address+undefined sanitizers      -> build/debug
  tsan     Debug, thread sanitizer                  -> build/tsan

examples:
  ./scripts/test.sh dev
  ./scripts/test.sh dev --rerun-failed
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

PRESET="$1"
shift

case "$PRESET" in
    dev|release|debug|tsan) ;;
    *)
        echo "error: unknown preset '$PRESET'" >&2
        echo >&2
        usage >&2
        exit 1
        ;;
esac

if [[ ! -d "$REPO_ROOT/build/$PRESET" ]]; then
    echo "error: build/$PRESET does not exist" >&2
    echo "       run ./scripts/build.sh $PRESET first" >&2
    exit 1
fi

cd "$REPO_ROOT"

ctest --preset "$PRESET" "$@"
