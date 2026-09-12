# The GUI's include dirs and its full translation-unit set (minus main.cpp), shared by
# the app and every test target that links the whole GUI. Included from CMakeLists.txt
# inside the Qt6_FOUND block.

# GUI sources are grouped under src/ by role. The headers are included bare
# (e.g. "fileStore.hpp"), so every group dir goes on the include path below and no
# cross-group include needs a path prefix.
set(STENCIL_GUI_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app
  ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/io
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support)

# The full GUI translation-unit set MINUS the entry point (main.cpp), shared between
# the app and the MainWindow GUI e2e test target below so the two never drift.
# CanvasWidget is split across canvas*.cpp partials, all defining CanvasWidget::
# members; every target that draws a canvas needs the whole set, so they travel
# together under one name.
set(STENCIL_CANVAS_SOURCES
  src/canvas/canvasWidget.cpp
  src/canvas/canvasDrag.cpp
  src/canvas/canvasDraw.cpp
  src/canvas/canvasDrawClick.cpp
  src/canvas/canvasDrawMode.cpp
  src/canvas/canvasGeometry.cpp
  src/canvas/canvasHold.cpp
  src/canvas/canvasHover.cpp
  src/canvas/canvasImage.cpp
  src/canvas/canvasLineEdit.cpp
  src/canvas/canvasMove.cpp
  src/canvas/canvasPaint.cpp
  src/canvas/canvasPaintCache.cpp
  src/canvas/canvasPress.cpp
  src/canvas/canvasRelease.cpp
  src/canvas/canvasRender.cpp
  src/canvas/canvasSelection.cpp
  src/canvas/canvasSettings.cpp
  src/canvas/canvasStrokeFx.cpp
  src/canvas/canvasTransform.cpp
  src/canvas/strokeGrowth.cpp)

# The projects dialog is split across projects*.cpp partials (plus its row delegate),
# all defining ProjectsDialog:: / ProjectRowDelegate:: members; they travel together.
set(STENCIL_PROJECTS_DIALOG_SOURCES
  src/dialogs/projectsDialog.cpp
  src/dialogs/projectRowDelegate.cpp
  src/dialogs/projectRowDelegateRow.cpp
  src/dialogs/projectsBatchBar.cpp
  src/dialogs/projectsFilter.cpp
  src/dialogs/projectsHoverPreview.cpp
  src/dialogs/projectsOpen.cpp
  src/dialogs/projectsRename.cpp
  src/dialogs/projectsRowChrome.cpp
  src/dialogs/projectsRowIcons.cpp
  src/dialogs/projectsRowMenu.cpp
  src/dialogs/projectsRows.cpp
  src/dialogs/projectsThumbs.cpp
  src/dialogs/projectsTransfer.cpp)

# The disintegration cloud (support/disintegrateOverlay.hpp) is split across five TUs
# defining one class: the maths, the factories, the per-instance state, the paint and the
# three mote flights. Every target that flies dust needs the whole set.
set(STENCIL_DISINTEGRATE_SOURCES
  src/support/disintegrateOverlay.cpp
  src/support/disintegrateFactory.cpp
  src/support/disintegrateState.cpp
  src/support/disintegratePaint.cpp
  src/support/disintegrateMotes.cpp)

# Per-icon hover motion (support/iconMotion.hpp) is split across four TUs: the spec
# table read off the glyph canon, the pose maths and markup, the two runners, and the
# app-wide event filter. Every target that hovers an icon needs the set.
set(STENCIL_ICONMOTION_SOURCES
  src/support/iconMotion.cpp
  src/support/iconMotionPose.cpp
  src/support/iconMotionRunner.cpp
  src/support/iconMotionFilter.cpp)

# The connect list (dialogs/connectDialog.hpp) is four TUs behind a private parts header:
# the build and row rebuild, the filter and row QSS, the row actions and the re-auth.
set(STENCIL_CONNECTDIALOG_SOURCES
  src/dialogs/connectDialog.cpp
  src/dialogs/connectDialogFilter.cpp
  src/dialogs/connectDialogActions.cpp
  src/dialogs/connectDialogAuth.cpp)

