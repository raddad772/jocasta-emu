#!/usr/bin/env bash
set -euo pipefail

cmake --build cmake-build-dreamcast-release --target build_cdi

echo "Build succeeded; launching Flycast..."
exec ./scripts/run-flycast.sh