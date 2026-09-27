# The guards: saved-connection secrets, the media loader's walk, the SSRF fetch guard, the
# import-direction lint and the test-count floor.

# Saved-connection secrets (net/connectionStore + io/fileStore): the token never lands in
# QSettings, and an older build's plaintext row is migrated out of it on load.
stencil_headless_test(stencil_connectionsecrets_headless
  SOURCES tests/dialogs/connect/connectionSecrets.headless.cpp src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Core)

# The media loader: its candidate walk (io/MediaLoader::loadFirstOf) over guard-blocked hosts, the
# route a picture that will not decode takes (a loopback server stands in for a download), and the
# shared image-header corpus through its sniffer.
stencil_headless_test(stencil_medialoader_headless
  SOURCES tests/io/MediaLoader.headless.cpp tests/io/mediaLoaderRouting.headless.cpp
    tests/io/imageHeaderFixtures.headless.cpp
    src/io/MediaLoader.cpp src/io/MediaLoaderVideo.cpp src/io/mediaTypes.cpp
    ${STENCIL_FETCHGUARD_SOURCES} resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS Qt6::Gui Qt6::Network Qt6::Multimedia)

# The SSRF guard (net/fetchGuard, a port of cli/src/net.zig) and the shared host corpus under every
# policy of net/blockedRanges.json. Nothing is fetched; the one DNS case resolves `localhost`.
stencil_headless_test(stencil_fetchguard_headless
  SOURCES tests/net/fetchGuard.headless.cpp tests/net/hostsFixtures.headless.cpp
    ${STENCIL_FETCHGUARD_SOURCES} resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS Qt6::Network)

# Import-direction lint (tests/layerBoundary.headless.cpp) over desktop/src's #include lines.
stencil_headless_test(stencil_layerboundary_headless
  SOURCES tests/layerBoundary.headless.cpp
  LIBS Qt6::Core)

# Test-count floor: counts the add_test( lines of the generated CTestTestfile.cmake.
stencil_headless_test(stencil_testfloor_headless
  SOURCES tests/testFloor.headless.cpp
  DEFS "STENCIL_CTEST_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake\""
  LIBS Qt6::Core)
