cmake_minimum_required(VERSION 3.16)
project(stencil_core VERSION 1.0.0 LANGUAGES CXX)

# The shared, GUI-free logic core. STL only — never Qt, never an image codec, never
# a JSON parser. It compiles three ways from one source list: a native static library
# (linked by the desktop GUI), a WebAssembly module (loaded by the browser app), and
# directly into the Zig CLI (cli/). Hosts own all file/codec/video/JSON I/O; core only
# transforms caller-owned RGBA8 buffers and parses geometry / length / colour tokens.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Build the Doctest suite when core is configured on its own. The desktop build pulls
# core in via add_subdirectory and forces this OFF (it only needs the library), and the
# Emscripten build never wants a native test executable.
option(STENCIL_CORE_BUILD_TESTS "Build the core doctest suite" ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Shared core sources (STL only, GUI-free), grouped by concern under core/. Listed once
# so the static library, the WebAssembly target, and the Zig CLI compile exactly the same
# files. Keep this list in sync with cli/build.zig, which recompiles these directly.
set(STENCIL_CORE_SOURCES
  geometry/geometry.cpp
  geometry/cropGeometry.cpp
  geometry/imageOps.cpp
  geometry/rasterize.cpp
  color/color.cpp
  color/colorNames.cpp
  color/imageFilter.cpp
  parse/formulaParser.cpp
  parse/durationParser.cpp
  parse/lengthTokens.cpp
  parse/cropSpec.cpp
  page/pageMetrics.cpp
  page/tooltipRows.cpp
  page/localeUnit.cpp
  page/hotkeyFormat.cpp
  state/historyStack.cpp
  state/projectsStore.cpp
  state/zoomPan.cpp
  state/holdDraw.cpp
)

# Every group dir (plus the core root for models.hpp + the extern "C" API files) is on
# the include path, so all headers are included bare ("cropGeometry.hpp") regardless of
# which group they live in. Reused by the wasm target, and propagated PUBLIC so the
# desktop GUI and the core tests inherit it transitively when they link stencil_core.
set(STENCIL_CORE_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/geometry
  ${CMAKE_CURRENT_SOURCE_DIR}/color
  ${CMAKE_CURRENT_SOURCE_DIR}/parse
  ${CMAKE_CURRENT_SOURCE_DIR}/page
  ${CMAKE_CURRENT_SOURCE_DIR}/state)

# ── stencil_core: the shared, GUI-free logic library (STL only) ───────────────
add_library(stencil_core STATIC ${STENCIL_CORE_SOURCES})
target_include_directories(stencil_core PUBLIC ${STENCIL_CORE_INCLUDE_DIRS})

# M_PI et al. are POSIX, not standard C++: GCC/Clang/Emscripten expose them by default,
# but MSVC only does when this is defined before <cmath>. PUBLIC so dependents inherit it.
if(MSVC)
  target_compile_definitions(stencil_core PUBLIC _USE_MATH_DEFINES)
endif()

# ── tests: Doctest over the core ──────────────────────────────────────────────
# Doctest is a single header, fetched on demand rather than committed (see WASM.md /
# README → Test). A fresh clone pulls the pinned release at configure time and verifies
# its hash, so the suite builds with no manual step.
if(STENCIL_CORE_BUILD_TESTS AND NOT EMSCRIPTEN)
  set(DOCTEST_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest.h)
  if(NOT EXISTS ${DOCTEST_HEADER})
    message(STATUS "doctest.h not found — fetching pinned v2.4.11 from upstream")
    file(DOWNLOAD
      https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
      ${DOCTEST_HEADER}
      EXPECTED_HASH SHA256=44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb
      TLS_VERIFY ON
      SHOW_PROGRESS)
  endif()

  enable_testing()
  add_executable(stencil_tests
    tests/testMain.cpp
    tests/geometry.test.cpp
    tests/holdDraw.test.cpp
    tests/color.test.cpp
    tests/pageMetrics.test.cpp
    tests/cropGeometry.test.cpp
    tests/formula.test.cpp
    tests/durationParser.test.cpp
    tests/history.test.cpp
    tests/projectsStore.test.cpp
    tests/zoomPan.test.cpp
    tests/tooltipRows.test.cpp
    tests/imageFilter.test.cpp
    tests/hotkeyFormat.test.cpp
    tests/localeUnit.test.cpp
    tests/lengthTokens.test.cpp
    tests/cropSpec.test.cpp
    tests/colorNames.test.cpp
    tests/imageOps.test.cpp
    tests/rasterize.test.cpp
    # Perf-regression benchmarks — decorated doctest::skip(), so `ctest` never runs them.
    # Run on demand: stencil_tests -ts=bench --no-skip  (see tests/bench.test.cpp).
    tests/bench.test.cpp
    # The extern "C" ABI surfaces (wasm exports + the CLI ABI) are plain STL, so they
    # compile and are exercised natively here — keeping both ABIs covered even on a
    # machine without Emscripten or Zig.
    tests/wasmApi.test.cpp
    tests/cliApi.test.cpp
    wasmApi.cpp
    cliApi.cpp
  )
  target_include_directories(stencil_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(stencil_tests PRIVATE stencil_core)
  add_test(NAME stencil_tests COMMAND stencil_tests)
endif()

# ── WebAssembly target — OPTIONAL, Emscripten only ────────────────────────────
# Only materializes when configured through emcmake (which sets EMSCRIPTEN). A normal
# native `cmake` build never enters this branch. The browser app loads the resulting
# stencil_core.js (see WASM.md).
if(EMSCRIPTEN)
  add_executable(stencil_wasm ${STENCIL_CORE_SOURCES} wasmApi.cpp)
  target_include_directories(stencil_wasm PRIVATE ${STENCIL_CORE_INCLUDE_DIRS})
  # Export the extern "C" wrappers + ccall/cwrap helpers; ship an ES6 module.
  set_target_properties(stencil_wasm PROPERTIES
    OUTPUT_NAME "stencil_core"
    # SINGLE_FILE=1 embeds the .wasm as base64 inside the .js so the module loads under
    # file:// (the browser app is serverless — no fetch()).
    LINK_FLAGS "-sMODULARIZE=1 -sEXPORT_ES6=1 -sSINGLE_FILE=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','HEAPU8','HEAPF64'] -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_FUNCTIONS=['_stencil_parseHex','_stencil_distToSegment','_stencil_shouldCloseShape','_stencil_pageDimensions','_stencil_pixelToPageRaw','_stencil_pageFormats','_stencil_formulaValidate','_stencil_formulaApply','_stencil_formulaEvaluate','_stencil_parseDuration','_stencil_applyFilterRGBA','_stencil_applyContourRGBA','_stencil_rotatePoints','_stencil_flipPoints','_stencil_boundingBoxCenter','_stencil_clampScale','_stencil_anchoredZoom','_stencil_rectZoom','_stencil_isAlbumOrientation','_stencil_cropAspect','_stencil_centeredCrop','_stencil_resizeCropFromCorner','_stencil_moveCropClamped','_stencil_scaleCropCentered','_stencil_cropResizeScale','_stencil_cropChange','_stencil_rotateCropRectQuarter','_malloc','_free']")
  message(STATUS "Emscripten — building stencil_wasm (stencil_core.js/.wasm)")
endif()
