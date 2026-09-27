# The window's header-only controllers: fullscreen, session, co-edit sync, the popover host, the
# drop candidates, the project-name bar and the units.

# Fullscreen band + zoom-handoff arithmetic (app/FullscreenController.hpp): the edge hysteresis.
stencil_headless_test(stencil_fullscreencontroller_headless
  SOURCES tests/app/view/FullscreenController.headless.cpp
  LIBS Qt6::Core)

# Persistence gates + debounces (app/SessionController.hpp): what stops a write, and the coalescing.
stencil_headless_test(stencil_sessioncontroller_headless
  SOURCES tests/app/remote/SessionController.headless.cpp
  LIBS Qt6::Core)

# The co-edit sync guards (app/remote/RemoteSyncController). RemoteSession joins as a HEADER: its
# ctor is inline, so the suite needs its moc but not its TU, which drags in the notification stack.
stencil_headless_test(stencil_remotesynccontroller_headless
  SOURCES tests/app/remote/RemoteSyncController.headless.cpp
    src/app/remote/RemoteSyncController.cpp src/app/remote/RemoteSession.hpp
    src/net/LiveFeed.cpp ${STENCIL_SERVERCLIENT_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Gui Qt6::Network)

# What a press outside an open compact popover means (app/events/popover/PopoverHost.hpp).
stencil_headless_test(stencil_popoverhost_headless
  SOURCES tests/app/events/popover/PopoverHost.headless.cpp
  LIBS Qt6::Widgets)

# Dropped-drag candidates once the NATIVE pasteboard joins them (app/events/dropSources.cpp): the
# drag reader comes along so the bounded wait and the promise scratch run on this platform.
stencil_headless_test(stencil_dropsources_headless
  SOURCES tests/app/events/dropSources.headless.cpp src/app/events/dropSources.cpp
    ${STENCIL_DRAG_SOURCES}
  LIBS Qt6::Gui ${STENCIL_DRAG_LIBS}
  INCLUDE_TESTS)

# Which of the project-name chips belong in the header row per state (app/ProjectNameBar.hpp).
stencil_headless_test(stencil_projectnamebar_headless
  SOURCES tests/app/project/ProjectNameBar.headless.cpp
  LIBS Qt6::Core)

# The window title and the name row (app/project/ProjectTitleController) over stand-in hooks.
stencil_headless_test(stencil_projecttitle_headless
  SOURCES tests/app/project/ProjectTitleController.headless.cpp
  LIBS stencil_gui_objs)

# Panning with the held arrows (app/events/ArrowPanner): the diagonal sum, Shift, release and stop.
stencil_headless_test(stencil_arrowpanner_headless
  SOURCES tests/app/events/ArrowPanner.headless.cpp src/app/events/ArrowPanner.cpp
  LIBS Qt6::Core)

# Display-unit and px→cm arithmetic (app/UnitsController.hpp), the browser's units.js twin.
stencil_headless_test(stencil_unitscontroller_headless
  SOURCES tests/app/view/UnitsController.headless.cpp
  LIBS Qt6::Core)
