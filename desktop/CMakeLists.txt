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

# ── stencil_core: the shared, GUI-free logic library ──────────────────────────
# The core now lives at the repo-root core/ (a sibling of desktop/), built by its own
# CMakeLists so it can also build standalone, to WebAssembly, and into the Zig CLI.
# Pull it in here for the GUI to link. It's an out-of-tree source dir, so name an
# explicit binary dir. Desktop only needs the library + the headless GUI test, so turn
# the core's own doctest suite OFF (the dedicated `core` CI job runs it).
set(STENCIL_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_BINARY_DIR}/core)

# The crop headless test below registers with CTest.
enable_testing()

# ── desktop GUI: Qt 6 (optional — core + tests build without it) ──────────────
# Network + Multimedia back the --src launch flag (remote-image fetch and
# video-frame grab); Widgets remains the gate for building the GUI at all.
#
# Release packaging MUST build the GUI (cpack wraps it), so it sets
# STENCIL_REQUIRE_QT=ON: Qt is then REQUIRED, turning a misconfigured prefix into a
# clear configure-time error instead of a silently GUI-less build that only blows up
# later at `cpack` with "Cannot find CPack config file" (CPackConfig.cmake is emitted
# by include(CPack) below, which lives in this Qt6_FOUND block).
option(STENCIL_REQUIRE_QT "Fail configuration if Qt6 is missing (release packaging)" OFF)
# Svg is needed by support/iconSet.cpp, which rasterizes the shared SVG icon set
# (ported from browser/js/ui/icons.js) into themed toolbar/menu QIcons. qt6-qtsvg
# is already an effective dependency — the window/taskbar icon (appicon.svg) is
# drawn by the same module's SVG icon engine.
if(STENCIL_REQUIRE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia Svg)
else()
  find_package(Qt6 QUIET COMPONENTS Widgets Network Multimedia Svg)
