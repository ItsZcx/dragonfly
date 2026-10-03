#!/usr/bin/env bash
# Format every C++ source file in the repository.
#
# Usage: ./scripts/format.sh [--check]
#
#   (no argument)  rewrite files in place
#   --check        exit non-zero if any file would change
#
# CI runs the check form. Run the plain form before committing.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat <<'EOF'
usage: ./scripts/format.sh [--check]

Format every .cpp and .hpp under contracts/, core/, apps/, tests/, and bench/.

  (no argument)  rewrite the files in place
  --check        report files that need formatting and exit non-zero

examples:
  ./scripts/format.sh
  ./scripts/format.sh --check
EOF
}

MODE="write"
case "${1:-}" in
    "")        MODE="write" ;;
    --check)   MODE="check" ;;
    -h|--help) usage; exit 0 ;;
    *)         echo "error: unknown argument '$1'" >&2; echo >&2; usage >&2; exit 1 ;;
esac

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not found on PATH" >&2
    echo "       macOS: brew install llvm@22, then add \$(brew --prefix llvm@22)/bin to PATH" >&2
    echo "       Debian and Ubuntu: see .github/workflows/ci.yml for the pinned version" >&2
    exit 1
fi

# The clang-format in PATH may be a different release than the one CI uses, and
# the releases disagree about how to format some constructs. Fail on a mismatch
# rather than reformat the tree to a style CI will reject.
WANT_MAJOR=22
GOT_MAJOR="$(clang-format --version | grep -oE '[0-9]+' | head -1)"
if [[ "$GOT_MAJOR" != "$WANT_MAJOR" ]]; then
    echo "error: clang-format $WANT_MAJOR required, found $(clang-format --version)" >&2
    echo "       macOS: brew install llvm@$WANT_MAJOR, then add \$(brew --prefix llvm@$WANT_MAJOR)/bin to PATH" >&2
    exit 1
fi

cd "$REPO_ROOT"

# Parenthesised so -name applies only to the directory arguments. Without the
# parentheses, -o binds looser than the implicit -a and the -name tests match
# outside the listed directories.
#
# Read into an array with a while loop rather than mapfile: macOS ships bash 3.2
# and mapfile needs bash 4.
FILES=()
while IFS= read -r f; do
    FILES+=("$f")
done < <(find contracts core apps tests bench \
    \( -name '*.cpp' -o -name '*.hpp' \) 2>/dev/null | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "error: no C++ files found under contracts/, core/, apps/, tests/, bench/" >&2
    exit 1
fi

if [[ "$MODE" == "check" ]]; then
    echo "==> checking ${#FILES[@]} files"
    clang-format --dry-run --Werror "${FILES[@]}"
    echo "==> all files are formatted"
else
    echo "==> formatting ${#FILES[@]} files"
    clang-format -i "${FILES[@]}"
    echo "==> done"
fi
