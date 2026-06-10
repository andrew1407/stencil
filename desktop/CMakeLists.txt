cmake_minimum_required(VERSION 3.16)
project(stencil LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Shared core sources (STL only, GUI-free). Listed once so both the native static
# library and the optional WebAssembly target compile exactly the same files.
set(STENCIL_CORE_SOURCES
  core/geometry.cpp
  core/color.cpp
  core/pageMetrics.cpp
  core/formulaParser.cpp
  core/historyStack.cpp
  core/projectsStore.cpp
  core/zoomPan.cpp
  core/tooltipRows.cpp
  core/imageFilter.cpp
)

# ── stencil_core: the shared, GUI-free logic library (STL only) ───────────────
# Same sources are the intended WebAssembly target for the browser app, so this
# library must never depend on Qt.
add_library(stencil_core STATIC ${STENCIL_CORE_SOURCES})
target_include_directories(stencil_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── tests: Doctest over the core ──────────────────────────────────────────────
# Doctest is a single header, fetched on demand rather than committed (see README
# → Test). If it isn't present, pull the pinned release from the official upstream
# at configure time and verify its hash, so a fresh clone builds with no manual step.
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
  tests/color.test.cpp
  tests/pageMetrics.test.cpp
  tests/formula.test.cpp
  tests/history.test.cpp
  tests/projectsStore.test.cpp
  tests/zoomPan.test.cpp
  tests/tooltipRows.test.cpp
  tests/imageFilter.test.cpp
  # The WebAssembly ABI surface (extern "C" wrappers) is plain STL, so it
  # compiles and is exercised natively here — keeping the wasm exports covered
  # even on a machine without Emscripten.
  tests/wasmApi.test.cpp
  core/wasmApi.cpp
)
target_include_directories(stencil_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(stencil_tests PRIVATE stencil_core)
add_test(NAME stencil_tests COMMAND stencil_tests)

# ── desktop GUI: Qt 6 (optional — core + tests build without it) ──────────────
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  add_executable(stencil_gui
    gui/main.cpp
    gui/mainWindow.cpp
    gui/canvasWidget.cpp
    gui/canvasTooltip.cpp
    gui/theme.cpp
    gui/fileStore.cpp
    gui/notifications.cpp
    gui/selectionPanel.cpp
    gui/settingsDialog.cpp
    gui/projectsDialog.cpp
    gui/blankImageDialog.cpp
    gui/infoDialog.cpp
    gui/shortcutsDialog.cpp
    gui/guiHelpers.cpp
    resources/app.qrc
  )
  target_link_libraries(stencil_gui PRIVATE stencil_core Qt6::Widgets)
  # Repo-local, gitignored runtime state dir (settings / session / projects).
  target_compile_definitions(stencil_gui PRIVATE
    "STENCIL_STATE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/.stencil\"")
  message(STATUS "Qt6 found — building stencil_gui (${Qt6_VERSION})")
else()
  message(STATUS "Qt6 not found — skipping stencil_gui (core + tests still build)")
endif()

# ── WebAssembly target (S15) — OPTIONAL, Emscripten only ──────────────────────
# Only materializes when configured through emcmake (which sets EMSCRIPTEN). A
# normal native `cmake` build never enters this branch, so the desktop targets
# above are unchanged. emcc is NOT installed in this environment; this is
# build-ready + documented (see desktop/WASM.md) but was not compiled here.
if(EMSCRIPTEN)
  add_executable(stencil_wasm ${STENCIL_CORE_SOURCES} core/wasmApi.cpp)
  target_include_directories(stencil_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # Export the extern "C" wrappers + ccall/cwrap helpers; ship an ES6 module.
  set_target_properties(stencil_wasm PROPERTIES
    OUTPUT_NAME "stencil_core"
    # SINGLE_FILE=1 embeds the .wasm as base64 inside the .js so the module
    # loads under file:// (the browser app is serverless — no fetch()).
    LINK_FLAGS "-sMODULARIZE=1 -sEXPORT_ES6=1 -sSINGLE_FILE=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','HEAPU8','HEAPF64'] -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_FUNCTIONS=['_stencil_parseHex','_stencil_distToSegment','_stencil_shouldCloseShape','_stencil_pageDimensions','_stencil_pixelToPageRaw','_stencil_formulaValidate','_stencil_formulaApply','_stencil_formulaEvaluate','_stencil_applyFilterRGBA','_stencil_rotatePoints','_stencil_boundingBoxCenter','_stencil_clampScale','_stencil_anchoredZoom','_stencil_rectZoom','_malloc','_free']")
  message(STATUS "Emscripten — building stencil_wasm (stencil_core.js/.wasm)")
endif()
