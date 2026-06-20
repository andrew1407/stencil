# Stencil — browser app (multi-stage: build the wasm core, then serve static files).
#
# The app is dependency-free vanilla ES modules with no build step; the only
# "build" is compiling core/ -> WebAssembly (the gitignored js/wasm/stencilCore.js).
# Because that step needs core/, the build context is the REPO ROOT, not browser/:
#
#   docker build -f browser/Dockerfile -t stencil-browser .
#   docker run --rm -p 8080:80 stencil-browser   # -> http://localhost:8080
#
# See core/WASM.md for the wasm toolchain details.

# ---- Stage 1: build the shared core to WebAssembly --------------------------
FROM emscripten/emsdk:latest AS wasm
WORKDIR /src
COPY core/ ./core/
# emcmake defines EMSCRIPTEN, so only the stencil_wasm target is produced
# (no Qt, no Doctest). Output: core/build-wasm/stencil_core.js (SINGLE_FILE).
RUN cd core \
 && emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release \
 && emmake cmake --build build-wasm -j --target stencil_wasm

# ---- Stage 2: serve the static app -----------------------------------------
FROM nginx:alpine
COPY browser/ /usr/share/nginx/html/
# Drop the generated artifact in where js/core/stencilCore.js imports it from.
COPY --from=wasm /src/core/build-wasm/stencil_core.js \
     /usr/share/nginx/html/js/wasm/stencilCore.js
COPY browser/nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
