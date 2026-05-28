FROM ubuntu:24.04

ARG SDL3_VERSION=3.4.4

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        file \
        libasound2-dev \
        libdbus-1-dev \
        libegl1-mesa-dev \
        libgl1-mesa-dev \
        libibus-1.0-dev \
        libpulse-dev \
        libudev-dev \
        libvulkan-dev \
        libwayland-dev \
        libx11-dev \
        libxcursor-dev \
        libxext-dev \
        libxi-dev \
        libxinerama-dev \
        libxkbcommon-dev \
        libxkbcommon-x11-dev \
        libxrandr-dev \
        libxrender-dev \
        libxss-dev \
        libxtst-dev \
        pkg-config \
        python3-venv \
        wayland-protocols \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/cmake \
    && /opt/cmake/bin/pip install --no-cache-dir "cmake>=3.31" ninja

ENV PATH="/opt/cmake/bin:${PATH}"

RUN curl -L \
        -o "/tmp/SDL3-${SDL3_VERSION}.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-${SDL3_VERSION}.tar.gz" \
    && tar -C /tmp -xzf "/tmp/SDL3-${SDL3_VERSION}.tar.gz" \
    && cmake -S "/tmp/SDL3-${SDL3_VERSION}" -B /tmp/sdl3-build -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl3 \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TEST_LIBRARY=OFF \
    && cmake --build /tmp/sdl3-build \
    && cmake --install /tmp/sdl3-build \
    && rm -rf "/tmp/SDL3-${SDL3_VERSION}" /tmp/sdl3-build "/tmp/SDL3-${SDL3_VERSION}.tar.gz"

ENV PKG_CONFIG_PATH="/opt/sdl3/lib/pkgconfig"

WORKDIR /work
