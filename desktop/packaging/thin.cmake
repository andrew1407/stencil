# Thins every universal Mach-O in a deployed .app to the one architecture the app was built for,
# then re-seals the bundle, since thinning breaks each framework's seal. Official Qt and its FFmpeg
# ship arm64 + x86_64; the other slice can never run beside a single-arch executable.
#   cmake -DAPP=<stencil.app> -DARCH=<arm64|x86_64|empty = keep all> [-DSIGN=<identity>] -P thin.cmake
if(NOT SIGN)
  set(SIGN -)  # ad hoc
endif()

set(_thinned 0)
if(ARCH)
  file(GLOB_RECURSE _files LIST_DIRECTORIES false "${APP}/*")
  foreach(_f IN LISTS _files)
    if(IS_SYMLINK "${_f}")
      continue()
    endif()
    execute_process(COMMAND lipo -archs "${_f}" RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _archs OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REPLACE " " ";" _archs "${_archs}")
    list(LENGTH _archs _count)
    if(NOT _rc EQUAL 0 OR _count LESS 2 OR NOT ARCH IN_LIST _archs)
      continue()
    endif()
    execute_process(COMMAND lipo "${_f}" -thin ${ARCH} -output "${_f}.thin" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "lipo could not thin ${_f} to ${ARCH}")
    endif()
    file(RENAME "${_f}.thin" "${_f}")
    math(EXPR _thinned "${_thinned} + 1")
  endforeach()
endif()

execute_process(COMMAND codesign --force --deep --sign "${SIGN}" "${APP}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "codesign could not seal ${APP}")
endif()
message(STATUS "Thinned ${_thinned} universal binaries to '${ARCH}', sealed ${APP} (${SIGN})")
