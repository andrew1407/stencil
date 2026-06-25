# Stencil — MCP server (Rust). Exposes the CLI's editing pipeline as MCP tools.
#
# The server shells out to the `stencil` CLI, so this image builds BOTH: the Zig CLI
# (which recompiles core/ from source) and the Rust server. The build context is the
# REPO ROOT:
#
#   docker build -f mcp/Dockerfile -t stencil-mcp .
#   docker run --rm -i -v "$PWD:/work" -w /work stencil-mcp
#
# It speaks MCP over stdio, so run it with -i (interactive stdin) and wire it into an
# MCP client. Override the Zig toolchain with --build-arg ZIG_VERSION=... (and ZIG_ARCH
# for arm64), or point ZIG_URL at a ziglang.org/builds/ tarball for dev/nightly. ffmpeg
# is included for video-frame input; drop it from the runtime stage if you never feed
# the editor video.

# ---- Stage 1: build the Stencil CLI ----------------------------------------
FROM debian:bookworm-slim AS cli
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

# ---- Stage 2: build the Rust MCP server ------------------------------------
FROM rust:slim-bookworm AS server
WORKDIR /src/mcp
COPY mcp/ ./
# cargo fetches the rmcp SDK and other crates on demand (needs network).
RUN cargo build --release

# ---- Stage 3: runtime ------------------------------------------------------
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends ffmpeg ca-certificates \
 && rm -rf /var/lib/apt/lists/*
COPY --from=cli /src/cli/zig-out/bin/stencil /usr/local/bin/stencil
COPY --from=server /src/mcp/target/release/stencil-mcp /usr/local/bin/stencil-mcp
# Tell the server where the CLI lives (it also auto-discovers it on PATH).
ENV STENCIL_CLI=/usr/local/bin/stencil
ENTRYPOINT ["stencil-mcp"]
