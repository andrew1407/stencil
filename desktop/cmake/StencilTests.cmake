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


# Headless functional check of the crop integration (CanvasWidget + cropGeometry).
# Runs offscreen (QT_QPA_PLATFORM=offscreen), so it needs no display. Kept out of
# stencil_tests (which is deliberately Qt-free) and built only when Qt is present.
stencil_headless_test(stencil_crop_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/cropCanvas.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Modal-popover placement (support/popover.hpp) — the pure clamp behind the anchored
# compact dialogs; header-only, so the test is the only extra compile unit.
stencil_headless_test(stencil_popover_headless
  SOURCES tests/popover.headless.cpp
  LIBS Qt6::Core)

# The theme wipe's easing (support/ThemeSwapOverlay.hpp) — header-only, so the test is
# the only compile unit. Holds the curve to the AREA it sweeps, like the browser's
# tests/motion.test.js does for its half.
stencil_headless_test(stencil_themeswapease_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_THEMESWAP_SOURCES}
    tests/themeSwapEase.headless.cpp
  LIBS Qt6::Widgets)

# Rich control tooltips (support/tipContent.cpp) — the desktop port of the browser's
# tipContent.js, carrying that suite's parse/render cases. Needs theme.cpp for the palette.
stencil_headless_test(stencil_tipcontent_headless
  SOURCES tests/tipContent.headless.cpp src/support/tipContent.cpp ${STENCIL_THEME_SOURCES}
    resources/app.qrc
  LIBS Qt6::Widgets)

# CSS colours with alpha (support/cssColor.hpp) — the forms Qt's own QColor does not
# parse, and the vocabulary it disagrees with core on. Header-only, but it resolves
# through core::parseColor so the screen and the export path read a colour the same way.
stencil_headless_test(stencil_csscolor_headless
  SOURCES tests/cssColor.headless.cpp
  LIBS stencil_core Qt6::Gui)

