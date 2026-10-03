#!/usr/bin/env bash
# Run clang-tidy over the C++ sources.
#
# Usage: ./scripts/lint.sh
#
# clang-tidy needs compile_commands.json, which scripts/build.sh writes as a
# symlink at the repository root. Configure a build before running this.
#
# clang-tidy comes from Homebrew LLVM on macOS, which is keg-only and so not on
# PATH by default. Add it to your shell profile:
#
#   export PATH="$(brew --prefix llvm@22)/bin:$PATH"
#
# The version is pinned. clang-tidy enables different checks between releases,
# so a local tool and the CI tool on different versions report different
# findings.
#
# tests/p0_smoke.cpp is excluded. It is P0 toolchain scaffolding that reports
# findings its own purpose makes unavoidable, and operations.md retires it once
# the phases it pre-verifies have real tests.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXCLUDE="tests/p0_smoke.cpp"

cd "$REPO_ROOT"

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "error: clang-tidy not found on PATH" >&2
    echo "       macOS: brew install llvm@22, then add \$(brew --prefix llvm@22)/bin to PATH" >&2
    echo "       Debian and Ubuntu: see .github/workflows/ci.yml for the pinned version" >&2
    exit 1
fi

# The clang-tidy in PATH may be a different release than the one CI uses, and
# the releases enable different checks. Fail on a mismatch rather than report
# findings CI will not reproduce.
WANT_MAJOR=22
GOT_MAJOR="$(clang-tidy --version | head -1 | grep -oE '[0-9]+' | head -1)"
if [[ "$GOT_MAJOR" != "$WANT_MAJOR" ]]; then
    echo "error: clang-tidy $WANT_MAJOR required, found $(clang-tidy --version | head -1)" >&2
    echo "       macOS: brew install llvm@$WANT_MAJOR, then add \$(brew --prefix llvm@$WANT_MAJOR)/bin to PATH" >&2
    exit 1
fi

if [[ ! -e compile_commands.json ]]; then
    echo "error: compile_commands.json not found" >&2
    echo "       run ./scripts/build.sh <preset> first" >&2
    exit 1
fi

EXTRA_ARGS=()
# Homebrew LLVM does not know where Apple's SDK is, so it cannot find the
# system headers without this. On Linux no equivalent is needed.
if [[ "$(uname -s)" == "Darwin" ]] && command -v xcrun >/dev/null 2>&1; then
    EXTRA_ARGS+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
fi

# Translation units only. clang-tidy checks headers through the #include graph and
# reports them according to HeaderFilterRegex in .clang-tidy, so a header does not
# need to be listed here. Passing one as its own argument makes clang-tidy guess a
# compile command, because compile_commands.json holds only .cpp entries, and the
# guess fails on system headers with a bogus 'cassert file not found'.
#
# No input file: that would lint every file in the tree, including those under
# build/ and .vcpkg/.
#
# Read into an array with a while loop rather than mapfile: macOS ships bash 3.2
# and mapfile needs bash 4.
FILES=()
while IFS= read -r f; do
    FILES+=("$f")
done < <(find contracts core apps tests bench \
    -name '*.cpp' 2>/dev/null \
    | grep -v "^${EXCLUDE}$" | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "error: no translation units found to lint" >&2
    exit 1
fi

echo "==> linting ${#FILES[@]} translation units (clang-tidy $(clang-tidy --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+'))"

# --warnings-as-errors is what makes this a check. Without it clang-tidy exits 0
# after reporting findings, so CI would pass on real violations.
#
# --quiet drops the per-file "N warnings generated" and "Suppressed N warnings"
# lines. Those count findings from every header reached through the include
# graph, including third-party ones, so they are large and mean nothing.
clang-tidy -p . ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} --warnings-as-errors='*' --quiet "${FILES[@]}"

echo "==> no findings"
