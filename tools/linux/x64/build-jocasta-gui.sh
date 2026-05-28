#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/../../.." && pwd)
DOCKER_BIN=${DOCKER_BIN:-docker}
IMAGE=${JOCASTA_LINUX_X64_IMAGE:-jocasta/linux-x64:local}
DOCKER_PLATFORM=${DOCKER_PLATFORM:-linux/amd64}
HOST_DEPS_DIR=${JOCASTA_HOST_DEPS_DIR:-}
CONTAINER_DEPS_DIR=${JOCASTA_CONTAINER_DEPS_DIR:-/opt/sdl3}

if ! command -v "${DOCKER_BIN}" >/dev/null 2>&1; then
    for candidate in /opt/homebrew/bin/docker /opt/homebrew/Cellar/docker/*/bin/docker; do
        if [[ -x "${candidate}" ]]; then
            DOCKER_BIN=${candidate}
            break
        fi
    done
fi

if ! command -v "${DOCKER_BIN}" >/dev/null 2>&1; then
    echo "docker not found" >&2
    exit 1
fi

"${DOCKER_BIN}" build --platform "${DOCKER_PLATFORM}" -t "${IMAGE}" "${SCRIPT_DIR}"

DOCKER_MOUNTS=(-v "${ROOT_DIR}:/work")
CMAKE_DEP_ARGS=("-DJOCASTA_LOCAL_LIBS_DIR=${CONTAINER_DEPS_DIR}")
if [[ -n "${HOST_DEPS_DIR}" && -d "${HOST_DEPS_DIR}" ]]; then
    CONTAINER_DEPS_DIR=/opt/jocasta-deps
    DOCKER_MOUNTS+=(-v "${HOST_DEPS_DIR}:${CONTAINER_DEPS_DIR}:ro")
    CMAKE_DEP_ARGS=("-DJOCASTA_LOCAL_LIBS_DIR=${CONTAINER_DEPS_DIR}")
fi
if [[ -n "${JOCASTA_SDL3_LIBRARY:-}" ]]; then
    CMAKE_DEP_ARGS+=("-DJOCASTA_SDL3_LIBRARY=${JOCASTA_SDL3_LIBRARY}")
fi

mkdir -p "${ROOT_DIR}/artifacts"

for preset in ${JOCASTA_PRESETS:-linux-x64-debug linux-x64-release}; do
    config=${preset#linux-x64-}
    artifact_name="jocasta-gui-linux-x64-${config}.tar.gz"
    status=0
    "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
        cmake --preset "${preset}" "${CMAKE_DEP_ARGS[@]}" || status=1
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            cmake --build "cmake-build-${preset}" --target jocasta-gui || status=1
    fi
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            sh -lc "cp -f -L '${CONTAINER_DEPS_DIR}/lib/libSDL3.so.0' \"\$(c++ -print-file-name=libstdc++.so.6)\" \"\$(cc -print-file-name=libgcc_s.so.1)\" 'cmake-build-${preset}/jocasta-gui/'" || status=1
    fi
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            file "cmake-build-${preset}/jocasta-gui/jocasta-gui" || status=1
    fi
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            sh -lc "cd cmake-build-${preset}/jocasta-gui && cmake -E tar czf /work/artifacts/${artifact_name} --format=gnutar -- jocasta-gui libSDL3.so.0 libstdc++.so.6 libgcc_s.so.1" || status=1
    fi
    if [[ ${status} -ne 0 ]]; then
        had_failure=1
    fi
done

exit ${had_failure:-0}
