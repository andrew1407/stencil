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
# Override the Zig toolchain with --build-arg ZIG_VERSION=... (and ZIG_ARCH for arm64), or
# point ZIG_URL at a ziglang.org/builds/ tarball for dev/nightly.

# ---- Stage 1: build the Stencil CLI (recompiles core/) ----------------------
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

# ---- Stage 2: build the .NET bot -------------------------------------------
FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src/bot
COPY bot/ ./
# Restores project-local NuGet packages (Telegram.Bot, StackExchange.Redis, …) on demand.
RUN dotnet publish src/Stencil.TelegramBot.Bot/Stencil.TelegramBot.Bot.csproj -c Release -o /app

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
