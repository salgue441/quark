# syntax=docker/dockerfile:1.7

# ── toolchain ────────────────────────────────────────────────────────────────
FROM gcc:13-bookworm AS toolchain

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      cmake \
      ninja-build \
      ccache \
      git \
      ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV CCACHE_DIR=/root/.cache/ccache \
    CMAKE_CXX_COMPILER_LAUNCHER=ccache \
    CMAKE_C_COMPILER_LAUNCHER=ccache \
    CC=gcc \
    CXX=g++

WORKDIR /src

# ── configure (stable layers first for cache hits) ───────────────────────────
FROM toolchain AS configure

COPY CMakeLists.txt .
COPY cmake/ cmake/
COPY include/ include/
COPY tests/ tests/
COPY bench/ bench/

RUN --mount=type=cache,target=/root/.cache/ccache \
    cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=23 \
      -DQUARK_BUILD_TESTS=ON \
      -DQUARK_BUILD_BENCH=OFF \
      -DQUARK_LOGGING=ON

# ── build ────────────────────────────────────────────────────────────────────
FROM configure AS build

RUN --mount=type=cache,target=/root/.cache/ccache \
    cmake --build build --parallel

# ── test (default CI target) ─────────────────────────────────────────────────
FROM build AS test
RUN ctest --test-dir build --output-on-failure --parallel

# ── headers-only export ──────────────────────────────────────────────────────
FROM scratch AS export
COPY --from=configure /src/include /include
