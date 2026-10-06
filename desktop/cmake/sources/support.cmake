# The shared widget and motion support: the theme, the notices, the modal shell, the dust and
# the controls' motion, the webcore skin, the icons, and the app's resources.

# The theme (support/theme.hpp) is three TUs: the accent resolution, the palette and the
# stylesheet build.
set(STENCIL_THEME_SOURCES
  src/support/theme/theme.cpp
  src/support/theme/themePalette.cpp
  src/support/theme/themeStylesheet.cpp)

# Notices (support/notify/Notifications.hpp): the router, the toast sink and the OS sink.
set(STENCIL_NOTIFY_SOURCES
  src/support/notify/Notifications.cpp
  src/support/notify/ToastStack.cpp
  src/support/notify/ToastStackReflow.cpp
  src/support/notify/SystemNotifier.cpp)
# macOS banners go through UserNotifications, which is OS-provided, not a new dependency.
set(STENCIL_NOTIFY_LIBS)
if(APPLE)
  list(APPEND STENCIL_NOTIFY_SOURCES src/support/notify/macBannerMac.mm)
  set_source_files_properties(src/support/notify/macBannerMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  find_library(STENCIL_USERNOTIFICATIONS_LIBRARY UserNotifications REQUIRED)
  find_library(STENCIL_FOUNDATION_LIBRARY Foundation REQUIRED)
  list(APPEND STENCIL_NOTIFY_LIBS ${STENCIL_USERNOTIFICATIONS_LIBRARY} ${STENCIL_FOUNDATION_LIBRARY})
endif()

# The shared modal shell (support/modalChrome.hpp) is four TUs: the parts, the install
# and confirm path, the prompt/choose dialogs and the footer.
set(STENCIL_MODALCHROME_SOURCES
  src/support/modal/modalChrome.cpp
  src/support/modal/modalChromeInstall.cpp
  src/support/modal/modalChromePrompt.cpp
  src/support/modal/modalChromeFooter.cpp)

# Motion and widget support split out of their headers: each group is several TUs
# defining one header's members, so every target that uses the header needs the group.
set(STENCIL_APPTOOLTIP_SOURCES
  src/support/tip/AppTooltip.cpp
  src/support/tip/AppTooltipShow.cpp
  src/support/tip/AppTooltipFilter.cpp)

set(STENCIL_CONTROLREVEAL_SOURCES
  src/support/control/reveal/controlReveal.cpp
  src/support/control/reveal/controlRevealShow.cpp
  src/support/control/reveal/controlRevealBar.cpp)

set(STENCIL_DUSTKIT_SOURCES
  src/support/dust/dustKit.cpp
  src/support/dust/dustKitSprites.cpp)

set(STENCIL_LOGOSTAGE_SOURCES
  src/support/logo/logoStageRules.cpp
  src/support/logo/logoStageConfig.cpp
  src/support/logo/logoStageMotion.cpp
  src/support/logo/logoStageCloud.cpp
  src/support/logo/typedLetter.cpp)

set(STENCIL_FILTERFADE_SOURCES
  src/support/theme/filterFade.cpp
  src/support/theme/filterFadeList.cpp)

# The webcore skin (support/webcore/): its table and rules, the picture, the overlay sheet and
# palette, the pixel icons, and the application look. The window's toggle is app/logo.
set(STENCIL_WEBCORE_SOURCES
  src/support/webcore/rules.cpp
  src/support/webcore/image.cpp
  src/support/webcore/stylesheet.cpp
  src/support/webcore/icons.cpp
  src/support/webcore/look.cpp)

set(STENCIL_THEMESWAP_SOURCES
  src/support/dust/ThemeSwapOverlay.cpp
  src/support/dust/ThemeSwapOverlayPaint.cpp)

# Form-control state swaps (support/controlSwap.hpp) are split across three TUs: the
# checkbox/combo pixmaps and bookkeeping, the value-swap cloud overlay, and the app-wide
# event filter. They define one header's members, so they travel together — with the menu
# row's check swap (menuCheckSwap.hpp), which draws on the same constants.
set(STENCIL_CONTROLSWAP_SOURCES
  src/support/control/swap/controlSwap.cpp
  src/support/control/swap/controlSwapValue.cpp
  src/support/control/swap/controlSwapFilter.cpp
  src/support/control/swap/menuCheckSwap.cpp)

# The toggle face swap is split across two TUs defining one header's functions (the
# frame maths and painting, then the live driver); every target that swaps a face needs
# both, so they travel under one name.
set(STENCIL_FACESWAP_SOURCES
  src/support/theme/faceSwap.cpp
  src/support/theme/faceSwapDriver.cpp)

# The disintegration cloud (support/DisintegrateOverlay.hpp) is split across five TUs
# defining one class: the maths, the factories, the per-instance state, the paint and the
# three mote flights. Every target that flies dust needs the whole set.
set(STENCIL_DISINTEGRATE_SOURCES
  src/support/motion/DisintegrateOverlay.cpp
  src/support/motion/DisintegrateFactory.cpp
  src/support/motion/DisintegrateState.cpp
  src/support/motion/DisintegratePaint.cpp
  src/support/motion/DisintegrateMotes.cpp)

# Per-icon hover motion (support/iconMotion.hpp) is split across four TUs: the spec
# table read off the glyph canon, the pose maths and markup, the two runners, and the
# app-wide event filter. Every target that hovers an icon needs the set.
set(STENCIL_ICONMOTION_SOURCES
  src/support/icon/iconMotion.cpp
  src/support/icon/iconMotionPose.cpp
  src/support/icon/iconMotionRunner.cpp
  src/support/icon/iconMotionFilter.cpp)

list(APPEND STENCIL_GUI_SOURCES
  ${STENCIL_THEME_SOURCES}
  ${STENCIL_NOTIFY_SOURCES}
  src/support/guiHelpers.cpp
  src/support/guiHelpersColor.cpp
  src/support/menu/menuReveal.cpp
  src/support/menu/popupSlide.cpp
  src/support/modal/modalReveal.cpp
  src/support/modal/hoverResync.cpp
  ${STENCIL_MODALCHROME_SOURCES}
  src/support/menu/SearchCombo.cpp
  src/support/menu/SearchComboPopup.cpp
  src/support/menu/comboAltPeek.cpp
  src/support/tip/altPeek.cpp
  ${STENCIL_APPTOOLTIP_SOURCES}
  ${STENCIL_CONTROLREVEAL_SOURCES}
  ${STENCIL_CONTROLSWAP_SOURCES}
  ${STENCIL_DUSTKIT_SOURCES}
  ${STENCIL_LOGOSTAGE_SOURCES}
  ${STENCIL_FILTERFADE_SOURCES}
  ${STENCIL_THEMESWAP_SOURCES}
  ${STENCIL_WEBCORE_SOURCES}
  ${STENCIL_DISINTEGRATE_SOURCES}
  ${STENCIL_ICONMOTION_SOURCES}
  ${STENCIL_FACESWAP_SOURCES}
  src/support/icon/iconSet.cpp
  src/support/menu/MenuHotkeys.cpp
  src/support/icon/motionIcons.cpp
  src/support/control/UnderlineTabBar.cpp
  src/support/control/numericInput.cpp
  src/support/share/exportPreview.cpp
  resources/app.qrc)
