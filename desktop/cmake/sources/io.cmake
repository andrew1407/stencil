# The server client, the fetch guard, local persistence and the media loader.

# Local persistence (io/fileStore.hpp) is four TUs behind a private io header: the layout
# JSON, the project file and chat doc, the settings, and the session/projects/hotkeys.
set(STENCIL_FILESTORE_SOURCES
  src/io/fileStore.cpp
  src/io/fileStoreProject.cpp
  src/io/fileStoreSettings.cpp
  src/io/fileStoreSession.cpp)

# The untrusted-fetch guard (net/fetchGuard.hpp) and the SSRF table it judges addresses by.
set(STENCIL_FETCHGUARD_SOURCES
  src/net/fetchGuard.cpp
  src/net/blockedRanges.cpp)

# The REST client and connection manager (net/ServerClient.hpp) are five TUs: the request
# plumbing, the project calls, the file calls, the guarded writes and invites, and the
# ConnectionManager.
set(STENCIL_SERVERCLIENT_SOURCES
  src/net/blockedRanges.cpp          # a server address is judged by the SSRF table's serverTarget
  src/net/ServerClient.cpp
  src/net/ServerClientProjects.cpp
  src/net/ServerClientFiles.cpp
  src/net/ServerClientWrites.cpp
  src/net/ServerClientManager.cpp)

list(APPEND STENCIL_GUI_SOURCES
  ${STENCIL_SERVERCLIENT_SOURCES}
  src/net/LiveFeed.cpp
  src/net/connectionStore.cpp
  ${STENCIL_FETCHGUARD_SOURCES}
  ${STENCIL_FILESTORE_SOURCES}
  src/io/deferredWrite.cpp
  src/io/MediaLoader.cpp
  src/io/MediaLoaderVideo.cpp
  src/io/mediaTypes.cpp)
