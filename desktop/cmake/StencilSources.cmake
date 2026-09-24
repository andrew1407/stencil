# The GUI's include dirs and its full translation-unit set (minus main.cpp), shared by
# the app and every test target that links the whole GUI. Included from CMakeLists.txt
# inside the Qt6_FOUND block.

# GUI sources are grouped under src/ by role. The headers are included bare
# (e.g. "fileStore.hpp"), so every group dir goes on the include path below and no
# cross-group include needs a path prefix.
set(STENCIL_GUI_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app
  ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas
  ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/input
  ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/draw
  ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/paint
  ${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/overlay
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
  ${CMAKE_CURRENT_SOURCE_DIR}/src/io
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm
  ${CMAKE_CURRENT_SOURCE_DIR}/src/model
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/actions
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/chat
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/chat/planTarget
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/chat/session
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/context
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/events
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/logo
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/meta
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/open
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/project
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/remote
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/selection
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/setup
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/theme
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/toolbar
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/view
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/connect
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/crop
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/meta
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/meta/links
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/meta/keywords
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/openImage
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/openImage/preview
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/openImage/dust
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/projects
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/projects/row
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/projects/list
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/script
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/client
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/dock
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/dock/card
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/dock/compose
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/panel
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/plan
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/plan/executor
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/control
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/control/reveal
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/control/swap
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/dust
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/icon
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/logo
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/menu
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/modal
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/motion
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/notify
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/share
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/theme
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/tip
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/webcore)

# The full GUI translation-unit set MINUS the entry point (main.cpp), shared between
# the app and the MainWindow GUI e2e test target below so the two never drift.
# CanvasWidget is split across Canvas*.cpp partials, all defining CanvasWidget::
# members; every target that draws a canvas needs the whole set, so they travel
# together under one name.
set(STENCIL_CANVAS_SOURCES
  src/canvas/CanvasWidget.cpp
  src/canvas/input/CanvasDrag.cpp
  src/canvas/draw/CanvasDraw.cpp
  src/canvas/draw/CanvasDrawClick.cpp
  src/canvas/draw/CanvasDrawMode.cpp
  src/canvas/paint/CanvasGeometry.cpp
  src/canvas/input/CanvasHold.cpp
  src/canvas/input/CanvasHover.cpp
  src/canvas/paint/CanvasImage.cpp
  src/canvas/draw/CanvasLineEdit.cpp
  src/canvas/input/CanvasMove.cpp
  src/canvas/paint/CanvasPaint.cpp
  src/canvas/paint/canvasPaintCache.cpp
  src/canvas/input/CanvasPress.cpp
  src/canvas/input/CanvasRelease.cpp
  src/canvas/paint/CanvasRender.cpp
  src/canvas/draw/CanvasSelection.cpp
  src/canvas/CanvasSettings.cpp
  src/canvas/draw/CanvasStrokeFx.cpp
  src/canvas/paint/CanvasTransform.cpp
  src/canvas/draw/strokeGrowth.cpp)

# The projects dialog is split across Projects*.cpp partials (plus its row delegate),
# all defining ProjectsDialog:: / ProjectRowDelegate:: members; they travel together.
set(STENCIL_PROJECTS_DIALOG_SOURCES
  src/dialogs/projects/ProjectsDialog.cpp
  src/dialogs/projects/ProjectsDialogBuild.cpp
  src/dialogs/projects/ProjectsDialogEvents.cpp
  src/dialogs/projects/list/ProjectsDialogList.cpp
  src/dialogs/projects/list/ProjectsDialogRefresh.cpp
  src/dialogs/projects/list/ProjectsDialogRows.cpp
  src/dialogs/projects/list/ProjectsDialogViewport.cpp
  src/dialogs/projects/row/ProjectRowDelegate.cpp
  src/dialogs/projects/row/ProjectRowDelegateRow.cpp
  src/dialogs/projects/list/ProjectsBatchBar.cpp
  src/dialogs/projects/list/ProjectsFilter.cpp
  src/dialogs/projects/row/ProjectsHoverPreview.cpp
  src/dialogs/projects/ProjectsOpen.cpp
  src/dialogs/projects/ProjectsRename.cpp
  src/dialogs/projects/row/projectsRowChrome.cpp
  src/dialogs/projects/row/ProjectsRowIcons.cpp
  src/dialogs/projects/row/ProjectsRowMenu.cpp
  src/dialogs/projects/row/ProjectsRows.cpp
  src/dialogs/projects/row/ProjectsThumbs.cpp
  src/dialogs/projects/ProjectsTransfer.cpp)

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

# The connect list (dialogs/ConnectDialog.hpp) is seven TUs behind a private parts header:
# the build and row rebuild, the filter and row QSS, the row actions and the re-auth.
set(STENCIL_CONNECTDIALOG_SOURCES
  src/dialogs/connect/ConnectDialog.cpp
  src/dialogs/connect/ConnectDialogBatchBar.cpp
  src/dialogs/connect/ConnectDialogRow.cpp
  src/dialogs/connect/ConnectDialogRowActions.cpp
  src/dialogs/connect/ConnectDialogFilter.cpp
  src/dialogs/connect/ConnectDialogActions.cpp
  src/dialogs/connect/ConnectDialogAuth.cpp)

# The links dialog (dialogs/LinksDialog.hpp) is a TU family: the shell, two constructor build
# stages, the preview/scrub player and the show path.
set(STENCIL_LINKSDIALOG_SOURCES
  src/dialogs/meta/links/LinksDialog.cpp
  src/dialogs/meta/links/LinksDialogQuickCrop.cpp
  src/dialogs/meta/links/LinksDialogPreviewWiring.cpp
  src/dialogs/meta/links/LinksDialogPreview.cpp
  src/dialogs/meta/links/LinksDialogShow.cpp)

# The open dialog (dialogs/OpenImageDialog.hpp) is several TUs behind a private parts
# header: the build, the preview, the video scrub player, the preview's dust flourish, its
# per-tab cache (a switch re-shows a decode instead of re-fetching it), the tab state, the
# crop stage and the read-out.
set(STENCIL_OPENIMAGE_SOURCES
  src/dialogs/openImage/OpenImageDialog.cpp
  src/dialogs/openImage/OpenImageDialogBuildTabs.cpp
  src/dialogs/openImage/preview/OpenImageDialogBuildPreview.cpp
  src/dialogs/openImage/preview/OpenImageDialogBuildCrop.cpp
  src/dialogs/openImage/preview/OpenImageDialogPreview.cpp
  src/dialogs/openImage/preview/OpenImageDialogFit.cpp
  src/dialogs/openImage/preview/OpenImageDialogScrub.cpp
  src/dialogs/openImage/dust/OpenImageDialogDust.cpp
  src/dialogs/openImage/dust/OpenImageDialogSizeDust.cpp
  src/dialogs/openImage/dust/OpenImageDialogAlbumDust.cpp
  src/dialogs/meta/keywords/chipDust.cpp
  src/dialogs/openImage/preview/OpenImageDialogCache.cpp
  src/dialogs/openImage/OpenImageDialogState.cpp
  src/dialogs/openImage/preview/OpenImageDialogCropStage.cpp
  src/dialogs/openImage/OpenImageDialogResult.cpp)

# Local persistence (io/fileStore.hpp) is four TUs behind a private io header: the layout
# JSON, the project file and chat doc, the settings, and the session/projects/hotkeys.
# Notices (support/notify/Notifications.hpp): the router, the toast sink and the OS sink.
set(STENCIL_NOTIFY_SOURCES
  src/support/notify/Notifications.cpp
  src/support/notify/ToastStack.cpp
  src/support/notify/ToastStackReflow.cpp
  src/support/notify/SystemNotifier.cpp)

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
set(STENCIL_OPPLAN_SOURCES
  src/llm/plan/opPlan.cpp                    # fences, the first JSON object, the plan
  src/llm/plan/opPlanFields.cpp              # the per-op field filling
  src/llm/plan/opPlanParse.cpp)              # the actions array and the ask card

set(STENCIL_OPREGISTRY_SOURCES
  src/llm/plan/opRegistry.cpp                # the table, the canon and the op names
  src/llm/plan/opRegistryBullets.cpp)        # the "Available ops" prompt section

set(STENCIL_PLANEXECUTOR_SOURCES
  src/llm/plan/executor/planExecutor.cpp
  src/llm/plan/executor/planExecutorImage.cpp          # crop / rotate / filter / layout / page / blank
  src/llm/plan/executor/planExecutorEdit.cpp           # history, frames, opens, image refs, save
  src/llm/plan/executor/planExecutorState.cpp          # the editor-settings ops
  src/llm/plan/executor/planExecutorOps.cpp
  src/llm/plan/executor/planExecutorCanvas.cpp)

# The op-schema validator (llm/OpSchema.hpp) is three TUs behind a private checks header:
# the table and limits, the per-kind field checks, and the action/ask validation.
set(STENCIL_OPSCHEMA_SOURCES
  src/llm/plan/OpSchema.cpp
  src/llm/plan/OpSchemaChecks.cpp
  src/llm/plan/OpSchemaValidate.cpp)

# The theme (support/theme.hpp) is three TUs: the accent resolution, the palette and the
# stylesheet build.
set(STENCIL_TIPCONTENT_SOURCES
  src/support/tip/tipContent.cpp          # parse: a composed title -> Tip
  src/support/tip/tipContentKeys.cpp      # the key vocabulary and the painted caps
  src/support/tip/tipContentRender.cpp    # Tip -> the HTML Qt draws, and its palette
  src/support/tip/tipContentWiring.cpp)   # keeping a control's tooltip composed

set(STENCIL_THEME_SOURCES
  src/support/theme/theme.cpp
  src/support/theme/themePalette.cpp
  src/support/theme/themeStylesheet.cpp)

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
# event filter. They define one header's members, so they travel together.
set(STENCIL_CONTROLSWAP_SOURCES
  src/support/control/swap/controlSwap.cpp
  src/support/control/swap/controlSwapValue.cpp
  src/support/control/swap/controlSwapFilter.cpp)

# The toggle face swap is split across two TUs defining one header's functions (the
# frame maths and painting, then the live driver); every target that swaps a face needs
# both, so they travel under one name.
set(STENCIL_FACESWAP_SOURCES
  src/support/theme/faceSwap.cpp
  src/support/theme/faceSwapDriver.cpp)

set(STENCIL_GUI_SOURCES
  src/app/MainWindow.cpp
  src/app/setup/MainWindowWireSignals.cpp
  src/app/events/MainWindowExecPopover.cpp
  src/app/open/MainWindowOpenDialogs.cpp
  src/app/open/MainWindowOpenProjects.cpp
  src/app/actions/MainWindowActions.cpp
  src/app/actions/MainWindowActionsData.cpp
  src/app/actions/MainWindowActionsTips.cpp
  src/app/actions/MainWindowActionsWiring.cpp
  src/app/actions/MainWindowExportMenu.cpp
  src/app/toolbar/MainWindowToolbar.cpp
  src/app/toolbar/MainWindowToolbarHeader.cpp
  src/app/toolbar/MainWindowToolbarRows.cpp
  src/app/toolbar/MainWindowToolbarSections.cpp
  src/app/toolbar/MainWindowToolbarName.cpp
  src/app/toolbar/MainWindowToolbarPage.cpp
  src/app/toolbar/MainWindowToolbarStyle.cpp
  src/app/toolbar/MainWindowToolbarView.cpp
  src/app/actions/MainWindowMenus.cpp
  src/app/theme/MainWindowTheme.cpp
  src/app/theme/MainWindowThemeIcons.cpp
  src/app/theme/MainWindowThemeFaces.cpp
  src/app/theme/MainWindowThemeButtons.cpp
  src/app/remote/StencilFileSync.cpp
  src/app/remote/StencilFileSyncWatch.cpp
  src/app/chat/MainWindowChat.cpp
  src/app/chat/session/MainWindowChatReply.cpp
  src/app/chat/session/MainWindowChatClient.cpp
  src/app/chat/session/MainWindowChatSend.cpp
  src/app/chat/MainWindowChatNotices.cpp
  src/app/chat/MainWindowChatState.cpp
  src/app/chat/session/MainWindowChatPersist.cpp
  src/app/chat/session/MainWindowChatMedia.cpp
  src/app/chat/session/MainWindowChatSave.cpp
  src/app/events/MainWindowEvents.cpp
  src/app/events/MainWindowEventsCanvas.cpp
  src/app/events/MainWindowEventsChrome.cpp
  src/app/events/MainWindowEventsPopover.cpp
  src/app/theme/MainWindowAccent.cpp
  src/app/open/MainWindowBlank.cpp
  src/app/open/MainWindowBlankColor.cpp
  src/app/chat/MainWindowChatDock.cpp
  src/app/chat/MainWindowChatPopover.cpp
  src/app/chat/MainWindowChatShow.cpp
  src/app/setup/MainWindowChrome.cpp
  src/app/context/MainWindowContextActions.cpp
  src/app/context/MainWindowContextMenu.cpp
  src/app/context/MainWindowContextRows.cpp
  src/app/events/dropSources.cpp
  src/app/events/MainWindowDnd.cpp
  src/app/theme/MainWindowDust.cpp
  src/app/view/MainWindowFullscreen.cpp
  src/app/view/MainWindowFullscreenZoom.cpp
  src/app/actions/MainWindowHelp.cpp
  src/app/meta/MainWindowHoverDetail.cpp
  src/app/meta/MainWindowHoverTip.cpp
  src/app/meta/MainWindowImageInfo.cpp
  src/app/events/MainWindowKeys.cpp
  src/app/open/MainWindowLaunch.cpp
  src/app/open/MainWindowLaunchImage.cpp
  src/app/meta/MainWindowLayoutMeta.cpp
  src/app/meta/MainWindowMeta.cpp
  src/app/meta/MainWindowNameBar.cpp
  src/app/meta/MainWindowNameEdit.cpp
  src/app/open/MainWindowOpen.cpp
  src/app/open/MainWindowOpenIn.cpp
  src/app/selection/MainWindowPanelToggle.cpp
  src/app/events/MainWindowPopover.cpp
  src/app/project/MainWindowProjectClose.cpp
  src/app/project/MainWindowProjectColor.cpp
  src/app/project/MainWindowProjectCreate.cpp
  src/app/project/MainWindowProjectCrud.cpp
  src/app/project/MainWindowProjectLoad.cpp
  src/app/project/MainWindowProjectName.cpp
  src/app/view/MainWindowRefresh.cpp
  src/app/MainWindowScript.cpp
  src/app/scriptRun.cpp
  src/model/ScriptBuffer.cpp
  src/model/ScriptDoc.cpp
  src/app/open/MainWindowReplace.cpp
  src/app/project/MainWindowServerProject.cpp
  src/app/project/MainWindowServerSave.cpp
  src/app/remote/MainWindowSession.cpp
  src/app/setup/MainWindowSetupCanvas.cpp
  src/app/setup/MainWindowSetupChat.cpp
  src/app/setup/MainWindowSetupState.cpp
  src/app/setup/MainWindowSetupWidgets.cpp
  src/app/meta/MainWindowSettings.cpp
  src/app/mainWindowShared.cpp
  src/app/open/MainWindowSource.cpp
  src/app/meta/MainWindowState.cpp
  src/app/actions/MainWindowStyleOps.cpp
  src/app/view/MainWindowUnits.cpp
  src/app/view/MainWindowWindows.cpp
  src/app/view/MainWindowZoom.cpp
  src/app/context/StayOpenMenu.cpp
  src/app/context/StayOpenMenuKeys.cpp
  src/app/context/StayOpenMenuWalk.cpp
  src/app/logo/LogoHoverFx.cpp
  src/app/logo/LogoHoverFxPaint.cpp
  src/app/logo/LogoStage.cpp
  src/app/logo/LogoStageInput.cpp
  src/app/logo/LogoStagePaint.cpp
  src/app/logo/MainWindowWebcore.cpp
  src/app/meta/DockZonesOverlay.cpp
  src/app/meta/DataExportController.cpp
  src/app/meta/DataExportImage.cpp
  src/app/remote/RemoteSession.cpp
  src/app/remote/RemoteSyncController.cpp
  src/app/project/ProjectTransferController.cpp
  src/app/project/ProjectTransferImport.cpp
  src/app/open/launchOptions.cpp
  src/io/deepLink.cpp
  src/app/selection/SelectionPanel.cpp
  src/app/selection/SelectionPanelRows.cpp
  src/app/selection/SelectionPanelState.cpp
  src/app/selection/SelectedLineBar.cpp
  src/app/selection/SelectedLineBarRow.cpp
  src/app/selection/SelectedLineBarAlpha.cpp
  src/llm/dock/ChatDock.cpp
  src/llm/dock/compose/ChatDockComposer.cpp
  src/llm/dock/ChatDockChrome.cpp
  src/llm/dock/chatDockShared.cpp
  src/llm/panel/chatMoreMenu.cpp
  src/llm/dock/ChatDockEvents.cpp
  src/llm/dock/compose/ChatDockDrag.cpp
  src/llm/dock/compose/ChatDockAttach.cpp
  src/llm/dock/ChatDockJumpPills.cpp
  src/llm/dock/compose/ChatDockCompose.cpp
  src/llm/dock/ChatDockTray.cpp
  src/llm/dock/card/ChatDockCard.cpp
  src/llm/dock/card/chatDockCardMenu.cpp
  src/llm/dock/card/ChatDockCardParts.cpp
  src/llm/dock/card/ChatDockBubbleWidth.cpp
  src/llm/dock/ChatDockPending.cpp
  src/llm/dock/ChatDockAppend.cpp
  src/llm/dock/ChatDockNotices.cpp
  src/llm/dock/card/ChatDockVariants.cpp
  src/llm/dock/ChatDockState.cpp
  src/llm/panel/chatWidgets.cpp
  src/llm/panel/chatBubbleTail.cpp
  src/llm/panel/chatWidgetsOverlays.cpp
  src/llm/panel/chatCardRenderer.cpp
  src/llm/panel/ChatMenuPanel.cpp
  src/llm/panel/ChatMenuPanelCompose.cpp
  src/llm/panel/ChatMenuPanelRows.cpp
  src/llm/panel/ChatMenuPanelState.cpp
  src/app/chat/planTarget/ChatPlanTarget.cpp
  src/app/chat/planTarget/ChatPlanTargetServer.cpp
  src/app/chat/planTarget/ChatPlanTargetProjects.cpp
  ${STENCIL_OPPLAN_SOURCES}
  ${STENCIL_OPREGISTRY_SOURCES}
  ${STENCIL_OPSCHEMA_SOURCES}
  src/llm/client/LlmClient.cpp
  src/llm/client/LlmClientProbe.cpp
  src/llm/client/LlmClientChat.cpp
  ${STENCIL_PLANEXECUTOR_SOURCES}
  src/llm/client/QtLlmTransport.cpp
  ${STENCIL_TIPCONTENT_SOURCES}
  ${STENCIL_CANVAS_SOURCES}
  src/canvas/overlay/IdleCard.cpp
  src/canvas/CanvasTooltip.cpp
  src/canvas/overlay/IncognitoOverlay.cpp
  src/dialogs/settings/SettingsDialog.cpp
  src/dialogs/settings/SettingsDialogMotionRows.cpp
  src/dialogs/settings/SettingsDialogDrawRows.cpp
  src/dialogs/settings/SettingsDialogPrefRows.cpp
  src/dialogs/settings/SettingsDialogState.cpp
  src/dialogs/settings/AssistantSettingsDialog.cpp
  src/dialogs/settings/LlmSettingsForm.cpp
  src/dialogs/settings/LlmSettingsFormRows.cpp
  src/dialogs/settings/LlmSettingsFormState.cpp
  ${STENCIL_PROJECTS_DIALOG_SOURCES}
  src/dialogs/meta/ExpirationDialog.cpp
  src/dialogs/meta/ExpirationDialogCalendar.cpp
  ${STENCIL_OPENIMAGE_SOURCES}
  ${STENCIL_LINKSDIALOG_SOURCES}
  src/dialogs/meta/DescriptionDialog.cpp
  src/dialogs/script/ScriptDialog.cpp
  src/dialogs/script/ScriptDialogFile.cpp
  src/dialogs/script/ScriptEditorWidget.cpp
  src/dialogs/script/ScriptHighlighter.cpp
  src/dialogs/script/ScriptMenuPanel.cpp
  src/dialogs/script/ScriptMenuPanelState.cpp
  src/dialogs/meta/keywords/KeywordsDialog.cpp
  src/dialogs/meta/keywords/KeywordChips.cpp
  src/dialogs/meta/keywords/KeywordChipsMotion.cpp
  src/dialogs/crop/CropDialog.cpp
  src/dialogs/crop/CropDialogDrag.cpp
  src/dialogs/meta/InfoDialog.cpp
  src/dialogs/settings/ShortcutsDialog.cpp
  src/dialogs/settings/ShortcutsDialogRows.cpp
  ${STENCIL_CONNECTDIALOG_SOURCES}
  src/dialogs/meta/OpenInDialog.cpp
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
  ${STENCIL_NOTIFY_SOURCES}
  src/support/guiHelpers.cpp
  src/support/guiHelpersColor.cpp
  src/support/menu/menuReveal.cpp
  src/support/modal/modalReveal.cpp
  ${STENCIL_MODALCHROME_SOURCES}
  src/support/menu/SearchCombo.cpp
  src/support/menu/SearchComboPopup.cpp
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
list(APPEND STENCIL_SHARE_LIBS ${STENCIL_DRAG_LIBS})
