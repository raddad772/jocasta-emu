#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/../../.." && pwd)
DOCKER_BIN=${DOCKER_BIN:-docker}
IMAGE=${JOCASTA_WINDOWS_IMAGE:-jocasta/windows-clang-mingw:local}
DOCKER_PLATFORM=${DOCKER_PLATFORM:-linux/amd64}
HOST_DEPS_DIR=${JOCASTA_HOST_DEPS_DIR:-}
CONTAINER_DEPS_DIR=${JOCASTA_CONTAINER_DEPS_DIR:-/opt/sdl3-mingw}
LLVM_MINGW_TARGET=${LLVM_MINGW_TARGET:-x86_64-w64-mingw32}
LLVM_MINGW_RUNTIME_DIR=${LLVM_MINGW_RUNTIME_DIR:-/opt/llvm-mingw/${LLVM_MINGW_TARGET}/bin}

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

for preset in ${JOCASTA_PRESETS:-windows-clang-mingw-debug windows-clang-mingw-release}; do
    config=${preset#windows-clang-mingw-}
    artifact_name="jocasta-gui-windows-clang-mingw-${config}.zip"
    status=0
    "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
        cmake --preset "${preset}" "${CMAKE_DEP_ARGS[@]}" || status=1
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            cmake --build "cmake-build-${preset}" --target jocasta-gui || status=1
    fi
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            cp -f "${CONTAINER_DEPS_DIR}/bin/SDL3.dll" "${LLVM_MINGW_RUNTIME_DIR}/libc++.dll" "${LLVM_MINGW_RUNTIME_DIR}/libunwind.dll" \
                "cmake-build-${preset}/jocasta-gui/" || status=1
    fi
    if [[ ${status} -eq 0 ]]; then
        "${DOCKER_BIN}" run --rm --platform "${DOCKER_PLATFORM}" "${DOCKER_MOUNTS[@]}" -w /work "${IMAGE}" \
            sh -lc "cd cmake-build-${preset}/jocasta-gui && cmake -E tar cf /work/artifacts/${artifact_name} --format=zip -- jocasta-gui.exe SDL3.dll libc++.dll libunwind.dll" || status=1
    fi
    if [[ ${status} -ne 0 ]]; then
        had_failure=1
    fi
done

exit ${had_failure:-0}
