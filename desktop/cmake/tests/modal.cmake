# The notices, the servers list, and the modal shell with the meta dialogs built on it.

# Toast coalescing (support/notifications): an identical text refreshes the standing toast.
stencil_headless_test(stencil_notifications_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/notify/Notifications.headless.cpp ${STENCIL_NOTIFY_SOURCES}
    src/support/logo/logoStageRules.cpp src/support/logo/logoStageConfig.cpp
    src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp   # the toast dust flight needs motionReduced()
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg ${STENCIL_NOTIFY_LIBS})

# The notice channel: toasts by default, the OS sink while it accepts, toasts when it declines.
stencil_headless_test(stencil_notifychannel_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/notify/channel.headless.cpp ${STENCIL_NOTIFY_SOURCES}
    src/support/logo/logoStageRules.cpp src/support/logo/logoStageConfig.cpp
    src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp
    resources/app.qrc
  LIBS Qt6::Widgets Qt6::Svg ${STENCIL_NOTIFY_LIBS})

# Servers dialog rows (dialogs/connectDialog): elision, removal, arrival and the kind filter's
# fade; a mock QTcpServer stands in for the collaboration server.
stencil_headless_test(stencil_connectrow_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/connect/connectRow.headless.cpp tests/dialogs/connect/connectRowLayout.headless.cpp
    tests/dialogs/connect/connectRowMotion.headless.cpp tests/dialogs/connect/connectRowEdges.headless.cpp
    tests/dialogs/connect/connectRowConnect.headless.cpp tests/dialogs/connect/connectRowToast.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    ${STENCIL_CONNECTDIALOG_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES} src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}
    src/io/deferredWrite.cpp
    src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp
    ${STENCIL_THEME_SOURCES}
    src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    src/support/menu/menuReveal.cpp
    src/support/menu/popupSlide.cpp
    src/support/menu/SearchCombo.cpp src/support/menu/SearchComboPopup.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The shared modal shell's small dialogs (support/modalChrome): choose, the prompts, the confirm,
# and the Open In… dialog's Telegram fallback row.
stencil_headless_test(stencil_modalchrome_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/modal/modalChrome.headless.cpp tests/support/modal/modalChromePrompt.headless.cpp
    tests/support/modal/modalChromeFooter.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    src/dialogs/meta/OpenInDialog.cpp src/io/deepLink.cpp
    ${STENCIL_SERVERCLIENT_SOURCES}      # deepLink's origin normalisation
    src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The "Make a copy" confirmation (dialogs/projects/copy/CopyProjectDialog): its question and the
# local / incognito / Just copy interlocks.
stencil_headless_test(stencil_copyprojectdialog_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/projects/copy/copyProjectDialog.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    src/dialogs/projects/copy/CopyProjectDialog.cpp
    src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The modal shell's header drag: it moves a dialog that is its own window, never one
# execMaybePopover reparented into the popover overlay.
stencil_headless_test(stencil_modalheaderdrag_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/modal/modalHeaderDrag.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The DESCRIPTION & ATTRIBUTES editors: the shell, Enter/Escape, keywords and the store write.
stencil_headless_test(stencil_projectmeta_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/meta/projectMetaDialogs.headless.cpp src/dialogs/meta/DescriptionDialog.cpp
    src/dialogs/meta/keywords/KeywordsDialog.cpp src/dialogs/meta/keywords/KeywordChips.cpp
    src/dialogs/meta/keywords/KeywordChipsMotion.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The open-image flow's flight anchors (support/modal/imageAnchor.hpp): where a confirm grows
# from, and which half of the toolbar's Open pair its answer pours into.
stencil_headless_test(stencil_imageanchor_headless
  SOURCES tests/support/modal/imageAnchor.headless.cpp
  LIBS Qt6::Widgets)

# A dialog opened at a point (support/modal DialogLanding): centred on it and kept on the screen,
# claimed by the next dialog alone, still flown out of its opener.
stencil_headless_test(stencil_dialoglanding_headless
  SOURCES tests/support/modal/dialogLanding.headless.cpp
  LIBS stencil_gui_objs Qt6::Test)

# The window backdrop (support/ModalBackdrop): the browser's scrim + blur, on its switch.
stencil_headless_test(stencil_modalbackdrop_headless
  SOURCES tests/support/modal/modalBackdrop.headless.cpp
  LIBS stencil_core Qt6::Widgets)

# The keyword chips' clocks (dialogs/meta/keywords/chipClocks.hpp over motion.json's CHIP_DUST_* and
# CHIP_ENTER_*).
stencil_headless_test(stencil_chipclocks_headless
  SOURCES tests/dialogs/meta/keywords/chipClocks.headless.cpp resources/app.qrc
  LIBS Qt6::Core)

# The keywords FIELD (dialogs/KeywordChips), with its browser twin's parse and addTo cases.
stencil_headless_test(stencil_keywordchips_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/meta/keywords/keywordChips.headless.cpp
    src/dialogs/meta/keywords/KeywordsDialog.cpp src/dialogs/meta/keywords/KeywordChips.cpp
    src/dialogs/meta/keywords/KeywordChipsMotion.cpp
    ${STENCIL_MODALCHROME_SOURCES} src/support/icon/iconSet.cpp
    src/support/modal/modalReveal.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)
