# Pinned build environment (Investigation §16; operations.md, "Build and run").
# The compiler and toolchain versions are baked in so local, CI, and
# production builds agree. The binary is always produced in CI or by
# scripts/build.sh — never compiled on the production host.

# ---- Stage 1: builder -------------------------------------------------------
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    lld \
    cmake \
    ninja-build \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
# The source is copied before the bootstrap because bootstrap.sh writes vcpkg
# into the repository at <repo>/.vcpkg, not to a configurable path.
COPY . .

# vcpkg bootstrap, then a release build of the tests.
RUN ./scripts/bootstrap.sh \
    && ./scripts/build.sh release \
    && ./scripts/test.sh release

# ---- Stage 2: runtime -------------------------------------------------------
# Slim image carrying only runtime dependencies and the built artefacts.
FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/dragonfly
COPY --from=builder /src/build/release/ /opt/dragonfly/build/
COPY --from=builder /src/config/ /opt/dragonfly/config/

# Loopback only; the host owns all networking (Investigation §7).
ENV DRAGONFLY_CONFIG_DIR=/opt/dragonfly/config

# placeholder
ENTRYPOINT ["/bin/bash"]