# The links dialog (dialogs/linksDialog.hpp) is three TUs behind a private parts header:
# the build, the preview/scrub player and the show path.
set(STENCIL_LINKSDIALOG_SOURCES
  src/dialogs/linksDialog.cpp
  src/dialogs/linksDialogPreview.cpp
  src/dialogs/linksDialogShow.cpp)

# The open dialog (dialogs/openImageDialog.hpp) is four TUs behind a private parts
# header: the build, the preview/scrub player, the tab state and the read-out.
set(STENCIL_OPENIMAGE_SOURCES
  src/dialogs/openImageDialog.cpp
  src/dialogs/openImageDialogPreview.cpp
  src/dialogs/openImageDialogState.cpp
  src/dialogs/openImageDialogResult.cpp)

# Local persistence (io/fileStore.hpp) is four TUs behind a private io header: the layout
# JSON, the project file and chat doc, the settings, and the session/projects/hotkeys.
set(STENCIL_FILESTORE_SOURCES
  src/io/fileStore.cpp
  src/io/fileStoreProject.cpp
  src/io/fileStoreSettings.cpp
  src/io/fileStoreSession.cpp)

# The REST client and connection manager (net/serverClient.hpp) are five TUs: the request
# plumbing, the project calls, the file calls, the guarded writes and invites, and the
# ConnectionManager.
set(STENCIL_SERVERCLIENT_SOURCES
  src/net/serverClient.cpp
  src/net/serverClientProjects.cpp
  src/net/serverClientFiles.cpp
  src/net/serverClientWrites.cpp
  src/net/serverClientManager.cpp)

# The shared modal shell (support/modalChrome.hpp) is four TUs: the parts, the install
# and confirm path, the prompt/choose dialogs and the footer.
set(STENCIL_MODALCHROME_SOURCES
  src/support/modalChrome.cpp
  src/support/modalChromeInstall.cpp
  src/support/modalChromePrompt.cpp
  src/support/modalChromeFooter.cpp)

# Motion and widget support split out of their headers: each group is several TUs
# defining one header's members, so every target that uses the header needs the group.
set(STENCIL_APPTOOLTIP_SOURCES
  src/support/appTooltip.cpp
  src/support/appTooltipShow.cpp
  src/support/appTooltipFilter.cpp)

set(STENCIL_CONTROLREVEAL_SOURCES
  src/support/controlReveal.cpp
  src/support/controlRevealShow.cpp
  src/support/controlRevealBar.cpp)

set(STENCIL_DUSTKIT_SOURCES
  src/support/dustKit.cpp
  src/support/dustKitSprites.cpp)

set(STENCIL_FILTERFADE_SOURCES
  src/support/filterFade.cpp
  src/support/filterFadeList.cpp)

set(STENCIL_THEMESWAP_SOURCES
  src/support/themeSwapOverlay.cpp
  src/support/themeSwapOverlayPaint.cpp)

# Form-control state swaps (support/controlSwap.hpp) are split across three TUs: the
# checkbox/combo pixmaps and bookkeeping, the value-swap cloud overlay, and the app-wide
# event filter. They define one header's members, so they travel together.
set(STENCIL_CONTROLSWAP_SOURCES
  src/support/controlSwap.cpp
  src/support/controlSwapValue.cpp
  src/support/controlSwapFilter.cpp)

# The toggle face swap is split across two TUs defining one header's functions (the
# frame maths and painting, then the live driver); every target that swaps a face needs
# both, so they travel under one name.
set(STENCIL_FACESWAP_SOURCES
  src/support/faceSwap.cpp
  src/support/faceSwapDriver.cpp)

