# The GUI's include dirs and its full translation-unit set (minus main.cpp), shared by
# the app and every test target that links the whole GUI. Included from CMakeLists.txt
# inside the Qt6_FOUND block.

# Each part appends to the one GUI set in this order, so the order is the translation units' own.
include(${CMAKE_CURRENT_LIST_DIR}/sources/dirs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/app.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/llm.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/canvas.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/dialogs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/io.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/support.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/sources/platform.cmake)
