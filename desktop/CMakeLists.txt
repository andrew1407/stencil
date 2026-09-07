cmake_minimum_required(VERSION 3.16)
project(stencil VERSION 1.0.0 LANGUAGES CXX)
# support/shareImageMac.mm (the macOS body of the native share sheet) is Objective-C++.
# Harmless to enable on every OS this configures on — it only matters to the compiler
# once a .mm file is actually compiled, which only happens under the APPLE branch below.
if(APPLE)
  enable_language(OBJCXX)
endif()

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
# (the qrc-embedded browser/js/config/icons.json canon) into themed toolbar/menu QIcons. qt6-qtsvg
# is already an effective dependency — the window/taskbar icon (appicon.svg) is
# drawn by the same module's SVG icon engine.
if(STENCIL_REQUIRE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia Svg Test)
else()
  find_package(Qt6 QUIET COMPONENTS Widgets Network Multimedia Svg Test)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/llm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/support)

  # The full GUI translation-unit set MINUS the entry point (main.cpp), shared between
  # the app and the MainWindow GUI e2e test target below so the two never drift.
  set(STENCIL_GUI_SOURCES
    src/app/mainWindow.cpp
    src/app/mainWindowActions.cpp
    src/app/mainWindowToolbar.cpp
    src/app/mainWindowMenus.cpp
    src/app/mainWindowTheme.cpp
    src/app/stencilFileSync.cpp
    src/app/mainWindowChat.cpp
    src/app/mainWindowEvents.cpp
    src/app/stayOpenMenu.cpp
    src/app/logoHoverFx.cpp
    src/app/dockZonesOverlay.cpp
    src/app/dataExportController.cpp
    src/app/remoteSession.cpp
    src/app/remoteSyncController.cpp
    src/app/projectTransferController.cpp
    src/app/launchOptions.cpp
    src/app/deepLink.cpp
    src/app/selectionPanel.cpp
    src/app/selectedLineBar.cpp
    src/llm/chatDock.cpp
    src/llm/chatWidgets.cpp
    src/llm/chatCardRenderer.cpp
    src/llm/chatMenuPanel.cpp
    src/llm/chatPlanTarget.cpp
    src/llm/opPlan.cpp
    src/llm/opRegistry.cpp
    src/llm/opSchema.cpp
    src/llm/llmClient.cpp
    src/llm/planExecutor.cpp
    src/llm/qtLlmTransport.cpp
    src/support/tipContent.cpp
    src/canvas/canvasWidget.cpp
    src/canvas/canvasTooltip.cpp
    src/canvas/incognitoOverlay.cpp
    src/dialogs/settingsDialog.cpp
    src/dialogs/assistantSettingsDialog.cpp
    src/dialogs/llmSettingsForm.cpp
    src/dialogs/projectsDialog.cpp
    src/dialogs/expirationDialog.cpp
    src/dialogs/openImageDialog.cpp
    src/dialogs/linksDialog.cpp
    src/dialogs/descriptionDialog.cpp
    src/dialogs/keywordsDialog.cpp
    src/dialogs/cropDialog.cpp
    src/dialogs/infoDialog.cpp
    src/dialogs/shortcutsDialog.cpp
    src/dialogs/connectDialog.cpp
    src/dialogs/openInDialog.cpp
    src/net/serverClient.cpp
    src/net/liveFeed.cpp
    src/net/connectionStore.cpp
    src/io/fileStore.cpp
    src/io/mediaLoader.cpp
    src/support/theme.cpp
    src/support/notifications.cpp
    src/support/guiHelpers.cpp
    src/support/menuReveal.cpp
    src/support/modalReveal.cpp
    src/support/modalChrome.cpp
    src/support/searchCombo.cpp
    src/support/iconSet.cpp
    src/support/numericInput.cpp
    src/support/exportPreview.cpp
    resources/app.qrc)

  # Native OS share sheet (support/shareImage.hpp — one Share button, browser/extension
  # parity, hotkeysConfig.json shareImage): a small platform-specific TU per OS, since
  # the native API behind each only exists on its own platform. The only extra library
  # either real body needs is OS-provided (AppKit / the WinRT projection), not a new
  # third-party dependency; Linux's fallback needs nothing beyond what's linked already.
  set(STENCIL_SHARE_LIBS)
  if(APPLE)
    list(APPEND STENCIL_GUI_SOURCES src/support/shareImageMac.mm)
    set_source_files_properties(src/support/shareImageMac.mm PROPERTIES
      COMPILE_FLAGS "-fobjc-arc")
    find_library(STENCIL_APPKIT_LIBRARY AppKit REQUIRED)
    list(APPEND STENCIL_SHARE_LIBS ${STENCIL_APPKIT_LIBRARY})
  elseif(WIN32)
    list(APPEND STENCIL_GUI_SOURCES src/support/shareImageWin.cpp)
    # C++/WinRT projection headers ship with the Windows SDK; `windowsapp` is its
    # umbrella import lib for the WinRT runtime classes used there.
    list(APPEND STENCIL_SHARE_LIBS windowsapp)
  else()
    list(APPEND STENCIL_GUI_SOURCES src/support/shareImageLinux.cpp)
  endif()

  add_executable(stencil
    src/app/main.cpp
    ${STENCIL_GUI_SOURCES}
  )
  target_include_directories(stencil PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil
    PRIVATE stencil_core Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::Svg
            ${STENCIL_SHARE_LIBS})
  if(STENCIL_DEV_STATE_DIR)
    # Repo-local, gitignored runtime state dir (settings / session / projects).
    target_compile_definitions(stencil PRIVATE
      "STENCIL_STATE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/.stencil\"")
    message(STATUS "stencil: dev state dir baked (${CMAKE_CURRENT_SOURCE_DIR}/.stencil)")
  else()
    message(STATUS "stencil: portable build — state resolves to per-user config dir")
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
        target_sources(stencil PRIVATE "${_icns}")
        set_source_files_properties("${_icns}" PROPERTIES
          MACOSX_PACKAGE_LOCATION Resources)
        set(STENCIL_BUNDLE_ICON_FILE "stencil.icns")
        message(STATUS "stencil: bundled app icon ${_icns}")
      else()
        message(WARNING "stencil: make-icns.sh failed (rc=${_icns_rc}) — building without an app icon")
      endif()
    else()
      message(WARNING "stencil: sips/iconutil not found — building without an app icon")
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
        target_sources(stencil PRIVATE "${_car}")
        set_source_files_properties("${_car}" PROPERTIES
          MACOSX_PACKAGE_LOCATION Resources)
        set(STENCIL_BUNDLE_ICON_NAME "AppIcon")
        message(STATUS "stencil: bundled themed AppIcon (Assets.car) — dark/tinted aware")
      else()
        message(STATUS "stencil: themed AppIcon skipped (actool rc=${_car_rc}) — plain .icns only")
      endif()
    else()
      message(STATUS "stencil: actool not found (needs full Xcode) — plain .icns only, no OS icon theming")
    endif()
  endif()
  # Ship as a macOS .app bundle and a Windows GUI app (no console window) so the
  # platform deploy tools have the right shape to bundle Qt into.
  # Custom Info.plist adds CFBundleDocumentTypes (image/movie/json) so Finder
  # offers Stencil for "Open With" / drag-onto-Dock, delivering a QFileOpenEvent.
  set_target_properties(stencil PROPERTIES
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
    add_custom_command(TARGET stencil POST_BUILD
      COMMAND /bin/sh -c
        "PL=\"$<TARGET_BUNDLE_CONTENT_DIR:stencil>/Info.plist\"; \
         /usr/libexec/PlistBuddy -c 'Delete :CFBundleIconName' \"$PL\" 2>/dev/null; \
         /usr/libexec/PlistBuddy -c 'Add :CFBundleIconName string ${STENCIL_BUNDLE_ICON_NAME}' \"$PL\""
      VERBATIM)
  endif()
  message(STATUS "Qt6 found — building stencil (${Qt6_VERSION})")

  # Headless functional check of the crop integration (CanvasWidget + cropGeometry).
  # Runs offscreen (QT_QPA_PLATFORM=offscreen), so it needs no display. Kept out of
  # stencil_tests (which is deliberately Qt-free) and built only when Qt is present.
  add_executable(stencil_crop_headless
    tests/cropCanvas.headless.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp
    resources/app.qrc)
  # "canvasWidget.hpp"/"theme.hpp" come from the src/ group dirs; the "core/*.hpp"
  # headers they pull in arrive transitively from the linked stencil_core.
  target_include_directories(stencil_crop_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_crop_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_crop_headless COMMAND stencil_crop_headless)
  set_tests_properties(stencil_crop_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Modal-popover placement (support/popover.hpp) — the pure clamp behind the anchored
  # compact dialogs; header-only, so the test is the only extra compile unit.
  add_executable(stencil_popover_headless tests/popover.headless.cpp)
  target_include_directories(stencil_popover_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_popover_headless PRIVATE Qt6::Core)
  add_test(NAME stencil_popover_headless COMMAND stencil_popover_headless)
  set_tests_properties(stencil_popover_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # The theme wipe's easing (support/themeSwapOverlay.hpp) — header-only, so the test is
  # the only compile unit. Holds the curve to the AREA it sweeps, like the browser's
  # tests/motion.test.js does for its half.
  add_executable(stencil_themeswapease_headless tests/themeSwapEase.headless.cpp)
  target_include_directories(stencil_themeswapease_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_themeswapease_headless PRIVATE Qt6::Widgets)
  add_test(NAME stencil_themeswapease_headless COMMAND stencil_themeswapease_headless)
  set_tests_properties(stencil_themeswapease_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Rich control tooltips (support/tipContent.cpp) — the desktop port of the browser's
  # tipContent.js, carrying that suite's parse/render cases. Needs theme.cpp for the palette.
  add_executable(stencil_tipcontent_headless
    tests/tipContent.headless.cpp
    src/support/tipContent.cpp
    src/support/theme.cpp
    resources/app.qrc)
  target_include_directories(stencil_tipcontent_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_tipcontent_headless PRIVATE Qt6::Widgets)
  add_test(NAME stencil_tipcontent_headless COMMAND stencil_tipcontent_headless)
  set_tests_properties(stencil_tipcontent_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # CSS colours with alpha (support/cssColor.hpp) — the #rrggbbaa form the browser writes,
  # which Qt's own QColor does not parse (its hex alpha comes FIRST). Header-only.
  add_executable(stencil_csscolor_headless tests/cssColor.headless.cpp)
  target_include_directories(stencil_csscolor_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_csscolor_headless PRIVATE Qt6::Gui)
  add_test(NAME stencil_csscolor_headless COMMAND stencil_csscolor_headless)
  set_tests_properties(stencil_csscolor_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Closing a shape and the ways back out of one (canvas/chainEdit.hpp) — the ring
  # helpers plus the Alt+Ctrl pull-out gesture on the real widget.
  add_executable(stencil_chainedit_headless
    tests/chainEdit.headless.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp
    resources/app.qrc)
  target_include_directories(stencil_chainedit_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_chainedit_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_chainedit_headless COMMAND stencil_chainedit_headless)
  set_tests_properties(stencil_chainedit_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Stroke growth (canvas/strokeGrowth.hpp) — where a just-added vertex is at a given
  # instant, and the flight bookkeeping behind it. Header-only, but it names core::Point /
  # core::Line, so it links the core.
  add_executable(stencil_strokegrowth_headless
    tests/strokeGrowth.headless.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp
    resources/app.qrc)
  target_include_directories(stencil_strokegrowth_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_strokegrowth_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_strokegrowth_headless COMMAND stencil_strokegrowth_headless)
  set_tests_properties(stencil_strokegrowth_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Scroll-reveal curve (app/scrollReveal.hpp) — how dim a row is at a given spot in its
  # scroller; header-only, so the test is the only extra compile unit.
  add_executable(stencil_scrollreveal_headless tests/scrollReveal.headless.cpp)
  target_include_directories(stencil_scrollreveal_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_scrollreveal_headless PRIVATE Qt6::Widgets)
  add_test(NAME stencil_scrollreveal_headless COMMAND stencil_scrollreveal_headless)
  set_tests_properties(stencil_scrollreveal_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Name display-shortening (support/displayName.hpp) — the middle-ellipsis behind the
  # project dialogs/notifications; header-only, so the test is the only extra compile unit.
  add_executable(stencil_displayname_headless tests/displayName.headless.cpp)
  target_include_directories(stencil_displayname_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_displayname_headless PRIVATE Qt6::Core)
  add_test(NAME stencil_displayname_headless COMMAND stencil_displayname_headless)
  set_tests_properties(stencil_displayname_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # The toggle face swap (support/faceSwap.hpp) — the shared exchange behind Draw's
  # Start/Stop and Line/Rect: the curve, plus the driver on a live QToolButton (converges,
  # flips the caller's state once, survives rapid supersession, obeys reduced motion).
  add_executable(stencil_faceswap_headless
    tests/faceSwap.headless.cpp
    src/support/iconSet.cpp       # the glyphs it turns
    src/support/modalReveal.cpp   # motionReduced()
    resources/app.qrc)
  target_include_directories(stencil_faceswap_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_faceswap_headless PRIVATE Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_faceswap_headless COMMAND stencil_faceswap_headless)
  set_tests_properties(stencil_faceswap_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Form-control state swaps (support/controlSwap.hpp) — the checkbox's particle toggle
  # and the combo's value exchange, driven on live widgets under the real app stylesheet
  # (converge on the true state, no geometry change, rapid changes land, reduced motion
  # goes straight to the end state). Needs theme for the QSS the indicator is drawn from,
  # iconSet for faceSwap's glyphs, menuReveal for the dropped list's dust and modalReveal
  # for motionReduced().
  add_executable(stencil_controlswap_headless
    tests/controlSwap.headless.cpp
    src/support/theme.cpp
    src/support/iconSet.cpp
    src/support/menuReveal.cpp
    src/support/modalReveal.cpp
    resources/app.qrc)
  target_include_directories(stencil_controlswap_headless PRIVATE
    ${STENCIL_GUI_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(stencil_controlswap_headless PRIVATE Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_controlswap_headless COMMAND stencil_controlswap_headless)
  set_tests_properties(stencil_controlswap_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Per-icon hover motion (support/iconMotion.hpp) — the table against the glyph canon,
  # the semantic pins (plus grows / minus shrinks / the fullscreen corners invert / the
  # trash hinges at its lid), and the driver on a live button. Needs iconSet for the
  # glyphs it poses and modalReveal for motionReduced().
  add_executable(stencil_iconmotion_headless
    tests/iconMotion.headless.cpp
    src/support/iconSet.cpp
    src/support/modalReveal.cpp
    resources/app.qrc)
  target_include_directories(stencil_iconmotion_headless PRIVATE
    ${STENCIL_GUI_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(stencil_iconmotion_headless PRIVATE Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_iconmotion_headless COMMAND stencil_iconmotion_headless)
  set_tests_properties(stencil_iconmotion_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # GUI end-to-end (Qt Test framework): drives the REAL MainWindow — load via the OS-open
  # path, trigger the actual Rotate/Undo/Start-Drawing QActions, and send real mouse clicks
  # to the live canvas. Links the whole GUI (STENCIL_GUI_SOURCES) + Qt6::Test. Offscreen.
  add_executable(stencil_mainwindow_gui
    tests/mainWindow.gui.cpp
    ${STENCIL_GUI_SOURCES})
  target_include_directories(stencil_mainwindow_gui PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_mainwindow_gui PRIVATE
    stencil_core Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::Svg Qt6::Test
    ${STENCIL_SHARE_LIBS})
  add_test(NAME stencil_mainwindow_gui COMMAND stencil_mainwindow_gui)

  # STENCIL_NO_ANIM: this test opens dialogs and acts on them at once, so the
  # grow-from-the-icon flight (support/modalReveal) is off here — it would resize the
  # window under the test for its first ~200 ms, which is exactly the kind of timing the
  # suite must not depend on.
  set_tests_properties(stencil_mainwindow_gui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;STENCIL_NO_ANIM=1")

  # Hold-to-draw + selection-delete headless check (CanvasWidget public API).
  add_executable(stencil_holddraw_headless
    tests/holdDrawCanvas.headless.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp
    resources/app.qrc)
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
    src/support/iconSet.cpp
    src/support/numericInput.cpp
    src/support/theme.cpp
    resources/app.qrc)
  target_include_directories(stencil_image_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_image_headless PRIVATE
    "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
  target_link_libraries(stencil_image_headless PRIVATE stencil_core Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_image_headless COMMAND stencil_image_headless)
  set_tests_properties(stencil_image_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Layout-envelope test: the page format + x/y formulas round-trip through
  # fileStore::buildLayoutJson <-> parseLayoutMeta (server save/load parity with browser + CLI).
  add_executable(stencil_layout_headless
    tests/layoutMeta.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)   # Settings defaults read the providers.json canon
  target_include_directories(stencil_layout_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_layout_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_layout_headless COMMAND stencil_layout_headless)
  set_tests_properties(stencil_layout_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # fileStore::projectToJson <-> projectFromJson per-project `color` round-trip
  # (browser/server colour-persistence parity).
  add_executable(stencil_projectcolor_headless
    tests/projectColor.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_projectcolor_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_projectcolor_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_projectcolor_headless COMMAND stencil_projectcolor_headless)
  set_tests_properties(stencil_projectcolor_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Per-project keywords persistence round-trip (browser/server keywords parity).
  add_executable(stencil_projectkeywords_headless
    tests/projectKeywords.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_projectkeywords_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_projectkeywords_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_projectkeywords_headless COMMAND stencil_projectkeywords_headless)
  set_tests_properties(stencil_projectkeywords_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # .stencil portable project file round-trip (fileStore::buildProjectFile <-> parseProjectFile):
  # image + layout + metadata + optional theme, cross-surface parity with browser projectFile.js.
  add_executable(stencil_projectfile_headless
    tests/projectFile.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_projectfile_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_projectfile_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_projectfile_headless COMMAND stencil_projectfile_headless)
  set_tests_properties(stencil_projectfile_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Server-connected transfer check: drives the extracted ProjectTransferController's
  # copy/move-to-server against a real running server (self-skips when none is reachable —
  # point it at one with STENCIL_TEST_SERVER, default http://localhost:8090).
  add_executable(stencil_projecttransfer_headless
    tests/projectTransfer.headless.cpp
    src/app/projectTransferController.cpp
    src/net/serverClient.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp
    src/support/notifications.cpp
    src/support/iconSet.cpp
    src/support/modalReveal.cpp   # notifications' toast dust needs motionReduced()
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_projecttransfer_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_projecttransfer_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)
  add_test(NAME stencil_projecttransfer_headless COMMAND stencil_projecttransfer_headless)
  set_tests_properties(stencil_projecttransfer_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Deep-link check: the stencil:// URL grammar (parseStencilUrl) + the Telegram
  # start-payload codec (golden vectors shared verbatim with browser/tests/
  # deepLink.test.js and the bot's DeepLinkCodecTests.cs) + the browser-fragment
  # URL builder's percent-encoding.
  add_executable(stencil_deeplink_headless
    tests/deepLink.headless.cpp
    src/app/deepLink.cpp
    src/app/launchOptions.cpp
    src/net/serverClient.cpp)
  target_include_directories(stencil_deeplink_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_deeplink_headless PRIVATE stencil_core Qt6::Network)
  add_test(NAME stencil_deeplink_headless COMMAND stencil_deeplink_headless)
  set_tests_properties(stencil_deeplink_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Live push-feed check (net/liveFeed) — drives LiveFeed against a mock QTcpServer that
  # speaks the NDJSON project-events protocol. Links Qt6::Network for QTcpServer/QTcpSocket.
  add_executable(stencil_livefeed_headless
    tests/liveFeed.headless.cpp
    src/net/liveFeed.cpp
    src/net/serverClient.cpp)
  target_include_directories(stencil_livefeed_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_livefeed_headless PRIVATE stencil_core Qt6::Network)
  add_test(NAME stencil_livefeed_headless COMMAND stencil_livefeed_headless)
  set_tests_properties(stencil_livefeed_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Toast coalescing (support/notifications): identical texts refresh the standing
  # toast's lifetime instead of stacking; distinct texts still stack.
  add_executable(stencil_notifications_headless
    tests/notifications.headless.cpp
    src/support/notifications.cpp
    src/support/iconSet.cpp
    src/support/modalReveal.cpp   # the toast dust flight needs motionReduced()
    resources/app.qrc)
  target_include_directories(stencil_notifications_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_notifications_headless PRIVATE Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_notifications_headless COMMAND stencil_notifications_headless)
  set_tests_properties(stencil_notifications_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Servers dialog rows (dialogs/connectDialog): long URLs elide inside the viewport
  # (no horizontal scrollbar), removal retires-then-finalizes (empty state waits for the
  # dust), a new row gathers in as the reverse, and a KIND-FILTER change fades +
  # collapses rows out and back (support/filterFade — not the removal's dust). Mock
  # QTcpServer stands in for the collaboration server.
  add_executable(stencil_connectrow_headless
    tests/connectRow.headless.cpp
    src/support/modalChrome.cpp
    src/dialogs/connectDialog.cpp
    src/net/serverClient.cpp
    src/net/connectionStore.cpp
    src/support/guiHelpers.cpp    # confirmYesNo() backs the disconnect prompts
    src/support/theme.cpp         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp
    src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/searchCombo.cpp   # the rows' All/Admin/Non-admin picker is one of these
    resources/app.qrc)
  target_include_directories(stencil_connectrow_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_connectrow_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)
  add_test(NAME stencil_connectrow_headless COMMAND stencil_connectrow_headless)
  set_tests_properties(stencil_connectrow_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # The shared modal shell's small dialogs (support/modalChrome): the browser's picker
  # (`choose`), the password prompt, the prompt's live validation, the confirm's
  # titleIcon, and the Open In… dialog's in-modal Telegram fallback row.
  add_executable(stencil_modalchrome_headless
    tests/modalChrome.headless.cpp
    src/support/modalChrome.cpp
    src/dialogs/openInDialog.cpp
    src/app/deepLink.cpp
    src/net/serverClient.cpp      # deepLink's origin normalisation
    src/support/iconSet.cpp
    src/support/modalReveal.cpp
    resources/app.qrc)
  target_include_directories(stencil_modalchrome_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_modalchrome_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)
  add_test(NAME stencil_modalchrome_headless COMMAND stencil_modalchrome_headless)
  set_tests_properties(stencil_modalchrome_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # The DESCRIPTION & ATTRIBUTES editors (dialogs/descriptionDialog, keywordsDialog):
  # the shell's structure, Enter/Escape, keyword normalisation, and the store write
  # against an isolated state dir.
  add_executable(stencil_projectmeta_headless
    tests/projectMetaDialogs.headless.cpp
    src/dialogs/descriptionDialog.cpp
    src/dialogs/keywordsDialog.cpp
    src/support/modalChrome.cpp
    src/support/iconSet.cpp
    src/support/modalReveal.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_projectmeta_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_projectmeta_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_projectmeta_headless COMMAND stencil_projectmeta_headless)
  set_tests_properties(stencil_projectmeta_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Stale-session handling (net/serverClient + the Servers row): a refused
  # credential is Expired (never the "unreachable" bucket), is not retried in a
  # loop, keeps the saved connection, and the row offers a labelled Reconnect.
  add_executable(stencil_serverauth_headless
    tests/serverAuth.headless.cpp
    src/support/modalChrome.cpp
    src/dialogs/connectDialog.cpp
    src/net/serverClient.cpp
    src/net/connectionStore.cpp
    src/support/guiHelpers.cpp    # confirmYesNo() backs the disconnect prompts
    src/support/theme.cpp         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp
    src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/searchCombo.cpp   # the rows' All/Admin/Non-admin picker is one of these
    resources/app.qrc)
  target_include_directories(stencil_serverauth_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_serverauth_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)
  add_test(NAME stencil_serverauth_headless COMMAND stencil_serverauth_headless)
  set_tests_properties(stencil_serverauth_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Projects dialog multi-select surface (dialogs/projectsDialog): the batch bar HIDES
  # inapplicable direction buttons by selection composition, Select all / Deselect all
  # toggles over the filtered view, filter/search changes fade + collapse rows out and
  # back (support/filterFade), and batch remove resolves every checked row into a
  # viewport-clipped DisintegrateOverlay. Mock QTcpServer serves the token + /projects
  # listing, so the server-row compositions need no Go server.
  add_executable(stencil_projectsbatch_headless
    tests/projectsBatchBar.headless.cpp
    src/support/modalChrome.cpp
    src/support/tipContent.cpp    # the rows' rich tooltips (appTooltip.hpp calls into it)
    src/dialogs/projectsDialog.cpp
    src/dialogs/expirationDialog.cpp  # the ⋯ menu's "Expiration…" opens it in place now
    src/net/serverClient.cpp
    src/io/fileStore.cpp
    src/support/guiHelpers.cpp
    src/support/theme.cpp         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp
    src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/searchCombo.cpp   # the dialog's filter/sort/mode pickers are these now
    resources/app.qrc)
  target_include_directories(stencil_projectsbatch_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_projectsbatch_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)
  add_test(NAME stencil_projectsbatch_headless COMMAND stencil_projectsbatch_headless)
  set_tests_properties(stencil_projectsbatch_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Server-connected co-edit smoke test: drives the async open/load + guarded-write-merge
  # primitives that MainWindow::openServerProject/saveToServer are built on, with two
  # connections as two editors (self-skips when no server is reachable — point it at one
  # with STENCIL_TEST_SERVER, default http://localhost:8090). Qt6::Gui for QImage decode.
  add_executable(stencil_coedit_headless
    tests/coEdit.headless.cpp
    src/net/serverClient.cpp)
  target_include_directories(stencil_coedit_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_coedit_headless PRIVATE
    stencil_core Qt6::Gui Qt6::Network)
  add_test(NAME stencil_coedit_headless COMMAND stencil_coedit_headless)
  set_tests_properties(stencil_coedit_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # LLM op-plan parser check (src/llm/opPlan) — the llm-contract.md §1-2
  # parse matrix (extraction tolerance, strict per-op validation, limits).
  add_executable(stencil_llmopplan_headless
    tests/llmOpPlan.headless.cpp
    src/llm/opPlan.cpp
    src/llm/opRegistry.cpp
    src/llm/opSchema.cpp
    resources/app.qrc)
  target_include_directories(stencil_llmopplan_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_llmopplan_headless PRIVATE stencil_core Qt6::Core)
  add_test(NAME stencil_llmopplan_headless COMMAND stencil_llmopplan_headless)
  set_tests_properties(stencil_llmopplan_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Shared conformance-fixture corpus (browser/js/config/**/fixtures) + the
  # desktop override map: compile definitions shared by the fixture walkers below.
  set(STENCIL_FIXTURE_WALKER_DEFS
    "STENCIL_CORPUS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../browser/js/config\""
    "STENCIL_OVERRIDES_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtureOverrides.json\"")

  # Op-plan corpus walker: every shared opPlan fixture (profile editor/all)
  # through the real desktop parseOpPlan, honouring knownDivergence.desktop and
  # the local override map (the desktop drift inventory).
  add_executable(stencil_opplanfixtures_headless
    tests/opPlanFixtures.headless.cpp
    src/llm/opPlan.cpp
    src/llm/opRegistry.cpp
    src/llm/opSchema.cpp
    resources/app.qrc)
  target_include_directories(stencil_opplanfixtures_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_opplanfixtures_headless PRIVATE ${STENCIL_FIXTURE_WALKER_DEFS})
  target_link_libraries(stencil_opplanfixtures_headless PRIVATE stencil_core Qt6::Core)
  add_test(NAME stencil_opplanfixtures_headless COMMAND stencil_opplanfixtures_headless)
  set_tests_properties(stencil_opplanfixtures_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Provider-wire + sanitizer corpus walker: the shared §6 wire vectors and the
  # sanitizer vectors through the real LlmClient (mock transport, no network).
  # llmClient.cpp is #included by the test TU for sanitizer access, not compiled here.
  add_executable(stencil_llmwirefixtures_headless
    tests/llmWireFixtures.headless.cpp
    src/llm/opPlan.cpp
    src/llm/opRegistry.cpp
    src/llm/opSchema.cpp
    src/net/serverClient.cpp
    src/net/connectionStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_llmwirefixtures_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_llmwirefixtures_headless PRIVATE ${STENCIL_FIXTURE_WALKER_DEFS})
  target_link_libraries(stencil_llmwirefixtures_headless PRIVATE stencil_core Qt6::Network)
  add_test(NAME stencil_llmwirefixtures_headless COMMAND stencil_llmwirefixtures_headless)
  set_tests_properties(stencil_llmwirefixtures_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # LLM client check (src/llm/llmClient) — the contract §6 wire mappings driven
  # through a mock transport (canned responses; no network).
  add_executable(stencil_llmclient_headless
    tests/llmClient.headless.cpp
    src/llm/llmClient.cpp
    src/llm/opPlan.cpp
    src/llm/opRegistry.cpp
    src/llm/opSchema.cpp
    src/net/serverClient.cpp
    src/net/connectionStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_llmclient_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_llmclient_headless PRIVATE stencil_core Qt6::Network)
  add_test(NAME stencil_llmclient_headless COMMAND stencil_llmclient_headless)
  set_tests_properties(stencil_llmclient_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Persistence-corpus walker: the shared chatDoc / layout / stencilProject
  # fixtures through the real io/fileStore serializers and parsers.
  add_executable(stencil_storefixtures_headless
    tests/storeFixtures.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_storefixtures_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_storefixtures_headless PRIVATE ${STENCIL_FIXTURE_WALKER_DEFS})
  target_link_libraries(stencil_storefixtures_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_storefixtures_headless COMMAND stencil_storefixtures_headless)
  set_tests_properties(stencil_storefixtures_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Deep-link corpus walker: the shared telegramStart golden vectors through the
  # real codec (the launchPayload receiver vectors don't apply — builders only).
  add_executable(stencil_deeplinkfixtures_headless
    tests/deepLinkFixtures.headless.cpp
    src/app/deepLink.cpp
    src/net/serverClient.cpp)
  target_include_directories(stencil_deeplinkfixtures_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_deeplinkfixtures_headless PRIVATE ${STENCIL_FIXTURE_WALKER_DEFS})
  target_link_libraries(stencil_deeplinkfixtures_headless PRIVATE stencil_core Qt6::Network)
  add_test(NAME stencil_deeplinkfixtures_headless COMMAND stencil_deeplinkfixtures_headless)
  set_tests_properties(stencil_deeplinkfixtures_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # LLM plan-executor check (src/llm/planExecutor) — runs a parsed plan with
  # actions + variants against a CanvasWidget-backed target seeded with the
  # PNG fixture, asserting pixel/setting outcomes and the variant images.
  add_executable(stencil_llmexecutor_headless
    tests/llmExecutor.headless.cpp
    src/llm/opPlan.cpp
    src/llm/opRegistry.cpp
    src/llm/opSchema.cpp
    src/llm/planExecutor.cpp
    src/canvas/canvasWidget.cpp
    src/support/theme.cpp
    resources/app.qrc)
  target_include_directories(stencil_llmexecutor_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_compile_definitions(stencil_llmexecutor_headless PRIVATE
    "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
  target_link_libraries(stencil_llmexecutor_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_llmexecutor_headless COMMAND stencil_llmexecutor_headless)
  set_tests_properties(stencil_llmexecutor_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # LLM settings defaults + fileStore Settings JSON round-trip of the contract
  # §5 llm* keys (and the windowState dock blob).
  add_executable(stencil_llmsettings_headless
    tests/llmSettings.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_llmsettings_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_llmsettings_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_llmsettings_headless COMMAND stencil_llmsettings_headless)
  set_tests_properties(stencil_llmsettings_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Motion preferences (support/motionPrefs.hpp) — the three interface modes and the
  # drawing switch, the DisintegrateOverlay factories' particle gate, and the two
  # settings keys. Links fileStore for the JSON round-trip.
  add_executable(stencil_motionprefs_headless
    tests/motionPrefs.headless.cpp
    src/io/fileStore.cpp
    resources/app.qrc)
  target_include_directories(stencil_motionprefs_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_motionprefs_headless PRIVATE stencil_core Qt6::Widgets)
  add_test(NAME stencil_motionprefs_headless COMMAND stencil_motionprefs_headless)
  set_tests_properties(stencil_motionprefs_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Shared-config canon check: the qrc-embedded browser JSON (accents / icons /
  # layoutFields) parses and matches what theme.cpp / iconSet.cpp / fileStore
  # consume — a broken app.qrc alias fails here fast.
  add_executable(stencil_configcanon_headless
    tests/configCanon.headless.cpp
    src/io/fileStore.cpp
    src/support/iconSet.cpp
    src/support/theme.cpp
    resources/app.qrc)
  target_include_directories(stencil_configcanon_headless PRIVATE ${STENCIL_GUI_DIRS})
  target_link_libraries(stencil_configcanon_headless PRIVATE
    stencil_core Qt6::Widgets Qt6::Svg)
  add_test(NAME stencil_configcanon_headless COMMAND stencil_configcanon_headless)
  set_tests_properties(stencil_configcanon_headless PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Every test writes to an ISOLATED state dir, never the dev .stencil the real
  # app uses — a ctest run used to persist test-flipped settings (view toggles,
  # provider) into the developer's own app state.
  get_property(stencil_all_tests DIRECTORY PROPERTY TESTS)
  foreach(t ${stencil_all_tests})
    set_property(TEST ${t} APPEND PROPERTY
      ENVIRONMENT "STENCIL_STATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-state/${t}")
  endforeach()

  # ── Distributable packaging ───────────────────────────────────────────────
  # `cmake --install build` lays the app out under the install prefix; the Qt
  # deploy script then copies the Qt libraries + plugins next to it (macdeployqt /
  # windeployqt, best-effort on Linux) so the result runs on a machine with no Qt
  # installed. `cpack` finally wraps that tree into one platform package:
  # .dmg (macOS) / .zip (Windows) / .tar.gz (Linux). Build releases with
  # -DSTENCIL_DEV_STATE_DIR=OFF (see desktop/README.md → Release packaging).
  install(TARGETS stencil
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)

  if(UNIX AND NOT APPLE)
    # Linux desktop integration: menu entry + scalable icon (shared with the
    # browser favicon), picked up by system menus and AppImage tooling.
    install(FILES packaging/stencil.desktop DESTINATION share/applications)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../browser/favicon.svg
      DESTINATION share/icons/hicolor/scalable/apps RENAME stencil.svg)
    # .stencil file-type association: the shared-mime-info definition (registers the
    # application/x-stencil type + *.stencil glob) plus its themed file icon, so the file
    # manager gives .stencil files the Stencil logo and double-click-to-open.
    install(FILES packaging/stencil-mime.xml DESTINATION share/mime/packages)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../browser/favicon.svg
      DESTINATION share/icons/hicolor/scalable/mimetypes RENAME application-x-stencil.svg)
  endif()

  # Qt's own deployment helper (Qt >= 6.3) runs the right *deployqt tool at install
  # time. The keyword that captures the generated script path was renamed in Qt 6.5
  # (FILENAME_VARIABLE → OUTPUT_SCRIPT), so pick it per version. Older Qt: skip it;
  # the install tree then has no bundled Qt and the manual *deployqt step is
  # documented in desktop/README.md.
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt6_generate_deploy_app_script(
      TARGET stencil
      OUTPUT_SCRIPT STENCIL_DEPLOY_SCRIPT
      NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${STENCIL_DEPLOY_SCRIPT})
  elseif(Qt6_VERSION VERSION_GREATER_EQUAL 6.3)
    qt6_generate_deploy_app_script(
      TARGET stencil
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
  message(STATUS "Qt6 not found — skipping stencil (core builds via ../core)")
endif()

# The WebAssembly target moved to core/CMakeLists.txt (it builds the shared core, not
# anything desktop-specific). Configure core through emcmake to produce stencil_core.js.
