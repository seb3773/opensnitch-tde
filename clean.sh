#!/usr/bin/env bash
set -euo pipefail

# Clean working tree for source upload (no build artifacts).

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

rm -rf -- \
	build

exit 0
