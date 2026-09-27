# The distributable: `cmake --install build` lays the app out under the install prefix, Qt's
# deploy script copies the Qt libraries and plugins in beside it (macdeployqt / windeployqt /
# the generic Linux deploy), and `cpack` wraps that tree into a .dmg / .zip / .tar.gz. Build
# releases with -DSTENCIL_DEV_STATE_DIR=OFF; `packaging/smoke.sh` / `smoke.ps1` start the result.
install(TARGETS stencil
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION bin)

# The installed binary looks for Qt where the deploy puts it; the build tree keeps Qt's own path.
# macdeployqt adds no rpath to a binary the install already stripped, so without this it aborts.
if(APPLE)
  set_target_properties(stencil PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX)
  set_target_properties(stencil PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()

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
  # No QTranslator loads Qt's own catalogues, so none ship.
  set(_stencil_deploy NO_TRANSLATIONS)
  if(WIN32)
    # The VC++ runtime rides app-local instead of windeployqt's 18 MB installer; when CMake
    # finds no redist beside the compiler, the installer stays.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin)
      list(APPEND _stencil_deploy NO_COMPILER_RUNTIME)
    endif()
    # A QPainter app draws no GL and no D3D12, so the software OpenGL and the DXC compiler stay
    # out; d3dcompiler_47 is in System32 on every Windows Qt 6 runs on.
    list(APPEND _stencil_deploy DEPLOY_TOOL_OPTIONS
      --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler)
  endif()
  qt6_generate_deploy_app_script(
    TARGET stencil
    OUTPUT_SCRIPT STENCIL_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${_stencil_deploy})
  install(SCRIPT ${STENCIL_DEPLOY_SCRIPT})
  # The generic Linux deploy copies only the plugins Qt's finalizer lists for a target marked
  # above, and a plain add_executable is never finalized; the *deployqt tools find their own.
  if(UNIX AND NOT APPLE)
    qt6_finalize_target(stencil)
  endif()
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