set(STENCIL_GUI_SOURCES
  src/app/mainWindow.cpp
  src/app/mainWindowActions.cpp
  src/app/mainWindowToolbar.cpp
  src/app/mainWindowToolbarSections.cpp
  src/app/mainWindowToolbarName.cpp
  src/app/mainWindowToolbarPage.cpp
  src/app/mainWindowToolbarStyle.cpp
  src/app/mainWindowToolbarView.cpp
  src/app/mainWindowMenus.cpp
  src/app/mainWindowTheme.cpp
  src/app/mainWindowThemeIcons.cpp
  src/app/mainWindowThemeFaces.cpp
  src/app/mainWindowThemeButtons.cpp
  src/app/stencilFileSync.cpp
  src/app/mainWindowChat.cpp
  src/app/mainWindowChatClient.cpp
  src/app/mainWindowChatSend.cpp
  src/app/mainWindowChatNotices.cpp
  src/app/mainWindowChatState.cpp
  src/app/mainWindowChatPersist.cpp
  src/app/mainWindowChatMedia.cpp
  src/app/mainWindowChatSave.cpp
  src/app/mainWindowEvents.cpp
  src/app/mainWindowAccent.cpp
  src/app/mainWindowBlank.cpp
  src/app/mainWindowBlankColor.cpp
  src/app/mainWindowChatDock.cpp
  src/app/mainWindowChatPopover.cpp
  src/app/mainWindowChatShow.cpp
  src/app/mainWindowChrome.cpp
  src/app/mainWindowContextMenu.cpp
  src/app/mainWindowDnd.cpp
  src/app/mainWindowDust.cpp
  src/app/mainWindowFullscreen.cpp
  src/app/mainWindowFullscreenZoom.cpp
  src/app/mainWindowHelp.cpp
  src/app/mainWindowHoverDetail.cpp
  src/app/mainWindowHoverTip.cpp
  src/app/mainWindowImageInfo.cpp
  src/app/mainWindowKeys.cpp
  src/app/mainWindowLaunch.cpp
  src/app/mainWindowLaunchImage.cpp
  src/app/mainWindowLayoutMeta.cpp
  src/app/mainWindowMeta.cpp
  src/app/mainWindowNameBar.cpp
  src/app/mainWindowNameEdit.cpp
  src/app/mainWindowOpen.cpp
  src/app/mainWindowOpenIn.cpp
  src/app/mainWindowPanelToggle.cpp
  src/app/mainWindowPopover.cpp
  src/app/mainWindowProjectClose.cpp
  src/app/mainWindowProjectColor.cpp
  src/app/mainWindowProjectCreate.cpp
  src/app/mainWindowProjectCrud.cpp
  src/app/mainWindowProjectLoad.cpp
  src/app/mainWindowProjectName.cpp
  src/app/mainWindowRefresh.cpp
  src/app/mainWindowReplace.cpp
  src/app/mainWindowServerProject.cpp
  src/app/mainWindowServerSave.cpp
  src/app/mainWindowSession.cpp
  src/app/mainWindowSettings.cpp
  src/app/mainWindowShared.cpp
  src/app/mainWindowSource.cpp
  src/app/mainWindowState.cpp
  src/app/mainWindowStyleOps.cpp
  src/app/mainWindowUnits.cpp
  src/app/mainWindowWindows.cpp
  src/app/mainWindowZoom.cpp
  src/app/stayOpenMenu.cpp
  src/app/logoHoverFx.cpp
  src/app/dockZonesOverlay.cpp
  src/app/dataExportController.cpp
  src/app/remoteSession.cpp
  src/app/remoteSyncController.cpp
  src/app/projectTransferController.cpp
  src/app/launchOptions.cpp
  src/io/deepLink.cpp
  src/app/selectionPanel.cpp
  src/app/selectionPanelRows.cpp
  src/app/selectionPanelState.cpp
  src/app/selectedLineBar.cpp
  src/llm/chatDock.cpp
  src/llm/chatDockChrome.cpp
  src/llm/chatDockShared.cpp
  src/llm/chatDockEvents.cpp
  src/llm/chatDockDrag.cpp
  src/llm/chatDockAttach.cpp
  src/llm/chatDockJumpPills.cpp
  src/llm/chatDockCompose.cpp
  src/llm/chatDockTray.cpp
  src/llm/chatDockCard.cpp
  src/llm/chatDockCardMenu.cpp
  src/llm/chatDockCardParts.cpp
  src/llm/chatDockBubbleWidth.cpp
  src/llm/chatDockPending.cpp
  src/llm/chatDockAppend.cpp
  src/llm/chatDockNotices.cpp
  src/llm/chatDockVariants.cpp
  src/llm/chatDockState.cpp
  src/llm/chatWidgets.cpp
  src/llm/chatCardRenderer.cpp
  src/llm/chatMenuPanel.cpp
  src/app/chatPlanTarget.cpp
  src/app/chatPlanTargetServer.cpp
  src/app/chatPlanTargetProjects.cpp
  src/llm/opPlan.cpp
  src/llm/opRegistry.cpp
  src/llm/opSchema.cpp
  src/llm/llmClient.cpp
  src/llm/llmClientProbe.cpp
  src/llm/llmClientChat.cpp
  src/llm/planExecutor.cpp
  src/llm/qtLlmTransport.cpp
  src/support/tipContent.cpp
  ${STENCIL_CANVAS_SOURCES}
  src/canvas/idleCard.cpp
  src/canvas/canvasTooltip.cpp
  src/canvas/incognitoOverlay.cpp
  src/dialogs/settingsDialog.cpp
  src/dialogs/settingsDialogState.cpp
  src/dialogs/assistantSettingsDialog.cpp
  src/dialogs/llmSettingsForm.cpp
  src/dialogs/llmSettingsFormState.cpp
  ${STENCIL_PROJECTS_DIALOG_SOURCES}
  src/dialogs/expirationDialog.cpp
  ${STENCIL_OPENIMAGE_SOURCES}
  ${STENCIL_LINKSDIALOG_SOURCES}
  src/dialogs/descriptionDialog.cpp
  src/dialogs/keywordsDialog.cpp
  src/dialogs/cropDialog.cpp
  src/dialogs/infoDialog.cpp
  src/dialogs/shortcutsDialog.cpp
  ${STENCIL_CONNECTDIALOG_SOURCES}
  src/dialogs/openInDialog.cpp
  ${STENCIL_SERVERCLIENT_SOURCES}
  src/net/liveFeed.cpp
  src/net/connectionStore.cpp
  src/net/fetchGuard.cpp
  ${STENCIL_FILESTORE_SOURCES}
  src/io/deferredWrite.cpp
  src/io/mediaLoader.cpp
  src/io/mediaTypes.cpp
  src/support/theme.cpp
  src/support/notifications.cpp
  src/support/guiHelpers.cpp
  src/support/menuReveal.cpp
  src/support/modalReveal.cpp
  ${STENCIL_MODALCHROME_SOURCES}
  src/support/searchCombo.cpp
  ${STENCIL_APPTOOLTIP_SOURCES}
  ${STENCIL_CONTROLREVEAL_SOURCES}
  ${STENCIL_CONTROLSWAP_SOURCES}
  ${STENCIL_DUSTKIT_SOURCES}
  ${STENCIL_FILTERFADE_SOURCES}
  ${STENCIL_THEMESWAP_SOURCES}
  ${STENCIL_DISINTEGRATE_SOURCES}
  ${STENCIL_ICONMOTION_SOURCES}
  ${STENCIL_FACESWAP_SOURCES}
  src/support/iconSet.cpp
  src/support/menuHotkeys.cpp
  src/support/motionIcons.cpp
  src/support/underlineTabBar.cpp
  src/support/numericInput.cpp
  src/support/exportPreview.cpp
  resources/app.qrc)

