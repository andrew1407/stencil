# Relinks a deployed ICU data library around the package packaging/trimicu.cpp trims. The data
# symbol and soname stay ICU's own, so the bundled libicuuc binds to it unchanged; a Qt that
# bundles no ICU (a distro build) leaves nothing to trim.
#   cmake -DLIB=<lib dir> -DTOOL=<stencil_trimicu> -DCXX=<compiler> -DWORK=<scratch dir> -P trimicu.cmake

# What the tool drops is proven unread by Qt 6.9's ICU calls against ICU 73's data; another ICU
# ships whole until that proof is redone.
set(_verified_major 73)

file(GLOB _data LIST_DIRECTORIES false "${LIB}/libicudata.so.*.*")
if(NOT _data)
  message(STATUS "No bundled ICU in ${LIB}, nothing to trim")
  return()
endif()
get_filename_component(_name "${_data}" NAME)
string(REGEX MATCH "^libicudata\\.so\\.([0-9]+)\\." _match "${_name}")
if(NOT CMAKE_MATCH_1 STREQUAL _verified_major)
  message(WARNING "ICU ${CMAKE_MATCH_1} is not the proven ICU ${_verified_major}: ${_name} ships whole")
  return()
endif()
set(_symbol "icudt${_verified_major}_dat")

file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${TOOL}" "${_data}" "${WORK}/icudata.dat" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "stencil_trimicu could not trim ${_data}")
endif()

file(WRITE "${WORK}/icudata.s"
  ".section .rodata\n.balign 16\n.globl ${_symbol}\n.type ${_symbol}, %object\n${_symbol}:\n"
  ".incbin \"${WORK}/icudata.dat\"\n.size ${_symbol}, . - ${_symbol}\n"
  ".section .note.GNU-stack, \"\", %progbits\n")
execute_process(
  COMMAND "${CXX}" -shared -nostdlib "-Wl,-soname,libicudata.so.${_verified_major}"
          -o "${_data}.trimmed" "${WORK}/icudata.s"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${CXX} could not link the trimmed ${_name}")
endif()
file(RENAME "${_data}.trimmed" "${_data}")
message(STATUS "Relinked ${_data} around the trimmed ICU data")
