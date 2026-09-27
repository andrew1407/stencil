# The suites over the whole GUI: its one object library, the MainWindow areas read off disk,
# the docs capture, the menu and flyout suites, and the UI pins.

# The whole GUI, compiled ONCE for every target below. OBJECT (not STATIC) so app.qrc's resource
# initialiser links in; only the app bakes STENCIL_STATE_DIR, which a test must never pick up.
add_library(stencil_gui_objs OBJECT ${STENCIL_GUI_SOURCES})
target_include_directories(stencil_gui_objs PUBLIC ${STENCIL_GUI_DIRS})
target_link_libraries(stencil_gui_objs PUBLIC stencil_core Qt6::Widgets Qt6::Network
  Qt6::Multimedia Qt6::Svg ${STENCIL_SHARE_LIBS})

# One QtTest binary per MainWindow area, read off disk so the list cannot drift; STENCIL_NO_ANIM,
# because a dialog's grow-from-the-icon flight would resize the window under the test.
file(GLOB_RECURSE _mw_gui CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/tests/MainWindow.*.gui.cpp)
foreach(_src IN LISTS _mw_gui)
  get_filename_component(_name ${_src} NAME)
  string(REGEX REPLACE "^MainWindow\\.(.+)\\.gui\\.cpp$" "\\1" _area ${_name})
  string(TOLOWER ${_area} _area_lc)
  stencil_headless_test(stencil_mainwindow_${_area_lc}_gui
    SOURCES ${_src}
    LIBS stencil_gui_objs Qt6::Test
    ENV STENCIL_NO_ANIM=1)
endforeach()

stencil_headless_test(stencil_cropfit_headless
  SOURCES tests/dialogs/crop/cropPreviewFitBox.headless.cpp
  LIBS stencil_gui_objs Qt6::Widgets)

# The use-case screenshot capture behind usecases/docs/desktop/img: an opt-in binary ctest never
# runs, driven by usecases/capture-runner/desktop.mjs.
if(STENCIL_DOCS_CAPTURE)
  set(_docs ${CMAKE_CURRENT_SOURCE_DIR}/../usecases/capture-runner/desktop)
  add_executable(stencil_docs_capture ${_docs}/captureUseCases.cpp ${_docs}/captureStates.cpp
    ${_docs}/captureAssistant.cpp ${_docs}/captureDialogs.cpp ${_docs}/captureVideo.cpp)
  target_include_directories(stencil_docs_capture PRIVATE ${STENCIL_GUI_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests ${_docs})
  target_compile_definitions(stencil_docs_capture PRIVATE
    "STENCIL_UI_PINS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/pins\"")
  target_link_libraries(stencil_docs_capture PRIVATE stencil_gui_objs Qt6::Test)
endif()


# Hovering a context-menu row: the icon play and the keycap shake start once on arrival and stop
# on leaving, though QMenu::hovered re-fires; drives a real QMenu.
stencil_headless_test(stencil_menuhover_headless
  SOURCES tests/support/menu/menuHover.headless.cpp
  LIBS stencil_gui_objs Qt6::Test
  INCLUDE_TESTS)

# The app-wide pointer cursor (support/guiHelpers.cpp installPointerCursor) over real controls and a menu.
stencil_headless_test(stencil_pointercursor_headless
  SOURCES tests/support/pointerCursor.headless.cpp
  LIBS stencil_gui_objs Qt6::Test)

# The 'slide' entrance of a selector's list (support/menu/popupSlide.hpp): the first frame's maths.
stencil_headless_test(stencil_popupslide_headless
  SOURCES tests/support/menu/popupSlide.headless.cpp
  LIBS Qt6::Gui
  INCLUDE_TESTS)

# Alt+hover selector peeks (support/tip/altPeek, support/menu/comboAltPeek) over live combos.
stencil_headless_test(stencil_altpeek_headless
  SOURCES tests/support/menu/comboAltPeek.headless.cpp tests/support/tip/altPeek.headless.cpp
  LIBS stencil_gui_objs Qt6::Test
  INCLUDE_TESTS)

# The assistant flyout's boxes (llm/ChatMenuPanel) against the browser sheet they port: the chat
# panel at MENU scale, while the dock keeps its own, larger, chip.
stencil_headless_test(stencil_chatmenupanel_headless
  SOURCES tests/llm/panel/chatMenuPanel.headless.cpp
  LIBS stencil_gui_objs Qt6::Test
  INCLUDE_TESTS)

# Appearance pins: the app stylesheet hashed per theme x accent, plus the rendered states against
# tests/pins. Registered TWICE: QT_SCALE_FACTOR (dpr 2) is read once per process.
stencil_headless_test(stencil_uipins_headless
  SOURCES tests/uiPins.headless.cpp tests/uiPins.states.cpp
  DEFS "STENCIL_UI_PINS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/pins\""
  LIBS stencil_gui_objs Qt6::Test
  ENV STENCIL_NO_ANIM=1)
add_test(NAME stencil_uipins_hidpi_headless COMMAND stencil_uipins_headless)
set_tests_properties(stencil_uipins_hidpi_headless PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;STENCIL_NO_ANIM=1;QT_SCALE_FACTOR=2")
