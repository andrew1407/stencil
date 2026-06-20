# Stencil — CLI (Zig). Headless image tool wrapping the C++ core.
#
# build.zig recompiles core/ from source, so the build context is the REPO ROOT:
#
#   docker build -f cli/Dockerfile -t stencil-cli .
#   docker run --rm -v "$PWD:/work" -w /work stencil-cli -i in.png -r 1 out.png
#
# Override the Zig toolchain with --build-arg ZIG_VERSION=... (and ZIG_ARCH for
# arm64). For Zig dev/nightly builds, point ZIG_URL at the ziglang.org/builds/
# tarball instead. ffmpeg is included for video-frame input; drop it from the
# runtime stage if you never feed the CLI video.

# ---- Stage 1: build the CLI -------------------------------------------------
FROM debian:bookworm-slim AS build
ARG ZIG_VERSION=0.16.0
ARG ZIG_ARCH=x86_64
ARG ZIG_URL=https://ziglang.org/download/${ZIG_VERSION}/zig-${ZIG_ARCH}-linux-${ZIG_VERSION}.tar.xz
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl xz-utils ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL "${ZIG_URL}" | tar -xJ -C /opt \
 && ln -s /opt/zig-*/zig /usr/local/bin/zig
WORKDIR /src
COPY core/ ./core/
COPY cli/ ./cli/
# Zig fetches the pinned stb_image dependency on demand (needs network).
RUN cd cli && zig build -Doptimize=ReleaseFast

# ---- Stage 2: runtime -------------------------------------------------------
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends ffmpeg ca-certificates \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/cli/zig-out/bin/stencil /usr/local/bin/stencil
ENTRYPOINT ["stencil"]
