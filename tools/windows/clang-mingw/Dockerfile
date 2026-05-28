ARG LLVM_MINGW_IMAGE=mstorsjo/llvm-mingw:dev
FROM ${LLVM_MINGW_IMAGE}

ARG SDL3_VERSION=3.4.4

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        pkg-config \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/cmake \
    && /opt/cmake/bin/pip install --no-cache-dir "cmake>=3.31" ninja

RUN curl -L \
        -o "/tmp/SDL3-devel-${SDL3_VERSION}-mingw.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-devel-${SDL3_VERSION}-mingw.tar.gz" \
    && tar -C /opt -xzf "/tmp/SDL3-devel-${SDL3_VERSION}-mingw.tar.gz" \
    && ln -s "/opt/SDL3-${SDL3_VERSION}/x86_64-w64-mingw32" /opt/sdl3-mingw \
    && rm "/tmp/SDL3-devel-${SDL3_VERSION}-mingw.tar.gz"

ENV PATH="/opt/cmake/bin:${PATH}"

WORKDIR /work