endif()
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)

  # GUI sources are grouped under src/ by role. The headers are included bare
  # (e.g. "fileStore.hpp"), so every group dir goes on the include path below and no
  # cross-group include needs a path prefix.
  set(STENCIL_GUI_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/support)

  add_executable(stencil_gui
    src/app/main.cpp
    src/app/mainWindow.cpp
    src/app/launchOptions.cpp
    src/app/selectionPanel.cpp
    src/canvas/canvasWidget.cpp
    src/canvas/canvasTooltip.cpp
    src/canvas/incognitoOverlay.cpp
    src/dialogs/settingsDialog.cpp
    src/dialogs/projectsDialog.cpp
    src/dialogs/blankImageDialog.cpp
    src/dialogs/openImageDialog.cpp
    src/dialogs/linksDialog.cpp
    src/dialogs/cropDialog.cpp
    src/dialogs/infoDialog.cpp
    src/dialogs/shortcutsDialog.cpp
    src/dialogs/connectDialog.cpp
    src/net/serverClient.cpp
    src/net/connectionStore.cpp
    src/io/fileStore.cpp
    src/io/mediaLoader.cpp
    src/support/theme.cpp
    src/support/notifications.cpp
    src/support/guiHelpers.cpp
    src/support/iconSet.cpp
    resources/app.qrc
  )
  target_include_directories(stencil_gui PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_gui
    PRIVATE stencil_core Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::Svg)
  if(STENCIL_DEV_STATE_DIR)
    # Repo-local, gitignored runtime state dir (settings / session / projects).
    target_compile_definitions(stencil_gui PRIVATE
      "STENCIL_STATE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/.stencil\"")
    message(STATUS "stencil_gui: dev state dir baked (${CMAKE_CURRENT_SOURCE_DIR}/.stencil)")
  else()
    message(STATUS "stencil_gui: portable build — state resolves to per-user config dir")
  endif()
  # App icon: rasterise browser/favicon.svg (the shared single-source artwork) into
  # a multi-resolution .icns at configure time and drop it in Contents/Resources/.
  # Without this the bundle ships no CFBundleIconFile, so the Dock/Finder fall back
  # to a generic icon. macOS-only tools (sips + iconutil) live in make-icns.sh; if
  # they're somehow absent we just warn and ship iconless (old behaviour) rather
  # than fail the build.
  set(STENCIL_BUNDLE_ICON_FILE "")
  if(APPLE)
    find_program(STENCIL_SIPS sips)
    find_program(STENCIL_ICONUTIL iconutil)
    if(STENCIL_SIPS AND STENCIL_ICONUTIL)
      set(_icns "${CMAKE_CURRENT_BINARY_DIR}/stencil.icns")
      execute_process(
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/packaging/make-icns.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/../browser/favicon.svg" "${_icns}"
        RESULT_VARIABLE _icns_rc OUTPUT_QUIET)
      if(_icns_rc EQUAL 0)
        target_sources(stencil_gui PRIVATE "${_icns}")
        set_source_files_properties("${_icns}" PROPERTIES
          MACOSX_PACKAGE_LOCATION Resources)
        set(STENCIL_BUNDLE_ICON_FILE "stencil.icns")
        message(STATUS "stencil_gui: bundled app icon ${_icns}")
      else()
        message(WARNING "stencil_gui: make-icns.sh failed (rc=${_icns_rc}) — building without an app icon")
      endif()
    else()
      message(WARNING "stencil_gui: sips/iconutil not found — building without an app icon")
    endif()

    # Themed AppIcon (macOS 26 "Icon & widget style": Default / Dark / Tinted, with
    # Clear OS-derived). Compiles an asset catalog from the three appicon-*.svg
    # variants into Assets.car via `actool` and sets CFBundleIconName so the OS can
    # recolor the Dock/Finder icon. actool ships ONLY with full Xcode (not the
    # Command Line Tools), so this is best-effort: when it's absent we keep the plain
    # .icns above. The CFBundleIconName key is added POST_BUILD with PlistBuddy only
    # when Assets.car was produced — setting it without the car would blank the icon.
    set(STENCIL_BUNDLE_ICON_NAME "")
    find_program(STENCIL_ACTOOL actool
      HINTS "$ENV{DEVELOPER_DIR}/usr/bin"
            "/Applications/Xcode.app/Contents/Developer/usr/bin")
    if(STENCIL_SIPS AND STENCIL_ACTOOL)
      set(_car "${CMAKE_CURRENT_BINARY_DIR}/Assets.car")
      execute_process(
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/packaging/make-appicon-assets.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon"
                "${CMAKE_CURRENT_BINARY_DIR}" "${STENCIL_ACTOOL}"
        RESULT_VARIABLE _car_rc OUTPUT_QUIET ERROR_QUIET)
      if(_car_rc EQUAL 0 AND EXISTS "${_car}")
        target_sources(stencil_gui PRIVATE "${_car}")
        set_source_files_properties("${_car}" PROPERTIES
          MACOSX_PACKAGE_LOCATION Resources)
        set(STENCIL_BUNDLE_ICON_NAME "AppIcon")
        message(STATUS "stencil_gui: bundled themed AppIcon (Assets.car) — dark/tinted aware")
      else()
        message(STATUS "stencil_gui: themed AppIcon skipped (actool rc=${_car_rc}) — plain .icns only")
      endif()
    else()
      message(STATUS "stencil_gui: actool not found (needs full Xcode) — plain .icns only, no OS icon theming")
    endif()
  endif()
  # Ship as a macOS .app bundle and a Windows GUI app (no console window) so the
  # platform deploy tools have the right shape to bundle Qt into.
  # Custom Info.plist adds CFBundleDocumentTypes (image/movie/json) so Finder
  # offers Stencil for "Open With" / drag-onto-Dock, delivering a QFileOpenEvent.
  set_target_properties(stencil_gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Stencil"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.stencil.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "${STENCIL_BUNDLE_ICON_FILE}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/MacOSXBundleInfo.plist.in"
    WIN32_EXECUTABLE TRUE)
  # Point CFBundleIconName at the compiled Assets.car so macOS uses the themed
  # AppIcon. Done POST_BUILD (after CMake assembles Info.plist) and only when the
  # car was built — Delete-then-Add makes it idempotent across rebuilds.
  if(STENCIL_BUNDLE_ICON_NAME)
    add_custom_command(TARGET stencil_gui POST_BUILD
      COMMAND /bin/sh -c
        "PL=\"$<TARGET_BUNDLE_CONTENT_DIR:stencil_gui>/Info.plist\"; \
         /usr/libexec/PlistBuddy -c 'Delete :CFBundleIconName' \"$PL\" 2>/dev/null; \
         /usr/libexec/PlistBuddy -c 'Add :CFBundleIconName string ${STENCIL_BUNDLE_ICON_NAME}' \"$PL\""
      VERBATIM)
  endif()
  message(STATUS "Qt6 found — building stencil_gui (${Qt6_VERSION})")

  # Headless functional check of the crop integration (CanvasWidget + cropGeometry).
  # Runs offscreen (QT_QPA_PLATFORM=offscreen), so it needs no display. Kept out of
  # stencil_tests (which is deliberately Qt-free) and built only when Qt is present.
  add_executable(stencil_crop_headless
    tests/cropCanvas.headless.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp)
  # "canvasWidget.hpp"/"theme.hpp" come from the src/ group dirs; the "core/*.hpp"
  # headers they pull in arrive transitively from the linked stencil_core.
  target_include_directories(stencil_crop_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_crop_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_crop_headless COMMAND stencil_crop_headless)
  set_tests_properties(stencil_crop_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Hold-to-draw + selection-delete headless check (CanvasWidget public API).
  add_executable(stencil_holddraw_headless
    tests/holdDrawCanvas.headless.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp)
  target_include_directories(stencil_holddraw_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_holddraw_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_holddraw_headless COMMAND stencil_holddraw_headless)
  set_tests_properties(stencil_holddraw_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Image-fixture test: loads a real PNG from tests/fixtures/ and runs it through the
  # load -> crop -> core image-filter path (desktop analogue of the CLI fixture tests).
  add_executable(stencil_image_headless
    tests/imageFixture.headless.cpp
    src/canvas/canvasWidget.cpp
    src/canvas/incognitoOverlay.cpp
    src/support/theme.cpp)
  target_include_directories(stencil_image_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_image_headless PRIVATE
    "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
  target_link_libraries(stencil_image_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_image_headless COMMAND stencil_image_headless)
  set_tests_properties(stencil_image_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Layout-envelope test: the page format + x/y formulas round-trip through
  # fileStore::buildLayoutJson <-> parseLayoutMeta (server save/load parity with browser + CLI).
  add_executable(stencil_layout_headless
    tests/layoutMeta.headless.cpp
    src/io/fileStore.cpp)
  target_include_directories(stencil_layout_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_layout_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_layout_headless COMMAND stencil_layout_headless)
  set_tests_properties(stencil_layout_headless PROPERTIES
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
  set(CPACK_PACKAGE_VENDOR "Stencil")
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
  message(STATUS "Qt6 not found — skipping stencil_gui (core builds via ../core)")
endif()

# The WebAssembly target moved to core/CMakeLists.txt (it builds the shared core, not
# anything desktop-specific). Configure core through emcmake to produce stencil_core.js.
