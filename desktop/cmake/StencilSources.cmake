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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/model
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support)

# The full GUI translation-unit set MINUS the entry point (main.cpp), shared between
# the app and the MainWindow GUI e2e test target below so the two never drift.
# CanvasWidget is split across Canvas*.cpp partials, all defining CanvasWidget::
# members; every target that draws a canvas needs the whole set, so they travel
# together under one name.
set(STENCIL_CANVAS_SOURCES
  src/canvas/CanvasWidget.cpp
  src/canvas/CanvasDrag.cpp
  src/canvas/CanvasDraw.cpp
  src/canvas/CanvasDrawClick.cpp
  src/canvas/CanvasDrawMode.cpp
  src/canvas/CanvasGeometry.cpp
  src/canvas/CanvasHold.cpp
  src/canvas/CanvasHover.cpp
  src/canvas/CanvasImage.cpp
  src/canvas/CanvasLineEdit.cpp
  src/canvas/CanvasMove.cpp
  src/canvas/CanvasPaint.cpp
  src/canvas/canvasPaintCache.cpp
  src/canvas/CanvasPress.cpp
  src/canvas/CanvasRelease.cpp
  src/canvas/CanvasRender.cpp
  src/canvas/CanvasSelection.cpp
  src/canvas/CanvasSettings.cpp
  src/canvas/CanvasStrokeFx.cpp
  src/canvas/CanvasTransform.cpp
  src/canvas/strokeGrowth.cpp)

# The projects dialog is split across Projects*.cpp partials (plus its row delegate),
# all defining ProjectsDialog:: / ProjectRowDelegate:: members; they travel together.
set(STENCIL_PROJECTS_DIALOG_SOURCES
  src/dialogs/ProjectsDialog.cpp
  src/dialogs/ProjectsDialogBuild.cpp
  src/dialogs/ProjectsDialogEvents.cpp
  src/dialogs/ProjectsDialogList.cpp
  src/dialogs/ProjectsDialogRefresh.cpp
  src/dialogs/ProjectsDialogRows.cpp
  src/dialogs/ProjectsDialogViewport.cpp
  src/dialogs/ProjectRowDelegate.cpp
  src/dialogs/ProjectRowDelegateRow.cpp
  src/dialogs/ProjectsBatchBar.cpp
  src/dialogs/ProjectsFilter.cpp
  src/dialogs/ProjectsHoverPreview.cpp
  src/dialogs/ProjectsOpen.cpp
  src/dialogs/ProjectsRename.cpp
  src/dialogs/projectsRowChrome.cpp
  src/dialogs/ProjectsRowIcons.cpp
  src/dialogs/ProjectsRowMenu.cpp
  src/dialogs/ProjectsRows.cpp
  src/dialogs/ProjectsThumbs.cpp
  src/dialogs/ProjectsTransfer.cpp)

# The disintegration cloud (support/DisintegrateOverlay.hpp) is split across five TUs
# defining one class: the maths, the factories, the per-instance state, the paint and the
# three mote flights. Every target that flies dust needs the whole set.
set(STENCIL_DISINTEGRATE_SOURCES
  src/support/DisintegrateOverlay.cpp
  src/support/DisintegrateFactory.cpp
  src/support/DisintegrateState.cpp
  src/support/DisintegratePaint.cpp
  src/support/DisintegrateMotes.cpp)

# Per-icon hover motion (support/iconMotion.hpp) is split across four TUs: the spec
# table read off the glyph canon, the pose maths and markup, the two runners, and the
# app-wide event filter. Every target that hovers an icon needs the set.
set(STENCIL_ICONMOTION_SOURCES
  src/support/iconMotion.cpp
  src/support/iconMotionPose.cpp
  src/support/iconMotionRunner.cpp
  src/support/iconMotionFilter.cpp)

# The connect list (dialogs/ConnectDialog.hpp) is seven TUs behind a private parts header:
# the build and row rebuild, the filter and row QSS, the row actions and the re-auth.
set(STENCIL_CONNECTDIALOG_SOURCES
  src/dialogs/ConnectDialog.cpp
  src/dialogs/ConnectDialogBatchBar.cpp
  src/dialogs/ConnectDialogRow.cpp
  src/dialogs/ConnectDialogRowActions.cpp
  src/dialogs/ConnectDialogFilter.cpp
  src/dialogs/ConnectDialogActions.cpp
  src/dialogs/ConnectDialogAuth.cpp)

