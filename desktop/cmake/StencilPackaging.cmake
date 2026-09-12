# Everything that turns the built `stencil` target into something shippable: the
# macOS app icon and bundle shape, the install rules, the Qt deploy script and CPack.
# Included from CMakeLists.txt after the app target exists.

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
