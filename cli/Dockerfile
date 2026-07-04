# Stencil — CLI (Zig). Headless image tool wrapping the C++ core.
#
# build.zig recompiles core/ from source, so the build context is the REPO ROOT:
#
#   docker build -f cli/Dockerfile -t stencil-cli .
#   docker run --rm -v "$PWD:/work" -w /work stencil-cli -i in.png -r 1 out.png
#
# The Zig arch auto-detects from the build platform (amd64/arm64), so this builds on
# an x86_64 CI runner and an Apple-Silicon Mac unchanged. Override --build-arg
# ZIG_VERSION=... (or ZIG_ARCH to force an arch); for Zig dev/nightly builds, point
# ZIG_URL at the ziglang.org/builds/ tarball. ffmpeg is included for video-frame
# input; drop it from the
# runtime stage if you never feed the CLI video.

# ---- Stage 1: build the CLI -------------------------------------------------
FROM debian:bookworm-slim AS build
ARG ZIG_VERSION=0.16.0
# BuildKit auto-populates TARGETARCH (amd64/arm64) for the build platform; map it to
# Zig's arch naming so `docker build` grabs the right toolchain on any host — CI's
# x86_64 runners and Apple-Silicon arm64 alike, no --build-arg needed. Override ZIG_ARCH
# to force an arch, or ZIG_URL to point at a ziglang.org/builds/ nightly tarball.
ARG TARGETARCH
ARG ZIG_ARCH=
ARG ZIG_URL=
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl xz-utils ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN set -eux; \
    arch="${ZIG_ARCH}"; \
    if [ -z "$arch" ]; then \
      case "${TARGETARCH:-amd64}" in \
        amd64) arch=x86_64 ;; \
        arm64) arch=aarch64 ;; \
        *) echo "unsupported TARGETARCH: ${TARGETARCH}" >&2; exit 1 ;; \
      esac; \
    fi; \
    url="${ZIG_URL:-https://ziglang.org/download/${ZIG_VERSION}/zig-${arch}-linux-${ZIG_VERSION}.tar.xz}"; \
    curl -fsSL "$url" | tar -xJ -C /opt; \
    ln -s /opt/zig-*/zig /usr/local/bin/zig
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
