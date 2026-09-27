# The canvas's fixture runs, and what the store persists and carries: layouts, project colour
# and keywords, the .stencil file, the transfer, deep links and the live feed.

# Hold-to-draw + selection-delete headless check (CanvasWidget public API).
stencil_headless_test(stencil_holddraw_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/holdDrawCanvas.headless.cpp tests/canvas/holdDrawSelection.headless.cpp ${STENCIL_CANVAS_SOURCES}
    src/canvas/overlay/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# A real PNG from tests/fixtures/ through the load -> crop -> core image-filter path.
stencil_headless_test(stencil_image_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/canvas/imageFixture.headless.cpp tests/canvas/imageFixtureInk.headless.cpp ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    src/canvas/overlay/IncognitoOverlay.cpp src/support/icon/iconSet.cpp src/support/control/numericInput.cpp
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\""
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The page format + x/y formulas round-trip through buildLayoutJson <-> parseLayoutMeta.
stencil_headless_test(stencil_layout_headless
  SOURCES tests/io/layoutMeta.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The layout caps (constants.json LIMITS) every read layout passes through linesFromJson.
stencil_headless_test(stencil_layoutcaps_headless
  SOURCES tests/io/layoutCaps.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The per-project `color` round-trip through projectToJson <-> projectFromJson.
stencil_headless_test(stencil_projectcolor_headless
  SOURCES tests/app/project/projectColor.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Per-project keywords persistence round-trip (browser/server keywords parity).
stencil_headless_test(stencil_projectkeywords_headless
  SOURCES tests/app/project/projectKeywords.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The .stencil file round-trip (buildProjectFile <-> parseProjectFile), browser project/file.js parity.
stencil_headless_test(stencil_projectfile_headless
  SOURCES tests/app/project/projectFile.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# ProjectTransferController's copy/move-to-server against a real server; self-skips with none
# reachable (STENCIL_TEST_SERVER, default http://localhost:8090).
stencil_headless_test(stencil_projecttransfer_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/app/project/projectTransfer.headless.cpp src/app/project/ProjectTransferController.cpp src/app/project/ProjectTransferImport.cpp
    ${STENCIL_SERVERCLIENT_SOURCES} ${STENCIL_CANVAS_SOURCES} src/canvas/overlay/IdleCard.cpp
    ${STENCIL_THEME_SOURCES} ${STENCIL_NOTIFY_SOURCES} src/support/icon/iconSet.cpp
    src/support/logo/logoStageRules.cpp src/support/logo/logoStageConfig.cpp
    src/support/modal/modalReveal.cpp   # notifications' toast dust needs motionReduced()
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg ${STENCIL_NOTIFY_LIBS})

# The stencil:// grammar, the Telegram start-payload codec (vectors shared with the browser and
# the bot) and the browser-fragment builder's percent-encoding.
stencil_headless_test(stencil_deeplink_headless
  SOURCES tests/io/deepLink.headless.cpp src/io/deepLink.cpp src/app/open/launchOptions.cpp
    ${STENCIL_SERVERCLIENT_SOURCES}
  LIBS stencil_core Qt6::Network)

# The live push feed (net/liveFeed) against a mock QTcpServer speaking the NDJSON events.
stencil_headless_test(stencil_livefeed_headless
  SOURCES tests/net/LiveFeed.headless.cpp src/net/LiveFeed.cpp ${STENCIL_SERVERCLIENT_SOURCES}
  LIBS stencil_core Qt6::Network)
