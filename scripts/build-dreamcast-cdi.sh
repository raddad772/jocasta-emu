source /opt/toolchains/dc/kos/environ.sh

#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-release}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

case "$BUILD_TYPE" in
  debug)
    BUILD_DIR="$ROOT/cmake-build-dreamcast-debug"
    ;;
  release)
    BUILD_DIR="$ROOT/cmake-build-dreamcast-release"
    ;;
  *)
    echo "Usage: $0 [debug|release]"
    exit 1
    ;;
esac

ELF="$BUILD_DIR/jocasta-cast/jocasta-cast.elf"
OUT_DIR="$ROOT/dist"
CDI="$OUT_DIR/jocasta-cast-${BUILD_TYPE}.cdi"

mkdir -p "$OUT_DIR"

if [ ! -f "$ELF" ]; then
  echo "Missing ELF: $ELF"
  echo "Build the Dreamcast ${BUILD_TYPE} preset first."
  exit 1
fi

~/dev/external/mkdcdisc/builddir/mkdcdisc \
  -e "$ELF" \
  -o "$CDI" \
  -n "JOCASTA CAST" \
  -d ~/Documents/dcbuild/fsys

echo "$CDI"

#/Applications/Flycast.app/Contents/MacOS/Flycast "$CDI"
