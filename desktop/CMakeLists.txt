cmake_minimum_required(VERSION 3.16)
project(stencil VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Where the GUI keeps its runtime state (settings / session / projects). Dev builds
# bake a repo-local desktop/.stencil for convenience; distributable builds turn this
# OFF so state resolves to the per-user config dir instead (the fileStore fallback,
# QStandardPaths::AppConfigLocation). Release packaging always configures with OFF.
option(STENCIL_DEV_STATE_DIR "Bake the repo-local .stencil state dir (dev only; OFF for releases)" ON)

# GCC/Clang use -Wall -Wextra; MSVC rejects those (it reads /Wextra as an invalid
# numeric warning-level arg, D8021), so give it its own near-equivalent.
if(MSVC)
  add_compile_options(/W4)
  # M_PI et al. are POSIX, not standard C++: GCC/Clang/Emscripten expose them by
  # default, but MSVC only does so when this is defined before <cmath>. Set it on
  # the command line so the Qt-free core/tests get it too (the GUI gets it via Qt).
  add_compile_definitions(_USE_MATH_DEFINES)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Shared core sources (STL only, GUI-free). Listed once so both the native static
# library and the optional WebAssembly target compile exactly the same files.
set(STENCIL_CORE_SOURCES
  core/geometry.cpp
  core/color.cpp
  core/pageMetrics.cpp
  core/cropGeometry.cpp
  core/formulaParser.cpp
  core/historyStack.cpp
  core/projectsStore.cpp
  core/zoomPan.cpp
  core/tooltipRows.cpp
  core/imageFilter.cpp
  core/hotkeyFormat.cpp
  core/localeUnit.cpp
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
  tests/cropGeometry.test.cpp
  tests/formula.test.cpp
  tests/history.test.cpp
  tests/projectsStore.test.cpp
  tests/zoomPan.test.cpp
  tests/tooltipRows.test.cpp
  tests/imageFilter.test.cpp
  tests/hotkeyFormat.test.cpp
  tests/localeUnit.test.cpp
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
    gui/cropDialog.cpp
    gui/infoDialog.cpp
    gui/shortcutsDialog.cpp
    gui/guiHelpers.cpp
    resources/app.qrc
  )
  target_link_libraries(stencil_gui PRIVATE stencil_core Qt6::Widgets)
  if(STENCIL_DEV_STATE_DIR)
    # Repo-local, gitignored runtime state dir (settings / session / projects).
    target_compile_definitions(stencil_gui PRIVATE
      "STENCIL_STATE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/.stencil\"")
    message(STATUS "stencil_gui: dev state dir baked (${CMAKE_CURRENT_SOURCE_DIR}/.stencil)")
  else()
    message(STATUS "stencil_gui: portable build — state resolves to per-user config dir")
  endif()
  # Ship as a macOS .app bundle and a Windows GUI app (no console window) so the
  # platform deploy tools have the right shape to bundle Qt into.
  set_target_properties(stencil_gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Stencil"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ai.datagrok.stencil"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    WIN32_EXECUTABLE TRUE)
  message(STATUS "Qt6 found — building stencil_gui (${Qt6_VERSION})")

  # Headless functional check of the crop integration (CanvasWidget + cropGeometry).
  # Runs offscreen (QT_QPA_PLATFORM=offscreen), so it needs no display. Kept out of
  # stencil_tests (which is deliberately Qt-free) and built only when Qt is present.
  add_executable(stencil_crop_headless
    tests/cropCanvas.headless.cpp
    gui/canvasWidget.cpp
    gui/theme.cpp)
  target_include_directories(stencil_crop_headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/gui)
  target_link_libraries(stencil_crop_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_crop_headless COMMAND stencil_crop_headless)
  set_tests_properties(stencil_crop_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # ── Distributable packaging ───────────────────────────────────────────────
  # `cmake --install build` lays the app out under the install prefix; the Qt
  # deploy script then copies the Qt libraries + plugins next to it (macdeployqt /
  # windeployqt, best-effort on Linux) so the result runs on a machine with no Qt
  # installed. `cpack` finally wraps that tree into one platform package:
  # .dmg (macOS) / .zip (Windows) / .tar.gz (Linux). Build releases with
  # -DSTENCIL_DEV_STATE_DIR=OFF (see desktop/README.md → Release packaging).
  install(TARGETS stencil_gui
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)

  if(UNIX AND NOT APPLE)
    # Linux desktop integration: menu entry + scalable icon (shared with the
    # browser favicon), picked up by system menus and AppImage tooling.
    install(FILES packaging/stencil.desktop DESTINATION share/applications)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../browser/favicon.svg
      DESTINATION share/icons/hicolor/scalable/apps RENAME stencil.svg)
  endif()

  # Qt's own deployment helper (Qt >= 6.3) runs the right *deployqt tool at install
  # time. The keyword that captures the generated script path was renamed in Qt 6.5
  # (FILENAME_VARIABLE → OUTPUT_SCRIPT), so pick it per version. Older Qt: skip it;
  # the install tree then has no bundled Qt and the manual *deployqt step is
  # documented in desktop/README.md.
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt6_generate_deploy_app_script(
      TARGET stencil_gui
      OUTPUT_SCRIPT STENCIL_DEPLOY_SCRIPT
      NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${STENCIL_DEPLOY_SCRIPT})
  elseif(Qt6_VERSION VERSION_GREATER_EQUAL 6.3)
    qt6_generate_deploy_app_script(
      TARGET stencil_gui
      FILENAME_VARIABLE STENCIL_DEPLOY_SCRIPT
      NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${STENCIL_DEPLOY_SCRIPT})
  else()
    message(STATUS "Qt ${Qt6_VERSION} < 6.3 — install will not bundle Qt; run *deployqt manually")
  endif()

  # CPack: one self-contained package per platform.
  set(CPACK_PACKAGE_NAME "Stencil")
  set(CPACK_PACKAGE_VENDOR "Datagrok")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Image annotation / drawing tool")
  set(CPACK_PACKAGE_FILE_NAME
    "stencil-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_STRIP_FILES ON)
  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
  elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
  else()
    set(CPACK_GENERATOR "TGZ")
  endif()
  include(CPack)
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
    LINK_FLAGS "-sMODULARIZE=1 -sEXPORT_ES6=1 -sSINGLE_FILE=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','HEAPU8','HEAPF64'] -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_FUNCTIONS=['_stencil_parseHex','_stencil_distToSegment','_stencil_shouldCloseShape','_stencil_pageDimensions','_stencil_pixelToPageRaw','_stencil_formulaValidate','_stencil_formulaApply','_stencil_formulaEvaluate','_stencil_applyFilterRGBA','_stencil_rotatePoints','_stencil_boundingBoxCenter','_stencil_clampScale','_stencil_anchoredZoom','_stencil_rectZoom','_stencil_isAlbumOrientation','_stencil_cropAspect','_stencil_centeredCrop','_stencil_resizeCropFromCorner','_stencil_moveCropClamped','_stencil_cropResizeScale','_stencil_cropChange','_malloc','_free']")
  message(STATUS "Emscripten — building stencil_wasm (stencil_core.js/.wasm)")
endif()