# Native OS share sheet (support/shareImage.hpp — one Share button, browser/extension
# parity, hotkeysConfig.json shareImage): a small platform-specific TU per OS, since
# the native API behind each only exists on its own platform. The only extra library
# either real body needs is OS-provided (AppKit / the WinRT projection), not a new
# third-party dependency; Linux's fallback needs nothing beyond what's linked already.
set(STENCIL_SHARE_LIBS)
if(APPLE)
  list(APPEND STENCIL_GUI_SOURCES src/support/shareImageMac.mm
                                  src/support/modalDismissMac.mm)
  set_source_files_properties(src/support/shareImageMac.mm src/support/modalDismissMac.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  find_library(STENCIL_APPKIT_LIBRARY AppKit REQUIRED)
  list(APPEND STENCIL_SHARE_LIBS ${STENCIL_APPKIT_LIBRARY})
elseif(WIN32)
  list(APPEND STENCIL_GUI_SOURCES src/support/shareImageWin.cpp)
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
    set_source_files_properties(src/support/shareImageWin.cpp PROPERTIES
      COMPILE_FLAGS "/await"
      COMPILE_DEFINITIONS "_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS")
  endif()
  list(APPEND STENCIL_SHARE_LIBS windowsapp)
else()
  list(APPEND STENCIL_GUI_SOURCES src/support/shareImageLinux.cpp)
endif()
