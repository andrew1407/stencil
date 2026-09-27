# The platform bodies: the native share sheet and the drag pasteboard, one TU per OS, and the
# last additions to the GUI set and its libraries.

# Native OS share sheet (support/shareImage.hpp — one Share button, browser/extension
# parity, hotkeysConfig.json shareImage): a small platform-specific TU per OS, since
# the native API behind each only exists on its own platform. The only extra library
# either real body needs is OS-provided (AppKit / the WinRT projection), not a new
# third-party dependency; Linux's fallback needs nothing beyond what's linked already.
set(STENCIL_SHARE_LIBS)
if(APPLE)
  list(APPEND STENCIL_GUI_SOURCES src/support/share/shareImageMac.mm
                                  src/support/modal/modalDismissMac.mm)
  set_source_files_properties(src/support/share/shareImageMac.mm src/support/modal/modalDismissMac.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  find_library(STENCIL_APPKIT_LIBRARY AppKit REQUIRED)
  list(APPEND STENCIL_SHARE_LIBS ${STENCIL_APPKIT_LIBRARY})
elseif(WIN32)
  list(APPEND STENCIL_GUI_SOURCES src/support/share/shareImageWin.cpp)
  # C++/WinRT projection headers ship with the Windows SDK; `windowsapp` is its
  # umbrella import lib for the WinRT runtime classes used there.
  #
  # Those headers reach for <experimental/coroutine> whenever __cpp_lib_coroutine is
  # absent, which on MSVC means anything below /std:c++20 — and its STL now refuses
  # that header outright unless the deprecation is silenced. Scoped to THIS ONE file:
  # bumping the standard would have to take core/ with it (MSVC does not support
  # mixing /std within a binary), and the shared core is C++17 by design. Nothing
  # here co_awaits anything; only the projection's own unused plumbing needs it.
  # When MSVC finally drops the header this file wants C++20, not a third flag.
  if(MSVC)
    set_source_files_properties(src/support/share/shareImageWin.cpp PROPERTIES
      COMPILE_FLAGS "/await"
      COMPILE_DEFINITIONS "_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS")
  endif()
  list(APPEND STENCIL_SHARE_LIBS windowsapp)
else()
  list(APPEND STENCIL_GUI_SOURCES src/support/share/shareImageLinux.cpp)
endif()

# The native drag pasteboard (support/dragPasteboard.hpp): the drop flavors Qt never maps onto
# QMimeData — public.html and the promised file a browser offers Finder. The portable TU carries
# the scratch dir and the bounded wait, and off Apple supplies the primitives as no-ops; the .mm
# is the only body that touches AppKit, which is OS-provided, not a new dependency.
set(STENCIL_DRAG_SOURCES src/support/dragPasteboard.cpp)
set(STENCIL_DRAG_LIBS)
if(APPLE)
  list(APPEND STENCIL_DRAG_SOURCES src/support/dragPasteboardMac.mm)
  set_source_files_properties(src/support/dragPasteboardMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  find_library(STENCIL_APPKIT_LIBRARY AppKit REQUIRED)
  list(APPEND STENCIL_DRAG_LIBS ${STENCIL_APPKIT_LIBRARY})
endif()
list(APPEND STENCIL_GUI_SOURCES ${STENCIL_DRAG_SOURCES})
list(APPEND STENCIL_SHARE_LIBS ${STENCIL_DRAG_LIBS} ${STENCIL_NOTIFY_LIBS})
