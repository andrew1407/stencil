# The consoles: the command grammar and table, the handlers' parity with it, the typewriter pace
# and the prompt line, and the CLI stage's banner maths and skins.

# The line grammar, the table's rows and aliases, the completions and the help layout (support/console/consoleGrammar).
stencil_headless_test(stencil_consolegrammar_headless
  SOURCES tests/support/console/consoleGrammar.headless.cpp ${STENCIL_CONSOLE_GRAMMAR_SOURCES} resources/app.qrc
  LIBS Qt6::Gui)

# Every desktop row has a handler and every handler a row (app/console/consoleCommands); the
# handlers reach the window, so the whole GUI comes along.
stencil_headless_test(stencil_consolecommands_headless
  SOURCES tests/app/console/consoleCommands.headless.cpp
  LIBS stencil_gui_objs)

# The console text speed, the reveal pace it sets and the typewriter over a fake clock, the prompt
# line, and the setting's round trip (support/console).
stencil_headless_test(stencil_consolepace_headless
  SOURCES tests/support/console/consolePace.headless.cpp ${STENCIL_CONSOLETEXT_SOURCES}
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Gui)

# The banner's cells out of the core rows, the clock-hand wipe and its schedule, the skin table,
# hues, pictures and the cat (app/cli/CliBanner, CliSkins).
stencil_headless_test(stencil_clibanner_headless
  SOURCES tests/app/cli/cliBanner.headless.cpp ${STENCIL_CLI_BANNER_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Gui)