# The links dialog (dialogs/LinksDialog.hpp) is three TUs behind a private parts header:
# the build, the preview/scrub player and the show path.
set(STENCIL_LINKSDIALOG_SOURCES
  src/dialogs/LinksDialog.cpp
  src/dialogs/LinksDialogPreview.cpp
  src/dialogs/LinksDialogShow.cpp)

# The open dialog (dialogs/OpenImageDialog.hpp) is several TUs behind a private parts
# header: the build, the preview, the video scrub player, the preview's dust flourish, its
# per-tab cache (a switch re-shows a decode instead of re-fetching it), the tab state and
# the read-out.
set(STENCIL_OPENIMAGE_SOURCES
  src/dialogs/OpenImageDialog.cpp
  src/dialogs/OpenImageDialogPreview.cpp
  src/dialogs/OpenImageDialogFit.cpp
  src/dialogs/OpenImageDialogScrub.cpp
  src/dialogs/OpenImageDialogDust.cpp
  src/dialogs/OpenImageDialogSizeDust.cpp
  src/dialogs/OpenImageDialogCache.cpp
  src/dialogs/OpenImageDialogState.cpp
  src/dialogs/OpenImageDialogResult.cpp)

# Local persistence (io/fileStore.hpp) is four TUs behind a private io header: the layout
# JSON, the project file and chat doc, the settings, and the session/projects/hotkeys.
set(STENCIL_FILESTORE_SOURCES
  src/io/fileStore.cpp
  src/io/fileStoreProject.cpp
  src/io/fileStoreSettings.cpp
  src/io/fileStoreSession.cpp)

# The REST client and connection manager (net/ServerClient.hpp) are five TUs: the request
# plumbing, the project calls, the file calls, the guarded writes and invites, and the
# ConnectionManager.
set(STENCIL_SERVERCLIENT_SOURCES
  src/net/ServerClient.cpp
  src/net/ServerClientProjects.cpp
  src/net/ServerClientFiles.cpp
  src/net/ServerClientWrites.cpp
  src/net/ServerClientManager.cpp)

# The plan executor (llm/planExecutor.hpp) is three TUs: the action dispatch, the
# MainWindow plan target and the canvas plan target.
set(STENCIL_PLANEXECUTOR_SOURCES
  src/llm/planExecutor.cpp
  src/llm/planExecutorOps.cpp
  src/llm/planExecutorCanvas.cpp)

# The op-schema validator (llm/OpSchema.hpp) is three TUs behind a private checks header:
# the table and limits, the per-kind field checks, and the action/ask validation.
set(STENCIL_OPSCHEMA_SOURCES
  src/llm/OpSchema.cpp
  src/llm/OpSchemaChecks.cpp
  src/llm/OpSchemaValidate.cpp)

