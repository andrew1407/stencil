# The canvas suites: crop, paint, strokes, pointer tunings, history and turns, with the small
# header-only checks of the tooltip, theme and window eases registered among them.

# The crop integration (CanvasWidget + cropGeometry); kept out of the Qt-free stencil_tests.
stencil_headless_test(stencil_crop_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/crop/cropCanvas.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Modal-popover placement (support/popover.hpp): the clamp behind the anchored compact dialogs.
stencil_headless_test(stencil_popover_headless
  SOURCES tests/support/tip/popover.headless.cpp
  LIBS Qt6::Core)

# The theme wipe's easing (support/ThemeSwapOverlay.hpp), held to the AREA it sweeps as the
# browser's motion.test.js holds its half.
stencil_headless_test(stencil_themeswapease_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_THEMESWAP_SOURCES}
    tests/support/theme/themeSwapEase.headless.cpp tests/support/theme/themeSwapEaseDust.headless.cpp
  LIBS Qt6::Widgets)

# The shared window-height ease (support/easeWindowHeight.hpp) the Assistant settings form rides.
stencil_headless_test(stencil_easewindowheight_headless
  SOURCES tests/support/easeWindowHeight.headless.cpp
  LIBS Qt6::Widgets)

# Rich control tooltips (support/tipContent.cpp), carrying the browser tip/content.js cases.
stencil_headless_test(stencil_tipcontent_headless
  SOURCES tests/support/tip/tipContent.headless.cpp tests/support/tip/tipContentParse.headless.cpp
    tests/support/tip/tipContentRender.headless.cpp tests/support/tip/tipContentCompose.headless.cpp
    ${STENCIL_TIPCONTENT_SOURCES} ${STENCIL_THEME_SOURCES}
    resources/app.qrc
  LIBS Qt6::Widgets)

# CSS colours with alpha (support/cssColor.hpp): resolved through core::parseColor, so the screen
# and the export path read a colour the same way.
stencil_headless_test(stencil_csscolor_headless
  SOURCES tests/support/theme/cssColor.headless.cpp
  LIBS stencil_core Qt6::Gui)

# The quarter-turn and crop over core's imageOps (model/imageTurn) against Qt's rotate-then-copy.
stencil_headless_test(stencil_imageturn_headless
  SOURCES tests/model/imageTurn.headless.cpp src/model/imageTurn.cpp
  LIBS stencil_core Qt6::Gui)

# A partial canvas repaint (exposed-rect culling, premultiplied blits) against a full one.
stencil_headless_test(stencil_partialpaint_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/paint/partialPaint.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The dash table in px at every line width (canvas/draw/strokeDash.hpp over constants.json).
stencil_headless_test(stencil_strokedash_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/draw/strokeDash.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The hit radii, hold-to-draw and wheel-commit tunings (canvas/input/pointerTuning.hpp over constants.json).
stencil_headless_test(stencil_pointertuning_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/input/pointerTuning.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Every pointer gesture's move and release, through the canvas/input/gestureRoutes table.
stencil_headless_test(stencil_gestureroutes_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/input/gestureRoutes.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The focus and hover rings, the selection glows and the compare divider (canvas/scene/markMetrics.hpp
# over constants.json).
stencil_headless_test(stencil_markmetrics_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/scene/markMetrics.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# A scene's render copy on a pool thread, the co-edit result's render (CanvasScene::renderCopy).
stencil_headless_test(stencil_scenerendercopy_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/scene/sceneRenderCopy.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The theme wipe's clocks and geometry (support/dust/ThemeSwapOverlay over motion.json's SWAP_* keys).
stencil_headless_test(stencil_themeswapclocks_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_THEMESWAP_SOURCES}
    tests/support/theme/themeSwapClocks.headless.cpp resources/app.qrc
  LIBS Qt6::Widgets)

# The coordinate panel's fold clocks (app/chrome/PanelSlide.hpp over motion.json's PANEL_* keys).
stencil_headless_test(stencil_panelfoldclocks_headless
  SOURCES tests/app/chrome/panelFoldClocks.headless.cpp resources/app.qrc
  LIBS Qt6::Core)

# The image rotate's quarter turn (canvas/overlay/quarterTurn.hpp over motion.json's ROTATE_MS).
stencil_headless_test(stencil_quarterturn_headless
  SOURCES tests/canvas/quarterTurn.headless.cpp resources/app.qrc
  LIBS Qt6::Core)

# The chat dock's slide clocks (app/chrome/chatSlideClocks.hpp over motion.json's CHAT_SURFACE_* keys).
stencil_headless_test(stencil_chatslideclocks_headless
  SOURCES tests/app/chrome/chatSlideClocks.headless.cpp resources/app.qrc
  LIBS Qt6::Core)

# Closing a shape and backing out of one (core lineChain), plus the Alt+Ctrl pull-out gesture.
stencil_headless_test(stencil_chainedit_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/draw/chainEdit.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The canvas's undo steps as editor mementos (core EditorHistory); a load's crop starts the stack,
# and a committed filter pick is one step.
stencil_headless_test(stencil_canvashistory_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/canvasHistory.headless.cpp tests/canvas/canvasHistoryFilter.headless.cpp
    ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Model seam: the co-edit / .stencil line union over core lineMerge, and a join cut at the caps.
stencil_headless_test(stencil_lineunion_headless
  SOURCES tests/model/lineUnion.headless.cpp src/model/lineUnion.cpp
  LIBS stencil_core Qt6::Core)

# Stroke growth (canvas/draw/strokeGrowth.hpp): where a just-added vertex is at a given instant.
stencil_headless_test(stencil_strokegrowth_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/draw/strokeGrowth.headless.cpp tests/canvas/draw/strokeGrowthCanvas.headless.cpp
    ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)
