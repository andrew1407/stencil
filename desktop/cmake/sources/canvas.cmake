# The canvas: the scene and the widget, the overlays that float over it, and the rich tooltip
# content its hover shows.

# The canvas is CanvasScene (scene/*.cpp: the document and the paint path, usable without a
# widget) plus the CanvasWidget partials (Canvas*.cpp) and the idle card's motion; every target
# that draws a canvas needs the whole set, so they travel together under one name.
set(STENCIL_CANVAS_SOURCES
  src/canvas/scene/CanvasScene.cpp
  src/canvas/scene/sceneDocument.cpp
  src/canvas/scene/scenePaint.cpp
  src/canvas/scene/sceneFlights.cpp
  src/canvas/scene/sceneRender.cpp
  src/canvas/CanvasWidget.cpp
  src/canvas/input/CanvasDrag.cpp
  src/canvas/draw/CanvasChainBreak.cpp
  src/canvas/draw/CanvasDrawClick.cpp
  src/canvas/draw/CanvasDrawMode.cpp
  src/canvas/paint/CanvasGeometry.cpp
  src/canvas/input/CanvasHold.cpp
  src/canvas/input/CanvasHover.cpp
  src/canvas/paint/CanvasImage.cpp
  src/canvas/draw/CanvasLineEdit.cpp
  src/canvas/input/CanvasMove.cpp
  src/canvas/paint/CanvasPaint.cpp
  src/canvas/paint/canvasPaintCache.cpp
  src/canvas/input/CanvasPress.cpp
  src/canvas/input/CanvasRelease.cpp
  src/canvas/input/gestureRoutes.cpp
  src/canvas/paint/CanvasRender.cpp
  src/canvas/draw/CanvasSelection.cpp
  src/canvas/CanvasSettings.cpp
  src/canvas/draw/CanvasStrokeFx.cpp
  src/canvas/paint/CanvasTransform.cpp
  src/canvas/draw/strokeGrowth.cpp
  src/canvas/overlay/idleCardMotion.cpp
  src/model/imageTurn.cpp)

set(STENCIL_TIPCONTENT_SOURCES
  src/support/tip/tipContent.cpp          # parse: a composed title -> Tip
  src/support/tip/tipContentKeys.cpp      # the key vocabulary and the painted caps
  src/support/tip/tipContentRender.cpp    # Tip -> the HTML Qt draws, and its palette
  src/support/tip/tipContentWiring.cpp)   # keeping a control's tooltip composed

list(APPEND STENCIL_GUI_SOURCES
  ${STENCIL_TIPCONTENT_SOURCES}
  ${STENCIL_CANVAS_SOURCES}
  src/canvas/overlay/IdleCard.cpp
  src/canvas/CanvasTooltip.cpp
  src/canvas/overlay/IncognitoOverlay.cpp)
