#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-release}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CDI="$ROOT/dist/jocasta-cast-${BUILD_TYPE}.cdi"

if [ ! -f "$CDI" ]; then
  echo "Missing CDI: $CDI"
  echo "Run scripts/build-dreamcast-cdi.sh $BUILD_TYPE first."
  exit 1
fi

/Applications/Flycast.app/Contents/MacOS/Flycast "$CDI"
