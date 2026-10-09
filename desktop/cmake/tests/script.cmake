# The .stc script: its window, its flyout, the shared buffer, the runner and its awaits, and the
# OS-driven opens (with the canvas dust clock registered among them).

# The script window's Ctrl+Enter, its popover bar and the faded placeholder.
stencil_headless_test(stencil_scriptwindowkeys_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/script/scriptWindowKeys.headless.cpp src/dialogs/script/ScriptDialog.cpp src/dialogs/script/ScriptDialogFile.cpp
    src/dialogs/script/ScriptMenuPanel.cpp src/dialogs/script/ScriptMenuPanelState.cpp
    src/dialogs/script/ScriptEditorWidget.cpp src/dialogs/script/ScriptEditorWidgetKeys.cpp src/dialogs/script/ScriptHighlighter.cpp
    src/model/ScriptBuffer.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The script window (dialogs/ScriptDialog): the empty-editor gate, "report nothing until run",
# and the core-driven colouring.
stencil_headless_test(stencil_scriptdialog_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/script/scriptDialog.headless.cpp src/dialogs/script/ScriptDialog.cpp src/dialogs/script/ScriptDialogFile.cpp
    src/dialogs/script/ScriptEditorWidget.cpp src/dialogs/script/ScriptEditorWidgetKeys.cpp src/dialogs/script/ScriptHighlighter.cpp
    src/model/ScriptBuffer.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The script FLYOUT (dialogs/ScriptMenuPanel): the window's gates, plus Tab and Ctrl+Enter.
stencil_headless_test(stencil_scriptmenupanel_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/script/scriptMenuPanel.headless.cpp src/dialogs/script/ScriptMenuPanel.cpp
    src/dialogs/script/ScriptMenuPanelState.cpp src/dialogs/script/ScriptEditorWidget.cpp src/dialogs/script/ScriptEditorWidgetKeys.cpp
    src/dialogs/script/ScriptHighlighter.cpp src/model/ScriptBuffer.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The ONE .stc both hosts edit (model/ScriptBuffer): shared, kept, cleared, never on disk.
stencil_headless_test(stencil_scriptbuffer_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/model/scriptBuffer.headless.cpp src/dialogs/script/ScriptDialog.cpp src/dialogs/script/ScriptDialogFile.cpp
    src/dialogs/script/ScriptMenuPanel.cpp src/dialogs/script/ScriptMenuPanelState.cpp
    src/dialogs/script/ScriptEditorWidget.cpp src/dialogs/script/ScriptEditorWidgetKeys.cpp src/dialogs/script/ScriptHighlighter.cpp
    src/model/ScriptBuffer.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The same buffer reaching a hidden host: no per-keystroke re-lex, a catch-up on show.
stencil_headless_test(stencil_scriptbufferhidden_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/model/scriptBufferHidden.headless.cpp src/dialogs/script/ScriptDialog.cpp src/dialogs/script/ScriptDialogFile.cpp
    src/dialogs/script/ScriptMenuPanel.cpp src/dialogs/script/ScriptMenuPanelState.cpp
    src/dialogs/script/ScriptEditorWidget.cpp src/dialogs/script/ScriptEditorWidgetKeys.cpp src/dialogs/script/ScriptHighlighter.cpp
    src/model/ScriptBuffer.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The .stc runner (app/scriptRun.cpp): which op reaches which PlanTarget call; an error runs nothing.
stencil_headless_test(stencil_scriptrunner_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/app/scriptRunner.headless.cpp src/app/scriptRun.cpp src/app/scriptRunOps.cpp
    src/model/ScriptDoc.cpp
    ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_PLANEXECUTOR_SOURCES} ${STENCIL_CANVAS_SOURCES}
    src/canvas/overlay/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../common/samples\""
  LIBS stencil_core Qt6::Widgets)

# The same runner across the loads that wait on I/O: parked, resumed, or ended by a failed answer.
stencil_headless_test(stencil_scriptrunnerawait_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/app/scriptRunnerAwait.headless.cpp src/app/scriptRun.cpp src/app/scriptRunOps.cpp
    src/model/ScriptDoc.cpp
    ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_PLANEXECUTOR_SOURCES} ${STENCIL_CANVAS_SOURCES}
    src/canvas/overlay/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../common/samples\""
  LIBS stencil_core Qt6::Widgets)

# The canvas image's own dust clock (CANVAS_DUST_MS), 1.5x every other cloud; motion is left ON.
stencil_headless_test(stencil_canvasdust_headless
  SOURCES tests/canvas/canvasDust.headless.cpp
  LIBS stencil_gui_objs Qt6::Test
  INCLUDE_TESTS)

# The OS-driven .stc entries on the real MainWindow: a shell open and a drop both RUN the script.
stencil_headless_test(stencil_scriptopen_headless
  SOURCES tests/dialogs/script/scriptOpen.headless.cpp
  LIBS stencil_gui_objs
  ENV STENCIL_NO_ANIM=1)
