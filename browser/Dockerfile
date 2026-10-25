# Stencil — browser app (multi-stage: build the wasm core, then serve static files).
#
# The app is dependency-free vanilla ES modules with no build step; the only
# "build" is compiling core/ -> WebAssembly (the gitignored js/wasm/stencilCore.{js,wasm}).
# Because that step needs core/, the build context is the REPO ROOT, not browser/:
#
#   docker build -f browser/Dockerfile -t stencil-browser .
#   docker run --rm -p 8080:80 stencil-browser   # -> http://localhost:8080/
#
# See core/WASM.md for the wasm toolchain details.

# ---- Stage 1: build the shared core to WebAssembly --------------------------
# Pinned to the emsdk CI and Pages build with, so the image's wasm is the one the parity suite proved.
FROM emscripten/emsdk:5.0.7 AS wasm
WORKDIR /src
COPY core/ ./core/
# emcmake defines EMSCRIPTEN, so only the stencil_wasm target is produced
# (no Qt, no Doctest). Output: core/build-wasm/stencilCore.js + stencilCore.wasm.
RUN cd core \
 && emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release \
 && emmake cmake --build build-wasm -j 4 --target stencil_wasm

# ---- Stage 2: stamp sw.js and index.html with this image's files (tools/swStamp.js)
FROM node:22-alpine AS stamp
WORKDIR /src
COPY browser/ ./browser/
COPY common/ ./common/
COPY --from=wasm /src/core/build-wasm/stencilCore.js /src/core/build-wasm/stencilCore.wasm ./browser/js/wasm/
RUN node browser/tools/swStamp.js --write

# ---- Stage 3: serve the static app -----------------------------------------
# The app at the root, with the shared tables and brand art its ../common imports land on at /common/.
FROM nginx:alpine
# What the page loads, and nothing else: no tests, tools, node_modules or .d.ts shapes.
COPY browser/launch.html browser/manifest.webmanifest /usr/share/nginx/html/
COPY --from=stamp /src/browser/index.html /src/browser/sw.js /usr/share/nginx/html/
COPY browser/css/ /usr/share/nginx/html/css/
COPY browser/js/ /usr/share/nginx/html/js/
COPY common/config/ /usr/share/nginx/html/common/config/
COPY common/icons/ /usr/share/nginx/html/common/icons/
# Drop the generated artifact in where js/core/stencilCore.js imports it from.
COPY --from=wasm /src/core/build-wasm/stencilCore.js /src/core/build-wasm/stencilCore.wasm \
     /usr/share/nginx/html/js/wasm/
COPY browser/nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
