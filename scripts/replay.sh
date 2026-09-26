#!/usr/bin/env bash
# Run a historical replay through the pipeline (phase P4+).
#
# Placeholder: the replay harness does not exist yet. This script documents
# the intended interface so the invocation is fixed before the binary is.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

INPUT="${1:?usage: replay.sh <binlog-path>}"
CONFIG_DIR="${CONFIG_DIR:-$REPO_ROOT/config}"

echo "replay harness not implemented yet (phase P4)"
echo "  input  : $INPUT"
echo "  config : $CONFIG_DIR"
echo
echo "when implemented, this will run:"
echo "  apps/replay <binlog> --config $CONFIG_DIR --clock=simulation"
exit 1
