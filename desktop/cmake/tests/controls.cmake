# The control and icon motion: scroll reveal, the display name, the face and control swaps,
# the double-click reset and the per-icon hover play.

# Scroll-reveal curve (support/scrollReveal.hpp): how dim a row is at a given spot in its scroller.
stencil_headless_test(stencil_scrollreveal_headless
  SOURCES tests/support/motion/scrollReveal.headless.cpp
  LIBS Qt6::Widgets)

# Name display-shortening (support/displayName.hpp): the middle-ellipsis of dialogs and notices.
stencil_headless_test(stencil_displayname_headless
  SOURCES tests/support/displayName.headless.cpp
  LIBS Qt6::Core)

# The toggle face swap (support/faceSwap.hpp) behind Draw's Start/Stop and Line/Rect: the curve,
# plus the driver on a live QToolButton.
stencil_headless_test(stencil_faceswap_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/theme/faceSwap.headless.cpp ${STENCIL_FACESWAP_SOURCES}
    src/support/icon/iconSet.cpp       # the glyphs it turns
    src/support/modal/modalReveal.cpp   # motionReduced()
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg)

# Form-control state swaps (support/controlSwap.hpp), driven on live widgets under the real app
# stylesheet, which draws the indicator.
stencil_headless_test(stencil_controlswap_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_CONTROLSWAP_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/control/swap/controlSwap.headless.cpp tests/support/control/swap/controlSwapCheck.headless.cpp
    tests/support/control/swap/controlSwapValue.headless.cpp tests/support/control/swap/controlSwapReduced.headless.cpp
    tests/support/control/swap/menuCheckSwap.headless.cpp
    ${STENCIL_THEME_SOURCES} src/support/icon/iconSet.cpp
    ${STENCIL_FACESWAP_SOURCES} src/support/menu/menuReveal.cpp src/support/menu/popupSlide.cpp
    src/support/modal/modalReveal.cpp
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg
  INCLUDE_TESTS)

# The row shimmer's band (support/motion/ShimmerOverlay.hpp shimmerSweepSpan).
stencil_headless_test(stencil_shimmersweep_headless
  SOURCES tests/support/motion/shimmerSweep.headless.cpp
  LIBS Qt6::Widgets)

# Double-click reset (support/control/dblReset.hpp).
stencil_headless_test(stencil_dblreset_headless
  SOURCES tests/support/control/dblReset.headless.cpp
  LIBS Qt6::Widgets)

# Per-icon hover motion (support/iconMotion.hpp): the table against the glyph canon, the semantic
# pins, and the driver on a live button.
stencil_headless_test(stencil_iconmotion_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_ICONMOTION_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/icon/iconMotion.headless.cpp tests/support/icon/iconMotionCanon.headless.cpp
    tests/support/icon/iconMotionGlyphs.headless.cpp tests/support/icon/iconMotionDriver.headless.cpp
    src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    ${STENCIL_FACESWAP_SOURCES} resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg
  INCLUDE_TESTS)
