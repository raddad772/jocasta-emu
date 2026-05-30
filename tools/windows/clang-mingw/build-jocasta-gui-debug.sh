#!/usr/bin/env bash
set -euo pipefail
JOCASTA_PRESETS=windows-clang-mingw-debug \
    exec "$(dirname "${BASH_SOURCE[0]}")/build-jocasta-gui.sh" "$@"
