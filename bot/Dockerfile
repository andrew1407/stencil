# Stencil — Telegram bot (.NET). Chat-driven editing over the CLI + server REST.
#
# The bot shells out to the `stencil` CLI, so this image builds BOTH the Zig CLI (which
# recompiles core/ from source) and the .NET bot. The build context is the REPO ROOT:
#
#   docker build -f bot/Dockerfile -t stencil-bot .
#   docker run --rm -e TELEGRAM_BOT_TOKEN=123456:ABC stencil-bot
#
# Provide TELEGRAM_BOT_TOKEN at runtime — never bake it into the image. ffmpeg is included
# for /frame video input. Optional runtime env: REDIS_URL (shared sessions),
# STENCIL_TLS_INSECURE, STENCIL_BOT_DATA_DIR (defaults to /data; mount a volume to persist).
# The Zig arch auto-detects from the build platform (amd64/arm64), so this builds on an
# x86_64 CI runner and an Apple-Silicon Mac unchanged. Override --build-arg ZIG_VERSION=...
# (or ZIG_ARCH to force an arch), or point ZIG_URL at a ziglang.org/builds/ tarball for
# dev/nightly.

# ---- Stage 1: build the Stencil CLI (recompiles core/) ----------------------
FROM debian:bookworm-slim AS cli
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

# ---- Stage 2: build the .NET bot -------------------------------------------
FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src/bot
COPY bot/ ./
# nuget.config (copied above) pins globalPackagesFolder to ./packages, so this restore
# pulls Telegram.Bot, StackExchange.Redis, … into an image-local bot/packages/ folder
# rather than any global cache. --packages is passed too so it holds even if the config
# is ever dropped.
RUN dotnet publish src/Stencil.TelegramBot.Bot/Stencil.TelegramBot.Bot.csproj -c Release -o /app --packages packages

# ---- Stage 3: runtime ------------------------------------------------------
FROM mcr.microsoft.com/dotnet/runtime:10.0
RUN apt-get update \
 && apt-get install -y --no-install-recommends ffmpeg ca-certificates \
 && rm -rf /var/lib/apt/lists/*
COPY --from=cli /src/cli/zig-out/bin/stencil /usr/local/bin/stencil
COPY --from=build /app /app
# Tell the bot where the CLI lives (it also auto-discovers it on PATH).
ENV STENCIL_CLI=/usr/local/bin/stencil
# Per-user working images live here; mount a volume to persist across restarts.
ENV STENCIL_BOT_DATA_DIR=/data
WORKDIR /app
ENTRYPOINT ["dotnet", "Stencil.TelegramBot.Bot.dll"]
