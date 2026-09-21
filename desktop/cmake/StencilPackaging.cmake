# Everything that turns the built `stencil` target into something shippable: the
# macOS app icon and bundle shape, the install rules, the Qt deploy script and CPack.
# Included from CMakeLists.txt after the app target exists.

# App icon: browser/favicon.svg — the artwork every front-end shares — rasterised into the
# container each platform names, by packaging/mkicon.cpp built here as a host tool. Nothing
# binary is committed, and no OS image tool takes part: the .icns and .ico are written
# directly, so a Linux or CI box needs only Qt.
set(STENCIL_BUNDLE_ICON_FILE "")
set(STENCIL_BUNDLE_ICON_NAME "")
if(APPLE OR WIN32)
  set(STENCIL_FAVICON "${CMAKE_CURRENT_SOURCE_DIR}/../browser/favicon.svg")
  add_executable(stencil_mkicon packaging/mkicon.cpp)
  target_link_libraries(stencil_mkicon PRIVATE Qt6::Gui Qt6::Svg)
endif()

if(APPLE)
  # The flat .icns named by CFBundleIconFile. Without it the bundle ships no icon at all and
  # the Dock/Finder fall back to a generic one.
  set(_icns "${CMAKE_CURRENT_BINARY_DIR}/stencil.icns")
  add_custom_command(OUTPUT "${_icns}"
    COMMAND stencil_mkicon "${STENCIL_FAVICON}" "${_icns}"
    DEPENDS stencil_mkicon "${STENCIL_FAVICON}"
    COMMENT "stencil: writing the app icon into stencil.icns"
    VERBATIM)
  target_sources(stencil PRIVATE "${_icns}")
  set_source_files_properties("${_icns}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources)
  set(STENCIL_BUNDLE_ICON_FILE "stencil.icns")

  # Themed AppIcon (macOS 26 "Icon & widget style": Default / Dark / Tinted, with Clear
  # OS-derived). macOS 26 derives all four appearances from ONE layered `.icon` design — a
  # background fill plus a foreground layer — which only `actool` can compile into an
  # Assets.car. The older asset-catalog route is silently dropped by actool, so this is the
  # only shape the OS actually tints. actool ships with full Xcode and NOT the Command Line
  # Tools, so it stays best-effort: without it the plain .icns above still ships.
  # /usr/bin/actool is only a shim forwarding to the active developer dir, and it errors out
  # when that is a Command Line Tools instance — so never accept it, and never use find_program
  # here: with DEVELOPER_DIR unset its hint collapses to /usr/bin and the shim always wins.
  execute_process(COMMAND /usr/bin/xcrun --find actool
    OUTPUT_VARIABLE _xcrun_actool OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  set(STENCIL_ACTOOL "")
  foreach(_candidate
      "$ENV{DEVELOPER_DIR}/usr/bin/actool"
      "${_xcrun_actool}"
      "/Applications/Xcode.app/Contents/Developer/usr/bin/actool")
    if(NOT STENCIL_ACTOOL
       AND NOT "${_candidate}" STREQUAL "/usr/bin/actool"
       AND EXISTS "${_candidate}")
      execute_process(COMMAND "${_candidate}" --version
        RESULT_VARIABLE _actool_rc OUTPUT_QUIET ERROR_QUIET)
      if(_actool_rc EQUAL 0)
        set(STENCIL_ACTOOL "${_candidate}")
      endif()
    endif()
  endforeach()

  if(STENCIL_ACTOOL)
    # DEVELOPER_DIR is what actool resolves its toolchain through; derive it from the binary
    # we picked so a Command Line Tools instance in the environment cannot win.
    string(REPLACE "/usr/bin/actool" "" _developer_dir "${STENCIL_ACTOOL}")
    set(_iconbundle "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icon")
    set(_foreground "${_iconbundle}/Assets/foreground.png")
    set(_car "${CMAKE_CURRENT_BINARY_DIR}/Assets.car")

    # One group: the transparent foreground over an automatic gradient in Stencil's panel
    # colour. The OS composites and themes it; there is no per-appearance artwork.
    add_custom_command(OUTPUT "${_iconbundle}/icon.json"
      COMMAND "${CMAKE_COMMAND}" -E copy
              "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/icon.json"
              "${_iconbundle}/icon.json"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/icon.json"
      VERBATIM)

    # Full resolution; the OS scales it per slot. The dark tile comes from the fill above.
    add_custom_command(OUTPUT "${_foreground}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_iconbundle}/Assets"
      COMMAND stencil_mkicon
              "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/appicon-foreground.svg"
              "${_foreground}" 1024
      DEPENDS stencil_mkicon "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/appicon-foreground.svg"
      VERBATIM)
    add_custom_command(OUTPUT "${_car}"
      COMMAND "${CMAKE_COMMAND}" -E env "DEVELOPER_DIR=${_developer_dir}"
              "${STENCIL_ACTOOL}" "${_iconbundle}"
              --compile "${CMAKE_CURRENT_BINARY_DIR}"
              --platform macosx
              --minimum-deployment-target 26.0
              --app-icon AppIcon
              --output-partial-info-plist "${CMAKE_CURRENT_BINARY_DIR}/appicon-partial.plist"
              --errors --warnings
      DEPENDS "${_foreground}" "${_iconbundle}/icon.json"
      COMMENT "stencil: compiling the themed AppIcon into Assets.car"
      VERBATIM)
    target_sources(stencil PRIVATE "${_car}")
    set_source_files_properties("${_car}" PROPERTIES
      GENERATED TRUE
      MACOSX_PACKAGE_LOCATION Resources)
    set(STENCIL_BUNDLE_ICON_NAME "AppIcon")
    message(STATUS "stencil: themed AppIcon — dark/tinted aware")
  else()
    message(STATUS "stencil: actool not found (needs full Xcode) — plain .icns only, no OS icon theming")
  endif()
elseif(WIN32)
  # The exe's own resource icon: what Explorer, the taskbar and a shortcut show before Qt is
  # running (main.cpp's setWindowIcon only reaches a live window).
  enable_language(RC)
  set(_ico "${CMAKE_CURRENT_BINARY_DIR}/stencil.ico")
  add_custom_command(OUTPUT "${_ico}"
    COMMAND stencil_mkicon "${STENCIL_FAVICON}" "${_ico}"
    DEPENDS stencil_mkicon "${STENCIL_FAVICON}"
    COMMENT "stencil: writing the app icon into stencil.ico"
    VERBATIM)
  add_custom_target(stencil_icon DEPENDS "${_ico}")
  # Forward slashes on purpose: the RC compiler reads a backslash in a quoted path as an escape.
  set(_rc "${CMAKE_CURRENT_BINARY_DIR}/stencil.rc")
  file(WRITE "${_rc}" "IDI_ICON1 ICON \"${_ico}\"\n")
  target_sources(stencil PRIVATE "${_rc}")
  add_dependencies(stencil stencil_icon)
  message(STATUS "stencil: app icon compiled into the exe (stencil.ico)")
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
