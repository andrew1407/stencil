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
# 6.3 is the floor: the install step below needs qt6_generate_deploy_app_script (Qt >= 6.3).
if(STENCIL_REQUIRE_QT)
  find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets Network Multimedia Svg Test)
else()
  find_package(Qt6 6.3 QUIET COMPONENTS Widgets Network Multimedia Svg Test)
endif()
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)

  include(cmake/StencilSources.cmake)   # STENCIL_GUI_DIRS / STENCIL_GUI_SOURCES / STENCIL_SHARE_LIBS

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
  message(STATUS "Qt6 found — building stencil (${Qt6_VERSION})")

  include(cmake/StencilPackaging.cmake)  # app icon, bundle shape, install + CPack
  include(cmake/StencilTests.cmake)      # every ctest target
else()
  message(STATUS "Qt6 not found — skipping stencil (core builds via ../core)")
endif()

# The WebAssembly target moved to core/CMakeLists.txt (it builds the shared core, not
# anything desktop-specific). Configure core through emcmake to produce stencil_core.js.