# The theme (support/theme.hpp) is three TUs: the accent resolution, the palette and the
# stylesheet build.
set(STENCIL_THEME_SOURCES
  src/support/theme.cpp
  src/support/themePalette.cpp
  src/support/themeStylesheet.cpp)

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
  src/support/AppTooltip.cpp
  src/support/AppTooltipShow.cpp
  src/support/AppTooltipFilter.cpp)

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
  src/support/ThemeSwapOverlay.cpp
  src/support/ThemeSwapOverlayPaint.cpp)

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
  src/app/MainWindow.cpp
  src/app/MainWindowActions.cpp
  src/app/MainWindowActionsData.cpp
  src/app/MainWindowActionsTips.cpp
  src/app/MainWindowActionsWiring.cpp
  src/app/MainWindowExportMenu.cpp
  src/app/MainWindowToolbar.cpp
  src/app/MainWindowToolbarHeader.cpp
  src/app/MainWindowToolbarRows.cpp
  src/app/MainWindowToolbarSections.cpp
  src/app/MainWindowToolbarName.cpp
  src/app/MainWindowToolbarPage.cpp
  src/app/MainWindowToolbarStyle.cpp
  src/app/MainWindowToolbarView.cpp
  src/app/MainWindowMenus.cpp
  src/app/MainWindowTheme.cpp
  src/app/MainWindowThemeIcons.cpp
  src/app/MainWindowThemeFaces.cpp
  src/app/MainWindowThemeButtons.cpp
  src/app/StencilFileSync.cpp
  src/app/StencilFileSyncWatch.cpp
  src/app/MainWindowChat.cpp
  src/app/MainWindowChatReply.cpp
  src/app/MainWindowChatClient.cpp
  src/app/MainWindowChatSend.cpp
  src/app/MainWindowChatNotices.cpp
  src/app/MainWindowChatState.cpp
  src/app/MainWindowChatPersist.cpp
  src/app/MainWindowChatMedia.cpp
  src/app/MainWindowChatSave.cpp
  src/app/MainWindowEvents.cpp
  src/app/MainWindowEventsCanvas.cpp
  src/app/MainWindowEventsChrome.cpp
  src/app/MainWindowEventsPopover.cpp
  src/app/MainWindowAccent.cpp
  src/app/MainWindowBlank.cpp
  src/app/MainWindowBlankColor.cpp
  src/app/MainWindowChatDock.cpp
  src/app/MainWindowChatPopover.cpp
  src/app/MainWindowChatShow.cpp
  src/app/MainWindowChrome.cpp
  src/app/MainWindowContextActions.cpp
  src/app/MainWindowContextMenu.cpp
  src/app/MainWindowContextRows.cpp
  src/app/MainWindowDnd.cpp
  src/app/MainWindowDust.cpp
  src/app/MainWindowFullscreen.cpp
  src/app/MainWindowFullscreenZoom.cpp
  src/app/MainWindowHelp.cpp
  src/app/MainWindowHoverDetail.cpp
  src/app/MainWindowHoverTip.cpp
  src/app/MainWindowImageInfo.cpp
  src/app/MainWindowKeys.cpp
  src/app/MainWindowLaunch.cpp
  src/app/MainWindowLaunchImage.cpp
  src/app/MainWindowLayoutMeta.cpp
  src/app/MainWindowMeta.cpp
  src/app/MainWindowNameBar.cpp
  src/app/MainWindowNameEdit.cpp
  src/app/MainWindowOpen.cpp
  src/app/MainWindowOpenIn.cpp
  src/app/MainWindowPanelToggle.cpp
  src/app/MainWindowPopover.cpp
  src/app/MainWindowProjectClose.cpp
  src/app/MainWindowProjectColor.cpp
  src/app/MainWindowProjectCreate.cpp
  src/app/MainWindowProjectCrud.cpp
  src/app/MainWindowProjectLoad.cpp
  src/app/MainWindowProjectName.cpp
  src/app/MainWindowRefresh.cpp
  src/app/MainWindowScript.cpp
  src/app/scriptRun.cpp
  src/model/ScriptBuffer.cpp
  src/model/ScriptDoc.cpp
  src/app/MainWindowReplace.cpp
  src/app/MainWindowServerProject.cpp
  src/app/MainWindowServerSave.cpp
  src/app/MainWindowSession.cpp
  src/app/MainWindowSetupCanvas.cpp
  src/app/MainWindowSetupChat.cpp
  src/app/MainWindowSetupState.cpp
  src/app/MainWindowSetupWidgets.cpp
  src/app/MainWindowSettings.cpp
  src/app/mainWindowShared.cpp
  src/app/MainWindowSource.cpp
  src/app/MainWindowState.cpp
  src/app/MainWindowStyleOps.cpp
  src/app/MainWindowUnits.cpp
  src/app/MainWindowWindows.cpp
  src/app/MainWindowZoom.cpp
  src/app/StayOpenMenu.cpp
  src/app/StayOpenMenuKeys.cpp
  src/app/StayOpenMenuWalk.cpp
  src/app/LogoHoverFx.cpp
  src/app/LogoHoverFxPaint.cpp
  src/app/DockZonesOverlay.cpp
  src/app/DataExportController.cpp
  src/app/DataExportImage.cpp
  src/app/RemoteSession.cpp
  src/app/RemoteSyncController.cpp
  src/app/ProjectTransferController.cpp
  src/app/ProjectTransferImport.cpp
  src/app/launchOptions.cpp
  src/io/deepLink.cpp
  src/app/SelectionPanel.cpp
  src/app/SelectionPanelRows.cpp
  src/app/SelectionPanelState.cpp
  src/app/SelectedLineBar.cpp
  src/app/SelectedLineBarRow.cpp
  src/llm/ChatDock.cpp
  src/llm/ChatDockChrome.cpp
  src/llm/chatDockShared.cpp
  src/llm/chatMoreMenu.cpp
  src/llm/ChatDockEvents.cpp
  src/llm/ChatDockDrag.cpp
  src/llm/ChatDockAttach.cpp
  src/llm/ChatDockJumpPills.cpp
  src/llm/ChatDockCompose.cpp
  src/llm/ChatDockTray.cpp
  src/llm/ChatDockCard.cpp
  src/llm/chatDockCardMenu.cpp
  src/llm/ChatDockCardParts.cpp
  src/llm/ChatDockBubbleWidth.cpp
  src/llm/ChatDockPending.cpp
  src/llm/ChatDockAppend.cpp
  src/llm/ChatDockNotices.cpp
  src/llm/ChatDockVariants.cpp
  src/llm/ChatDockState.cpp
  src/llm/chatWidgets.cpp
  src/llm/chatWidgetsOverlays.cpp
  src/llm/chatCardRenderer.cpp
  src/llm/ChatMenuPanel.cpp
  src/llm/ChatMenuPanelCompose.cpp
  src/llm/ChatMenuPanelRows.cpp
  src/llm/ChatMenuPanelState.cpp
  src/app/ChatPlanTarget.cpp
  src/app/ChatPlanTargetServer.cpp
  src/app/ChatPlanTargetProjects.cpp
  src/llm/opPlan.cpp
  src/llm/opRegistry.cpp
  ${STENCIL_OPSCHEMA_SOURCES}
  src/llm/LlmClient.cpp
  src/llm/LlmClientProbe.cpp
  src/llm/LlmClientChat.cpp
  ${STENCIL_PLANEXECUTOR_SOURCES}
  src/llm/QtLlmTransport.cpp
  src/support/tipContent.cpp
  ${STENCIL_CANVAS_SOURCES}
  src/canvas/IdleCard.cpp
  src/canvas/CanvasTooltip.cpp
  src/canvas/IncognitoOverlay.cpp
  src/dialogs/SettingsDialog.cpp
  src/dialogs/SettingsDialogState.cpp
  src/dialogs/AssistantSettingsDialog.cpp
  src/dialogs/LlmSettingsForm.cpp
  src/dialogs/LlmSettingsFormRows.cpp
  src/dialogs/LlmSettingsFormState.cpp
  ${STENCIL_PROJECTS_DIALOG_SOURCES}
  src/dialogs/ExpirationDialog.cpp
  src/dialogs/ExpirationDialogCalendar.cpp
  ${STENCIL_OPENIMAGE_SOURCES}
  ${STENCIL_LINKSDIALOG_SOURCES}
  src/dialogs/DescriptionDialog.cpp
  src/dialogs/ScriptDialog.cpp
  src/dialogs/ScriptDialogFile.cpp
  src/dialogs/ScriptEditorWidget.cpp
  src/dialogs/ScriptHighlighter.cpp
  src/dialogs/ScriptMenuPanel.cpp
  src/dialogs/ScriptMenuPanelState.cpp
  src/dialogs/KeywordsDialog.cpp
  src/dialogs/KeywordChips.cpp
  src/dialogs/KeywordChipsMotion.cpp
  src/dialogs/CropDialog.cpp
  src/dialogs/CropDialogDrag.cpp
  src/dialogs/InfoDialog.cpp
  src/dialogs/ShortcutsDialog.cpp
  src/dialogs/ShortcutsDialogRows.cpp
  ${STENCIL_CONNECTDIALOG_SOURCES}
  src/dialogs/OpenInDialog.cpp
  ${STENCIL_SERVERCLIENT_SOURCES}
  src/net/LiveFeed.cpp
  src/net/connectionStore.cpp
  src/net/fetchGuard.cpp
  ${STENCIL_FILESTORE_SOURCES}
  src/io/deferredWrite.cpp
  src/io/MediaLoader.cpp
  src/io/MediaLoaderVideo.cpp
  src/io/mediaTypes.cpp
  ${STENCIL_THEME_SOURCES}
  src/support/Notifications.cpp
  src/support/NotificationsStack.cpp
  src/support/guiHelpers.cpp
  src/support/guiHelpersColor.cpp
  src/support/menuReveal.cpp
  src/support/modalReveal.cpp
  ${STENCIL_MODALCHROME_SOURCES}
  src/support/SearchCombo.cpp
  src/support/SearchComboPopup.cpp
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
  src/support/MenuHotkeys.cpp
  src/support/motionIcons.cpp
  src/support/UnderlineTabBar.cpp
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