# Closing a shape and the ways back out of one (canvas/chainEdit.hpp) — the ring
# helpers plus the Alt+Ctrl pull-out gesture on the real widget.
stencil_headless_test(stencil_chainedit_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/chainEdit.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Stroke growth (canvas/strokeGrowth.hpp) — where a just-added vertex is at a given
# instant, and the flight bookkeeping behind it. Header-only, but it names core::Point /
# core::Line, so it links the core.
stencil_headless_test(stencil_strokegrowth_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/strokeGrowth.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Fullscreen band + zoom-handoff arithmetic (app/FullscreenController.hpp) — the edge
# hysteresis and the viewport ratio; header-only, so the test is the only compile unit.
stencil_headless_test(stencil_fullscreencontroller_headless
  SOURCES tests/FullscreenController.headless.cpp
  LIBS Qt6::Core)

# Persistence gates + debounces (app/SessionController.hpp) — what stops a write, and
# that a burst of edits coalesces into one; header-only, so the test is the only unit.
stencil_headless_test(stencil_sessioncontroller_headless
  SOURCES tests/SessionController.headless.cpp
  LIBS Qt6::Core)

# What a press outside an open compact popover means (app/PopoverHost.hpp) — including
# the logo/accent exception; header-only, so the test is the only compile unit.
stencil_headless_test(stencil_popoverhost_headless
  SOURCES tests/PopoverHost.headless.cpp
  LIBS Qt6::Widgets)

# The project-name group's chip visibility (app/ProjectNameBar.hpp) — which of ✓/✗/✎/🎨
# belong in the header row per state; header-only, so the test is the only compile unit.
stencil_headless_test(stencil_projectnamebar_headless
  SOURCES tests/ProjectNameBar.headless.cpp
  LIBS Qt6::Core)

# Display-unit and px→cm arithmetic (app/UnitsController.hpp) — the twin of the browser's
# units.js length rule; header-only, so the test is the only compile unit.
stencil_headless_test(stencil_unitscontroller_headless
  SOURCES tests/UnitsController.headless.cpp
  LIBS Qt6::Core)

# Scroll-reveal curve (support/scrollReveal.hpp) — how dim a row is at a given spot in its
# scroller; header-only, so the test is the only extra compile unit.
stencil_headless_test(stencil_scrollreveal_headless
  SOURCES tests/scrollReveal.headless.cpp
  LIBS Qt6::Widgets)

# Name display-shortening (support/displayName.hpp) — the middle-ellipsis behind the
# project dialogs/notifications; header-only, so the test is the only extra compile unit.
stencil_headless_test(stencil_displayname_headless
  SOURCES tests/displayName.headless.cpp
  LIBS Qt6::Core)

# The toggle face swap (support/faceSwap.hpp) — the shared exchange behind Draw's
# Start/Stop and Line/Rect: the curve, plus the driver on a live QToolButton (converges,
# flips the caller's state once, survives rapid supersession, obeys reduced motion).
stencil_headless_test(stencil_faceswap_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/faceSwap.headless.cpp ${STENCIL_FACESWAP_SOURCES}
    src/support/iconSet.cpp       # the glyphs it turns
    src/support/modalReveal.cpp   # motionReduced()
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg)

# Form-control state swaps (support/controlSwap.hpp) — the checkbox's particle toggle
# and the combo's value exchange, driven on live widgets under the real app stylesheet
# (converge on the true state, no geometry change, rapid changes land, reduced motion
# goes straight to the end state). Needs theme for the QSS the indicator is drawn from,
# iconSet for faceSwap's glyphs, menuReveal for the dropped list's dust and modalReveal
# for motionReduced().
stencil_headless_test(stencil_controlswap_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_CONTROLSWAP_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/controlSwap.headless.cpp ${STENCIL_THEME_SOURCES} src/support/iconSet.cpp
    ${STENCIL_FACESWAP_SOURCES} src/support/menuReveal.cpp src/support/modalReveal.cpp
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg
  INCLUDE_TESTS)

# Per-icon hover motion (support/iconMotion.hpp) — the table against the glyph canon,
# the semantic pins (plus grows / minus shrinks / the fullscreen corners invert / the
# trash hinges at its lid), and the driver on a live button. Needs iconSet for the
# glyphs it poses and modalReveal for motionReduced().
stencil_headless_test(stencil_iconmotion_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_ICONMOTION_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/iconMotion.headless.cpp src/support/iconSet.cpp src/support/modalReveal.cpp
    ${STENCIL_FACESWAP_SOURCES} resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg
  INCLUDE_TESTS)

# The whole GUI, compiled ONCE for every target below that needs it instead of once per
# binary. OBJECT (not STATIC) so app.qrc's resource initialiser links in directly. The
# app keeps its own compile: only it bakes STENCIL_STATE_DIR, which a test must never
# pick up (every test writes to the isolated state dir set at the bottom of this file).
add_library(stencil_gui_objs OBJECT ${STENCIL_GUI_SOURCES})
target_include_directories(stencil_gui_objs PUBLIC ${STENCIL_GUI_DIRS})
target_link_libraries(stencil_gui_objs PUBLIC stencil_core Qt6::Widgets Qt6::Network
  Qt6::Multimedia Qt6::Svg ${STENCIL_SHARE_LIBS})

# GUI end-to-end (Qt Test framework): drives the REAL MainWindow — load via the OS-open
# path, trigger the actual Rotate/Undo/Start-Drawing QActions, and send real mouse clicks
# to the live canvas. One binary per feature area (tests/MainWindow.<area>.gui.cpp), not
# one binary over several TUs: ctest then runs the areas in parallel, and each area keeps
# the single MainWindowGuiTest class name the two production headers befriend. The chat is
# four of them because it is much the largest area — one binary was longer than the whole
# rest of the suite run in parallel, so it alone set the wall.
# STENCIL_NO_ANIM: these tests open dialogs and act on them at once, so the
# grow-from-the-icon flight (support/modalReveal) is off here — it would resize the
# window under the test for its first ~200 ms, which is exactly the kind of timing the
# suite must not depend on.
foreach(area chatCards chatCompact chatDock chatPanel chatTurns canvas chrome composition
             menus menuKeys motion projects theme toolbar tooltips)
  string(TOLOWER ${area} _area_lc)
  stencil_headless_test(stencil_mainwindow_${_area_lc}_gui
    SOURCES tests/MainWindow.${area}.gui.cpp
    LIBS stencil_gui_objs Qt6::Test
    ENV STENCIL_NO_ANIM=1)
endforeach()


# Hovering a context-menu row (support/iconMotion + support/MenuHotkeys): the icon play and
# the keycap shake start once on arrival, never restart while the pointer stays on the row,
# and are cancelled when it leaves — QMenu::hovered re-fires and climbs the caused stack, so
# neither can be driven straight off it. Drives a real QMenu, so it links the GUI objects.
stencil_headless_test(stencil_menuhover_headless
  SOURCES tests/menuHover.headless.cpp
  LIBS stencil_gui_objs Qt6::Test
  INCLUDE_TESTS)

# Appearance pins: the app stylesheet hashed per theme x accent, plus twelve rendered
# MainWindow / dialog states diffed against tests/pins (per platform; a platform with
# no baselines skips that half). Registered TWICE — offscreen runs at devicePixelRatio
# 1, so the hi-dpi pass forces dpr 2 with QT_SCALE_FACTOR, which is read once at
# QApplication construction and so needs its own process.
stencil_headless_test(stencil_uipins_headless
  SOURCES tests/uiPins.headless.cpp tests/uiPins.states.cpp
  DEFS "STENCIL_UI_PINS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/pins\""
  LIBS stencil_gui_objs Qt6::Test
  ENV STENCIL_NO_ANIM=1)
add_test(NAME stencil_uipins_hidpi_headless COMMAND stencil_uipins_headless)
set_tests_properties(stencil_uipins_hidpi_headless PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;STENCIL_NO_ANIM=1;QT_SCALE_FACTOR=2")

# Hold-to-draw + selection-delete headless check (CanvasWidget public API).
stencil_headless_test(stencil_holddraw_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/holdDrawCanvas.headless.cpp ${STENCIL_CANVAS_SOURCES}
    src/canvas/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Image-fixture test: loads a real PNG from tests/fixtures/ and runs it through the
# load -> crop -> core image-filter path (desktop analogue of the CLI fixture tests).
stencil_headless_test(stencil_image_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/imageFixture.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/IdleCard.cpp
    src/canvas/IncognitoOverlay.cpp src/support/iconSet.cpp src/support/numericInput.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\""
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# Layout-envelope test: the page format + x/y formulas round-trip through
# fileStore::buildLayoutJson <-> parseLayoutMeta (server save/load parity with browser + CLI).
stencil_headless_test(stencil_layout_headless
  SOURCES tests/layoutMeta.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# fileStore::projectToJson <-> projectFromJson per-project `color` round-trip
# (browser/server colour-persistence parity).
stencil_headless_test(stencil_projectcolor_headless
  SOURCES tests/projectColor.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Per-project keywords persistence round-trip (browser/server keywords parity).
stencil_headless_test(stencil_projectkeywords_headless
  SOURCES tests/projectKeywords.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# .stencil portable project file round-trip (fileStore::buildProjectFile <-> parseProjectFile):
# image + layout + metadata + optional theme, cross-surface parity with browser projectFile.js.
stencil_headless_test(stencil_projectfile_headless
  SOURCES tests/projectFile.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Server-connected transfer check: drives the extracted ProjectTransferController's
# copy/move-to-server against a real running server (self-skips when none is reachable —
# point it at one with STENCIL_TEST_SERVER, default http://localhost:8090).
stencil_headless_test(stencil_projecttransfer_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/projectTransfer.headless.cpp src/app/ProjectTransferController.cpp src/app/ProjectTransferImport.cpp
    ${STENCIL_SERVERCLIENT_SOURCES} ${STENCIL_CANVAS_SOURCES} src/canvas/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} src/support/Notifications.cpp src/support/NotificationsStack.cpp src/support/iconSet.cpp
    src/support/modalReveal.cpp   # notifications' toast dust needs motionReduced()
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# Deep-link check: the stencil:// URL grammar (parseStencilUrl) + the Telegram
# start-payload codec (golden vectors shared verbatim with browser/tests/
# deepLink.test.js and the bot's DeepLinkCodecTests.cs) + the browser-fragment
# URL builder's percent-encoding.
stencil_headless_test(stencil_deeplink_headless
  SOURCES tests/deepLink.headless.cpp src/io/deepLink.cpp src/app/launchOptions.cpp
    ${STENCIL_SERVERCLIENT_SOURCES}
  LIBS stencil_core Qt6::Network)

# Live push-feed check (net/liveFeed) — drives LiveFeed against a mock QTcpServer that
# speaks the NDJSON project-events protocol. Links Qt6::Network for QTcpServer/QTcpSocket.
stencil_headless_test(stencil_livefeed_headless
  SOURCES tests/LiveFeed.headless.cpp src/net/LiveFeed.cpp ${STENCIL_SERVERCLIENT_SOURCES}
  LIBS stencil_core Qt6::Network)

# Toast coalescing (support/notifications): identical texts refresh the standing
# toast's lifetime instead of stacking; distinct texts still stack.
stencil_headless_test(stencil_notifications_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/Notifications.headless.cpp src/support/Notifications.cpp src/support/NotificationsStack.cpp
    src/support/iconSet.cpp
    src/support/modalReveal.cpp   # the toast dust flight needs motionReduced()
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg)

# Servers dialog rows (dialogs/connectDialog): long URLs elide inside the viewport
# (no horizontal scrollbar), removal retires-then-finalizes (empty state waits for the
# dust), a new row gathers in as the reverse, and a KIND-FILTER change fades +
# collapses rows out and back (support/filterFade — not the removal's dust). Mock
# QTcpServer stands in for the collaboration server.
stencil_headless_test(stencil_connectrow_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/connectRow.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    ${STENCIL_CONNECTDIALOG_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES} src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}          # …whose tokens live in its owner-only store
    src/io/deferredWrite.cpp
    src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp    # confirmYesNo() backs the disconnect prompts
    ${STENCIL_THEME_SOURCES}         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/SearchCombo.cpp src/support/SearchComboPopup.cpp   # the rows' All/Admin/Non-admin picker is one of these
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The shared modal shell's small dialogs (support/modalChrome): the browser's picker
# (`choose`), the password prompt, the prompt's live validation, the confirm's
# titleIcon, and the Open In… dialog's in-modal Telegram fallback row.
stencil_headless_test(stencil_modalchrome_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/modalChrome.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    src/dialogs/OpenInDialog.cpp src/io/deepLink.cpp
    ${STENCIL_SERVERCLIENT_SOURCES}      # deepLink's origin normalisation
    src/support/iconSet.cpp
    src/support/modalReveal.cpp resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The DESCRIPTION & ATTRIBUTES editors (dialogs/descriptionDialog, keywordsDialog):
# the shell's structure, Enter/Escape, keyword normalisation, and the store write
# against an isolated state dir.
stencil_headless_test(stencil_projectmeta_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/projectMetaDialogs.headless.cpp src/dialogs/DescriptionDialog.cpp
    src/dialogs/KeywordsDialog.cpp ${STENCIL_MODALCHROME_SOURCES} src/support/iconSet.cpp
    src/support/modalReveal.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# Stale-session handling (net/serverClient + the Servers row): a refused
# credential is Expired (never the "unreachable" bucket), is not retried in a
# loop, keeps the saved connection, and the row offers a labelled Reconnect.
stencil_headless_test(stencil_serverauth_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/serverAuth.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    ${STENCIL_CONNECTDIALOG_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES} src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}          # …whose tokens live in its owner-only store
    src/io/deferredWrite.cpp
    src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp    # confirmYesNo() backs the disconnect prompts
    ${STENCIL_THEME_SOURCES}         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/SearchCombo.cpp src/support/SearchComboPopup.cpp   # the rows' All/Admin/Non-admin picker is one of these
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# Projects dialog multi-select surface (dialogs/projectsDialog): the batch bar HIDES
# inapplicable direction buttons by selection composition, Select all / Deselect all
# toggles over the filtered view, filter/search changes fade + collapse rows out and
# back (support/filterFade), and batch remove resolves every checked row into a
# viewport-clipped DisintegrateOverlay. Mock QTcpServer serves the token + /projects
# listing, so the server-row compositions need no Go server.
stencil_headless_test(stencil_projectsbatch_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_APPTOOLTIP_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/ProjectsBatchBar.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    src/support/tipContent.cpp    # the rows' rich tooltips (AppTooltip.hpp calls into it)
    ${STENCIL_PROJECTS_DIALOG_SOURCES}
    src/dialogs/ExpirationDialog.cpp src/dialogs/ExpirationDialogCalendar.cpp  # the ⋯ menu's "Expiration…" opens it in place now
    ${STENCIL_SERVERCLIENT_SOURCES} src/net/fetchGuard.cpp        # the row thumbnails' SSRF guard
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp
    ${STENCIL_THEME_SOURCES}         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/SearchCombo.cpp src/support/SearchComboPopup.cpp   # the dialog's filter/sort/mode pickers are these now
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The same dialog's composition + row-data pin: what the constructor builds, in which order,
# and exactly what refresh() writes onto every row. Same source set as the batch suite.
stencil_headless_test(stencil_projectsdialogrows_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_APPTOOLTIP_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/ProjectsDialogRows.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    src/support/tipContent.cpp    # the rows' rich tooltips (AppTooltip.hpp calls into it)
    ${STENCIL_PROJECTS_DIALOG_SOURCES}
    src/dialogs/ExpirationDialog.cpp src/dialogs/ExpirationDialogCalendar.cpp  # the ⋯ menu's "Expiration…" opens it in place now
    ${STENCIL_SERVERCLIENT_SOURCES} src/net/fetchGuard.cpp        # the row thumbnails' SSRF guard
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp
    ${STENCIL_THEME_SOURCES}         # …and its colour wells take the theme's input chrome
    src/support/iconSet.cpp src/support/modalReveal.cpp
    src/support/menuReveal.cpp    # searchCombo's popup plays this dust
    src/support/SearchCombo.cpp src/support/SearchComboPopup.cpp   # the dialog's filter/sort/mode pickers are these now
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# Server-connected co-edit smoke test: drives the async open/load + guarded-write-merge
# primitives that MainWindow::openServerProject/saveToServer are built on, with two
# connections as two editors (self-skips when no server is reachable — point it at one
# with STENCIL_TEST_SERVER, default http://localhost:8090). Qt6::Gui for QImage decode.
stencil_headless_test(stencil_coedit_headless
  SOURCES tests/coEdit.headless.cpp ${STENCIL_SERVERCLIENT_SOURCES}
  LIBS stencil_core Qt6::Gui Qt6::Network)

# LLM op-plan parser check (src/llm/opPlan) — the llm-contract.md §1-2
# parse matrix (extraction tolerance, strict per-op validation, limits).
stencil_headless_test(stencil_llmopplan_headless
  SOURCES tests/llmOpPlan.headless.cpp src/llm/opPlan.cpp src/llm/opRegistry.cpp
    ${STENCIL_OPSCHEMA_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Core)

# Shared conformance-fixture corpus (browser/js/config/**/fixtures) + the
# desktop override map: compile definitions shared by the fixture walkers below.
set(STENCIL_FIXTURE_WALKER_DEFS
  "STENCIL_CORPUS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../browser/js/config\""
  "STENCIL_OVERRIDES_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtureOverrides.json\"")

# Op-plan corpus walker: every shared opPlan fixture (profile editor/all)
# through the real desktop parseOpPlan, honouring knownDivergence.desktop and
# the local override map (the desktop drift inventory).
stencil_headless_test(stencil_opplanfixtures_headless
  SOURCES tests/opPlanFixtures.headless.cpp src/llm/opPlan.cpp src/llm/opRegistry.cpp
    ${STENCIL_OPSCHEMA_SOURCES} resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Core)

# Provider-wire + sanitizer corpus walker: the shared §6 wire vectors and the
# sanitizer vectors through the real LlmClient (mock transport, no network).
# LlmClient.cpp is #included by the test TU for sanitizer access, not compiled here; its
# sibling TUs (the probes and the per-provider chat calls) still are.
stencil_headless_test(stencil_llmwirefixtures_headless
  SOURCES tests/llmWireFixtures.headless.cpp src/llm/LlmClientProbe.cpp
    src/llm/LlmClientChat.cpp src/llm/opPlan.cpp src/llm/opRegistry.cpp
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES} src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}          # …whose tokens live in its owner-only store
    src/io/deferredWrite.cpp resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Network)

# LLM client check (src/llm/llmClient) — the contract §6 wire mappings driven
# through a mock transport (canned responses; no network).
stencil_headless_test(stencil_llmclient_headless
  SOURCES tests/LlmClient.headless.cpp src/llm/LlmClient.cpp src/llm/LlmClientProbe.cpp
    src/llm/LlmClientChat.cpp src/llm/opPlan.cpp
    src/llm/opRegistry.cpp ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES}
    src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}          # …whose tokens live in its owner-only store
    src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Network)

# Persistence-corpus walker: the shared chatDoc / layout / stencilProject
# fixtures through the real io/fileStore serializers and parsers.
stencil_headless_test(stencil_storefixtures_headless
  SOURCES tests/storeFixtures.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Widgets)

# Deep-link corpus walker: the shared telegramStart golden vectors through the
# real codec (the launchPayload receiver vectors don't apply — builders only).
stencil_headless_test(stencil_deeplinkfixtures_headless
  SOURCES tests/deepLinkFixtures.headless.cpp src/io/deepLink.cpp ${STENCIL_SERVERCLIENT_SOURCES}
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Network)

# LLM plan-executor check (src/llm/planExecutor) — runs a parsed plan with
# actions + variants against a CanvasWidget-backed target seeded with the
# PNG fixture, asserting pixel/setting outcomes and the variant images.
stencil_headless_test(stencil_llmexecutor_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/llmExecutor.headless.cpp src/llm/opPlan.cpp src/llm/opRegistry.cpp
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_PLANEXECUTOR_SOURCES} ${STENCIL_CANVAS_SOURCES}
    src/canvas/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\""
  LIBS stencil_core Qt6::Widgets)

# The script window (dialogs/ScriptDialog + ScriptHighlighter): the empty-editor gate, the
# "report nothing until it has been run" rule, and the core-driven colouring.
stencil_headless_test(stencil_scriptdialog_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/scriptDialog.headless.cpp src/dialogs/ScriptDialog.cpp src/dialogs/ScriptDialogFile.cpp
    src/dialogs/ScriptEditorWidget.cpp src/dialogs/ScriptHighlighter.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/iconSet.cpp src/support/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The script FLYOUT hosted in the canvas context menu (dialogs/ScriptMenuPanel): the same
# gates as the window, plus the two keys a code editor owns (Tab indents, Ctrl+Enter runs).
stencil_headless_test(stencil_scriptmenupanel_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/scriptMenuPanel.headless.cpp src/dialogs/ScriptMenuPanel.cpp
    src/dialogs/ScriptMenuPanelState.cpp src/dialogs/ScriptEditorWidget.cpp
    src/dialogs/ScriptHighlighter.cpp src/model/ScriptDoc.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/iconSet.cpp src/support/modalReveal.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# .stc runner (app/scriptRun.cpp + model/ScriptDoc) — the desktop half of the script
# contract: which op reaches which PlanTarget call, and that an error runs nothing.
stencil_headless_test(stencil_scriptrunner_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/scriptRunner.headless.cpp src/app/scriptRun.cpp src/model/ScriptDoc.cpp
    src/llm/opPlan.cpp src/llm/opRegistry.cpp
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_PLANEXECUTOR_SOURCES} ${STENCIL_CANVAS_SOURCES}
    src/canvas/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\""
  LIBS stencil_core Qt6::Widgets)

# The two OS-driven .stc entries on the real MainWindow (rule 7's one path): a file handed
# over by the shell and a file dropped on the window both RUN the script.
stencil_headless_test(stencil_scriptopen_headless
  SOURCES tests/scriptOpen.headless.cpp
  LIBS stencil_gui_objs
  ENV STENCIL_NO_ANIM=1)

# LLM settings defaults + fileStore Settings JSON round-trip of the contract
# §5 llm* keys (and the windowState dock blob).
stencil_headless_test(stencil_llmsettings_headless
  SOURCES tests/llmSettings.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Motion preferences (support/motionPrefs.hpp) — the three interface modes and the
# drawing switch, the DisintegrateOverlay factories' particle gate, and the two
# settings keys. Links fileStore for the JSON round-trip.
stencil_headless_test(stencil_motionprefs_headless
  SOURCES src/support/motionIcons.cpp
    ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/motionPrefs.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Shared-config canon check: the qrc-embedded browser JSON (accents / icons /
# layoutFields) parses and matches what theme.cpp / iconSet.cpp / fileStore
# consume — a broken app.qrc alias fails here fast.
stencil_headless_test(stencil_configcanon_headless
  SOURCES tests/configCanon.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    src/support/iconSet.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# Drift guards for the assets the desktop reads instead of embedding: themeTokens.json
# against the Palette, resources/app.qss against buildStylesheet's token map,
# mediaTypes.json against the suffix sniffers, and the prompt canon's context suffixes.
stencil_headless_test(stencil_canonassets_headless
  SOURCES tests/canonAssets.headless.cpp src/app/launchOptions.cpp ${STENCIL_FILESTORE_SOURCES}
    src/io/deferredWrite.cpp src/io/mediaTypes.cpp src/llm/opRegistry.cpp ${STENCIL_OPSCHEMA_SOURCES}
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# Saved-connection secrets (net/connectionStore + io/fileStore): the token never lands
# in QSettings, an older build's plaintext row is migrated out of it on load, and every
# stored shape still round-trips.
stencil_headless_test(stencil_connectionsecrets_headless
  SOURCES tests/connectionSecrets.headless.cpp src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Core)

# Untrusted-fetch SSRF guard (net/fetchGuard) — the port of cli/src/net.zig: the
# blocked host classes and their alternate numeric encodings, strict-vs-loose
# loopback, the scheme gate, and the shape of the request it hands out. Nothing is
# fetched; the one DNS case resolves `localhost`.
stencil_headless_test(stencil_fetchguard_headless
  SOURCES tests/fetchGuard.headless.cpp src/net/fetchGuard.cpp
  LIBS Qt6::Network)

# Import-direction lint (tests/layerBoundary.headless.cpp): reads the #include lines of
# desktop/src and fails on a layer reaching right — app/ from below, canvas/ from
# dialogs/, core/ from anywhere but the listed seam. Compiles no app source.
stencil_headless_test(stencil_layerboundary_headless
  SOURCES tests/layerBoundary.headless.cpp
  LIBS Qt6::Core)

# Size + comment ratchet (tests/sizeBudget.json): reads the desktop .cpp/.hpp tree
# itself, so it compiles no app source — no new oversized file, no budgeted file
# growing, no directory raising its comment share.
stencil_headless_test(stencil_sizebudget_headless
  SOURCES tests/sizeBudget.headless.cpp
  DEFS "STENCIL_CTEST_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake\""
  LIBS Qt6::Core)

# Every test writes to an ISOLATED state dir, never the dev .stencil the real
# app uses — a ctest run used to persist test-flipped settings (view toggles,
# provider) into the developer's own app state.
get_property(stencil_all_tests DIRECTORY PROPERTY TESTS)
foreach(t ${stencil_all_tests})
  set_property(TEST ${t} APPEND PROPERTY
    ENVIRONMENT "STENCIL_STATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-state/${t}")
endforeach()
