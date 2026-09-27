# The desktop's ctest targets: one stencil_headless_test() call each, then the shared
# per-test state dir. Included from CMakeLists.txt inside the Qt6_FOUND block.

# Every headless/GUI test target below is the same five calls, so they are one call
# here: build `name` from SOURCES, link LIBS, register it with CTest, and run it
# offscreen. Group dirs are always on the include path (headers are included bare, and
# the core/*.hpp they pull in arrive transitively from stencil_core); INCLUDE_TESTS adds
# tests/ for the suites with a shared helper there. DEFS and ENV are appended as given.
function(stencil_headless_test name)
  cmake_parse_arguments(T "INCLUDE_TESTS" "" "SOURCES;LIBS;DEFS;ENV" ${ARGN})
  add_executable(${name} ${T_SOURCES})
  set(_dirs ${STENCIL_GUI_DIRS})
  if(T_INCLUDE_TESTS)
    list(APPEND _dirs ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  endif()
  target_include_directories(${name} PRIVATE ${_dirs})
  if(T_DEFS)
    target_compile_definitions(${name} PRIVATE ${T_DEFS})
  endif()
  target_link_libraries(${name} PRIVATE ${T_LIBS})
  add_test(NAME ${name} COMMAND ${name})
  set(_env "QT_QPA_PLATFORM=offscreen" ${T_ENV})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_env}")
endfunction()

# The suites by area, in registration order (the ctest numbering follows it).
include(${CMAKE_CURRENT_LIST_DIR}/tests/canvas.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/app.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/controls.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/gui.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/store.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/modal.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/server.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/llm.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/script.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/canon.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests/guards.cmake)

# Every test writes to an ISOLATED state dir, never the dev .stencil the real
# app uses — a ctest run used to persist test-flipped settings (view toggles,
# provider) into the developer's own app state.
get_property(stencil_all_tests DIRECTORY PROPERTY TESTS)
foreach(t ${stencil_all_tests})
  set_property(TEST ${t} APPEND PROPERTY
    ENVIRONMENT "STENCIL_STATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-state/${t}")
endforeach()
